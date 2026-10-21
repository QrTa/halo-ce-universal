/*
UPDATE.H

What the desktop self-updater (updater.c) needs of the system: a download
over HTTPS, and putting files in place of the running game's and starting
it again. posix_update.c does it on Linux (Mbed TLS, the system's
certificate authorities), win32_update.c on Windows (WinHTTP). The Android
app updates itself in Java (port/android).

And a request over HTTPS with its answer kept in memory (update_request),
which the community server's leaderboards and daily challenges ask with
(community_net.c), on every platform: posix_update.c's on Linux and in the
Android, Linux desktop and PS5 hosts (port/android/host/host_https.c, which
the guests reach through port/android/guest/runtime/guest_misc.c),
win32_update.c's on Windows, the Switch's ssl service
(port/switch/host/host_https.c) and the browser's (port/web/src/web_https.c).

Only integers and strings cross here: the two sides are compiled with
different structure layouts.
*/

#ifndef UPDATE_H
#define UPDATE_H

/* bytes received so far, of total (0 when the size is not known) */
typedef void (*update_progress_proc)(void *context, unsigned long long received, unsigned long long total);

/* fetches the https:// url (following redirects) into the file at path,
with the certificate and host name checked, and no more than maximum bytes
of it; 1 on success, else 0 and why in error */
int update_download(const char *url, const char *path, unsigned long long maximum, update_progress_proc progress,
	void *context, char *error, int error_size);

/* sends one request to the https:// url (no redirects followed): method
"GET" or "POST", with the header lines in headers (each ending in "\r\n", or
NULL) and the text in body (NULL for none), and keeps up to response_size - 1
bytes of the answer's body in response, NUL terminated. The certificate and
host name are checked as update_download's. The answer's HTTP status,
whatever it is; 0 when none came, and why in error */
int update_request(const char *method, const char *url, const char *headers, const char *body, char *response,
	int response_size, char *error, int error_size);

/* the full path of this executable; 1 on success */
int update_executable_path(char *path, int size);

/* puts the file at new_path in place of the one at path, which is renamed
to old_path first (a running executable can be renamed, not overwritten),
keeping its permissions; 1 on success */
int update_replace_file(const char *path, const char *new_path, const char *old_path);

/* deletes the file (or empty folder) at path, if there is one */
void update_delete_file(const char *path);

/* makes the directory at path, if there is none; 1 when it is there */
int update_make_directory(const char *path);

/* starts the executable at path as a process of its own, with no arguments,
this process's environment and none of its files; 1 on success */
int update_launch(const char *path);

#endif
