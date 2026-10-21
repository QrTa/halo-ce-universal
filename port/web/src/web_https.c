/*
WEB_HTTPS.C

update.h's update_request in a browser: the page's XMLHttpRequest, made on
the calling thread (web_library.js's web_js_https), which is the community
server's own worker (port/linux/src/community_net.c). The browser checks the
certificate.
*/

#include <stdio.h>
#include <string.h>

#include "update.h"

int web_js_https(int post, const char *url, const char *headers, const char *body, char *response,
	int response_size);

int update_request(const char *method, const char *url, const char *headers, const char *body, char *response,
	int response_size, char *error, int error_size)
{
	int status;

	if (!response || response_size <= 0)
	{
		snprintf(error, (size_t)error_size, "no room for the answer");
		return 0;
	}
	status = web_js_https(!strcmp(method, "POST"), url, headers, body, response, response_size);
	if (!status)
	{
		snprintf(error, (size_t)error_size, "%s", response);
		response[0] = 0;
	}
	return status;
}
