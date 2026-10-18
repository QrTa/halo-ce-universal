/*
PS5_HOST.H

What the PS5 host's own files share (port/ps5/PS5.md): the log, which
host_main.c keeps, and the bootstrap it runs in phase 1 (host_bootstrap.c,
which phase 2 deletes).
*/

#ifndef PS5_HOST_H
#define PS5_HOST_H

/* one line to the log file and the kernel log (host_main.c), with no
newline: the log adds it. Safe from any thread: each line is one write. */
void ps5_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
void ps5_log_line(const char *line);

/* phase 1's stand-in for the host (host_bootstrap.c): the guest's ranges,
/app0/halo_guest.elf loaded and started, and its host calls logged until
the first import it does not answer, a fault, its exit or 60 s. Returns
when the game has stopped; its last line says why. */
void host_bootstrap_run(void);

#endif
