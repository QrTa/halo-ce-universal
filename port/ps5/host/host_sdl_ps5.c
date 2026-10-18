/*
HOST_SDL_PS5.C

The game's SDL3 calls on the PS5 (port/android/guest/runtime/guest_sdl.c and
port/linux/host/guest_desktop.c send them here, as the host_sdl_* imports
of host_imports.list), which Android answers with SDL3 (host_sdl.c) and the
Linux host with SDL3 and its desktop half (host_desktop.c).

In phase 2 of port/ps5/PS5.md every one of them is a stand-in, so that the
game runs headless from a fresh data folder to its main menu's map and
keeps running: a fixed console has one fullscreen window, one 1080p
display mode, no pad, no keyboard and no sound yet. Phase 3 fills the pad,
audio and event parts with pacbrew's SDL2 (the Switch's host_sdl2.c is the
model of both); phase 4's renderer presents, after which its swapchain
paces the frames instead of the stand-in swap here. What stays a stand-in
for good: the window, the GL context, the displays, the 2D renderer and the
desktop's dialogs.

- The stand-in swap paces the game: with a swap interval, each frame waits
  for the next tick of 1/59.94 s (the console's only display mode,
  phase 0 B) on the monotonic clock, as the Switch's stand-in did under
  deko3d before it presented. Every 5 s a line says how many frames, their
  mean and longest: the headless run's heartbeat; every minute it adds the
  host's heap and direct memory's largest free block.
- The guest's audio is a callback stream (dsound_sdl.c): with no device,
  a thread of the stream's own (with a guest stack, host_thread.c) calls
  the guest's callback as a device at the stream's rate would, a period of
  the guest's SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES at a time, and counts
  what it is given; so the game's mixer runs and keeps time, as it will
  over SDL2's device in phase 3.
- The first start's question about the disc image is answered "Yes", and
  the file dialog with the first .iso or .xiso in the data folder, so the
  extraction runs with nobody at the console; the extraction's progress
  screen (the 2D renderer) draws nothing and counts its frames.

Only integers, floats, strings and flat structures cross, as in host_sdl.c;
SDL's objects are handles into the table here (host.h). SDL3's headers give
the constants and layouts the guest uses. The one SDL call made is the
scancode table's, which pacbrew's SDL2 links in: SDL2's
SDL_GetScancodeName and SDL_GetScancodeFromName take and give the same
values as SDL3's (USB HID usage numbers), and need no SDL_Init.
*/

#include "host.h"
#include "ps5_host.h"

#include <SDL3/SDL.h>

#include <dirent.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

_Static_assert(sizeof(SDL_Event) == 128, "SDL_Event is 128 bytes for the guest too");

/* ---------- handles (host.h; Android's host_sdl.c's) */

#define HANDLE_COUNT 256

struct handle
{
	int type;
	void *object;
};

static struct handle handles[HANDLE_COUNT];
static pthread_mutex_t handle_lock = PTHREAD_MUTEX_INITIALIZER;

uint32_t host_sdl_handle_new(int type, void *object)
{
	uint32_t index;

	if (!object)
		return 0;
	pthread_mutex_lock(&handle_lock);
	for (index = 1; index < HANDLE_COUNT; index++)
	{
		if (handles[index].type == type && handles[index].object == object)
		{
			pthread_mutex_unlock(&handle_lock);
			return index;
		}
	}
	for (index = 1; index < HANDLE_COUNT; index++)
	{
		if (handles[index].type == _handle_free)
		{
			handles[index].type = type;
			handles[index].object = object;
			pthread_mutex_unlock(&handle_lock);
			return index;
		}
	}
	pthread_mutex_unlock(&handle_lock);
	host_logf(HOST_LOG_ERROR, "out of SDL handles");
	return 0;
}

void *host_sdl_handle_get(uint32_t handle, int type)
{
	void *object = NULL;

	if (handle == 0 || handle >= HANDLE_COUNT)
		return NULL;
	pthread_mutex_lock(&handle_lock);
	if (handles[handle].type == type)
		object = handles[handle].object;
	pthread_mutex_unlock(&handle_lock);
	return object;
}

void host_sdl_handle_free(uint32_t handle)
{
	if (handle == 0 || handle >= HANDLE_COUNT)
		return;
	pthread_mutex_lock(&handle_lock);
	handles[handle].type = _handle_free;
	handles[handle].object = NULL;
	pthread_mutex_unlock(&handle_lock);
}

/* the stand-ins' objects: a handle to one of these is all there is */
static char standin_window, standin_context, standin_renderer;

/* the console's display: VideoOut's one mode is 3840x2160 at 59.94 Hz, and
the game renders at 1080p, which phase 4's present scales (port/ps5/PS5.md,
"Display") */
#define DISPLAY_WIDTH 1920
#define DISPLAY_HEIGHT 1080
#define DISPLAY_RATE (60000.0f / 1001.0f)
#define DISPLAY_ID 1

/* one line, the first time a name is called */
static void log_once(int *done, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void log_once(int *done, const char *format, ...)
{
	char line[512];
	va_list arguments;

	if (__atomic_exchange_n(done, 1, __ATOMIC_RELAXED))
		return;
	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	host_log(HOST_LOG_INFO, line);
}

/* ---------- start-up */

/* the period the guest asks its audio device for (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES) */
static int audio_period_frames = 1024;

int host_sdl_init(uint32_t flags)
{
	static int logged;

	log_once(&logged, "SDL: SDL_Init(0x%x): the stand-ins (port/ps5/host/host_sdl_ps5.c), no pad or sound yet", flags);
	return 1;
}

int host_sdl_set_hint(const char *name, const char *value)
{
	if (name && value && !strcmp(name, SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES) && atoi(value) > 0)
		audio_period_frames = atoi(value);
	return 1;
}

void host_sdl_get_error(char *buffer, uint32_t size)
{
	if (size)
		buffer[0] = 0;
}

int64_t host_sdl_ticks(void)
{
	return (int64_t)(ps5_now_ns() / 1000000ull);
}

int64_t host_sdl_ticks_ns(void)
{
	return (int64_t)ps5_now_ns();
}

/* a small number of the calling thread's, as SDL's are */
int64_t host_sdl_thread_id(void)
{
	return ps5_thread_id();
}

/* the game's data folder, with its separator: where the desktop game looks
for config.toml (port_config.c) */
void host_sdl_get_base_path(char *buffer, uint32_t size)
{
	if (size)
		snprintf(buffer, size, "%s/", PS5_DATA_FOLDER);
}

int host_sdl_get_hint_boolean(const char *name, int default_value)
{
	(void)name;
	return default_value != 0;
}

int host_sdl_has_screen_keyboard_support(void)
{
	return 0;
}

int host_sdl_text_input_active(uint32_t window)
{
	(void)window;
	return 0;
}

int host_sdl_start_text_input(uint32_t window, int multiline, int type)
{
	(void)window;
	(void)multiline;
	(void)type;
	return 0;
}

int host_sdl_stop_text_input(uint32_t window)
{
	(void)window;
	return 1;
}

/* ---------- the window and the context */

static int is_window(uint32_t window)
{
	return host_sdl_handle_get(window, _handle_window) == &standin_window;
}

uint32_t host_sdl_create_window(const char *title, int width, int height, int64_t flags)
{
	host_logf(HOST_LOG_INFO, "window \"%s\" (%dx%d asked, flags 0x%llx): the stand-in, %dx%d fullscreen",
		title ? title : "", width, height, (unsigned long long)flags, DISPLAY_WIDTH, DISPLAY_HEIGHT);
	return host_sdl_handle_new(_handle_window, &standin_window);
}

void host_sdl_window_size_in_pixels(uint32_t window, int *width, int *height)
{
	*width = is_window(window) ? DISPLAY_WIDTH : 0;
	*height = is_window(window) ? DISPLAY_HEIGHT : 0;
}

void host_sdl_get_window_size(uint32_t window, int *width, int *height)
{
	host_sdl_window_size_in_pixels(window, width, height);
}

int host_sdl_set_window_size(uint32_t window, int width, int height)
{
	(void)width;
	(void)height;
	return is_window(window);
}

int64_t host_sdl_get_window_flags(uint32_t window)
{
	return is_window(window) ? (int64_t)(SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL | SDL_WINDOW_INPUT_FOCUS) : 0;
}

int host_sdl_set_window_fullscreen(uint32_t window, int fullscreen)
{
	(void)fullscreen;
	return is_window(window);
}

int host_sdl_set_window_fullscreen_mode(uint32_t window, uint32_t display, int width, int height, float refresh_rate)
{
	(void)display;
	(void)width;
	(void)height;
	(void)refresh_rate;
	return is_window(window);
}

void host_sdl_destroy_window(uint32_t window)
{
	if (is_window(window))
		host_sdl_handle_free(window);
}

int host_sdl_warp_mouse_in_window(uint32_t window, float x, float y)
{
	(void)window;
	(void)x;
	(void)y;
	return 1;
}

int host_sdl_set_relative_mouse(uint32_t window, int enabled)
{
	(void)enabled;
	return is_window(window);
}

int host_sdl_gl_set_attribute(int attribute, int value)
{
	(void)attribute;
	(void)value;
	return 1;
}

uint32_t host_sdl_gl_create_context(uint32_t window)
{
	if (!is_window(window))
		return 0;
	host_logf(HOST_LOG_INFO, "GL context: the stand-in (the console has no OpenGL; the image's renderer is Vulkan's)");
	return host_sdl_handle_new(_handle_context, &standin_context);
}

int host_sdl_gl_make_current(uint32_t window, uint32_t context)
{
	return is_window(window) && host_sdl_handle_get(context, _handle_context) == &standin_context;
}

static int swap_interval = 1;

int host_sdl_gl_set_swap_interval(int interval)
{
	swap_interval = interval;
	return 1;
}

/* the heartbeat: frames since the last line, their mean and longest */
#define HEARTBEAT_NS 5000000000ull
#define MEMORY_LINE_NS 60000000000ull

static uint64_t frames_swapped;

static void heartbeat(uint64_t now)
{
	static uint64_t line_start, last_frame, longest, frames, memory_reported;

	if (last_frame && now - last_frame > longest)
		longest = now - last_frame;
	last_frame = now;
	frames++;
	if (!line_start)
	{
		line_start = memory_reported = now;
		host_logf(HOST_LOG_INFO, "heartbeat: the first frame at %.1f s", ps5_seconds());
		return;
	}
	if (now - line_start < HEARTBEAT_NS)
		return;
	if (now - memory_reported >= MEMORY_LINE_NS)
	{
		double heap, peak, direct;

		ps5_memory_figures(&heap, &peak, &direct);
		host_logf(HOST_LOG_INFO, "heartbeat: %llu frames in %.2f s (%.2f a second), mean %.2f ms, longest %.2f ms; "
			"the host's heap %.0f MB (peak %.0f MB), direct memory's largest free block %.0f MB",
			(unsigned long long)frames, (now - line_start) / 1e9, frames / ((now - line_start) / 1e9),
			(now - line_start) / 1e6 / (double)frames, longest / 1e6, heap, peak, direct);
		memory_reported = now;
	}
	else
	{
		host_logf(HOST_LOG_INFO, "heartbeat: %llu frames in %.2f s (%.2f a second), mean %.2f ms, longest %.2f ms",
			(unsigned long long)frames, (now - line_start) / 1e9, frames / ((now - line_start) / 1e9),
			(now - line_start) / 1e6 / (double)frames, longest / 1e6);
	}
	line_start = now;
	frames = 0;
	longest = 0;
}

/* The stand-in swap: until the renderer presents (host_vk_presenting, phase
4) and its swapchain waits for the display, each frame is held to the
display's 59.94 Hz here, as the swap's vsync holds them elsewhere. */
int host_sdl_gl_swap_window(uint32_t window)
{
	static uint64_t next;
	uint64_t now = ps5_now_ns();

	if (!is_window(window))
		return 0;
	if (swap_interval > 0 && !host_vk_presenting)
	{
		const uint64_t interval = (uint64_t)(1e9 / DISPLAY_RATE);

		if (next > now)
		{
			struct timespec deadline;

			deadline.tv_sec = (time_t)(next / 1000000000ull);
			deadline.tv_nsec = (long)(next % 1000000000ull);
			clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
			now = ps5_now_ns();
		}
		/* a frame more than an interval late starts the count again */
		next = now > next + interval ? now + interval : next + interval;
	}
	__atomic_add_fetch(&frames_swapped, 1, __ATOMIC_RELAXED);
	host_gl_frame();
	heartbeat(now);
	return 1;
}

/* ---------- displays */

/* host_desktop.c's struct host_display_mode, which guest_desktop.c reads */
struct host_display_mode
{
	uint32_t display;
	uint32_t format;
	int32_t width, height;
	float pixel_density;
	float refresh_rate;
	int32_t refresh_rate_numerator, refresh_rate_denominator;
};

static void display_mode(struct host_display_mode *mode)
{
	mode->display = DISPLAY_ID;
	mode->format = SDL_PIXELFORMAT_XRGB8888;
	mode->width = DISPLAY_WIDTH;
	mode->height = DISPLAY_HEIGHT;
	mode->pixel_density = 1.0f;
	mode->refresh_rate = DISPLAY_RATE;
	mode->refresh_rate_numerator = 60000;
	mode->refresh_rate_denominator = 1001;
}

uint32_t host_sdl_get_display_for_window(uint32_t window)
{
	return is_window(window) ? DISPLAY_ID : 0;
}

uint32_t host_sdl_get_primary_display(void)
{
	return DISPLAY_ID;
}

int host_sdl_get_display_mode(uint32_t display, int current, void *result)
{
	(void)current;
	if (display != DISPLAY_ID)
		return 0;
	display_mode(result);
	return 1;
}

int host_sdl_get_fullscreen_display_modes(uint32_t display, void *result, int maximum)
{
	if (display != DISPLAY_ID)
		return 0;
	if (maximum > 0)
		display_mode(result);
	return 1;
}

int host_sdl_get_closest_fullscreen_display_mode(uint32_t display, int width, int height, float refresh_rate,
	int include_high_density_modes, void *result)
{
	(void)width;
	(void)height;
	(void)refresh_rate;
	(void)include_high_density_modes;
	return host_sdl_get_display_mode(display, 1, result);
}

int host_sdl_get_display_usable_bounds(uint32_t display, void *rectangle)
{
	SDL_Rect bounds = { 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT };

	if (display != DISPLAY_ID)
		return 0;
	memcpy(rectangle, &bounds, sizeof(bounds));
	return 1;
}

/* ---------- events: none from the console yet; the guest's own pushed ones */

#define EVENT_QUEUE 32

static SDL_Event event_queue[EVENT_QUEUE];
static int event_first, event_count;
static pthread_mutex_t event_lock = PTHREAD_MUTEX_INITIALIZER;

int host_sdl_poll_event(void *event)
{
	int found = 0;

	pthread_mutex_lock(&event_lock);
	if (event_count)
	{
		memcpy(event, &event_queue[event_first], sizeof(SDL_Event));
		event_first = (event_first + 1) % EVENT_QUEUE;
		event_count--;
		found = 1;
	}
	pthread_mutex_unlock(&event_lock);
	return found;
}

void host_sdl_pump_events(void)
{
}

/* (the menus' Quit pushes SDL_EVENT_QUIT) */
int host_sdl_push_event(const void *event)
{
	int pushed = 0;

	pthread_mutex_lock(&event_lock);
	if (event_count < EVENT_QUEUE)
	{
		memcpy(&event_queue[(event_first + event_count) % EVENT_QUEUE], event, sizeof(SDL_Event));
		event_count++;
		pushed = 1;
	}
	pthread_mutex_unlock(&event_lock);
	host_logf(HOST_LOG_INFO, "SDL: the game pushed event 0x%x", ((const SDL_Event *)event)->type);
	return pushed;
}

/* ---------- gamepads: none until phase 3 */

int host_sdl_get_gamepads(uint32_t *ids, int capacity)
{
	(void)ids;
	(void)capacity;
	return 0;
}

uint32_t host_sdl_open_gamepad(uint32_t id)
{
	(void)id;
	return 0;
}

uint32_t host_sdl_gamepad_from_id(uint32_t id)
{
	(void)id;
	return 0;
}

void host_sdl_close_gamepad(uint32_t gamepad)
{
	(void)gamepad;
}

int host_sdl_gamepad_axis(uint32_t gamepad, int axis)
{
	(void)gamepad;
	(void)axis;
	return 0;
}

int host_sdl_gamepad_button(uint32_t gamepad, int button)
{
	(void)gamepad;
	(void)button;
	return 0;
}

int host_sdl_gamepad_type(uint32_t gamepad)
{
	(void)gamepad;
	return SDL_GAMEPAD_TYPE_UNKNOWN;
}

int host_sdl_rumble_gamepad(uint32_t gamepad, uint32_t low, uint32_t high, uint32_t milliseconds)
{
	(void)gamepad;
	(void)low;
	(void)high;
	(void)milliseconds;
	return 0;
}

/* the desktop guest maps keys by name at start: SDL's own table */
void host_sdl_scancode_name(int32_t scancode, char *buffer, uint32_t size)
{
	const char *name = SDL_GetScancodeName((SDL_Scancode)scancode);

	if (size)
		snprintf(buffer, size, "%s", name ? name : "");
}

int32_t host_sdl_scancode_from_name(const char *name)
{
	return (int32_t)SDL_GetScancodeFromName(name);
}

/* ---------- audio: a device's clock with no device */

#define AUDIO_DEVICE_ID 2

struct audio_stream
{
	uint32_t handle;
	SDL_AudioSpec spec;
	uint32_t callback, userdata;
	/* bytes the guest put, and the stream's clock from its resume */
	uint64_t put_bytes, played_base;
	uint64_t resumed_ns;
	uint64_t callbacks;
	int running, quit;
};

static uint64_t audio_bytes_total, audio_callbacks_total;

static uint32_t frame_bytes(const SDL_AudioSpec *spec)
{
	return (uint32_t)SDL_AUDIO_BYTESIZE(spec->format) * (uint32_t)(spec->channels > 0 ? spec->channels : 1);
}

/* what a device would have played by now */
static uint64_t played_bytes(const struct audio_stream *stream, uint64_t now)
{
	if (!stream->resumed_ns || !stream->spec.freq)
		return stream->played_base;
	return stream->played_base + (now - stream->resumed_ns) * (uint64_t)stream->spec.freq / 1000000000ull *
		frame_bytes(&stream->spec);
}

/* the stream's device: the guest's callback a period at a time, on time
(a thread with a guest stack, so it can run guest code) */
static void *audio_clock(void *context)
{
	struct audio_stream *stream = context;
	uint64_t period_ns = (uint64_t)audio_period_frames * 1000000000ull /
		(uint64_t)(stream->spec.freq > 0 ? stream->spec.freq : 48000);
	uint32_t period_bytes = (uint32_t)audio_period_frames * frame_bytes(&stream->spec);
	uint64_t next = ps5_now_ns();

	host_logf(HOST_LOG_INFO, "audio: the stand-in device calls the game's mixer every %.1f ms (%d frames at %d Hz)",
		period_ns / 1e6, audio_period_frames, stream->spec.freq);
	while (!__atomic_load_n(&stream->quit, __ATOMIC_ACQUIRE))
	{
		struct timespec deadline;

		host_call_guest(stream->callback, stream->userdata, stream->handle, period_bytes, period_bytes);
		stream->callbacks++;
		__atomic_add_fetch(&audio_callbacks_total, 1, __ATOMIC_RELAXED);
		next += period_ns;
		deadline.tv_sec = (time_t)(next / 1000000000ull);
		deadline.tv_nsec = (long)(next % 1000000000ull);
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
	}
	free(stream);
	return NULL;
}

int host_sdl_get_audio_devices(int recording, uint32_t *ids, int maximum)
{
	if (recording)
		return 0;
	if (maximum > 0)
		ids[0] = AUDIO_DEVICE_ID;
	return 1;
}

int host_sdl_get_audio_device_name(uint32_t device, char *name, int size)
{
	if (device != AUDIO_DEVICE_ID || size < 1)
		return 0;
	snprintf(name, (size_t)size, "PS5");
	return 1;
}

uint32_t host_sdl_open_audio_stream(uint32_t device, const void *spec, uint32_t callback, uint32_t userdata)
{
	struct audio_stream *stream;

	if (device == SDL_AUDIO_DEVICE_DEFAULT_RECORDING)
		return 0;
	stream = calloc(1, sizeof(*stream));
	if (!stream)
		return 0;
	memcpy(&stream->spec, spec, sizeof(stream->spec));
	stream->callback = callback;
	stream->userdata = userdata;
	stream->handle = host_sdl_handle_new(_handle_audio, stream);
	if (!stream->handle)
	{
		free(stream);
		return 0;
	}
	host_logf(HOST_LOG_INFO, "audio: stream %u opened on the stand-in device: format 0x%x, %d channels, %d Hz, %s",
		stream->handle, (unsigned)stream->spec.format, stream->spec.channels, stream->spec.freq,
		callback ? "with the game's callback" : "without a callback");
	return stream->handle;
}

int host_sdl_put_audio_stream_data(uint32_t handle, const void *data, int length)
{
	struct audio_stream *stream = host_sdl_handle_get(handle, _handle_audio);

	(void)data;
	if (!stream || length < 0)
		return 0;
	stream->put_bytes += (uint64_t)length;
	__atomic_add_fetch(&audio_bytes_total, (uint64_t)length, __ATOMIC_RELAXED);
	return 1;
}

int host_sdl_resume_audio_stream_device(uint32_t handle)
{
	struct audio_stream *stream = host_sdl_handle_get(handle, _handle_audio);

	if (!stream)
		return 0;
	if (stream->running)
		return 1;
	stream->running = 1;
	stream->resumed_ns = ps5_now_ns();
	if (stream->callback && host_native_thread_create(audio_clock, stream, 256 * 1024) != 0)
		host_fatal("cannot start the audio stand-in's thread");
	return 1;
}

int host_sdl_get_audio_stream_data(uint32_t handle, void *data, int length)
{
	(void)handle;
	(void)data;
	(void)length;
	return 0;
}

/* the bytes queued less what a device at the stream's rate would have
played since it was resumed */
int host_sdl_get_audio_stream_available(uint32_t handle)
{
	struct audio_stream *stream = host_sdl_handle_get(handle, _handle_audio);
	uint64_t played;

	if (!stream)
		return -1;
	played = played_bytes(stream, ps5_now_ns());
	return stream->put_bytes > played ? (int)(stream->put_bytes - played) : 0;
}

void host_sdl_destroy_audio_stream(uint32_t handle)
{
	struct audio_stream *stream = host_sdl_handle_get(handle, _handle_audio);

	if (!stream)
		return;
	host_sdl_handle_free(handle);
	/* (its thread frees it; one never resumed has none) */
	if (stream->running && stream->callback)
		__atomic_store_n(&stream->quit, 1, __ATOMIC_RELEASE);
	else
		free(stream);
}

/* ---------- the clipboard, the toast, the messages */

static char clipboard[1024];
static pthread_mutex_t clipboard_lock = PTHREAD_MUTEX_INITIALIZER;

int host_sdl_set_clipboard_text(const char *text)
{
	pthread_mutex_lock(&clipboard_lock);
	snprintf(clipboard, sizeof(clipboard), "%s", text ? text : "");
	pthread_mutex_unlock(&clipboard_lock);
	return 1;
}

void host_sdl_get_clipboard_text(char *buffer, uint32_t size)
{
	pthread_mutex_lock(&clipboard_lock);
	if (size)
		snprintf(buffer, size, "%s", clipboard);
	pthread_mutex_unlock(&clipboard_lock);
}

int host_sdl_show_toast(const char *message, int duration, int gravity, int x, int y)
{
	(void)duration;
	(void)gravity;
	(void)x;
	(void)y;
	host_logf(HOST_LOG_INFO, "toast: %s", message ? message : "");
	return 0;
}

int host_sdl_show_simple_message_box(uint32_t flags, const char *title, const char *message)
{
	host_logf(HOST_LOG_INFO, "message box (flags 0x%x) \"%s\": %s", flags, title ? title : "", message ? message : "");
	return 1;
}

/* guest_desktop.c's struct message_box, host_desktop.c's */
#define MESSAGE_BOX_BUTTONS 8

struct host_message_box
{
	uint32_t flags;
	uint32_t title;
	uint32_t message;
	int32_t button_count;
	int32_t button_flags[MESSAGE_BOX_BUTTONS];
	int32_t button_ids[MESSAGE_BOX_BUTTONS];
	uint32_t button_texts;
	uint32_t button_texts_size;
	int32_t chosen;
};

typedef char host_message_box_size_check[sizeof(struct host_message_box) == 92 ? 1 : -1];

/* nobody is there to answer: "Yes" if a button says so, else the first */
int host_sdl_show_message_box(struct host_message_box *box)
{
	const char *texts = (const char *)(uintptr_t)box->button_texts;
	char buttons[256];
	size_t used = 0, listed = 0;
	int index, chosen = 0;

	if (box->button_count < 1 || box->button_count > MESSAGE_BOX_BUTTONS)
		return 0;
	buttons[0] = 0;
	for (index = 0; index < box->button_count; index++)
	{
		const char *end = used < box->button_texts_size ? memchr(texts + used, 0, box->button_texts_size - used) : NULL;

		if (!end)
			return 0;
		if (!strcasecmp(texts + used, "Yes"))
			chosen = index;
		if (listed < sizeof(buttons))
			listed += (size_t)snprintf(buttons + listed, sizeof(buttons) - listed, "%s\"%s\"", index ? ", " : "",
				texts + used);
		used = (size_t)(end - texts) + 1;
	}
	host_logf(HOST_LOG_INFO, "message box \"%s\": %s", (const char *)(uintptr_t)box->title,
		(const char *)(uintptr_t)box->message);
	host_logf(HOST_LOG_INFO, "message box: buttons %s; answered %d (button %d) with nobody at the console", buttons,
		box->button_ids[chosen], chosen);
	box->chosen = box->button_ids[chosen];
	return 1;
}

/* whether name ends in .iso or .xiso, in any case */
static int is_disc_image(const char *name)
{
	size_t length = strlen(name);

	return (length > 4 && !strcasecmp(name + length - 4, ".iso")) ||
		(length > 5 && !strcasecmp(name + length - 5, ".xiso"));
}

/* the first disc image in the data folder, for the first start's question
(sdl_platform.c's data_choose_image) */
int host_sdl_open_file_dialog(int filter_count, const char *filter_names, const char *filter_patterns, char *path,
	uint32_t size)
{
	DIR *folder;
	struct dirent *entry;
	int index, found = 0;

	for (index = 0; index < filter_count && index < 8; index++)
	{
		host_logf(HOST_LOG_INFO, "file dialog: filter \"%s\" (%s)", filter_names, filter_patterns);
		filter_names += strlen(filter_names) + 1;
		filter_patterns += strlen(filter_patterns) + 1;
	}
	if (!size)
		return 0;
	path[0] = 0;
	folder = opendir(PS5_DATA_FOLDER);
	while (folder && !found && (entry = readdir(folder)))
	{
		if (entry->d_type != DT_DIR && is_disc_image(entry->d_name))
			found = (uint32_t)snprintf(path, size, "%s/%s", PS5_DATA_FOLDER, entry->d_name) < size;
	}
	if (folder)
		closedir(folder);
	if (found)
		host_logf(HOST_LOG_INFO, "file dialog: answered %s with nobody at the console", path);
	else
	{
		path[0] = 0;
		host_logf(HOST_LOG_ERROR, "file dialog: no .iso or .xiso in %s to answer with: put the game's disc image "
			"there (port/ps5/README.md)", PS5_DATA_FOLDER);
	}
	return found;
}

/* ---------- the 2D renderer (the extraction's progress screen): nothing
drawn, its presents counted */

static uint64_t renderer_presents;

uint32_t host_sdl_create_renderer(uint32_t window, const char *name)
{
	static int logged;

	log_once(&logged, "2D renderer (%s): the stand-in, which draws nothing", name ? name : "default");
	return is_window(window) ? host_sdl_handle_new(_handle_renderer, &standin_renderer) : 0;
}

static int is_renderer(uint32_t renderer)
{
	return host_sdl_handle_get(renderer, _handle_renderer) == &standin_renderer;
}

void host_sdl_destroy_renderer(uint32_t renderer)
{
	if (is_renderer(renderer))
	{
		host_sdl_handle_free(renderer);
		host_logf(HOST_LOG_INFO, "2D renderer: destroyed after %llu presents", (unsigned long long)renderer_presents);
	}
}

int host_sdl_set_render_vsync(uint32_t renderer, int vsync)
{
	(void)vsync;
	return is_renderer(renderer);
}

int host_sdl_set_render_draw_color(uint32_t renderer, uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha)
{
	(void)red;
	(void)green;
	(void)blue;
	(void)alpha;
	return is_renderer(renderer);
}

int host_sdl_set_render_scale(uint32_t renderer, float x, float y)
{
	(void)x;
	(void)y;
	return is_renderer(renderer);
}

int host_sdl_render_clear(uint32_t renderer)
{
	return is_renderer(renderer);
}

int host_sdl_render_fill_rect(uint32_t renderer, const void *rectangle)
{
	(void)rectangle;
	return is_renderer(renderer);
}

/* (what it would write: the extraction's file and its megabytes, every
ten seconds) */
int host_sdl_render_debug_text(uint32_t renderer, float x, float y, const char *text)
{
	static uint64_t logged_at;
	uint64_t now = ps5_now_ns();

	(void)x;
	if (y > 50.0f && text && now - logged_at >= 10000000000ull)
	{
		logged_at = now;
		host_logf(HOST_LOG_INFO, "2D renderer: \"%s\" at %.1f s", text, ps5_seconds());
	}
	return is_renderer(renderer);
}

int host_sdl_render_present(uint32_t renderer)
{
	if (!is_renderer(renderer))
		return 0;
	__atomic_add_fetch(&renderer_presents, 1, __ATOMIC_RELAXED);
	return 1;
}

void ps5_sdl_report(void)
{
	host_logf(HOST_LOG_INFO, "SDL stand-ins: %llu frames swapped, %llu presents of the 2D renderer, the game's mixer "
		"called %llu times for %.1f MB of sound", (unsigned long long)frames_swapped,
		(unsigned long long)renderer_presents, (unsigned long long)audio_callbacks_total,
		audio_bytes_total / (1024.0 * 1024.0));
}
