/*
HOST_UPDATE_NONE.C

The desktop game's self-updater (port/linux/src/updater.c, update.h) asks
the host to download a release, replace the executable and start it again
(port/linux/host/host_desktop.c, over posix_update.c). A title is updated
by installing it again, so on the PS5 each of these answers "unavailable",
as a build without the updater does: the path of the executable is the
title's eboot, and the rest fail, each saying so once in the log.
*/

#include "host.h"
#include "ps5_host.h"

#include <stdio.h>

static void unavailable(int *logged, const char *what)
{
	if (!__atomic_exchange_n(logged, 1, __ATOMIC_RELAXED))
		host_logf(HOST_LOG_INFO, "updater: %s is not available on the PS5 (a title is updated by installing it again)",
			what);
}

int host_update_download(const char *url, const char *path, uint32_t progress, uint32_t context, char *error,
	int error_size)
{
	static int logged;

	(void)url;
	(void)path;
	(void)progress;
	(void)context;
	unavailable(&logged, "downloading a release");
	if (error && error_size > 0)
		snprintf(error, (size_t)error_size, "The PS5 build cannot update itself.");
	return 0;
}

int host_update_executable_path(char *path, int size)
{
	if (!path || size <= 0)
		return 0;
	return snprintf(path, (size_t)size, "%s/eboot.bin", PS5_TITLE_FOLDER) < size;
}

int host_update_replace_file(const char *path, const char *new_path, const char *old_path)
{
	static int logged;

	(void)path;
	(void)new_path;
	(void)old_path;
	unavailable(&logged, "replacing a file");
	return 0;
}

void host_update_delete_file(const char *path)
{
	static int logged;

	(void)path;
	unavailable(&logged, "deleting a file");
}

int host_update_make_directory(const char *path)
{
	static int logged;

	(void)path;
	unavailable(&logged, "making a folder");
	return 0;
}

int host_update_launch(const char *path)
{
	static int logged;

	(void)path;
	unavailable(&logged, "starting the new version");
	return 0;
}
