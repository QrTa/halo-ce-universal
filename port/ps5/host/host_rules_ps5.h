/*
HOST_RULES_PS5.H

The PS5 host's rules that need no console (port/ps5/PS5.md, phase 7): the
maximum resolution against the display's, the gyroscope's turn from the
DualSense's samples, and the microphone's samples, compiled into the title
and,
with port/ps5/tests/host_rules_ps5_test.c, on the PC
(tools/test_ps5_host.py).
*/

#ifndef HOST_RULES_PS5_H
#define HOST_RULES_PS5_H

/* the fewest and most lines display.resolution_limit may ask for */
#define PS5_LIMIT_LINES_LOWEST 480
#define PS5_LIMIT_LINES_DEFAULT 2160

/* display.resolution_limit's text: its lines, or 0 if it is not a whole
number from PS5_LIMIT_LINES_LOWEST to 4320 */
int ps5_limit_lines(const char *text);

/* the size the game draws at for a limit of `lines` on a display of
display_width x display_height: the smaller of the limit and the display's
height, in the display's shape (an even width). 1 if the display's height
is the smaller (the display decides), else 0. */
int ps5_limit_rule(int lines, int display_width, int display_height, int *width, int *height);

/* the gyroscope (port/ps5/probe/results/pad-probe-run2.txt, -run3.txt): an
angular velocity under this, radians a second, is the sensor's noise at rest
(at most 0.02 there), and is no turn */
#define PS5_GYRO_NOISE 0.02f
/* samples further apart than this (microseconds), or a read this long after
the last, are not a turn: the sensor was not being read (the Switch's
limit) */
#define PS5_GYRO_LONGEST_GAP_US 100000ull

/* one sample of the DualSense's: its timestamp (microseconds) and its
angular velocity about the pad's x (right) and z (toward the player),
radians a second */
struct ps5_gyro_sample
{
	unsigned long long timestamp_us;
	float x, z;
};

/* a pad's place in its samples */
struct ps5_gyro_state
{
	unsigned long long last_timestamp_us;
	unsigned long long last_read_us;
	int had_sample;
};

/* the turn in `count` samples (oldest first; the host gives the pad's
state, one a call), read at now_us, in the sensor's radians: *yaw the
steering's, +z (the guest's sign: positive to the left, as steering left
turns the pad), *pitch +x's, positive looking up. A first read, or one PS5_GYRO_LONGEST_GAP_US after
the last, only notes where the samples are and turns nothing, so aiming
turned on does not apply what was queued. */
void ps5_gyro_turn(struct ps5_gyro_state *state, unsigned long long now_us, const struct ps5_gyro_sample *samples,
	int count, float *yaw, float *pitch);

/* the microphone (port/ps5/probe/results/audioin-probe-run3.txt): a buffer
of the input's 16-bit mono samples as voice chat's floats (-1 to 1) in out,
and how many; 0, and nothing written, for a buffer all zeros, which the
input gives while the pad's mute button is lit or a headset is changing
over (speech or silence never is: silence reads a peak of 22) */
int ps5_microphone_samples(const short *samples, int count, float *out);

#endif
