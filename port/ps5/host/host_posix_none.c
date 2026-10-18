/*
HOST_POSIX_NONE.C

The desktop game's network and desktop helpers (port/linux/src/posix.h:
sockets, UPnP, DTLS, Discord, the command line, the user's secret), which
the Linux host compiles from posix_net.c, posix_upnp.c and posix_dtls.c.
Networking is phase 6 of port/ps5/PS5.md; until then each answers as a
machine whose network is down, the way the desktop guest already starts
when it has none: no socket is made (WSAENETDOWN), no address is known, no
router answers, no Discord client listens. Each family says so once in the
log. The rest are answered as Android's posix_net.c answers them (no command
line, no user secret, no link handler), and the random bytes and the
process id come from the console.

The file helpers of posix.h are posix_files.c's, compiled for the PS5 as
they are (tools/ps5_build.py).
*/

#include "host.h"
#include "ps5_host.h"
#include "posix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Winsock's error for a network that is down (posix_net.c's table) */
#define WSAENETDOWN 10050

static void network_down(const char *what)
{
	static int logged;

	if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED))
		host_logf(HOST_LOG_INFO, "network: %s asked for; the network is phase 6's, and down until then", what);
}

int posix_socket_last_error(void)
{
	return WSAENETDOWN;
}

int posix_socket(int family, int type, int protocol)
{
	(void)family;
	(void)type;
	(void)protocol;
	network_down("a socket");
	return -1;
}

/* (no socket was ever made, so these are never given one) */
int posix_socket_close(int socket)
{
	(void)socket;
	return -1;
}

int posix_socket_bind(int socket, const void *address, int address_length)
{
	(void)socket;
	(void)address;
	(void)address_length;
	return -1;
}

int posix_socket_connect(int socket, const void *address, int address_length)
{
	(void)socket;
	(void)address;
	(void)address_length;
	return -1;
}

int posix_socket_listen(int socket, int backlog)
{
	(void)socket;
	(void)backlog;
	return -1;
}

int posix_socket_accept(int socket, void *address, int *address_length)
{
	(void)socket;
	(void)address;
	(void)address_length;
	return -1;
}

int posix_socket_send(int socket, const void *buffer, int length, int flags)
{
	(void)socket;
	(void)buffer;
	(void)length;
	(void)flags;
	return -1;
}

int posix_socket_sendto(int socket, const void *buffer, int length, int flags, const void *address,
	int address_length)
{
	(void)socket;
	(void)buffer;
	(void)length;
	(void)flags;
	(void)address;
	(void)address_length;
	return -1;
}

int posix_socket_recv(int socket, void *buffer, int length, int flags)
{
	(void)socket;
	(void)buffer;
	(void)length;
	(void)flags;
	return -1;
}

int posix_socket_recvfrom(int socket, void *buffer, int length, int flags, void *address, int *address_length)
{
	(void)socket;
	(void)buffer;
	(void)length;
	(void)flags;
	(void)address;
	(void)address_length;
	return -1;
}

int posix_socket_shutdown(int socket, int how)
{
	(void)socket;
	(void)how;
	return -1;
}

int posix_socket_set_nonblocking(int socket, int nonblocking)
{
	(void)socket;
	(void)nonblocking;
	return -1;
}

int posix_socket_bytes_available(int socket, posix_ulong *count)
{
	(void)socket;
	if (count)
		*count = 0;
	return -1;
}

int posix_socket_set_nodelay(int socket)
{
	(void)socket;
	return -1;
}

int posix_socket_setsockopt(int socket, int level, int name, const void *value, int length)
{
	(void)socket;
	(void)level;
	(void)name;
	(void)value;
	(void)length;
	return -1;
}

int posix_socket_getsockopt(int socket, int level, int name, void *value, int *length)
{
	(void)socket;
	(void)level;
	(void)name;
	(void)value;
	(void)length;
	return -1;
}

int posix_socket_getsockname(int socket, void *address, int *address_length)
{
	(void)socket;
	(void)address;
	(void)address_length;
	return -1;
}

int posix_socket_getpeername(int socket, void *address, int *address_length)
{
	(void)socket;
	(void)address;
	(void)address_length;
	return -1;
}

/* nothing is ever ready: the lists come back empty, after the timeout
(or a tenth of a second for none), so a caller that waits still waits */
int posix_socket_select(int *read, int *read_count, int *write, int *write_count, int *error, int *error_count,
	posix_long timeout_seconds, posix_long timeout_microseconds, int infinite)
{
	struct timespec wait;

	(void)read;
	(void)write;
	(void)error;
	if (read_count)
		*read_count = 0;
	if (write_count)
		*write_count = 0;
	if (error_count)
		*error_count = 0;
	wait.tv_sec = infinite ? 0 : timeout_seconds;
	wait.tv_nsec = infinite ? 100000000L : (long)timeout_microseconds * 1000L;
	if (wait.tv_sec > 0 || wait.tv_nsec > 0)
		nanosleep(&wait, NULL);
	return 0;
}

posix_ulong posix_local_ipv4_address(void)
{
	return 0;
}

void posix_random_bytes(void *buffer, posix_ulong size)
{
	arc4random_buf(buffer, size);
}

posix_ulong posix_resolve_ipv4(const char *host)
{
	(void)host;
	network_down("a name lookup");
	return 0;
}

int posix_upnp_forward_udp(unsigned short port, unsigned short preferred_port, posix_ulong *external_address,
	unsigned short *external_port, char *error, int error_size)
{
	(void)port;
	(void)preferred_port;
	(void)external_address;
	(void)external_port;
	if (error && error_size > 0)
		snprintf(error, (size_t)error_size, "no network on the PS5 yet");
	return 0;
}

void posix_upnp_stop_forwarding_udp(unsigned short external_port)
{
	(void)external_port;
}

int posix_dtls_fingerprint(unsigned char *fingerprint)
{
	(void)fingerprint;
	return 0;
}

int posix_dtls_open(void)
{
	return -1;
}

void posix_dtls_close(int handle)
{
	(void)handle;
}

void posix_dtls_input(int handle, const void *data, int size)
{
	(void)handle;
	(void)data;
	(void)size;
}

int posix_dtls_receive(int handle, posix_ulong now, void *buffer, int size)
{
	(void)handle;
	(void)now;
	(void)buffer;
	(void)size;
	return -1;
}

int posix_dtls_send(int handle, const void *data, int size)
{
	(void)handle;
	(void)data;
	(void)size;
	return 0;
}

int posix_dtls_output(int handle, void *buffer, int size)
{
	(void)handle;
	(void)buffer;
	(void)size;
	return 0;
}

int posix_dtls_peer_fingerprint(int handle, unsigned char *fingerprint)
{
	(void)handle;
	(void)fingerprint;
	return 0;
}

/* (STUN's message integrity: internet play only, which needs the network) */
void posix_hmac_sha1(const void *key, int key_size, const void *data, int size, unsigned char *digest)
{
	(void)key;
	(void)key_size;
	(void)data;
	(void)size;
	memset(digest, 0, 20);
}

/* a title has no command line */
int posix_command_line_argument(int index, char *buffer, posix_ulong size)
{
	(void)index;
	(void)buffer;
	(void)size;
	return 0;
}

posix_ulong posix_process_id(void)
{
	return (posix_ulong)getpid();
}

int posix_register_url_scheme(const char *scheme, const char *description)
{
	(void)scheme;
	(void)description;
	return 0;
}

int posix_user_secret(unsigned char *secret, int size)
{
	(void)secret;
	(void)size;
	return 0;
}

int posix_discord_connect(void)
{
	return -1;
}

int posix_discord_write(int handle, const void *buffer, int length)
{
	(void)handle;
	(void)buffer;
	(void)length;
	return -1;
}

int posix_discord_read(int handle, void *buffer, int length)
{
	(void)handle;
	(void)buffer;
	(void)length;
	return -1;
}

void posix_discord_close(int handle)
{
	(void)handle;
}
