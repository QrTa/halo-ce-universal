/*
HOST_RULES_PS5.C

The PS5 host's rules that need no console (host_rules_ps5.h). Nothing here
calls the system, so port/ps5/tests/host_rules_ps5_test.c runs it on the PC.

The maximum resolution (port/ps5/PS5.md, phase 7, step 2) is the Switch's
display.resolution_limit with the PS5's values: the most lines the game
draws at, no more than the display shows. The display is VideoOut's mode,
which the console's own output setting decides (Screen and Video); the
picture is drawn in its shape and the present scales it to the mode.

The gyroscope (step 3) is the Switch's gyro_read with the DualSense's
axes, as probe 1 found them: radians a second, the pad's x to its right, y
out of its face, z toward the player; looking up turns it about x the
positive way, steering it left (as a wheel) about z the positive way. The
view turns sideways as the pad is steered (the user's choice in play, over
turning it flat about y), and the guest's yaw is positive to the left
(input_abstraction.c: the stick's yaw is -x), so the yaw is +z; the pitch,
positive looking up, is +x. The samples' own timestamps (microseconds)
give the time each turned for.

The microphone (step 4) gives 48 kHz 16-bit mono, voice chat's rate, so its
samples are only turned to floats; a buffer all zeros (muted, or a
headset changing over) is dropped, as the Switch's host_microphone.c
drops them, so voice chat hears nothing rather than a silence it would
send.
*/

#include "host_rules_ps5.h"

#include <stdlib.h>

/* (8K's lines: more than any display the console drives) */
#define LINES_HIGHEST 4320

int ps5_limit_lines(const char *text)
{
	char *end;
	long lines;

	if (!text || !*text)
		return 0;
	lines = strtol(text, &end, 10);
	if (*end || lines < PS5_LIMIT_LINES_LOWEST || lines > LINES_HIGHEST)
		return 0;
	return (int)lines;
}

int ps5_limit_rule(int lines, int display_width, int display_height, int *width, int *height)
{
	int display_decides = display_height > 0 && display_height <= lines;

	if (display_width <= 0 || display_height <= 0)
	{
		/* (no display mode known: the limit, 16:9) */
		display_width = 16;
		display_height = 9;
		display_decides = 0;
	}
	*height = display_decides ? display_height : lines;
	*width = display_decides ? display_width :
		(int)(((long long)lines * display_width + display_height / 2) / display_height) & ~1;
	return display_decides;
}

void ps5_gyro_turn(struct ps5_gyro_state *state, unsigned long long now_us, const struct ps5_gyro_sample *samples,
	int count, float *yaw, float *pitch)
{
	unsigned long long previous = state->last_timestamp_us;
	int index, fresh = !state->had_sample || now_us - state->last_read_us > PS5_GYRO_LONGEST_GAP_US;

	*yaw = 0.0f;
	*pitch = 0.0f;
	state->last_read_us = now_us;
	if (count <= 0)
		return;
	if (fresh)
	{
		for (index = 0; index < count; index++)
		{
			if (samples[index].timestamp_us > previous || !state->had_sample)
				previous = samples[index].timestamp_us;
		}
		state->last_timestamp_us = previous;
		state->had_sample = 1;
		return;
	}
	for (index = 0; index < count; index++)
	{
		const struct ps5_gyro_sample *sample = &samples[index];
		unsigned long long gap;
		float x = sample->x, z = sample->z, seconds;

		if (sample->timestamp_us <= previous)
			continue;
		gap = sample->timestamp_us - previous;
		previous = sample->timestamp_us;
		if (gap > PS5_GYRO_LONGEST_GAP_US)
			continue;
		if (x < PS5_GYRO_NOISE && x > -PS5_GYRO_NOISE)
			x = 0.0f;
		if (z < PS5_GYRO_NOISE && z > -PS5_GYRO_NOISE)
			z = 0.0f;
		seconds = (float)gap * 1.0e-6f;
		*yaw += z * seconds;
		*pitch += x * seconds;
	}
	state->last_timestamp_us = previous;
}

int ps5_microphone_samples(const short *samples, int count, float *out)
{
	int index, any = 0;

	for (index = 0; index < count && !any; index++)
		any = samples[index] != 0;
	if (!any)
		return 0;
	for (index = 0; index < count; index++)
		out[index] = samples[index] / 32768.0f;
	return count;
}
