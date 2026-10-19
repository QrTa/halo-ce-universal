/*
HOST_RULES_PS5_TEST.C

The PS5 host's rules that need no console (port/ps5/host/host_rules_ps5.c),
tested on the PC with its own compiler (tools/test_ps5_host.py): the
maximum resolution's reading and its rule against the display's mode; the
gyroscope's turn, from samples made after probe 1's figures (the probe kept
its figures, not every sample): its rate (a sample about every 1.6 ms), the
noise at rest (at most 0.0199 rad/s), a quarter turn's angular velocity
and the signs of the axes used (x looks up, z steers left); the microphone's samples to floats, its buffers of
zeros dropped (probe 2: muted, every buffer is zeros; silence is not).
*/

#include "host_rules_ps5.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("%s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* ---------- the maximum resolution */

static void test_resolution_lines(void)
{
	CHECK(ps5_limit_lines("2160") == 2160);
	CHECK(ps5_limit_lines("1440") == 1440);
	CHECK(ps5_limit_lines("1080") == 1080);
	CHECK(ps5_limit_lines("720") == 720);
	CHECK(ps5_limit_lines("480") == 480);
	CHECK(ps5_limit_lines("479") == 0);
	CHECK(ps5_limit_lines("4321") == 0);
	CHECK(ps5_limit_lines("") == 0);
	CHECK(ps5_limit_lines(NULL) == 0);
	CHECK(ps5_limit_lines("2160p") == 0);
	CHECK(ps5_limit_lines("4k") == 0);
}

static void test_resolution_rule(void)
{
	int width, height;

	/* the user's 4K display: each limit as it is */
	CHECK(ps5_limit_rule(2160, 3840, 2160, &width, &height) == 1 && width == 3840 && height == 2160);
	CHECK(ps5_limit_rule(1440, 3840, 2160, &width, &height) == 0 && width == 2560 && height == 1440);
	CHECK(ps5_limit_rule(1080, 3840, 2160, &width, &height) == 0 && width == 1920 && height == 1080);
	/* a 1080p display (or the console's output set to 1080p): 2160 and
	1440 draw at the display's 1920x1080, which decides */
	CHECK(ps5_limit_rule(2160, 1920, 1080, &width, &height) == 1 && width == 1920 && height == 1080);
	CHECK(ps5_limit_rule(1440, 1920, 1080, &width, &height) == 1 && width == 1920 && height == 1080);
	CHECK(ps5_limit_rule(1080, 1920, 1080, &width, &height) == 1 && width == 1920 && height == 1080);
	/* a 1440p output */
	CHECK(ps5_limit_rule(2160, 2560, 1440, &width, &height) == 1 && width == 2560 && height == 1440);
	CHECK(ps5_limit_rule(1080, 2560, 1440, &width, &height) == 0 && width == 1920 && height == 1080);
	/* another shape: the display's, the width even */
	CHECK(ps5_limit_rule(1080, 3440, 1440, &width, &height) == 0 && width == 2580 && height == 1080);
	CHECK(ps5_limit_rule(720, 4096, 2160, &width, &height) == 0 && width == 1364 && height == 720);
	/* no display mode known: the limit at 16:9 */
	CHECK(ps5_limit_rule(1440, 0, 0, &width, &height) == 0 && width == 2560 && height == 1440);
}

/* ---------- the gyroscope */

#define SAMPLE_GAP_US 1600ull

static int near(float value, float wanted)
{
	return fabsf(value - wanted) < 0.002f;
}

/* `count` samples from `start`, SAMPLE_GAP_US apart, each turning (x, z) */
static void samples_make(struct ps5_gyro_sample *samples, int count, unsigned long long start, float x, float z)
{
	int index;

	for (index = 0; index < count; index++)
	{
		samples[index].timestamp_us = start + (unsigned long long)index * SAMPLE_GAP_US;
		samples[index].x = x;
		samples[index].z = z;
	}
}

static void test_gyro(void)
{
	struct ps5_gyro_state state = { 0, 0, 0 };
	struct ps5_gyro_sample samples[64];
	unsigned long long now = 1000000ull, stamp = 5000000ull;
	float yaw, pitch;

	/* the first read only notes where the samples are */
	samples_make(samples, 10, stamp, 0.0f, -3.0f);
	ps5_gyro_turn(&state, now, samples, 10, &yaw, &pitch);
	CHECK(yaw == 0.0f && pitch == 0.0f && state.last_timestamp_us == stamp + 9 * SAMPLE_GAP_US);
	stamp += 10 * SAMPLE_GAP_US;

	/* steering right is -z (run 3: -1.23 for a quarter turn), the guest's
	yaw to the right, negative (input_abstraction.c); 10 samples at -1.5
	rad/s for 1.6 ms each is 0.024 rad */
	now += 16000;
	samples_make(samples, 10, stamp, 0.0f, -1.5f);
	ps5_gyro_turn(&state, now, samples, 10, &yaw, &pitch);
	CHECK(near(yaw, -0.024f) && pitch == 0.0f);
	stamp += 10 * SAMPLE_GAP_US;

	/* looking up is +x: a positive pitch */
	now += 16000;
	samples_make(samples, 10, stamp, 2.0f, 0.0f);
	ps5_gyro_turn(&state, now, samples, 10, &yaw, &pitch);
	CHECK(yaw == 0.0f && near(pitch, 0.032f));
	stamp += 10 * SAMPLE_GAP_US;

	/* the noise at rest (pad-probe-run1-unattended.txt's largest, 0.0199)
	turns nothing */
	now += 16000;
	samples_make(samples, 10, stamp, 0.0199f, -0.0135f);
	samples[3].x = -0.0199f;
	ps5_gyro_turn(&state, now, samples, 10, &yaw, &pitch);
	CHECK(yaw == 0.0f && pitch == 0.0f);
	stamp += 10 * SAMPLE_GAP_US;

	/* samples read already (the queue gives some again, or out of order)
	count once */
	now += 16000;
	samples_make(samples, 4, stamp - 2 * SAMPLE_GAP_US, 0.0f, -1.0f);
	ps5_gyro_turn(&state, now, samples, 4, &yaw, &pitch);
	CHECK(near(yaw, -2 * 0.0016f));
	stamp += 2 * SAMPLE_GAP_US;

	/* a gap of more than 100 ms between samples is no turn; the samples
	after it are */
	now += 16000;
	samples[0].timestamp_us = stamp + 150000ull;
	samples[0].x = 0.0f;
	samples[0].z = -10.0f;
	samples[1].timestamp_us = samples[0].timestamp_us + SAMPLE_GAP_US;
	samples[1].x = 0.0f;
	samples[1].z = -1.0f;
	ps5_gyro_turn(&state, now, samples, 2, &yaw, &pitch);
	CHECK(near(yaw, -0.0016f));
	stamp = samples[1].timestamp_us + SAMPLE_GAP_US;

	/* a read more than 100 ms after the last (aiming off, a menu) starts
	again: what was queued is not applied */
	now += 500000;
	samples_make(samples, 64, stamp, 0.0f, -3.0f);
	ps5_gyro_turn(&state, now, samples, 64, &yaw, &pitch);
	CHECK(yaw == 0.0f && pitch == 0.0f);
	stamp += 64 * SAMPLE_GAP_US;

	/* a second's turn, read 60 times a second, adds up to the sensor's:
	a quarter turn steered right at 1.5708 rad/s */
	{
		unsigned long long frame_end = stamp;
		float total = 0.0f;
		int frame;

		for (frame = 0; frame < 60; frame++)
		{
			int count = 0;

			now += 16667;
			frame_end += 16667;
			while (stamp < frame_end && count < 64)
			{
				samples[count].timestamp_us = stamp;
				samples[count].x = 0.0f;
				samples[count].z = -1.5708f;
				stamp += SAMPLE_GAP_US;
				count++;
			}
			ps5_gyro_turn(&state, now, samples, count, &yaw, &pitch);
			total += yaw;
		}
		CHECK(total < -1.55f && total > -1.59f);
	}

	/* no samples: no turn */
	ps5_gyro_turn(&state, now + 1000, samples, 0, &yaw, &pitch);
	CHECK(yaw == 0.0f && pitch == 0.0f);
}

/* ---------- the microphone */

static void test_microphone(void)
{
	short samples[256];
	float out[256];
	int index;

	/* muted: all zeros, dropped and nothing written */
	memset(samples, 0, sizeof(samples));
	out[0] = 5.0f;
	CHECK(ps5_microphone_samples(samples, 256, out) == 0 && out[0] == 5.0f);
	/* silence is a little noise (a peak of 22): kept */
	samples[100] = 22;
	samples[200] = -22;
	CHECK(ps5_microphone_samples(samples, 256, out) == 256 && out[0] == 0.0f && near(out[100], 22.0f / 32768.0f) &&
		near(out[200], -22.0f / 32768.0f));
	/* speech, to its full scale */
	for (index = 0; index < 256; index++)
		samples[index] = (short)(index & 1 ? 32767 : -32768);
	CHECK(ps5_microphone_samples(samples, 256, out) == 256 && out[0] == -1.0f && out[1] > 0.9999f && out[1] < 1.0f);
	CHECK(ps5_microphone_samples(samples, 0, out) == 0);
}

int main(void)
{
	test_resolution_lines();
	test_resolution_rule();
	test_gyro();
	test_microphone();
	if (failures)
		printf("%d failures\n", failures);
	else
		printf("all passed\n");
	return failures != 0;
}
