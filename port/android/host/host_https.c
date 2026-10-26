/*
HOST_HTTPS.C

The host's half of the guest's update_request (guest/runtime/guest_https.c):
posix_update.c's, with Mbed TLS and the system's certificate authorities, in
the Android app, the Linux desktop's host and the PS5's. The Switch has its
own (port/switch/host/host_https.c), over the console's TLS.

And the server browser's requests to the master servers (host_https_get,
port/android/guest/runtime/guest_host.h; port/linux/src/master_list.c), with
posix_update.c's update_https_request: the guest has no TLS of its own, and
its memory is the host's to read and write.
*/

#include "host.h"
#include "update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int host_https_request(int post, const char *url, const char *headers, const char *body, char *response,
	int response_size)
{
	char error[256] = "";
	int status = update_request(post ? "POST" : "GET", url, headers, body, response, response_size, error,
		sizeof(error));

	/* (why, in the answer's room: guest_https.c moves it to its caller's) */
	if (!status && response && response_size > 0)
	{
		strncpy(response, error, (size_t)response_size - 1);
		response[response_size - 1] = 0;
	}
	return status;
}

int host_https_get(const char *url, char *headers, int headers_size, char *body, int body_capacity,
	int *body_size)
{
	struct update_response response;
	char error[256];

	*body_size = 0;
	if (!update_https_request("GET", url, NULL, NULL, 0, (unsigned long)body_capacity, &response, error,
		sizeof(error)))
	{
		/* (why, in the headers' room: master_list.c logs it) */
		snprintf(headers, (size_t)headers_size, "%s", error);
		return 0;
	}
	snprintf(headers, (size_t)headers_size, "%s", response.headers);
	if (response.body_size > (unsigned long)body_capacity)
		response.body_size = (unsigned long)body_capacity;
	memcpy(body, response.body, response.body_size);
	*body_size = (int)response.body_size;
	free(response.body);
	/* (an answer with no status line's number is still an answer) */
	return response.status > 0 ? response.status : 1;
}
