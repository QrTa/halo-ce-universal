/*
HOST_MAIN.C

Entry point of the PS5 port: a title (eboot.bin, port/ps5/README.md) that
runs the x32 guest image of `ninja ps5` (the game compiled as 32-bit code
for x86-64, with the Vulkan renderer and the Xbox window movable) in the
Android port's host library, as the Linux host does
(port/linux/host/host_main.c) and Android's host_main.c, with the PS5's own
files in place of the ones that touch the system (port/ps5/PS5.md, "The
host"). The plan is port/ps5/PS5.md.

In order, the title:

- sets the floating point mode the game computes in (a title starts with
  flush-to-zero; each thread sets its own, host_debug_ps5.c), makes sure
  descriptors 0 to 2 are taken (the guest's standard streams go to the
  log, so no file of its may be given those numbers), and opens the log;
- logs the build, the system software, the pins and the proofs that the
  link holds SDL2, RADV and glslang, and what the data folder holds;
- sends the host library's log to the PS5's (host_log_set_sink), and needs
  /data/halo, the game's data folder (its saves in save/ there);
- reads /app0/environment.txt, runs the network's smoke test if it asks
  (HALO_PS5_NET_TEST), and brings the network up (ps5_net_start,
  host_net_ps5.c: internet play's brokers.txt written beside config.toml,
  this machine's address and the DTLS certificate logged);
- claims the guest's three fixed ranges as direct memory
  (host_memory_reserve_early), installs the fault handlers and the signal
  stack;
- brings the Vulkan renderer up on RADV (host_vk_startup: the instance and
  the physical device; the device and the swapchain on VideoOut's display
  plane are made at the game's first submission, port/ps5/PS5.md, phase 4),
  with RADV's standard error sent to the kernel log and the renderer's
  debugging switches read from the environment; the console has no other
  renderer, so a renderer that does not come up ends the title;
- starts the game's thread, whose stack is guest memory, and waits.

The game's thread passes the guest its environment (HOME and the saves'
folder in the data folder, the time zone, and the HALO_ settings the host
was given), loads /app0/halo_guest.elf, reads config.toml's debugging
settings, and runs the game's main; the game ends the process itself
(host_exit, host_common.c).

The log is one file, /data/halo/halo.log, and the kernel log (klogsrv,
port 3232), where each line is prefixed "halo: "; each line is written as it
is made, with no lock and no stdio, so signal handlers log through it and a
crash leaves everything up to it. tools/ps5_deploy.py follows the kernel
log, stops at the host's last line ("the host ends", or the hold's), and
fetches the file.

When the game ends, its end is reported (the system calls answered
-ENOSYS, the GL names called, the mmap calls through the wrap, the
renderer's and the stand-ins' counts) before "the host ends". The host
library's host_exit and host_fatal are shared files' (host_common.c) and
end in _exit; their last lines, "the game exited (<code>)" and the fatal
line, are what the log's sink here recognises as the end.

Two files in the title's folder change a run, for the deploy's tests:
/app0/hold.txt keeps the title for 120 s after the game ends, so it can be
killed from the PC; /app0/environment.txt, NAME=VALUE lines, sets the
host's environment (HALO_ settings are passed on to the game: HALO_EXIT_AFTER
ends it after that many seconds; HALO_PS5_CRASH_TEST makes the host fault on
purpose once the image is loaded, to show the crash report;
HALO_PS5_NET_TEST runs the console's socket checks before the game,
host_net_ps5.c). The renderer's
switches are read from there too, since the desktop game's settings table
keeps them for Android: HALO_VK_SELF_TEST=1 (the backend's and the game's
self-tests), HALO_VK_PRESENT_MARKER=1 (a red and a green square at the
picture's top corners), HALO_GPU_STATS=1 (the draws' and the GL names'
counts as the run goes) and HALO_PS5_SCREENSHOT=<seconds> (the presented
picture written to /data/halo/screenshots, host_vk_screenshot_ps5.c); RADV's
own (RADV_DEBUG, RADV_PERFTEST, MESA_VK_*) reach the driver as they are.

The title ends in _exit, which the kernel log follows with a SIGSYS for
every title (phase 0 B); the README says so.
*/

#include "host.h"
#include "ps5_host.h"
#include "tomlc17.h"

/* HALO_PROBE_BUILD, the commit the title is built from (written at every
build by tools/android_build_stamp.py, as the Android host's) */
#include "ps5_build.h"

#include <SDL2/SDL_version.h>
#include <vulkan/vulkan.h>
#include "glslang/Include/glslang_c_interface.h"

#include "vk_shaders.h"

#include <ps5platform/fp.h>
#include <ps5platform/kernel.h>
#include <ps5platform/klog.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* the pins of PS5_Vulkan, the SDK fork, PS5_Mesa, pacbrew and glslang the
title is built with (tools/ps5_build.py) */
#ifndef HALO_PS5_PINS
#define HALO_PS5_PINS "unknown"
#endif

#ifndef HOST_VK_GLSLANG_TAG
#define HOST_VK_GLSLANG_TAG "unknown"
#endif

#define HOLD_SECONDS 120
#define GAME_STACK_SIZE (16 * 1024 * 1024)
#define GUEST_IMAGE PS5_TITLE_FOLDER "/halo_guest.elf"
#define ENVIRONMENT_FILE PS5_TITLE_FOLDER "/environment.txt"

/* RADV's ICD entry point: the archive has no vkGetInstanceProcAddr
(port/ps5/PS5.md, phase 0 B) */
PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);

/* the image is the Vulkan one, which makes no GL context (host.h); the
console has no other */
int host_renderer_vulkan;

/* a notification on the screen (the probes' declaration, after OpenOrbis's) */
struct notify_request
{
	char unused[45];
	char message[3075];
};
int sceKernelSendNotificationRequest(int device, struct notify_request *request, size_t size, int blocking);

extern char **environ;

/* ---------- time */

static uint64_t start_ns;

uint64_t ps5_now_ns(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

double ps5_seconds(void)
{
	return (ps5_now_ns() - start_ns) / 1e9;
}

/* ---------- the log */

static int log_file = -1;

void ps5_log_line(const char *line)
{
	char text[1100];
	int length = snprintf(text, sizeof(text), "halo: %s\n", line);

	if (length < 0)
		return;
	if ((size_t)length >= sizeof(text))
	{
		length = sizeof(text) - 1;
		text[length - 1] = '\n';
	}
	sceKernelDebugOutText(0, text);
	/* (the file has the line without the kernel log's prefix) */
	if (log_file >= 0 && write(log_file, text + 6, (size_t)length - 6) < 0)
		return;
}

void ps5_log(const char *format, ...)
{
	char line[1024];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	ps5_log_line(line);
}

static void notify(const char *message)
{
	struct notify_request request;

	memset(&request, 0, sizeof(request));
	snprintf(request.message, sizeof(request.message), "%s", message);
	sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

static int is_folder(const char *path)
{
	struct stat status;

	return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

/* the log file in the data folder, or the title's own when there is none;
FTP's user sees only what everyone may, so it is made 0666 */
static void log_open(void)
{
	const char *path = is_folder(PS5_DATA_FOLDER) ? PS5_DATA_FOLDER "/halo.log" : PS5_TITLE_FOLDER "/halo.log";

	log_file = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
	if (log_file >= 0)
		chmod(path, 0666);
	if (log_file < 0)
		ps5_log("halo.log: cannot open %s; the kernel log only", path);
	else if (strcmp(path, PS5_DATA_FOLDER "/halo.log"))
		ps5_log("halo.log: writing to %s and the kernel log (there is no " PS5_DATA_FOLDER ")", path);
	else
		ps5_log("halo.log: writing to %s and the kernel log", path);
}

/* Descriptors 0, 1 and 2: a title starts without them, and the guest's
writes to 1 and 2 go to the log (host_syscall.c), so the first files
opened, the guest's or the host's, must not be given those numbers. Each
that is free is taken by a descriptor of the title's own eboot, read only,
which nothing reads. Returns which were free, as bits. */
static int take_standard_descriptors(void)
{
	int fd, taken = 0;

	for (fd = 0; fd < 3; fd++)
	{
		int opened;

		if (fcntl(fd, F_GETFD) != -1)
			continue;
		/* (the lowest free number, which is this one) */
		opened = open(PS5_TITLE_FOLDER "/eboot.bin", O_RDONLY);
		if (opened == fd)
			taken |= 1 << fd;
		else if (opened >= 0)
			close(opened);
	}
	return taken;
}

/* the host library's log (host.h): every line, with its level */
static void end_of_game(void);
static int game_ended;

static void log_sink(int priority, const char *line)
{
	const char *level = priority >= HOST_LOG_FATAL ? "fatal: " : priority >= HOST_LOG_ERROR ? "error: " :
		priority >= HOST_LOG_WARN ? "warning: " : "";

	ps5_log("%s%s", level, line);
	/* host_common.c's host_exit and host_fatal: their lines are the last
	before _exit, and the end is reported after them */
	if (priority >= HOST_LOG_FATAL || !strncmp(line, "the game exited (", 17))
		end_of_game();
}

/* ---------- the end */

static void end_of_game(void)
{
	struct stat status;

	if (__atomic_exchange_n(&game_ended, 1, __ATOMIC_ACQ_REL))
		return;
	ps5_log("the game ended at %.1f s", ps5_seconds());
	host_vk_exit();
	ps5_sdl_report();
	ps5_syscall_report();
	ps5_gl_report();
	ps5_memory_report();
	if (stat(PS5_TITLE_FOLDER "/hold.txt", &status) == 0)
	{
		ps5_log("holding for %d s before the end (" PS5_TITLE_FOLDER "/hold.txt), so the title can be killed",
			HOLD_SECONDS);
		sleep(HOLD_SECONDS);
	}
	notify("Halo: Combat Evolved ends");
	/* (the kernel log then reports exit() and a SIGSYS, as for every title
	that ends: port/ps5/README.md) */
	ps5_log("the host ends");
}

/* host_fatal's message box (host_common.c calls SDL3's, which is SDL2's
in this link, and SDL2's video is never started here: phase 0 B): a
notification on the screen instead. The link wraps the name
(--wrap=SDL_ShowSimpleMessageBox, tools/ps5_build.py). */
__attribute__((visibility("hidden"))) bool __wrap_SDL_ShowSimpleMessageBox(uint32_t flags, const char *title,
	const char *message, void *window);

bool __wrap_SDL_ShowSimpleMessageBox(uint32_t flags, const char *title, const char *message, void *window)
{
	char text[512];

	(void)flags;
	(void)window;
	snprintf(text, sizeof(text), "%s: %s", title ? title : "Halo", message ? message : "");
	notify(text);
	return true;
}

/* ---------- what the title is */

static void log_identity(void)
{
	struct ps5_kernel_sw_version version;

	ps5_log("Halo: Combat Evolved for the PS5, build %s (phase 5: the network)", HALO_PROBE_BUILD);
	memset(&version, 0, sizeof(version));
	version.size = sizeof(version);
	if (sceKernelGetSystemSwVersion(&version) == 0)
		ps5_log("system software %.28s (0x%08x)", version.text, version.version);
	else
		ps5_log("system software: unknown");
	ps5_log("built with %s", HALO_PS5_PINS);
}

/* that SDL2, RADV and glslang are in the link and answer, before the
renderer uses them */
static void log_link_proofs(void)
{
	SDL_version compiled, linked;
	PFN_vkVoidFunction create_instance;
	int initialized;

	SDL_VERSION(&compiled);
	SDL_GetVersion(&linked);
	ps5_log("SDL2: SDL_GetVersion: %d.%d.%d (compiled against %d.%d.%d)", linked.major, linked.minor, linked.patch,
		compiled.major, compiled.minor, compiled.patch);
	create_instance = vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
	ps5_log("RADV: vk_icdGetInstanceProcAddr(NULL, \"vkCreateInstance\"): %p (%s)", (void *)create_instance,
		create_instance ? "non-null" : "NULL");
	initialized = glslang_initialize_process();
	ps5_log("glslang: glslang_initialize_process: %d", initialized);
	glslang_finalize_process();
	ps5_log("glslang: glslang_finalize_process returned");
}

/* whether the data folder is there, and what is in it, on one line */
static void log_data_folder(void)
{
	char line[900];
	size_t used;
	int count = 0, listed = 0;
	DIR *folder;
	struct dirent *entry;

	folder = is_folder(PS5_DATA_FOLDER) ? opendir(PS5_DATA_FOLDER) : NULL;
	if (!folder)
	{
		ps5_log(PS5_DATA_FOLDER ": not found");
		return;
	}
	used = (size_t)snprintf(line, sizeof(line), PS5_DATA_FOLDER ": found; holds");
	while ((entry = readdir(folder)))
	{
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
		count++;
		if (used + strlen(entry->d_name) + 64 < sizeof(line))
		{
			used += (size_t)snprintf(line + used, sizeof(line) - used, "%s %s%s", listed ? "," : "", entry->d_name,
				entry->d_type == DT_DIR ? "/" : "");
			listed++;
		}
	}
	closedir(folder);
	if (!count)
		snprintf(line + used, sizeof(line) - used, " nothing");
	else if (listed < count)
		snprintf(line + used, sizeof(line) - used, " and %d more", count - listed);
	ps5_log_line(line);
}

/* /app0/environment.txt's NAME=VALUE lines into the host's environment */
static void read_environment_file(void)
{
	char text[4096];
	char *line, *next;
	ssize_t length;
	int fd = open(ENVIRONMENT_FILE, O_RDONLY);

	if (fd < 0)
		return;
	length = read(fd, text, sizeof(text) - 1);
	close(fd);
	if (length <= 0)
		return;
	text[length] = 0;
	for (line = text; line && *line; line = next)
	{
		char *equals;

		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		line[strcspn(line, "\r")] = 0;
		equals = strchr(line, '=');
		if (!equals || equals == line || line[0] == '#')
			continue;
		*equals = 0;
		setenv(line, equals + 1, 1);
		ps5_log(ENVIRONMENT_FILE ": %s=%s", line, equals + 1);
	}
}

/* ---------- the game */

/* the main thread's mark in its __thread storage, which the game's thread
must not see (host_thread.c keeps the guest's thread pointer there) */
#define MAIN_THREAD_MARK 0x0badf00du

/* the guest's thread pointer, in host_thread.c's __thread storage (the
guest's imports; host.h does not declare them) */
uint32_t host_get_tp(void);
void host_set_tp(uint32_t thread);

static int main_thread_id;

static void *thread_check_main(void *context)
{
	*(volatile int *)context = ps5_thread_id();
	return NULL;
}

/* __thread storage and the thread id on a thread the platform layer's
pthread_create wrapper made (port/ps5/PS5.md, phase 2, "Threads"), and a
thread made from this one, whose stack is guest memory, as the game makes
its own */
static void check_threads(void)
{
	static volatile int made_id;
	int made_error;

	uint32_t before = host_get_tp(), after;
	int id = ps5_thread_id();

	host_set_tp(0x12345678u);
	after = host_get_tp();
	host_set_tp(0);
	ps5_log("thread checks: __thread on the game's thread %s (it read %08x before it was set, the main thread "
		"holds %08x), and %s back; thread ids: main %d, the game's %d (%s)",
		before == 0 ? "is its own" : "is SHARED", before, MAIN_THREAD_MARK, after == 0x12345678u ? "it reads" :
		"it DOES NOT read", main_thread_id, id, id != main_thread_id ? "distinct" : "THE SAME");
	made_error = host_native_thread_create(thread_check_main, (void *)&made_id, 256 * 1024);
	if (!made_error)
	{
		uint64_t until = ps5_now_ns() + 1000000000ull;

		while (!made_id && ps5_now_ns() < until)
			usleep(1000);
	}
	ps5_log("thread checks: a thread made from the game's thread: %s (%d, %s), its id %d", made_error ? "FAILED" : "made",
		made_error, made_error ? strerror(made_error) : "-", made_id);
}

/* config.toml's debug.gpu_stats (Android's host_main.c reads it the same way) */
static void read_gpu_stats(const char *path)
{
	toml_result_t result = toml_parse_file_ex(path);

	if (!result.ok)
		return;
	{
		toml_datum_t statistics = toml_seek(result.toptab, "debug.gpu_stats");

		/* (HALO_GPU_STATS in the environment has set it already, if it is set) */
		if (statistics.type == TOML_BOOLEAN && statistics.u.boolean)
			host_gl_statistics = 1;
	}
	toml_free(result);
}

/* /app0/halo_guest.elf, whole, in the host's heap; NULL if it cannot be read */
static void *read_image(size_t *size)
{
	struct stat status;
	char *image;
	size_t done = 0;
	int fd = open(GUEST_IMAGE, O_RDONLY);

	if (fd < 0 || fstat(fd, &status) != 0 || status.st_size <= 0)
	{
		ps5_log(GUEST_IMAGE ": %s", strerror(errno));
		if (fd >= 0)
			close(fd);
		return NULL;
	}
	image = malloc((size_t)status.st_size);
	while (image && done < (size_t)status.st_size)
	{
		ssize_t got = read(fd, image + done, (size_t)status.st_size - done);

		if (got <= 0)
		{
			free(image);
			image = NULL;
			break;
		}
		done += (size_t)got;
	}
	close(fd);
	*size = done;
	return image;
}

/* what the game reads of the environment: its settings (HALO_), passed on
from the host's (environment.txt) */
static void pass_settings(struct host_environment *environment)
{
	char **entry;

	for (entry = environ; entry && *entry; entry++)
	{
		if (!strncmp(*entry, "HALO_", 5) && strncmp(*entry, "HALO_PS5_", 9))
			host_environment_add(environment, *entry);
	}
}

static void *game_main(void *unused)
{
	struct host_environment environment = { { 0 }, 0 };
	uint32_t mxcsr = 0;
	char zone[64];
	void *image;
	size_t size = 0;
	uint64_t start;

	(void)unused;
	__asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
	ps5_log("the game's thread starts on %p, at %.1f s; MXCSR 0x%08x", (void *)&mxcsr, ps5_seconds(), mxcsr);
	check_threads();

	/* The guest is the desktop game: its data is found where the
	executable is (/proc/self/exe is /data/halo/halo, host_syscall.c), and
	on a first start without it the game offers to extract it from a disc
	image there (xbox_files.c). paths.data (HALO_DATA_ROOT) is not set: it
	would skip that offer. */
	host_environment_set(&environment, "HOME", PS5_DATA_FOLDER);
	host_environment_set(&environment, "XDG_DATA_HOME", PS5_DATA_FOLDER);
	host_environment_set(&environment, "HALO_SAVE_ROOT", PS5_DATA_FOLDER "/save");
	host_time_zone(zone, sizeof(zone));
	host_environment_set(&environment, "TZ", zone);
	pass_settings(&environment);

	start = ps5_now_ns();
	image = read_image(&size);
	if (!image)
		host_fatal("cannot read the game image " GUEST_IMAGE);
	ps5_log(GUEST_IMAGE ": %zu bytes, read in %.1f ms", size, (ps5_now_ns() - start) / 1e6);
	if (host_load_image(image, size, NULL, 0) != 0)
		host_fatal("cannot load the game image");
	free(image);

	if (getenv("HALO_PS5_CRASH_TEST") && atoi(getenv("HALO_PS5_CRASH_TEST")) > 0)
	{
		ps5_log("HALO_PS5_CRASH_TEST: writing to the unmapped guest address 0x08000000 from the game's thread");
		*(volatile uint32_t *)(uintptr_t)0x08000000u = 0x0badc0deu;
	}

	host_apply_debug_settings(PS5_DATA_FOLDER "/config.toml", NULL);
	read_gpu_stats(PS5_DATA_FOLDER "/config.toml");
	ps5_log("the game starts at %.1f s", ps5_seconds());
	host_run_guest_main(host_make_boot(&environment, "halo"));
}

/* ---------- the renderer */

/* an environment switch of the renderer's: 1 if NAME is set to a number
above 0 (logged), else 0 */
static int switch_set(const char *name)
{
	const char *value = getenv(name);
	int number = value ? atoi(value) : 0;

	if (number > 0)
		ps5_log("renderer: %s=%d", name, number);
	return number > 0 ? number : 0;
}

/* a folder FTP can list (0777: the console's FTP server sees only what
everyone may), made before the renderer makes it 0770 */
static void open_folder(const char *path)
{
	if (mkdir(path, 0777) == 0)
		chmod(path, 0777);
}

/* Vulkan on RADV, before the game's thread, after the guest's ranges are
claimed (as Android's host_main.c starts it after the image's range is
reserved): the instance and the physical device here, the device and the
swapchain at the game's first submission */
static void renderer_start(void)
{
	char line[600];
	char folder[256];

	ps5_log("RADV's standard error to the kernel log, prefixed [radv]: %s",
		ps5_klog_capture_stderr("[radv] ") == 0 ? "yes" : strerror(errno));
	host_vk_self_test = switch_set("HALO_VK_SELF_TEST") != 0;
	host_vk_present_marker = switch_set("HALO_VK_PRESENT_MARKER") != 0;
	host_gl_statistics = switch_set("HALO_GPU_STATS") != 0;
	ps5_screenshot_seconds = switch_set("HALO_PS5_SCREENSHOT");
	/* (the renderer's caches, host_vk_shaders.c's layout, open to FTP) */
	open_folder(PS5_DATA_FOLDER "/vk_cache");
	open_folder(PS5_DATA_FOLDER "/vk_cache/spirv");
	snprintf(folder, sizeof(folder), PS5_DATA_FOLDER "/vk_cache/spirv/%s-%d", HOST_VK_GLSLANG_TAG,
		VK_SHADER_GENERATOR_VERSION);
	open_folder(folder);
	open_folder(PS5_DATA_FOLDER "/vk_cache/pipelines");
	open_folder(PS5_DATA_FOLDER "/vk_cache/failed");
	{
		uint64_t start = ps5_now_ns();

		host_renderer_vulkan = host_vk_startup("", 0, line, sizeof(line));
		ps5_log("renderer: %s (%.1f ms)", line, (ps5_now_ns() - start) / 1e6);
	}
	if (!host_renderer_vulkan)
		host_fatal("the Vulkan renderer did not come up on RADV, and the console has no other: %s", line);
}

int main(void)
{
	int taken, waited;

	start_ns = ps5_now_ns();
	/* a title starts with flush-to-zero and denormals-are-zero set; the game
	computes as IEEE (PS5_FINDINGS.md, "Floating point") */
	ps5_fp_ieee();
	taken = take_standard_descriptors();
	/* (once, a title started 15 s after another had ended did not see
	/data/halo, and saw it when started again: it is given a moment) */
	for (waited = 0; waited < 20 && !is_folder(PS5_DATA_FOLDER); waited++)
		usleep(100000);
	log_open();
	notify("Halo: Combat Evolved starts");
	log_identity();
	ps5_log("descriptors 0 to 2: %s", taken == 7 ? "free at start, taken with " PS5_TITLE_FOLDER "/eboot.bin" :
		taken ? "some free at start, taken with " PS5_TITLE_FOLDER "/eboot.bin" : "open at start");
	log_link_proofs();
	if (waited)
		ps5_log(PS5_DATA_FOLDER ": waited %d ms for it", waited * 100);
	log_data_folder();

	host_log_set_sink(log_sink);
	if (!is_folder(PS5_DATA_FOLDER))
	{
		ps5_log("the game needs " PS5_DATA_FOLDER ", with the game's disc image in it (port/ps5/README.md)");
		ps5_log("the host ends");
		_exit(1);
	}
	if (!is_folder(PS5_DATA_FOLDER "/save") && mkdir(PS5_DATA_FOLDER "/save", 0777) != 0)
		ps5_log(PS5_DATA_FOLDER "/save: cannot be made (%s)", strerror(errno));
	/* (the game's own relative paths are made absolute on the data folder,
	host_syscall.c; this is for the host's file helpers, posix_files.c) */
	if (chdir(PS5_DATA_FOLDER) != 0)
		ps5_log("chdir(" PS5_DATA_FOLDER "): %s", strerror(errno));
	read_environment_file();
	if (getenv("HALO_PS5_NET_TEST") && atoi(getenv("HALO_PS5_NET_TEST")) > 0)
		ps5_net_test();
	ps5_net_start();

	host_memory_reserve_early();
	if (host_memory_fixed_unavailable())
		host_fatal("the guest's memory could not be placed below 4 GB");
	host_install_signal_handlers();
	host_signal_stack_install();
	renderer_start();
	main_thread_id = ps5_thread_id();
	host_set_tp(MAIN_THREAD_MARK);
	if (host_native_thread_create(game_main, NULL, GAME_STACK_SIZE) != 0)
		host_fatal("cannot start the game's thread");
	/* the game ends the process itself (host_exit) */
	for (;;)
		pause();
}
