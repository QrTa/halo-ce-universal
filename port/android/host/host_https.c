/*
HOST_HTTPS.C

The host's half of the guest's update_request (guest/runtime/guest_https.c):
posix_update.c's, with Mbed TLS and the system's certificate authorities, in
the Android app, the Linux desktop's host and the PS5's. The Switch has its
own (port/switch/host/host_https.c), over the console's TLS.
*/

#include "host.h"
#include "update.h"

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
