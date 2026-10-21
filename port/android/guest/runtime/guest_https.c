/*
GUEST_HTTPS.C

update.h's update_request for a guest (Android, the Linux desktop, the
Switch, the PS5): the host makes the request (port/android/host/host_https.c,
the Switch's port/switch/host/host_https.c), on the calling thread. Only
strings and integers cross, six of them: the host says why a request failed
in the answer's room.
*/

#include "guest_host.h"
#include "update.h"

#include <stdio.h>
#include <string.h>

int update_request(const char *method, const char *url, const char *headers, const char *body, char *response,
	int response_size, char *error, int error_size)
{
	int status;

	if (!response || response_size <= 0)
	{
		snprintf(error, (size_t)error_size, "no room for the answer");
		return 0;
	}
	status = host_https_request(!strcmp(method, "POST"), url, headers, body, response, response_size);
	if (!status)
	{
		snprintf(error, (size_t)error_size, "%s", response);
		response[0] = 0;
	}
	return status;
}
