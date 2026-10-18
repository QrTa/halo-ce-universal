/*
PS5_HOST.H

What the PS5's own host files (port/ps5/host; port/ps5/PS5.md) share
beyond the Android host's host.h: the log, the data folder, the thread id,
the futexes, and the reports each file writes when the game ends.
*/

#ifndef PS5_HOST_H
#define PS5_HOST_H

#include <stdint.h>

/* the game's data, settings, saves and log (a folder the player fills over
FTP), and the title's own read-only folder */
#define PS5_DATA_FOLDER "/data/halo"
#define PS5_TITLE_FOLDER "/app0"

/* one line to the log file and the kernel log (host_main.c), with no
newline: the log adds it. Safe from any thread and from signal handlers:
each line is one write, without locks or stdio. */
void ps5_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
void ps5_log_line(const char *line);

/* the monotonic clock in nanoseconds, and the time since the host started
in seconds, for the log */
uint64_t ps5_now_ns(void);
double ps5_seconds(void);

/* the calling thread's id: the console's (pthread_getthreadid_np, which
libkernel exports to a title), distinct per thread; the guest's gettid
(host_syscall.c) and the debug file's (host_debug_ps5.c) */
int ps5_thread_id(void);

/* the guest's futex calls (host_futex.c): Linux's operations on a 32-bit
word below 4 GB, over FreeBSD's _umtx_op; the result as the guest's
system call returns it (0, a count, or -errno in Linux's numbers) */
long host_futex(uint32_t address, int operation, uint32_t value, uint64_t timeout_address, uint64_t address2,
	uint32_t value3);

/* the reports the game's end writes, each file its own (host_main.c calls
them, after "the game exited" or a fatal line, before "the host ends") */
void ps5_syscall_report(void);   /* host_syscall.c: the calls answered -ENOSYS, by number */
void ps5_memory_report(void);    /* host_memory.c: __wrap_mmap's calls, the pools, direct memory */
void ps5_gl_report(void);        /* host_gl_none.c: the GL names the Vulkan image called */
void ps5_sdl_report(void);       /* host_sdl_ps5.c: frames, the 2D renderer's presents, audio */

/* the host's heap (the platform layer's, in direct memory, which never
gives memory back) and direct memory's largest free block, in MB, for the
heartbeat (host_memory.c) */
void ps5_memory_figures(double *heap_mb, double *heap_peak_mb, double *direct_free_mb);

/* config.toml's debug.gpu_stats, read by host_main.c: the GL names are
reported every 10 s, not only at the end (host_gl_none.c) */
extern int host_gl_statistics;

#endif
