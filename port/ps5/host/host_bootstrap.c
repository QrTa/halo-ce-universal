/*
HOST_BOOTSTRAP.C

TEMPORARY: phase 2 of port/ps5/PS5.md deletes this file and host_bootstrap.S,
and puts the Android host's loader (port/android/host/host_loader.c) and the
real import table in their place. Nothing else is to build on them.

Phase 1's stand-in for the host, which proves the build and the deploy end
to end on the console: the guest probe's section 6 (port/ps5/probe/guest,
results/guest-run3.txt), moved here and trimmed of the probe's other
sections. It

- maps the guest's ranges in direct memory: the image's (0x70000000, 64 MB,
  read, write and execute), the Xbox window (512 MB at 0x80000000 if free,
  else on the next 256 MB that is: a title has libc.prx at 0x80000000),
  Custom Edition's tag cache (0x40440000) and a pool for the guest's own
  mappings and its stack;
- loads /app0/halo_guest.elf (the Vulkan image of `ninja ps5`) where it is
  linked, checks its header, and points every import at a thunk that leads
  to import_called (host_bootstrap.S);
- answers the imports a real host answers trivially (the thread pointer,
  logging, the system calls musl makes at start: memory from the pool,
  clocks, standard output; files are not found), logs every call, and stops
  the game at the first import it does not answer: host_sdl_get_base_path
  on the probe's run 3. A fault in the game is logged with its registers.
  The game is given 60 s.
*/

#include "ps5_host.h"

#include "guest_syscall_names.h"
#include "halo_android_abi.h"
#include "halo_guest_syscall.h"

#include <ps5platform/context.h>
#include <ps5platform/fp.h>
#include <ps5platform/kernel.h>

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#define MEGABYTE 0x100000ULL
#define LOW_LIMIT 0x100000000ULL
#define DIRECT_UNIT 0x10000ULL
#define PROT_CPU_RW (PS5_KERNEL_PROT_CPU_READ | PS5_KERNEL_PROT_CPU_WRITE)
#define PROT_CPU_RWX (PROT_CPU_RW | PS5_KERNEL_PROT_CPU_EXEC)

/* the pool for the guest's own mappings (its malloc), and the game's stack
at its top */
#define POOL_SIZE (256 * MEGABYTE)
#define GUEST_STACK_SIZE (2 * MEGABYTE)

/* the thunks every import leads to, in the last megabyte of the image's range */
#define THUNK_SIZE 32
#define MAXIMUM_IMPORTS 1024
#define GUEST_SECONDS 60
#define CALL_LINES 400

extern const unsigned char import_dispatch[];

static uint64_t now_us(void)
{
	static uint64_t frequency;

	if (!frequency)
		frequency = sceKernelGetTscFrequency();
	return frequency ? sceKernelReadTsc() / (frequency / 1000000) : 0;
}

/* ---------- faults */

static sigjmp_buf guest_jump;
static volatile sig_atomic_t guest_running;

static const char *signal_name(int signal_number)
{
	switch (signal_number)
	{
	case SIGSEGV:
		return "SIGSEGV";
	case SIGBUS:
		return "SIGBUS";
	case SIGILL:
		return "SIGILL";
	case SIGFPE:
		return "SIGFPE";
	case SIGTRAP:
		return "SIGTRAP";
	}
	return "signal";
}

static void log_registers(const ucontext_t *context)
{
	const mcontext_t *m = &context->uc_mcontext;

	ps5_log("  rip %016lx rsp %016lx rbp %016lx rflags %08lx", (unsigned long)m->mc_rip, (unsigned long)m->mc_rsp,
		(unsigned long)m->mc_rbp, (unsigned long)m->mc_rflags);
	ps5_log("  rax %016lx rbx %016lx rcx %016lx rdx %016lx", (unsigned long)m->mc_rax, (unsigned long)m->mc_rbx,
		(unsigned long)m->mc_rcx, (unsigned long)m->mc_rdx);
	ps5_log("  rsi %016lx rdi %016lx r8  %016lx r9  %016lx", (unsigned long)m->mc_rsi, (unsigned long)m->mc_rdi,
		(unsigned long)m->mc_r8, (unsigned long)m->mc_r9);
	ps5_log("  r10 %016lx r11 %016lx r12 %016lx r13 %016lx", (unsigned long)m->mc_r10, (unsigned long)m->mc_r11,
		(unsigned long)m->mc_r12, (unsigned long)m->mc_r13);
	ps5_log("  r14 %016lx r15 %016lx; err 0x%lx, trap %lu, address 0x%lx", (unsigned long)m->mc_r14,
		(unsigned long)m->mc_r15, (unsigned long)m->mc_err, (unsigned long)m->mc_trapno, (unsigned long)m->mc_addr);
}

static char guest_end[256];

/* a fault in the game is logged and ends its run (siglongjmp back to
guest_thread); anything else is the host's own, and goes to the system's
handler, which puts it in the kernel log */
static void fault_handler(int signal_number, siginfo_t *info, void *context)
{
	const ucontext_t *state = context;

	if (guest_running)
	{
		guest_running = 0;
		snprintf(guest_end, sizeof(guest_end), "the game faulted: %s (code %d) at address 0x%lx, rip 0x%lx%s",
			signal_name(signal_number), info->si_code, (unsigned long)info->si_addr,
			(unsigned long)state->uc_mcontext.mc_rip,
			state->uc_mcontext.mc_rip >= HALO_GUEST_IMAGE_BASE &&
				state->uc_mcontext.mc_rip < HALO_GUEST_IMAGE_BASE + HALO_GUEST_IMAGE_RESERVE ?
				" (in the game's image: look it up in build/ps5/halo_guest_vk.elf.map)" : "");
		ps5_log("%s", guest_end);
		log_registers(state);
		siglongjmp(guest_jump, 2);
	}
	signal(signal_number, SIG_DFL);
	raise(signal_number);
}

/* on its own stack, as a fault on a bad stack needs */
static void install_signal_stack(void)
{
	stack_t alternate;

	alternate.ss_sp = malloc(0x20000);
	alternate.ss_size = 0x20000;
	alternate.ss_flags = 0;
	if (!alternate.ss_sp || sigaltstack(&alternate, NULL) != 0)
		ps5_log("sigaltstack: failed (%s); a fault on a bad stack will not be caught", strerror(errno));
}

static void install_fault_handler(void)
{
	static const int signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP };
	struct sigaction action;
	size_t index;

	memset(&action, 0, sizeof(action));
	action.sa_sigaction = fault_handler;
	action.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigemptyset(&action.sa_mask);
	for (index = 0; index < sizeof(signals) / sizeof(signals[0]); index++)
	{
		if (sigaction(signals[index], &action, NULL) != 0)
			ps5_log("sigaction(%d): failed (%s)", signals[index], strerror(errno));
	}
}

/* ---------- the guest's ranges */

/* reserves size bytes exactly at address, or nothing: 0, or the kernel's
result, or -1 when it placed the range elsewhere */
static int32_t reserve_exactly(uint64_t address, uint64_t size)
{
	void *at = (void *)address;
	int32_t result = sceKernelReserveVirtualRange(&at, size, 0, DIRECT_UNIT);

	if (result != 0)
		return result;
	if ((uint64_t)at != address)
	{
		sceKernelMunmap(at, size);
		return -1;
	}
	return 0;
}

/* reserves [address, address + size), allocates direct memory and maps it
there, read and write (and execute when asked), zeroed; 1 on success */
static int map_direct(const char *what, uint64_t address, uint64_t size, int executable)
{
	int64_t physical = 0;
	void *at;
	int32_t result;
	uint64_t start = now_us();

	result = reserve_exactly(address, size);
	if (result != 0)
	{
		ps5_log("%s: reserving 0x%llx-0x%llx: %s 0x%08x", what, (unsigned long long)address,
			(unsigned long long)(address + size), result == -1 ? "placed elsewhere," : "refused,", (unsigned)result);
		return 0;
	}
	result = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), size, DIRECT_UNIT,
		PS5_KERNEL_DIRECT_TYPE_CPU, &physical);
	if (result != 0)
	{
		ps5_log("%s: allocating %llu MB of direct memory: 0x%08x", what, (unsigned long long)(size / MEGABYTE),
			(unsigned)result);
		sceKernelMunmap((void *)address, size);
		return 0;
	}
	at = (void *)address;
	result = sceKernelMapDirectMemory(&at, size, PROT_CPU_RW, PS5_KERNEL_MAP_FIXED, physical, DIRECT_UNIT);
	if (result != 0 || (uint64_t)at != address)
	{
		ps5_log("%s: mapping at 0x%llx: 0x%08x, placed at %p", what, (unsigned long long)address, (unsigned)result, at);
		sceKernelMunmap((void *)address, size);
		sceKernelReleaseDirectMemory(physical, size);
		return 0;
	}
	if (executable)
	{
		result = sceKernelMprotect((void *)address, size, PROT_CPU_RWX);
		if (result != 0)
		{
			ps5_log("%s: sceKernelMprotect(read, write, execute): 0x%08x", what, (unsigned)result);
			return 0;
		}
	}
	/* (direct memory comes back holding what it held before) */
	memset((void *)address, 0, size);
	ps5_log("%s: 0x%llx-0x%llx, %llu MB of direct memory%s, zeroed, in %llu ms", what, (unsigned long long)address,
		(unsigned long long)(address + size), (unsigned long long)(size / MEGABYTE),
		executable ? ", read, write and execute" : "", (unsigned long long)((now_us() - start) / 1000));
	return 1;
}

static int image_ready, window_ready, pool_ready;
static uint64_t pool_base, pool_next, pool_end;
/* where the Xbox window was placed */
static uint64_t window_base;

/* whether [address, address + size) lies inside one of the ranges mapped
for the guest */
static int guest_range_mapped(uint64_t address, uint64_t size)
{
	uint64_t ranges[][2] = {
		{ HALO_GUEST_IMAGE_BASE, HALO_GUEST_IMAGE_RESERVE },
		{ window_base, window_ready ? HALO_GUEST_WINDOW_SIZE : 0 },
		{ HALO_GUEST_CUSTOM_EDITION_BASE, HALO_GUEST_CUSTOM_EDITION_END - HALO_GUEST_CUSTOM_EDITION_BASE },
		{ pool_base, pool_ready ? POOL_SIZE : 0 },
	};
	size_t index;

	for (index = 0; index < sizeof(ranges) / sizeof(ranges[0]); index++)
		if (address >= ranges[index][0] && address + size <= ranges[index][0] + ranges[index][1])
			return 1;
	return 0;
}

static void map_guest_ranges(void)
{
	uint64_t candidate;

	image_ready = map_direct("image", HALO_GUEST_IMAGE_BASE, HALO_GUEST_IMAGE_RESERVE, 1);
	/* the window: at 0x80000000 if it is free, else on the next whole 256 MB
	below 4 GB (the guest is built to take it anywhere, HALO_MOVABLE_WINDOW) */
	for (window_base = HALO_GUEST_WINDOW_BASE; window_base + HALO_GUEST_WINDOW_SIZE <= LOW_LIMIT;
		window_base += HALO_GUEST_WINDOW_ALIGNMENT)
		if ((window_ready = map_direct("Xbox window", window_base, HALO_GUEST_WINDOW_SIZE, 0)))
			break;
	if (!window_ready)
		window_base = 0;
	map_direct("Custom Edition's tag cache", HALO_GUEST_CUSTOM_EDITION_BASE,
		HALO_GUEST_CUSTOM_EDITION_END - HALO_GUEST_CUSTOM_EDITION_BASE, 0);
	/* the pool: on a whole 256 MB below 4 GB, clear of the ranges above
	(each candidate is reserved exactly first, so one in use is passed by) */
	for (candidate = 0x10000000ULL; candidate + POOL_SIZE <= LOW_LIMIT && !pool_ready; candidate += 0x10000000ULL)
	{
		if (candidate < HALO_GUEST_CUSTOM_EDITION_END && candidate + POOL_SIZE > HALO_GUEST_CUSTOM_EDITION_BASE)
			continue;
		if (candidate < HALO_GUEST_IMAGE_BASE + HALO_GUEST_IMAGE_RESERVE && candidate + POOL_SIZE > HALO_GUEST_IMAGE_BASE)
			continue;
		if (window_ready && candidate < window_base + HALO_GUEST_WINDOW_SIZE && candidate + POOL_SIZE > window_base)
			continue;
		pool_ready = map_direct("pool", candidate, POOL_SIZE, 0);
		if (pool_ready)
			pool_base = candidate;
	}
	if (!pool_ready)
		ps5_log("pool: no room was found for %llu MB", (unsigned long long)(POOL_SIZE / MEGABYTE));
	pool_next = pool_base;
	pool_end = pool_base + POOL_SIZE;
}

static uint64_t pool_take(uint64_t size)
{
	uint64_t taken;

	size = (size + DIRECT_UNIT - 1) & ~(DIRECT_UNIT - 1);
	if (!pool_ready || pool_end - pool_next < size)
		return 0;
	taken = pool_next;
	pool_next += size;
	return taken;
}

/* takes the stack from the top of the pool, so the guest's own mappings,
which take from the bottom, do not run into it */
static uint64_t stack_take(uint64_t size)
{
	if (!pool_ready || pool_end - pool_next < size)
		return 0;
	pool_end -= size;
	return pool_end;
}

/* ---------- the game */

static const struct halo_guest_header *guest_header;
static const char *import_name_list[MAXIMUM_IMPORTS];
static uint32_t import_count;
static uint32_t guest_tp;
static volatile uint64_t calls;
static volatile int guest_done;
/* (set by import_dispatch, host_bootstrap.S) */
unsigned int current_import;
static unsigned int syscall_counts[512];

static const char *syscall_name(long long number)
{
	if (number >= 0 && number < (long long)(sizeof(guest_syscall_names) / sizeof(guest_syscall_names[0])) &&
		guest_syscall_names[number])
		return guest_syscall_names[number];
	return "?";
}

static void finish(const char *format, ...) __attribute__((format(printf, 1, 2), noreturn));
static void finish(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(guest_end, sizeof(guest_end), format, arguments);
	va_end(arguments);
	guest_running = 0;
	siglongjmp(guest_jump, 1);
}

/* one host call, numbered; the first CALL_LINES only */
static void log_call(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void log_call(const char *format, ...)
{
	char line[512];
	va_list arguments;

	if (calls > CALL_LINES)
		return;
	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	ps5_log("%6llu %s", (unsigned long long)calls, line);
}

/* a NUL-terminated string in guest memory, for the log */
static const char *guest_string(uint64_t address)
{
	if (!address || address >= LOW_LIMIT)
		return "(none)";
	return (const char *)address;
}

/* what the game writes to its standard output or error, a line at a time */
static void log_written(long long descriptor, const void *data, size_t size)
{
	char text[400];
	size_t length = size < sizeof(text) - 1 ? size : sizeof(text) - 1;

	memcpy(text, data, length);
	text[length] = 0;
	if (length && text[length - 1] == '\n')
		text[length - 1] = 0;
	log_call("  the game writes to %s: %s", descriptor == 1 ? "stdout" : "stderr", text);
}

static int64_t guest_clock(long long clock, uint64_t timespec)
{
	struct timespec now;
	/* Linux's clock numbers: REALTIME 0, MONOTONIC 1, and the rest are
	treated as the monotonic clock */
	clockid_t host = clock == 0 ? CLOCK_REALTIME : CLOCK_MONOTONIC;

	if (clock_gettime(host, &now) != 0)
		return -GUEST_EINVAL;
	/* x32's struct timespec: a 64-bit tv_sec, and tv_nsec in 8 bytes */
	((int64_t *)timespec)[0] = now.tv_sec;
	((int64_t *)timespec)[1] = now.tv_nsec;
	return 0;
}

static int64_t guest_system_call(long long number, long long a, long long b, long long c, long long d,
	long long e, long long f)
{
	(void)f;
	if (number >= 0 && number < 512)
		syscall_counts[number]++;
	switch (number)
	{
	case GUEST_SYS_mmap:
	{
		uint64_t taken;

		if ((int)e != -1)
		{
			log_call("  mmap of a file (%d): not served", (int)e);
			return -GUEST_ENOSYS;
		}
		if (d & (0x10 | 0x100000))
		{
			/* MAP_FIXED, or MAP_FIXED_NOREPLACE (the window's and Custom
			Edition's reservations), inside what is already mapped for the
			guest */
			if (!guest_range_mapped((uint64_t)a, (uint64_t)b))
			{
				log_call("  mmap(0x%llx, %lld bytes, fixed): outside the guest's ranges", (unsigned long long)a, b);
				return (d & 0x10) ? -GUEST_ENOMEM : -GUEST_EEXIST;
			}
			if (d & 0x10)
				memset((void *)(uintptr_t)a, 0, (size_t)b);
			return a;
		}
		taken = pool_take((uint64_t)b);
		if (!taken)
		{
			log_call("  mmap(%lld bytes): the pool is out of room", b);
			return -GUEST_ENOMEM;
		}
		memset((void *)taken, 0, (size_t)b);
		return (int64_t)taken;
	}
	case GUEST_SYS_munmap:
	case GUEST_SYS_mprotect:
	case GUEST_SYS_madvise:
	case GUEST_SYS_rt_sigprocmask:
	case GUEST_SYS_rt_sigaction:
	case GUEST_SYS_sigaltstack:
	case GUEST_SYS_sched_yield:
		return 0;
	case GUEST_SYS_brk:
		return 0;
	case GUEST_SYS_set_tid_address:
	case GUEST_SYS_gettid:
	case GUEST_SYS_getpid:
		return 1;
	case GUEST_SYS_getppid:
		return 0;
	case GUEST_SYS_getuid:
	case GUEST_SYS_geteuid:
	case GUEST_SYS_getgid:
	case GUEST_SYS_getegid:
		return 1000;
	case GUEST_SYS_clock_gettime:
		return guest_clock(a, (uint64_t)b);
	case GUEST_SYS_clock_getres:
		if (b)
		{
			((int64_t *)(uintptr_t)b)[0] = 0;
			((int64_t *)(uintptr_t)b)[1] = 1;
		}
		return 0;
	case GUEST_SYS_futex:
		/* one thread: a wait would never end, and a wake wakes no one */
		return (b & 0x7f) == 0 ? -GUEST_EAGAIN : 0;
	case GUEST_SYS_write:
		if (a == 1 || a == 2)
		{
			log_written(a, (const void *)(uintptr_t)b, (size_t)c);
			return c;
		}
		break;
	case GUEST_SYS_writev:
		if (a == 1 || a == 2)
		{
			const uint32_t *vector = (const uint32_t *)(uintptr_t)b;
			long long total = 0;
			int index;

			for (index = 0; index < (int)c; index++)
			{
				if (vector[index * 2 + 1])
					log_written(a, (const void *)(uintptr_t)vector[index * 2], vector[index * 2 + 1]);
				total += vector[index * 2 + 1];
			}
			return total;
		}
		break;
	case GUEST_SYS_getcwd:
		/* (the host's own: libc's getcwd crashes a title, phase 0 B) */
		if (b > 6)
		{
			memcpy((void *)(uintptr_t)a, "/app0", 6);
			return 6;
		}
		return -GUEST_EINVAL;
	case GUEST_SYS_openat:
	case GUEST_SYS_newfstatat:
	case GUEST_SYS_faccessat:
	case GUEST_SYS_readlinkat:
	case GUEST_SYS_mkdirat:
		log_call("  %s(\"%s\"): not found (the bootstrap serves no files)", syscall_name(number),
			guest_string((uint64_t)b));
		return -GUEST_ENOENT;
	case GUEST_SYS_exit:
	case GUEST_SYS_exit_group:
		finish("the game exited with %lld", a);
	}
	log_call("  %s (%lld): not served", syscall_name(number), number);
	return -GUEST_ENOSYS;
}

/* every import's thunk leads here (import_dispatch, host_bootstrap.S) */
int64_t import_called(long long a, long long b, long long c, long long d, long long e, long long f, long long g);
int64_t import_called(long long a, long long b, long long c, long long d, long long e, long long f, long long g)
{
	const char *name = current_import < import_count ? import_name_list[current_import] : "?";
	int64_t result;

	calls++;
	if (!strcmp(name, "host_get_tp"))
		return guest_tp;
	if (!strcmp(name, "host_set_tp"))
	{
		guest_tp = (uint32_t)a;
		log_call("host_set_tp(0x%08x)", guest_tp);
		return 0;
	}
	if (!strcmp(name, "host_errno"))
		return 0;
	if (!strcmp(name, "host_log"))
	{
		log_call("the game logs (%lld): %s", a, guest_string((uint64_t)b));
		return 0;
	}
	if (!strcmp(name, "host_abort"))
		finish("the game aborted: %s", guest_string((uint64_t)a));
	if (!strcmp(name, "host_exit"))
		finish("the game exited with %lld", a);
	if (!strcmp(name, "host_syscall"))
	{
		result = guest_system_call(a, b, c, d, e, f, g);
		if (a != GUEST_SYS_write && a != GUEST_SYS_writev)
			log_call("host_syscall %s(0x%llx, 0x%llx, 0x%llx, 0x%llx) = %lld", syscall_name(a), b, c, d, e,
				(long long)result);
		return result;
	}
	finish("the first import not answered: %s(0x%llx, 0x%llx, 0x%llx, 0x%llx)", name, a, b, c, d);
}

/* /app0/halo_guest.elf, loaded where it is linked; 1 if it is an x32 image
of the guest ABI this host was built for */
static int load_guest(void)
{
	Elf32_Ehdr header;
	Elf32_Phdr segments[16];
	int file;
	int index;
	uint64_t image_end = HALO_GUEST_IMAGE_BASE;

	file = open("/app0/halo_guest.elf", O_RDONLY);
	if (file < 0)
	{
		ps5_log("/app0/halo_guest.elf: %s", strerror(errno));
		return 0;
	}
	if (pread(file, &header, sizeof(header), 0) != (ssize_t)sizeof(header) ||
		memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 || header.e_ident[EI_CLASS] != ELFCLASS32 ||
		header.e_machine != EM_X86_64 || header.e_type != ET_EXEC || header.e_phnum > 16 ||
		header.e_phentsize != sizeof(Elf32_Phdr))
	{
		ps5_log("/app0/halo_guest.elf is not an x32 image (a 32-bit x86-64 executable)");
		close(file);
		return 0;
	}
	if (pread(file, segments, header.e_phnum * sizeof(Elf32_Phdr), header.e_phoff) !=
		(ssize_t)(header.e_phnum * sizeof(Elf32_Phdr)))
	{
		ps5_log("/app0/halo_guest.elf: its program headers cannot be read");
		close(file);
		return 0;
	}
	for (index = 0; index < header.e_phnum; index++)
	{
		const Elf32_Phdr *segment = &segments[index];

		if (segment->p_type != PT_LOAD)
			continue;
		if (segment->p_vaddr < HALO_GUEST_IMAGE_BASE ||
			(uint64_t)segment->p_vaddr + segment->p_memsz > HALO_GUEST_IMAGE_BASE + HALO_GUEST_IMAGE_RESERVE - 4 * MEGABYTE)
		{
			ps5_log("segment %d (0x%08x, %u bytes) is outside the image's range", index, segment->p_vaddr,
				segment->p_memsz);
			close(file);
			return 0;
		}
		if (segment->p_filesz &&
			pread(file, (void *)(uintptr_t)segment->p_vaddr, segment->p_filesz, segment->p_offset) !=
				(ssize_t)segment->p_filesz)
		{
			ps5_log("segment %d cannot be read", index);
			close(file);
			return 0;
		}
		ps5_log("segment %d: 0x%08x-0x%08x, %u bytes from the file, flags %x", index, segment->p_vaddr,
			segment->p_vaddr + segment->p_memsz, segment->p_filesz, segment->p_flags);
		if (segment->p_vaddr + segment->p_memsz > image_end)
			image_end = segment->p_vaddr + segment->p_memsz;
	}
	close(file);

	guest_header = (const struct halo_guest_header *)(uintptr_t)HALO_GUEST_IMAGE_BASE;
	ps5_log("header: magic 0x%08x (%s), ABI %u (the host's %u), image end 0x%08x (loaded to 0x%08llx)",
		guest_header->magic, guest_header->magic == HALO_GUEST_MAGIC ? "right" : "WRONG", guest_header->abi_version,
		HALO_GUEST_ABI_VERSION, guest_header->image_end, (unsigned long long)image_end);
	ps5_log("entry points: start 0x%08x, thread start 0x%08x, attach 0x%08x, detach 0x%08x, constructors 0x%08x-0x%08x",
		guest_header->start, guest_header->thread_start, guest_header->thread_attach, guest_header->thread_detach,
		guest_header->init_array_start, guest_header->init_array_end);
	return guest_header->magic == HALO_GUEST_MAGIC && guest_header->abi_version == HALO_GUEST_ABI_VERSION;
}

/* every import's table entry, pointed at a thunk that loads the import's
index and jumps to import_dispatch */
static int bind_imports(void)
{
	uint64_t *table = (uint64_t *)(uintptr_t)guest_header->import_table;
	const char *names = (const char *)(uintptr_t)guest_header->import_names;
	unsigned char *thunks = (unsigned char *)(uintptr_t)(HALO_GUEST_IMAGE_BASE + HALO_GUEST_IMAGE_RESERVE - MEGABYTE);
	uint32_t index;

	import_count = *(const uint32_t *)(uintptr_t)guest_header->import_count;
	ps5_log("%u imports", import_count);
	if (import_count > MAXIMUM_IMPORTS)
	{
		ps5_log("more imports than the bootstrap has room for (%d)", MAXIMUM_IMPORTS);
		return 0;
	}
	for (index = 0; index < import_count; index++)
	{
		unsigned char *thunk = thunks + index * THUNK_SIZE;
		uint64_t dispatch = (uint64_t)import_dispatch;

		import_name_list[index] = names;
		names += strlen(names) + 1;
		/* mov $index, %r10d; movabs $import_dispatch, %r11; jmp *%r11 */
		thunk[0] = 0x41;
		thunk[1] = 0xba;
		memcpy(thunk + 2, &index, 4);
		thunk[6] = 0x49;
		thunk[7] = 0xbb;
		memcpy(thunk + 8, &dispatch, 8);
		thunk[16] = 0x41;
		thunk[17] = 0xff;
		thunk[18] = 0xe3;
		table[index] = (uint64_t)(uintptr_t)thunk;
	}
	return 1;
}

static uint64_t boot_address;

static void *guest_thread(void *argument)
{
	uint32_t mxcsr = 0;

	(void)argument;
	/* (the floating point mode is a thread's own) */
	ps5_fp_ieee();
	__asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
	ps5_log("the game's thread starts on %p; MXCSR 0x%08x", (void *)&mxcsr, mxcsr);
	install_signal_stack();
	if (sigsetjmp(guest_jump, 1) == 0)
	{
		guest_running = 1;
		((void (*)(uint64_t))(uintptr_t)guest_header->start)(boot_address);
		guest_running = 0;
		snprintf(guest_end, sizeof(guest_end), "the game returned from its start");
	}
	guest_done = 1;
	return NULL;
}

/* the boot structure, argv ("halo") and an empty environment, in guest memory */
static void make_boot(void)
{
	struct halo_guest_boot *boot = (struct halo_guest_boot *)(uintptr_t)pool_take(DIRECT_UNIT);
	uint32_t *pointers = (uint32_t *)(boot + 1);
	char *strings = (char *)(pointers + 8);

	strcpy(strings, "halo");
	pointers[0] = (uint32_t)(uintptr_t)strings;
	pointers[1] = 0;
	pointers[2] = 0;
	boot->argc = 1;
	boot->argv = (uint32_t)(uintptr_t)&pointers[0];
	boot->environment = (uint32_t)(uintptr_t)&pointers[2];
	boot->page_size = 0x4000;
	boot->contiguous_base = (uint32_t)window_base;
	boot->contiguous_size = HALO_GUEST_WINDOW_SIZE;
	boot->image_shift = 0;
	boot_address = (uint64_t)(uintptr_t)boot;
}

void host_bootstrap_run(void)
{
	pthread_attr_t attributes;
	pthread_t thread;
	uint64_t stack, start;
	int result;
	int number;
	char line[1024];
	size_t used;

	ps5_log("bootstrap: the guest's ranges, the image, its start-up (port/ps5/host/host_bootstrap.c)");
	install_signal_stack();
	install_fault_handler();
	map_guest_ranges();
	if (!image_ready || !window_ready || !pool_ready)
	{
		ps5_log("bootstrap: the game was not started: the image's range, the window or the pool is missing");
		return;
	}
	if (!load_guest())
	{
		ps5_log("bootstrap: the game was not started: /app0/halo_guest.elf is not the image this host takes");
		return;
	}
	if (!bind_imports())
	{
		ps5_log("bootstrap: the game was not started: its imports were not bound");
		return;
	}
	make_boot();

	stack = stack_take(GUEST_STACK_SIZE);
	pthread_attr_init(&attributes);
	pthread_attr_setstack(&attributes, (void *)stack, GUEST_STACK_SIZE);
	result = pthread_create(&thread, &attributes, guest_thread, NULL);
	pthread_attr_destroy(&attributes);
	if (result != 0)
	{
		ps5_log("bootstrap: the game was not started: pthread_create: %d", result);
		return;
	}
	start = now_us();
	while (!guest_done && now_us() - start < GUEST_SECONDS * 1000000ULL)
		usleep(10000);
	if (!guest_done)
	{
		ps5_log("bootstrap: the game is still running after %d s, after %llu calls; the last was %s", GUEST_SECONDS,
			(unsigned long long)calls, current_import < import_count ? import_name_list[current_import] : "none");
		return;
	}
	pthread_join(thread, NULL);
	ps5_log("after %llu calls to the host in %.3f s, %llu MB of the pool taken", (unsigned long long)calls,
		(now_us() - start) / 1e6, (unsigned long long)((pool_next - pool_base) / MEGABYTE));
	used = (size_t)snprintf(line, sizeof(line), "system calls:");
	for (number = 0; number < 512 && used < sizeof(line); number++)
	{
		if (syscall_counts[number])
			used += (size_t)snprintf(line + used, sizeof(line) - used, " %s %u", syscall_name(number),
				syscall_counts[number]);
	}
	ps5_log_line(line);
	ps5_log("bootstrap: %s", guest_end);
}
