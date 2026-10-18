/*
HOST_MAIN.C

Entry point of the PS5 port: a title (eboot.bin, port/ps5/README.md) that
will run the x32 guest image of `ninja linux_x86_64`, the game compiled as
32-bit code for x86-64, as the Linux host does (port/linux/host/host_main.c)
and the Switch's (port/switch/host/host_main.c) on their systems. The plan is
port/ps5/PS5.md.

This file is written to stay. It starts the title: the floating point mode,
the log, the build's identity, the proofs that the link holds SDL2, RADV
and glslang, and the data folder; then it runs the host. In phase 1 the host
is the bootstrap (host_bootstrap.c), which loads the image and follows its
start-up to the first SDL call; phase 2 puts the Android host's start-up in
its place.

The log is one file, /data/halo/halo.log (the data folder, which FTP
reaches; /app0/halo.log, the title's own folder, when there is none), and
the kernel log (klogsrv, port 3232), where each line is prefixed "halo: ".
Each line is written as it is made, so a crash leaves everything up to it.
tools/ps5_deploy.py follows the kernel log and fetches the file.

A file /app0/hold.txt (tools/ps5_deploy.py --hold puts it there) keeps the
title running for 120 s after its last line, so that it can be killed from
the PC.
*/

#include "ps5_host.h"

/* HALO_PROBE_BUILD, the commit the title is built from (written at every
build by tools/android_build_stamp.py, as the Android host's) */
#include "ps5_build.h"

#include <SDL2/SDL_version.h>
#include <vulkan/vulkan.h>
#include "glslang/Include/glslang_c_interface.h"

#include <ps5platform/fp.h>
#include <ps5platform/kernel.h>
#include <ps5platform/klog.h>

#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* the pins of PS5_Vulkan, the SDK fork, PS5_Mesa, pacbrew and glslang the
title is built with (tools/ps5_build.py) */
#ifndef HALO_PS5_PINS
#define HALO_PS5_PINS "unknown"
#endif

#define DATA_FOLDER "/data/halo"
#define HOLD_SECONDS 120

/* RADV's ICD entry point: the archive has no vkGetInstanceProcAddr
(port/ps5/PS5.md, phase 0 B) */
PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);

/* a notification on the screen (the probes' declaration, after OpenOrbis's) */
struct notify_request
{
	char unused[45];
	char message[3075];
};
int sceKernelSendNotificationRequest(int device, struct notify_request *request, size_t size, int blocking);

/* ---------- the log */

static int log_file = -1;

void ps5_log_line(const char *line)
{
	char text[1100];
	int length = snprintf(text, sizeof(text), "halo: %s\n", line);

	if (length < 0)
		return;
	if ((size_t)length >= sizeof(text))
		length = sizeof(text) - 1;
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
	const char *path = is_folder(DATA_FOLDER) ? DATA_FOLDER "/halo.log" : "/app0/halo.log";

	log_file = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (log_file >= 0)
		chmod(path, 0666);
	if (log_file < 0)
		ps5_log("halo.log: cannot open %s; the kernel log only", path);
	else if (strcmp(path, DATA_FOLDER "/halo.log"))
		ps5_log("halo.log: writing to %s and the kernel log (there is no " DATA_FOLDER ")", path);
	else
		ps5_log("halo.log: writing to %s and the kernel log", path);
}

/* ---------- what the title is */

static void log_identity(void)
{
	struct ps5_kernel_sw_version version;

	ps5_log("Halo: Combat Evolved for the PS5, build %s (phase 1: the bootstrap host)", HALO_PROBE_BUILD);
	memset(&version, 0, sizeof(version));
	version.size = sizeof(version);
	if (sceKernelGetSystemSwVersion(&version) == 0)
		ps5_log("system software %.28s (0x%08x)", version.text, version.version);
	else
		ps5_log("system software: unknown");
	ps5_log("built with %s", HALO_PS5_PINS);
}

/* that SDL2, RADV and glslang are in the link and answer (port/ps5/PS5.md,
phase 1, step 6): nothing more of them is used yet */
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

	folder = is_folder(DATA_FOLDER) ? opendir(DATA_FOLDER) : NULL;
	if (!folder)
	{
		ps5_log(DATA_FOLDER ": not found");
		return;
	}
	used = (size_t)snprintf(line, sizeof(line), DATA_FOLDER ": found; holds");
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

/* ---------- main */

int main(void)
{
	struct stat status;

	/* a title starts with flush-to-zero and denormals-are-zero set; the game
	computes as IEEE (PS5_FINDINGS.md, "Floating point") */
	ps5_fp_ieee();
	log_open();
	notify("Halo: Combat Evolved starts");
	log_identity();
	log_link_proofs();
	log_data_folder();

	host_bootstrap_run();

	if (stat("/app0/hold.txt", &status) == 0)
	{
		ps5_log("holding for %d s before the end (/app0/hold.txt), so the title can be killed", HOLD_SECONDS);
		sleep(HOLD_SECONDS);
	}
	notify("Halo: Combat Evolved ends");
	/* (the kernel log then reports exit() and a SIGSYS, as for every title
	that ends; phase 2 looks at it) */
	ps5_log("the host ends");
	if (log_file >= 0)
		close(log_file);
	/* (the game's thread may still be running) */
	_exit(0);
}
