#include <eggdrop/eggdrop.h>
#include "server.h"
#include "channels.h"
#include "output.h"
#include "binds.h"
#include "servsock.h"
#include "nicklist.h"
#include "dcc.h"

char *global_input_string = NULL;

int server_parse_input(char *text)
{
	irc_msg_t msg;
	char *from_nick = NULL, *from_uhost = NULL;
	user_t *u = NULL;
	char buf[128], *full;
	int r;

	if (global_input_string) {
		free(global_input_string);
		global_input_string = NULL;
	}
	r = bind_check(BT_server_input, NULL, text, text);
	if (r & BIND_RET_BREAK) return(0);
	if (global_input_string) text = global_input_string;

	/* This would be a good place to put an SFILT bind, so that scripts
		and other modules can modify text sent from the server. */
	if (server_config.raw_log) {
		putlog(LOG_RAW, "*", "[@] %s", text);
	}

	irc_msg_parse(text, &msg);

	if (msg.prefix) {
		from_nick = msg.prefix;
		from_uhost = strchr(from_nick, '!');
		if (from_uhost) {
			u = user_lookup_by_irchost(from_nick);
			*from_uhost = 0;
			from_uhost++;
		}
		else {
			from_uhost = uhost_cache_lookup(from_nick);
			if (from_uhost) {
				full = egg_msprintf(buf, sizeof(buf), NULL, "%s!%s", from_nick, from_uhost);
				u = user_lookup_by_irchost(full);
				if (full != buf) free(full);
			}
		}
	}

	bind_check(BT_raw, NULL, msg.cmd, from_nick, from_uhost, u, msg.cmd, msg.nargs, msg.args);
	return(0);
}

/* 001: welcome to IRC */
static int got001(char *from_nick, char *from_uhost, user_t *u, char *cmd, int nargs, char *args[])
{
	current_server.registered = 1;

	/* First arg is what server decided our nick is. */
	str_redup(&current_server.nick, args[0]);

	/* Save the name the server calls itself. */
	str_redup(&current_server.server_self, from_nick);

	eggdrop_event("init-server");

	/* If the init-server bind made us leave the server, stop processing. */
	if (!current_server.registered) return BIND_RET_BREAK;

	/* Send a whois request so we can see our nick!user@host */
	printserv(SERVER_QUICK, "WHOIS %s", current_server.nick);

	return(0);
}

/* 005: things the server supports
 * :irc.example.org 005 nick NOQUIT WATCH=128 SAFELIST MODES=6 MAXCHANNELS=15 MAXBANS=100 NICKLEN=30
 * TOPICLEN=307 KICKLEN=307 CHANTYPES=# PREFIX=(ov)@+ NETWORK=DALnet SILENCE=10 CASEMAPPING=ascii
 * CHANMODES=b,k,l,ciLmMnOprRst :are available on this server
 */
static int got005(char *from_nick, char *from_uhost, user_t *u, char *cmd, int nargs, char *args[])
{
	char *arg, *name, *equalsign, *value;
	int i;

	/* Some servers send multiple 005 messages, so we need to process
	 * them all. */
	current_server.got005++;

	current_server.support = realloc(current_server.support, sizeof(*current_server.support) * (current_server.nsupport+nargs-1));
	current_server.nsupport += nargs-2;

	for (i = 1; i < nargs-1; i++) {
		arg = args[i];

		equalsign = strchr(arg, '=');
		if (equalsign) {
			int len;

			len = equalsign - arg;
			name = malloc(len + 1);
			memcpy(name, arg, len);
			name[len] = 0;
			value = strdup(equalsign+1);
		}
		else {
			name = strdup(arg);
			value = strdup("true");
		}

		current_server.support[i-1].name = name;
		current_server.support[i-1].value = value;

		if (!strcasecmp(name, "chantypes")) {
			str_redup(&current_server.chantypes, value);
		}
		else if (!strcasecmp(arg, "casemapping")) {
			if (!strcasecmp(value, "ascii")) current_server.strcmp = strcasecmp;
			else if (!strcasecmp(value, "rfc1459")) current_server.strcmp = irccmp;
			else current_server.strcmp = strcasecmp;
		}
		else if (!strcasecmp(name, "chanmodes")) {
			char types[4][33];
			char *comma;
			int j;

			for (j = 0; j < 3; j++) {
				comma = strchr(value, ',');
				if (comma) *comma = 0;
				strlcpy(types[j], value, sizeof types[j]);
				if (comma) *comma = ',';
				value = comma+1;
			}
			str_redup(&current_server.type1modes, types[0]);
			str_redup(&current_server.type2modes, types[1]);
			str_redup(&current_server.type3modes, types[2]);
			str_redup(&current_server.type4modes, types[3]);
		}
		else if (!strcasecmp(name, "prefix")) {
			char *paren;

			value++;
			paren = strchr(value, ')');
			if (!paren) continue;
			*paren = 0;
			str_redup(&current_server.modeprefix, value);
			*paren = ')';
			value = paren+1;
			str_redup(&current_server.whoprefix, value);
		}
	}
	return(0);
}

static int got376(char *from_nick, char *from_uhost, user_t *u, char *cmd, int nargs, char *args[])
{
	char *fake;

	if (!current_server.got005 && server_config.fake005 && server_config.fake005[0]) {
		fake = strdup(server_config.fake005);
		server_parse_input(fake);
		free(fake);
	}
	if (!current_server.chantypes) current_server.chantypes = strdup("#&");
	if (!current_server.strcmp) current_server.strcmp = irccmp;
	if (!current_server.type1modes) current_server.type1modes = strdup("b");
	if (!current_server.type2modes) current_server.type2modes = strdup("k");
	if (!current_server.type3modes) current_server.type3modes = strdup("l");
	if (!current_server.type4modes) current_server.type4modes = strdup("imnprst");
	if (!current_server.modeprefix) current_server.modeprefix = strdup("ov");
	if (!current_server.whoprefix) current_server.whoprefix = strdup("@+");
	if (strlen(current_server.modeprefix) != strlen(current_server.whoprefix)) {
		str_redup(&current_server.modeprefix, "ov");
		str_redup(&current_server.whoprefix, "@+");
	}
	return(0);
}

static int check_ctcp_ctcr(int which, int to_channel, user_t *u, char *nick, char *uhost, char *dest, char *trailing)
{
	char *cmd, *space, *logdest, *text, *ctcptype;
	bind_table_t *table;
	int r, len;
	int flags;

	len = strlen(trailing);
	if ((len < 2) || (trailing[0] != 1) || (trailing[len-1] != 1)) {
		/* Not a ctcp/ctcr. */
		return(0);
	}

	cmd = trailing+1;	/* Skip over the \001 */
	trailing[len-1] = 0;

	space = strchr(trailing, ' ');
	if (space) {
		*space = 0;
		text = space+1;
	}
	else text = NULL;

	if (which == 0) table = BT_ctcp;
	else table = BT_ctcr;

	r = bind_check(table, u ? &u->settings[0].flags : NULL, cmd, nick, uhost, u, dest, cmd, text);

	if (!(r & BIND_RET_BREAK)) {
		if (which == 0) ctcptype = "";
		else ctcptype = " reply";

		if (to_channel) {
			flags = LOG_PUBLIC;
			logdest = dest;
		}
		else {
			flags = LOG_MSGS;
			logdest = "*";
		}
		if (!strcasecmp(cmd, "ACTION")) putlog(flags, logdest, "Action: %s %s", nick, text);
		else putlog(flags, logdest, "CTCP%s %s%s%s from %s (to %s)", ctcptype, cmd, text ? ": " : "", text ? text : "", nick, dest);
	}

	trailing[len-1] = 1;
	if (space) *space = ' ';

	return(1);
}

static int check_global_notice(char *from_nick, char *from_uhost, char *dest, char *trailing)
{
	if (*dest == '$') {
		putlog(LOG_MSGS|LOG_SERV, "*", "[%s!%s to %s] %s", from_nick, from_uhost, dest, trailing);
		return(1);
	}
	return(0);
}

/* Got a private (or public) message.
	:nick!uhost PRIVMSG dest :msg
 */
static int gotmsg(char *from_nick, char *from_uhost, user_t *u, char *cmd, int nargs, char *args[])
{
	char *dest, *destname, *trailing, *first, *space, *text;
	int to_channel;	/* Is it going to a channel? */
	int r;

	dest = args[0];
	trailing = args[1];

	/* Check if it's a global message. */
	r = check_global_notice(from_nick, from_uhost, dest, trailing);
	if (r) return(0);

	/* Skip any mode prefix to the destination (e.g. PRIVMSG @#trivia :blah). */
	if (current_server.whoprefix && strchr(current_server.whoprefix, *dest)) destname = dest+1;
	else destname = dest;

	if (current_server.chantypes && strchr(current_server.chantypes, *destname)) to_channel = 1;
	else to_channel = 0;

	/* Check if it's a ctcp. */
	r = check_ctcp_ctcr(0, to_channel, u, from_nick, from_uhost, dest, trailing);
	if (r) return(0);


	/* If it's a message, it goes to msg/msgm or pub/pubm. */
	/* Get the first word so we can do msg or pub. */
	first = trailing;
	space = strchr(trailing, ' ');
	if (space) {
		*space = 0;
		text = space+1;
	}
	else text = "";

	if (to_channel) {
		r = bind_check(BT_pub, u ? &u->settings[0].flags : NULL, first, from_nick, from_uhost, u, dest, text);
		if (r & BIND_RET_LOG) {
			putlog(LOG_CMDS, dest, "<<%s>> !%s! %s %s", from_nick, u ? u->handle : "*", first, text);
		}
	}
	else {
		r = bind_check(BT_msg, u ? &u->settings[0].flags : NULL, first, from_nick, from_uhost, u, text);
		if (r & BIND_RET_LOG) {
			putlog(LOG_CMDS, "*", "(%s!%s) !%s! %s %s", from_nick, from_uhost, u ? u->handle : "*", first, text);
		}
	}

	if (space) *space = ' ';

	if (r & BIND_RET_BREAK) return(0);

	/* And now the stackable version. */
	if (to_channel) {
		r = bind_check(BT_pubm, u ? &u->settings[0].flags : NULL, trailing, from_nick, from_uhost, u, dest, trailing);
	}
	else {
		r = bind_check(BT_msgm, u ? &u->settings[0].flags : NULL, trailing, from_nick, from_uhost, u, trailing);
	}

	if (!(r & BIND_RET_BREAK)) {
		/* This should probably go in the partyline module later. */
		if (to_channel) {
			putlog(LOG_PUBLIC, dest, "<%s> %s", from_nick, trailing);
		}
		else {
			putlog(LOG_MSGS, "*", "[%s (%s)] %s", from_nick, from_uhost, trailing);
		}
	}
	return(0);
}

/* Got a private notice.
	:nick!uhost NOTICE dest :hello there
 */
static int gotnotice(char *from_nick, char *from_uhost, user_t *u, char *cmd, int nargs, char *args[])
{
	char *dest, *destname, *trailing;
	int r, to_channel;

	dest = args[0];
	trailing = args[1];

	/* See if it's a server notice. */
	if (!from_uhost) {
		putlog(LOG_SERV, "*", "-NOTICE- %s", trailing);
		return(0);
	}

	/* Check if it's a global notice. */
	r = check_global_notice(from_nick, from_uhost, dest, trailing);
	if (r) return(0);

	/* Skip any mode prefix to the destination (e.g. PRIVMSG @#trivia :blah). */
	if (current_server.whoprefix && strchr(current_server.whoprefix, *dest)) destname = dest+1;
	else destname = dest;

	if (current_server.chantypes && strchr(current_server.chantypes, *destname)) to_channel = 1;
	else to_channel = 0;

	/* Check if it's a ctcr. */
	r = check_ctcp_ctcr(1, to_channel, u, from_nick, from_uhost, dest, trailing);
	if (r) return(0);

	r = bind_check(BT_notice, u ? &u->settings[0].flags : NULL, trailing, from_nick, from_uhost, u, dest, trailing);

	if (!(r & BIND_RET_BREAK)) {
		/* This should probably go in the partyline module later. */
		if (to_channel) {
			putlog(LOG_PUBLIC, dest, "-%s:%s- %s", from_nick, dest, trailing);
		}
		else {
			putlog(LOG_MSGS, "*", "-%s (%s)- %s", from_nick, from_uhost, trailing);
		}
	}
	return(0);
}

/* WALLOPS: oper's nuisance
	:csd.bu.edu WALLOPS :Connect '*.uiuc.edu 6667' from Joshua
 */
static int gotwall(char *from_nick, char *from_uhost, user_t *u, char *cmd, int nargs, char *args[])
{
	char *msg;
	int r;

	msg = args[1];
	r = bind_check(BT_wall, NULL, msg, from_nick, msg);
	if (!(r & BIND_RET_BREAK)) {
		if (from_uhost) putlog(LOG_WALL, "*", "!%s (%s)! %s", from_nick, from_uhost, msg);
		else putlog(LOG_WALL, "*", "!%s! %s", from_nick, msg);
	}
	return 0;
}


/* 432 : Bad nickname
 * If we're registered already, then just inform the user and keep the current
 * nick. Otherwise, generate a random nick so that we can get registered. */
static int got432(char *from_nick, char *from_uhost, user_t *u, char *cmd, int nargs, char *args[])
{
	char *badnick;

	badnick = args[1];
	if (current_server.registered) {
		putlog(LOG_MISC, "*", "NICK IS INVALID: %s (keeping '%s').", badnick, current_server.nick);
	}
	else {
		putlog(LOG_MISC, "*", "Server says my nickname ('%s') is invalid, trying new random nick.", badnick);
		try_random_nick();
	}
	return(0);
}

/* 433 : Nickname in use
 * Change nicks till we're acceptable or we give up
 */
static int got433(char *from_nick, char *from_uhost, user_t *u, char *cmd, int nargs, char *args[])
{
	char *badnick;

	badnick = args[1];
	if (current_server.registered) {
		/* We are online and have a nickname, we'll keep it */
		putlog(LOG_MISC, "*", "Nick is in use: '%s' (keeping '%s')", badnick, current_server.nick);
	}
	else {
		putlog(LOG_MISC, "*", "Nick is in use: '%s' (trying next one)", badnick, current_server.nick);
		try_next_nick();
	}
	return(0);
}

/* 435 : Cannot change to a banned nickname. */
static int got435(char *from_nick, char *from_uhost, user_t *u, char *cmd, int nargs, char *args[])
{
	char *banned_nick, *chan;

	banned_nick = args[1];
	chan = args[2];
	putlog(LOG_MISC, "*", "Cannot change to banned nickname (%s on %s)", banned_nick, chan);
	return(0);
}

/* 437 : Nickname juped (IRCnet) */
static int got437(char *from_nick, char *from_uhost, user_t *u, char *cmd, int nargs, char *args[])
{
	char *chan;

	chan = args[1];
	putlog(LOG_MISC, "*", "Can't change nickname to %s. Is my nickname banned?", chan);

	/* If we aren't registered yet, try another nick. Otherwise just keep
	 * our current nick (no action). */
	if (!current_server.registered) {
		try_next_nick();
	}
	return(0);
}

/* 438 : Nick change too fast */
static int got438(char *from_nick, char *from_uhost, user_t *u, char *cmd, int nargs, char *args[])
{
	putlog(LOG_MISC, "*", "%s", _("Nick change was too fast."));
	return(0);
}

static int got451(char *from_nick, char *from_uhost, user_t *u, char *cmd, int nargs, char *args[])
{
	/* Usually if we get this then we really messed up somewhere
	* or this is a non-standard server, so we log it and kill the socket
	* hoping the next server will work :) -poptix
	*/
	putlog(LOG_MISC, "*", _("%s says I'm not registered, trying next one."), from_nick);
	kill_server("The server says I'm not registered, trying next one.");
	return 0;
}

/* Got error */
static int goterror(char *from_nick, char *from_uhost, user_t *u, char *cmd, int nargs, char *args[])
{
	if (current_server.registered) {
		char *uhost, *full;

		if (current_server.user && current_server.host) {
			uhost = egg_mprintf("%s@%s", current_server.user, current_server.host);
			full = egg_mprintf("%s!%s", current_server.nick, uhost);
			u = user_lookup_by_irchost_nocache(full);
			free(full);
		}
		else uhost = NULL;
		channel_on_quit(current_server.nick, uhost, u);
		if (uhost) free(uhost);
	}

	putlog(LOG_MSGS | LOG_SERV, "*", "-ERROR from server- %s", args[0]);
	putlog(LOG_SERV, "*", "Disconnecting from server.");
	kill_server("disconnecting due to error");
	return(0);
}


/* Pings are immediately returned, no queue. */
static int gotping(char *from_nick, char *from_uhost, user_t *u, char *cmd, int nargs, char *args[])
{
	printserv(SERVER_NOQUEUE, "PONG :%s", args[0]);
	return(0);
}

/* 311 : save our user@host from whois reply */
static int got311(char *from_nick, char *from_uhost, user_t *u, char *cmd, int nargs, char *args[])
{
	char *nick, *user, *host, *realname;
  
	if (nargs < 6) return(0);

	nick = args[1];
	user = args[2];
	host = args[3];
	realname = args[5];
  
	if (match_my_nick(nick)) {
		str_redup(&current_server.user, user);
		str_redup(&current_server.host, host);
		str_redup(&current_server.real_name, realname);
		/* If we're using server lookup to determine ip address, start that now. */
		if (server_config.ip_lookup == 1) dcc_dns_set(host);
	}

	return(0);
}

bind_list_t server_raw_binds[] = {
	{NULL, "PRIVMSG", gotmsg},
	{NULL, "NOTICE", gotnotice},
	{NULL, "WALLOPS", gotwall},
	{NULL, "PING", gotping},
	{NULL, "ERROR", goterror},
	{NULL, "001", got001},
	{NULL, "005", got005},
	{NULL, "376", got376},
	{NULL, "432", got432},
	{NULL, "433", got433},
	{NULL, "435", got435},
	{NULL, "438", got438},
	{NULL, "437", got437},
	{NULL, "451", got451},
	{NULL, "311", got311},
	{NULL, NULL, NULL}
};
