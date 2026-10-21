/*
COMMUNITY_NET.H

The community server's requests (community_net.c), for the game's code
(port/linux/game/community.c): each made on a thread of its own with
update.h's update_request, and its answer picked up later by its ticket, so
that the game never waits on the network. The server's address is
the default (COMMUNITY_NET_DEFAULT_SERVER) or community.custom_server's;
every request carries this install's key
(community.key, made the first time it is wanted).

Only integers and strings cross here: the game's code is compiled with the
Xbox's structure layouts.
*/

#ifndef COMMUNITY_NET_H
#define COMMUNITY_NET_H

/* the most of an answer kept, NUL included (a leaderboard of 50 rows, a
history of 60 days) */
#define COMMUNITY_NET_ANSWER_MAXIMUM 49152

enum
{
	_community_net_waiting = 0,
	_community_net_answered = 1,
	_community_net_failed = -1,
	/* (a ticket never given, or already picked up) */
	_community_net_unknown = -2,
};

/* the OpenCE server (community.server_mode "default") */
#define COMMUNITY_NET_DEFAULT_SERVER "https://oceapi.watanoge.com"

/* community.enabled: online (else the daily challenge and its leaderboards
are this machine's: community.c) */
int community_net_enabled(void);
/* community.server_mode "custom": community.custom_server is asked */
int community_net_custom(void);
/* the server asked, as text (its address, else what the setting says) */
void community_net_server_text(char *text, int size);

/* asks the server for path below its address ("/daily", "/levels/a10/
leaderboard?limit=10"): a GET, or with body (JSON text) a POST. A ticket
(1 or more), or 0 when offline, with no server's address, or when too many
are being asked already */
int community_net_send(const char *path, const char *body);

/* the ticket's answer: _community_net_waiting; _community_net_answered, with
its HTTP status and up to size - 1 bytes of its body in answer; or
_community_net_failed (no answer came: the server is down, or this machine is
offline), and why in answer. A ticket answered or failed is forgotten */
int community_net_poll(int ticket, int *status, char *answer, int size);

/* the ticket's answer is not wanted (its screen closed): dropped when it comes */
void community_net_forget(int ticket);

/* the platform this build is, as the server is told ("windows", "web") */
char const *community_net_platform(void);

#endif
