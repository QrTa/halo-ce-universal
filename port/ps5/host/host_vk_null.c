/*
HOST_VK_NULL.C

Until phase 4 of port/ps5/PS5.md: the Vulkan renderer's host half
(port/android/host/host_vk*.c) is not yet brought up on RADV, and the
game's Vulkan image runs against this instead, with every draw dropped and
nothing waited for. host_vk.h's imports, answered:

- host_vk_submit reads nothing and counts the submission; host_vk_retired
  answers that count, so every submission is retired at once;
- host_vk_shader_find has no shader and says "compiling"
  (VK_SHADER_STATUS_COMPILING, port/android/guest/vk_shaders.h), so the
  renderer skips every draw as it does for a shader not ready yet; a
  compile is accepted and dropped;
- the device's capabilities answer yes (formats, BC textures, one sample),
  the visibility tests count nothing, and the driver's name says what this
  is.

Phase 4 replaces this file with the renderer's own (host_vk_driver_ps5.c,
host_vk_present_ps5.c and the shared host_vk*.c).
*/

#include "host.h"
#include "ps5_host.h"

#include <string.h>

/* port/android/guest/vk_shaders.h's */
#define VK_SHADER_STATUS_COMPILING 2

/* the image is the Vulkan one, which makes no GL context (host.h) */
int host_renderer_vulkan = 1;
/* nothing presents: the stand-in swap paces the frames (host_sdl_ps5.c) */
int host_vk_presenting;
int host_vk_present_marker;
int host_vk_self_test;
void *host_vk_window;

static uint32_t submissions;
static uint32_t submitted_bytes_megabytes, submitted_bytes;
static uint32_t shader_compiles;

void host_vk_submit(uint32_t commands, uint32_t size)
{
	(void)commands;
	__atomic_add_fetch(&submissions, 1, __ATOMIC_RELAXED);
	if (__atomic_add_fetch(&submitted_bytes, size, __ATOMIC_RELAXED) >= 1024 * 1024)
	{
		__atomic_sub_fetch(&submitted_bytes, 1024 * 1024, __ATOMIC_RELAXED);
		__atomic_add_fetch(&submitted_bytes_megabytes, 1, __ATOMIC_RELAXED);
	}
}

uint32_t host_vk_retired(void)
{
	return __atomic_load_n(&submissions, __ATOMIC_RELAXED);
}

uint32_t host_vk_shader_find(uint32_t stage, uint64_t hash, uint32_t status_out)
{
	(void)stage;
	(void)hash;
	if (status_out)
		*(uint32_t *)(uintptr_t)status_out = VK_SHADER_STATUS_COMPILING;
	return 0;
}

void host_vk_shader_compile(uint32_t stage, uint64_t hash, uint32_t glsl, uint32_t glsl_size)
{
	(void)stage;
	(void)hash;
	(void)glsl;
	(void)glsl_size;
	__atomic_add_fetch(&shader_compiles, 1, __ATOMIC_RELAXED);
}

uint32_t host_vk_format_supported(uint32_t format)
{
	(void)format;
	return 1;
}

uint32_t host_vk_bc_supported(void)
{
	return 1;
}

uint32_t host_vk_samples_supported(uint32_t samples)
{
	(void)samples;
	return 1;
}

uint32_t host_vk_visibility(uint32_t index)
{
	(void)index;
	return 0;
}

uint32_t host_vk_driver_name(uint32_t out, uint32_t size)
{
	static const char name[] = "none (phase 2)";
	char *buffer = (char *)(uintptr_t)out;
	size_t length = sizeof(name) - 1;

	if (!out || !size)
		return 0;
	if (length >= size)
		length = size - 1;
	memcpy(buffer, name, length);
	buffer[length] = 0;
	return (uint32_t)length;
}

void host_vk_exit(void)
{
	host_logf(HOST_LOG_INFO, "renderer: none (phase 2): %u submissions of %u MB, %u shaders sent to compile, every "
		"draw dropped", submissions, submitted_bytes_megabytes, shader_compiles);
}

unsigned host_vk_validation_errors(void)
{
	return 0;
}
