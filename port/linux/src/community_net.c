/*
COMMUNITY_NET.C

The community server's requests (community_net.h): a slot each, its request
made on a detached thread of its own (update_request: Mbed TLS, WinHTTP, the
Switch's ssl service or the browser's XMLHttpRequest, update.h), its answer
kept in the slot until the game's code picks it up by its ticket. A request
whose ticket was forgotten is let finish, and its answer dropped.

This install's key (community.key) is 32 random bytes as hex, made and
written to config.toml the first time a request is sent; the server keeps
only its hash, and tells players apart by it (database/halo_sql.js).
*/

#include "community_net.h"
#include "platform.h"
#include "port_config.h"
#include "posix.h"
#include "update.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMMUNITY_NET_SLOTS 8
#define COMMUNITY_NET_URL_MAXIMUM 1024
#define COMMUNITY_NET_KEY_BYTES 32

enum
{
	_slot_free = 0,
	_slot_running,
	_slot_done,
	/* (its ticket forgotten: freed by its thread when it is done) */
	_slot_abandoned,
};

struct community_net_slot
{
	int state;
	int ticket;
	int status;
	char url[COMMUNITY_NET_URL_MAXIMUM];
	char headers[160];
	char *body;
	char *answer;
	int answer_size;
	char error[256];
};

static pthread_mutex_t community_net_lock = PTHREAD_MUTEX_INITIALIZER;
static struct community_net_slot community_net_slots[COMMUNITY_NET_SLOTS];
static int community_net_next_ticket = 1;

char const *community_net_platform(void)
{
#if defined(HALO_WEB)
	return "web";
#elif defined(HALO_ANDROID)
	return "android";
#elif defined(HALO_SWITCH)
	return "switch";
#elif defined(HALO_PS5)
	return "ps5";
#elif defined(_WIN32)
	return "windows";
#else
	return "linux";
#endif
}

int community_net_custom(void)
{
	return !strcmp(config_string("community.server_mode"), "custom");
}

/* the server asked: the default, or community.custom_server's, without a
trailing slash (an address without a scheme taken as https://); FALSE when
it is not https:// */
static int community_net_server(char *server, size_t size)
{
	char const *configured = community_net_custom() ? config_string("community.custom_server") :
		COMMUNITY_NET_DEFAULT_SERVER;
	size_t length;

	while (*configured == ' ')
		configured++;
	if (!strstr(configured, "://"))
		snprintf(server, size, "https://%s", configured);
	else
		snprintf(server, size, "%s", configured);
	length = strlen(server);
	while (length > 8 && (server[length - 1] == '/' || server[length - 1] == ' '))
		server[--length] = 0;
	return !strncmp(server, "https://", 8) && length > 8;
}

void community_net_server_text(char *text, int size)
{
	if (!community_net_server(text, (size_t)size))
	{
		snprintf(text, (size_t)size, "%s", community_net_custom() ? config_string("community.custom_server") :
			COMMUNITY_NET_DEFAULT_SERVER);
	}
}

int community_net_enabled(void)
{
	return config_boolean("community.enabled") != 0;
}

/* this install's key: community.key's, else a new one, written there (64
hex digits; the server takes 32 to 128 letters, digits, - and _) */
static int community_net_key(char *key, size_t size)
{
	char const *configured = config_string("community.key");
	size_t length = strlen(configured);
	size_t index;
	int valid = length >= 32 && length <= 128;

	for (index = 0; valid && index < length; index++)
	{
		char c = configured[index];

		valid = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
	}
	if (!valid)
	{
		unsigned char bytes[COMMUNITY_NET_KEY_BYTES];
		char made[COMMUNITY_NET_KEY_BYTES * 2 + 1];

		posix_random_bytes(bytes, sizeof(bytes));
		for (index = 0; index < sizeof(bytes); index++)
			snprintf(made + index * 2, 3, "%02x", bytes[index]);
		if (!config_write("community.key", made))
		{
			platform_log("community: could not write community.key");
			return 0;
		}
		configured = config_string("community.key");
	}
	snprintf(key, size, "%s", configured);
	return 1;
}

static void community_net_slot_free(struct community_net_slot *slot)
{
	free(slot->body);
	free(slot->answer);
	memset(slot, 0, sizeof(*slot));
}

static void *community_net_thread(void *context)
{
	struct community_net_slot *slot = context;
	int status = update_request(slot->body ? "POST" : "GET", slot->url, slot->headers, slot->body, slot->answer,
		slot->answer_size, slot->error, sizeof(slot->error));

	pthread_mutex_lock(&community_net_lock);
	slot->status = status;
	if (slot->state == _slot_abandoned)
		community_net_slot_free(slot);
	else
		slot->state = _slot_done;
	pthread_mutex_unlock(&community_net_lock);
	return NULL;
}

/* a request in a free slot, on a thread of its own: its ticket, 0 for none */
static int community_net_start(const char *url, const char *headers, const char *body, int answer_size)
{
	struct community_net_slot *slot = NULL;
	pthread_t thread;
	int index;
	int ticket;

	pthread_mutex_lock(&community_net_lock);
	for (index = 0; index < COMMUNITY_NET_SLOTS && !slot; index++)
	{
		if (community_net_slots[index].state == _slot_free)
			slot = &community_net_slots[index];
	}
	if (!slot)
	{
		pthread_mutex_unlock(&community_net_lock);
		platform_log("community: too many requests at once; %s not sent", url);
		return 0;
	}
	slot->answer = malloc((size_t)answer_size);
	slot->answer_size = answer_size;
	slot->body = body ? strdup(body) : NULL;
	if (!slot->answer || (body && !slot->body))
	{
		community_net_slot_free(slot);
		pthread_mutex_unlock(&community_net_lock);
		return 0;
	}
	slot->answer[0] = 0;
	snprintf(slot->url, sizeof(slot->url), "%s", url);
	snprintf(slot->headers, sizeof(slot->headers), "%s", headers);
	ticket = slot->ticket = community_net_next_ticket++;
	if (community_net_next_ticket <= 0)
		community_net_next_ticket = 1;
	slot->state = _slot_running;
	if (pthread_create(&thread, NULL, community_net_thread, slot) != 0)
	{
		community_net_slot_free(slot);
		pthread_mutex_unlock(&community_net_lock);
		platform_log("community: could not start a request's thread");
		return 0;
	}
	pthread_detach(thread);
	pthread_mutex_unlock(&community_net_lock);
	return ticket;
}

int community_net_send(const char *path, const char *body, const char *player)
{
	char server[COMMUNITY_NET_URL_MAXIMUM];
	char url[COMMUNITY_NET_URL_MAXIMUM];
	char headers[160];
	char key[160];

	if (!community_net_enabled() || !community_net_server(server, sizeof(server)) ||
		!community_net_key(key, sizeof(key)))
	{
		return 0;
	}
	snprintf(url, sizeof(url), "%s%s", server, path);
	/* (a profile's own: the install's key and the profile's, as the server
	tells players apart) */
	snprintf(headers, sizeof(headers), "%sX-Halo-Key: %s%s%.16s\r\n",
		body ? "Content-Type: application/json\r\n" : "", key, player && player[0] ? "-" : "", player ? player : "");
	return community_net_start(url, headers, body, COMMUNITY_NET_ANSWER_MAXIMUM);
}

int community_net_get(const char *url, const char *headers, int answer_maximum)
{
	return answer_maximum > 0 ? community_net_start(url, headers ? headers : "", NULL, answer_maximum) : 0;
}

static struct community_net_slot *community_net_slot_of(int ticket)
{
	int index;

	for (index = 0; ticket > 0 && index < COMMUNITY_NET_SLOTS; index++)
	{
		struct community_net_slot *slot = &community_net_slots[index];

		if (slot->ticket == ticket && (slot->state == _slot_running || slot->state == _slot_done))
			return slot;
	}
	return NULL;
}

int community_net_poll(int ticket, int *status, char *answer, int size)
{
	struct community_net_slot *slot;
	int result;

	pthread_mutex_lock(&community_net_lock);
	slot = community_net_slot_of(ticket);
	if (!slot)
		result = _community_net_unknown;
	else if (slot->state == _slot_running)
		result = _community_net_waiting;
	else
	{
		if (status)
			*status = slot->status;
		if (answer && size > 0)
			snprintf(answer, (size_t)size, "%s", slot->status ? slot->answer : slot->error);
		result = slot->status ? _community_net_answered : _community_net_failed;
		if (!slot->status)
			platform_log("community: %s: %s", slot->url, slot->error);
		community_net_slot_free(slot);
	}
	pthread_mutex_unlock(&community_net_lock);
	return result;
}

void community_net_forget(int ticket)
{
	struct community_net_slot *slot;

	pthread_mutex_lock(&community_net_lock);
	slot = community_net_slot_of(ticket);
	if (slot && slot->state == _slot_running)
		slot->state = _slot_abandoned;
	else if (slot)
		community_net_slot_free(slot);
	pthread_mutex_unlock(&community_net_lock);
}
