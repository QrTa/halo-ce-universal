/*
OVERLAY_COMMANDS.C

Commands from outside the game, one at a time, through a file: an overlay
(the Switch's Ultrahand, say) writes a line to overlay/command.txt in the
data folder, and once a second the game looks for it, takes it (the file is
deleted), and runs it. The line is a console command, run as typed at the
console (hs_compile_and_evaluate: cheats, map_name, game_revert...), or
"config <setting> <value>" (or "config <setting> = <value>", the line as
config.toml has it), which sets and saves a setting as the menus do
(config_set_text). overlay/result.txt gets the command and what came of it,
for looking at when one does not work. The whole protocol is the overlay's
spec, halo-overlay-command-spec.md; one command in the file at a time, and
its contents are trusted.

The Switch's only (its Ultrahand overlays are the reason; the other builds
have an empty overlay_commands_update), and on while game.overlay_commands
is: looking for the file is one request a second to the file system.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "hs/hs.h"
#include "../src/posix.h"

#ifdef HALO_SWITCH

#include <stdio.h>
#include <string.h>

/* the platform layer's (port/linux/src) */
extern const char *platform_data_root(void);
extern FILE *platform_fopen_host(char const *path, char const *mode);
extern int platform_remove_host(char const *path);
extern int config_boolean(const char *name);
extern int config_set_text(const char *name, const char *value, char *error, size_t error_size);
extern void platform_log(const char *format, ...);

enum
{
	OVERLAY_COMMAND_SIZE = 1024,
	OVERLAY_LOOK_MILLISECONDS = 1000,
};

/* the result, for debugging: the command, then ok or why not */
static void overlay_result_write(
	char const *command,
	boolean succeeded,
	char const *message)
{
	char path[1024];
	FILE *file;

	snprintf(path, sizeof(path), "%s/overlay/result.txt", platform_data_root());
	file = platform_fopen_host(path, "wb");
	if (!file)
		return;
	fprintf(file, "%s\n%s%s%s\n", command, succeeded ? "ok" : "error", message && *message ? ": " : "",
		message ? message : "");
	fclose(file);
}

/* "config <setting> <value>" or "config <setting> = <value>" */
static void overlay_config(
	char const *command,
	char *arguments)
{
	char error[256] = "";
	char *name, *value, *end;

	while (*arguments == ' ' || *arguments == '\t')
		arguments++;
	name = arguments;
	end = name + strcspn(name, " \t=");
	value = end;
	while (*value == ' ' || *value == '\t' || *value == '=')
		value++;
	*end = 0;
	if (!*name || !*value)
	{
		overlay_result_write(command, FALSE, "config takes a setting and a value: config display.vsync true");
		return;
	}
	if (config_set_text(name, value, error, sizeof(error)))
	{
		platform_log("overlay: %s set to %s", name, value);
		overlay_result_write(command, TRUE, NULL);
	}
	else
	{
		platform_log("overlay: %s", error);
		overlay_result_write(command, FALSE, error);
	}
}

/* (main_loop's, every frame: a look at the file once a second) */
void overlay_commands_update(
	void)
{
	static unsigned long looked_at;
	static boolean folder_made;
	unsigned long now = system_milliseconds();
	char path[1024];
	char command[OVERLAY_COMMAND_SIZE + 1];
	size_t length;
	FILE *file;

	if (now - looked_at < OVERLAY_LOOK_MILLISECONDS)
		return;
	looked_at = now;
	if (!config_boolean("game.overlay_commands"))
		return;
	if (!folder_made)
	{
		snprintf(path, sizeof(path), "%s/overlay", platform_data_root());
		posix_make_directory(path);
		folder_made = TRUE;
	}
	snprintf(path, sizeof(path), "%s/overlay/command.txt", platform_data_root());
	/* (looked for first by the host, which says nothing of a file that is not there: the guest's open of a missing
	file is logged, once a second) */
	{
		struct posix_file_information information;

		if (posix_stat(path, &information) != 0)
			return;
	}
	file = platform_fopen_host(path, "rb");
	if (!file)
		return;
	length = fread(command, 1, OVERLAY_COMMAND_SIZE, file);
	fclose(file);
	platform_remove_host(path);
	command[length] = 0;
	/* (the first line, without its end or the spaces around it) */
	command[strcspn(command, "\r\n")] = 0;
	while (length = strlen(command), length && (command[length - 1] == ' ' || command[length - 1] == '\t'))
		command[length - 1] = 0;
	{
		char *start = command;

		while (*start == ' ' || *start == '\t')
			start++;
		if (start != command)
			memmove(command, start, strlen(start) + 1);
	}
	if (!*command)
		return;
	if (!strncmp(command, "config ", 7) || !strncmp(command, "config\t", 7))
	{
		char copy[OVERLAY_COMMAND_SIZE + 1];

		snprintf(copy, sizeof(copy), "%s", command);
		overlay_config(command, copy + 7);
		return;
	}
	platform_log("overlay: %s", command);
	if (hs_compile_and_evaluate(command))
		overlay_result_write(command, TRUE, NULL);
	else
		overlay_result_write(command, FALSE, "the console did not run it (debug.txt says why)");
}

#else

void overlay_commands_update(
	void)
{
	return;
}

#endif
