/*
 * servmsg.c -- part of server.mod
 *
 * $Id: servmsg.c,v 1.1 2001/10/27 16:34:52 ite Exp $
 */
/*
 * Copyright (C) 1997 Robey Pointer
 * Copyright (C) 1999, 2000, 2001 Eggheads Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */


static time_t last_ctcp    = (time_t) 0L;
static int    count_ctcp   = 0;
static char   altnick_char = 0;

/* We try to change to a preferred unique nick here. We always first try the
 * specified alternate nick. If that failes, we repeatedly modify the nick
 * until it gets accepted.
 *
 * sent nick:
 *     "<altnick><c>"
 *                ^--- additional count character: 1-9^-_\\[]`a-z
 *          ^--------- given, alternate nick
 *
 * The last added character is always saved in altnick_char. At the very first
 * attempt (were altnick_char is 0), we try the alternate nick without any
 * additions.
 *
 * fixed by guppy (1999/02/24) and Fabian (1999/11/26)
 */
static int gotfake433(char *from)
{
  int l = strlen(botname) - 1;

  /* First run? */
  if (altnick_char == 0) {
    char *alt = get_altbotnick();

    if (alt[0] && (irccmp(alt, botname)))
      /* Alternate nickname defined. Let's try that first. */
      strcpy(botname, alt);
    else {
      /* Fall back to appending count char. */
      altnick_char = '0';
      if ((l + 1) == nick_len) {
	botname[l] = altnick_char;
      } else {
	botname[++l]   = altnick_char;
	botname[l + 1] = 0;
      }
    }
  /* No, we already tried the default stuff. Now we'll go through variations
   * of the original alternate nick.
   */
  } else {
    char *oknicks = "^-_\\[]`";
    char *p = strchr(oknicks, altnick_char);

    if (p == NULL) {
      if (altnick_char == '9')
        altnick_char = oknicks[0];
      else
	altnick_char = altnick_char + 1;
    } else {
      p++;
      if (!*p)
	altnick_char = 'a' + random() % 26;
      else
	altnick_char = (*p);
    }
    botname[l] = altnick_char;
  }
  putlog(LOG_MISC, "*", _("NICK IN USE: Trying %s"), botname);
  dprintf(DP_MODE, "NICK %s\n", botname);
  return 0;
}

/* Check for tcl-bound msg command, return 1 if found
 *
 * msg: proc-name <nick> <user@host> <handle> <args...>
 */
static int check_tcl_msg(char *cmd, char *nick, char *uhost,
			 struct userrec *u, char *args)
{
  struct flag_record fr = {FR_GLOBAL | FR_CHAN | FR_ANYWH, 0, 0, 0, 0, 0};
  char *hand = u ? u->handle : "*";
  int x;

  get_user_flagrec(u, &fr, NULL);

  x = check_bind(BT_msg, cmd, &fr, nick, uhost, u, args);
  if (x & BIND_RET_LOG) putlog(LOG_CMDS, "*", "(%s!%s) !%s! %s %s", nick, uhost, hand, args);
  if (x) return(1);
  else return(0);
}

static void check_tcl_notc(char *nick, char *uhost, struct userrec *u,
	       		   char *dest, char *arg)
{
  struct flag_record fr = {FR_GLOBAL | FR_CHAN | FR_ANYWH, 0, 0, 0, 0, 0};

  get_user_flagrec(u, &fr, NULL);
  check_bind(BT_notice, arg, &fr, nick, uhost, u, arg, dest);
}

static void check_tcl_msgm(char *nick, char *uhost, struct userrec *u, char *arg)
{
  struct flag_record fr = {FR_GLOBAL | FR_CHAN | FR_ANYWH, 0, 0, 0, 0, 0};

/*
  if (arg[0])
    simple_sprintf(args, "%s %s", cmd, arg);
  else
    strcpy(args, cmd);
*/

  get_user_flagrec(u, &fr, NULL);
  check_bind(BT_msgm, arg, &fr, nick, uhost, u, arg);
}

/* Return 1 if processed.
 */
static int check_tcl_raw(char *from, char *code, char *msg)
{
  return check_bind(BT_raw, code, NULL, from, code, msg);
}

static int check_tcl_ctcpr(char *nick, char *uhost, struct userrec *u,
			   char *dest, char *keyword, char *args,
			   bind_table_t *table)
{
  struct flag_record fr = {FR_GLOBAL | FR_CHAN | FR_ANYWH, 0, 0, 0, 0, 0};
  get_user_flagrec(u, &fr, NULL);

  return check_bind(table, keyword, &fr, nick, uhost, u, dest, keyword, args);
}

static int check_tcl_wall(char *from, char *msg)
{
  int x;

  x = check_bind(BT_wall, msg, NULL, from, msg);
  if (x & BIND_RET_LOG) {
    putlog(LOG_WALL, "*", "!%s! %s", from, msg);
    return 1;
  }
  else return 0;
}

static int check_tcl_flud(char *nick, char *uhost, struct userrec *u,
			  char *ftype, char *chname)
{
  return check_bind(BT_flood, ftype, NULL, nick, uhost, u, ftype, chname);
}

static int match_my_nick(char *nick)
{
  if (!irccmp(nick, botname))
    return 1;
  return 0;
}

/* 001: welcome to IRC (use it to fix the server name)
 */
static int got001(char *from, char *ignore, char *msg)
{
  struct server_list *x;
  int i, servidx = findanyidx(serv);
  struct chanset_t *chan;

  /* Ok...param #1 of 001 = what server thinks my nick is */
  server_online = now;
  fixcolon(msg);
  strncpyz(botname, msg, NICKLEN);
  altnick_char = 0;
  dprintf(DP_SERVER, "WHOIS %s\n", botname); /* get user@host */
  check_bind_event("init-server");
  x = serverlist;
  if (x == NULL)
    return 0;			/* Uh, no server list */
  /* Only join if the IRC module is loaded. */
  if (module_find("irc", 0, 0))
    for (chan = chanset; chan; chan = chan->next) {
      chan->status &= ~(CHAN_ACTIVE | CHAN_PEND);
      if (!channel_inactive(chan))
	dprintf(DP_SERVER, "JOIN %s %s\n",
	        (chan->name[0]) ? chan->name : chan->dname,
	        chan->channel.key[0] ? chan->channel.key : chan->key_prot);
    }
  if (strcasecmp(from, dcc[servidx].host)) {
    putlog(LOG_MISC, "*", "(%s claims to be %s; updating server list)",
	   dcc[servidx].host, from);
    for (i = curserv; i > 0 && x != NULL; i--)
      x = x->next;
    if (x == NULL) {
      putlog(LOG_MISC, "*", "Invalid server list!");
      return 0;
    }
    if (x->realname)
      free(x->realname);
    if (strict_servernames == 1) {
      x->realname = NULL;
      if (x->name)
	free(x->name);
      malloc_strcpy(x->name, from);
    } else
      malloc_strcpy(x->realname, from);
  }
  return 0;
}

/* Got 442: not on channel
 */
static int got442(char *from, char *ignore, char *msg)
{
  char			*chname;
  struct chanset_t	*chan;
  struct server_list	*x;
  int			 i;

  for (x = serverlist, i = 0; x; x = x->next, i++)
    if (i == curserv) {
      if (strcasecmp(from, x->realname ? x->realname : x->name))
	return 0;
      break;
    }
  newsplit(&msg);
  chname = newsplit(&msg);
  chan = findchan(chname);
  if (chan)
    if (!channel_inactive(chan)) {
      module_entry	*me = module_find("channels", 0, 0);

      putlog(LOG_MISC, chname, _("Server says Im not on channel: %s"), chname);
      if (me && me->funcs)
	(me->funcs[CHANNEL_CLEAR])(chan, 1);
      chan->status &= ~CHAN_ACTIVE;
      dprintf(DP_MODE, "JOIN %s %s\n", chan->name,
	      chan->channel.key[0] ? chan->channel.key : chan->key_prot);
    }

  return 0;
}

/* Close the current server connection.
 */
static void nuke_server(char *reason)
{
  if (serv >= 0) {
    int servidx = findanyidx(serv);

    if (reason && (servidx > 0))
      dprintf(servidx, "QUIT :%s\n", reason);
    disconnect_server(servidx);
    lostdcc(servidx);
  }
}

static char ctcp_reply[1024] = "";

static int lastmsgs[FLOOD_GLOBAL_MAX];
static char lastmsghost[FLOOD_GLOBAL_MAX][81];
static time_t lastmsgtime[FLOOD_GLOBAL_MAX];

/* Do on NICK, PRIVMSG, NOTICE and JOIN.
 */
static int detect_flood(char *floodnick, char *floodhost, char *from, int which)
{
  char *p, ftype[10], h[1024];
  struct userrec *u;
  int thr = 0, lapse = 0, atr;

  u = get_user_by_host(from);
  atr = u ? u->flags : 0;
  if (atr & (USER_BOT | USER_FRIEND))
    return 0;

  /* Determine how many are necessary to make a flood */
  switch (which) {
  case FLOOD_PRIVMSG:
  case FLOOD_NOTICE:
    thr = flud_thr;
    lapse = flud_time;
    strcpy(ftype, "msg");
    break;
  case FLOOD_CTCP:
    thr = flud_ctcp_thr;
    lapse = flud_ctcp_time;
    strcpy(ftype, "ctcp");
    break;
  }
  if ((thr == 0) || (lapse == 0))
    return 0;			/* No flood protection */
  /* Okay, make sure i'm not flood-checking myself */
  if (match_my_nick(floodnick))
    return 0;
  if (!strcasecmp(floodhost, botuserhost))
    return 0;			/* My user@host (?) */
  p = strchr(floodhost, '@');
  if (p) {
    p++;
    if (strcasecmp(lastmsghost[which], p)) {	/* New */
      strcpy(lastmsghost[which], p);
      lastmsgtime[which] = now;
      lastmsgs[which] = 0;
      return 0;
    }
  } else
    return 0;			/* Uh... whatever. */

  if (lastmsgtime[which] < now - lapse) {
    /* Flood timer expired, reset it */
    lastmsgtime[which] = now;
    lastmsgs[which] = 0;
    return 0;
  }
  lastmsgs[which]++;
  if (lastmsgs[which] >= thr) {	/* FLOOD */
    /* Reset counters */
    lastmsgs[which] = 0;
    lastmsgtime[which] = 0;
    lastmsghost[which][0] = 0;
    u = get_user_by_host(from);
    if (check_tcl_flud(floodnick, floodhost, u, ftype, "*"))
      return 0;
    /* Private msg */
    simple_sprintf(h, "*!*@%s", p);
    putlog(LOG_MISC, "*", _("Flood from @%s!  Placing on ignore!"), p);
    addignore(h, origbotname, (which == FLOOD_CTCP) ? "CTCP flood" :
	      "MSG/NOTICE flood", now + (60 * ignore_time));
  }
  return 0;
}

/* Check for more than 8 control characters in a line.
 * This could indicate: beep flood CTCP avalanche.
 */
static int detect_avalanche(char *msg)
{
  int count = 0;
  unsigned char *p;

  for (p = (unsigned char *) msg; (*p) && (count < 8); p++)
    if ((*p == 7) || (*p == 1))
      count++;
  if (count >= 8)
    return 1;
  else
    return 0;
}

/* Got a private message.
 */
static int gotmsg(char *from, char *ignore, char *msg)
{
  char *to, buf[UHOSTLEN], *nick, *uhost, ctcpbuf[512], *ctcp, *p, *p1;
  char *code;
  struct userrec *u;
  int ctcp_count = 0;
  int ignoring;

  if (msg[0] && ((strchr(CHANMETA, *msg) != NULL) ||
     (*msg == '@')))           /* Notice to a channel, not handled here */
    return 0;
  ignoring = match_ignore(from);
  to = newsplit(&msg);
  fixcolon(msg);
  strncpyz(buf, from, sizeof buf);
  nick = strtok(buf, "!");
  uhost = strtok(NULL, "!");
  /* Only check if flood-ctcp is active */
  if (flud_ctcp_thr && detect_avalanche(msg)) {
    if (!ignoring) {
      putlog(LOG_MODES, "*", "Avalanche from %s - ignoring", from);
      /* FIXME: get rid of this mess */
      p = strchr(uhost, '@');
      if (p != NULL)
	p++;
      else
	p = uhost;
      simple_sprintf(ctcpbuf, "*!*@%s", p);
      addignore(ctcpbuf, origbotname, "ctcp avalanche",
		now + (60 * ignore_time));
    }
  }
  /* Check for CTCP: */
  ctcp_reply[0] = 0;
  p = strchr(msg, 1);
  while ((p != NULL) && (*p)) {
    p++;
    p1 = p;
    while ((*p != 1) && (*p != 0))
      p++;
    if (*p == 1) {
      *p = 0;
      ctcp = strcpy(ctcpbuf, p1);
      strcpy(p1 - 1, p + 1);
      if (!ignoring)
	detect_flood(nick, uhost, from,
		     strncmp(ctcp, "ACTION ", 7) ? FLOOD_CTCP : FLOOD_PRIVMSG);
      /* Respond to the first answer_ctcp */
      p = strchr(msg, 1);
      if (ctcp_count < answer_ctcp) {
	ctcp_count++;
	if (ctcp[0] != ' ') {
	  code = newsplit(&ctcp);
	  if ((to[0] == '$') || strchr(to, '.')) {
	    if (!ignoring)
	      /* Don't interpret */
	      putlog(LOG_PUBLIC, to, "CTCP %s: %s from %s (%s) to %s",
		     code, ctcp, nick, uhost, to);
	  } else {
	    u = get_user_by_host(from);
	    if (!ignoring || trigger_on_ignore) {
	      if (!check_tcl_ctcp(nick, uhost, u, to, code, ctcp) &&
		  !ignoring) {
		if (!strcasecmp(code, "DCC")) {
		  /* If it gets this far unhandled, it means that
		   * the user is totally unknown.
		   */
		  code = newsplit(&ctcp);
		  if (!strcmp(code, "CHAT")) {
		    if (!quiet_reject) {
		      if (u)
			dprintf(DP_HELP, "NOTICE %s :%s\n", nick,
				"I'm not accepting call at the moment.");
		      else
			dprintf(DP_HELP, "NOTICE %s :%s\n",
				nick, _("I dont accept DCC chats from strangers."));
		    }
		    putlog(LOG_MISC, "*", "%s: %s",
			   _("Refused DCC chat (no access)"), from);
		  } else
		    putlog(LOG_MISC, "*", "Refused DCC %s: %s",
			   code, from);
		}
	      }
	      if (!strcmp(code, "ACTION")) {
		putlog(LOG_MSGS, "*", "Action to %s: %s %s",
		       to, nick, ctcp);
	      } else {
		putlog(LOG_MSGS, "*", "CTCP %s: %s from %s (%s)",
		       code, ctcp, nick, uhost);
	      }			/* I love a good close cascade ;) */
	    }
	  }
	}
      }
    }
  }
  /* Send out possible ctcp responses */
  if (ctcp_reply[0]) {
    if (ctcp_mode != 2) {
      dprintf(DP_HELP, "NOTICE %s :%s\n", nick, ctcp_reply);
    } else {
      if (now - last_ctcp > flud_ctcp_time) {
	dprintf(DP_HELP, "NOTICE %s :%s\n", nick, ctcp_reply);
	count_ctcp = 1;
      } else if (count_ctcp < flud_ctcp_thr) {
	dprintf(DP_HELP, "NOTICE %s :%s\n", nick, ctcp_reply);
	count_ctcp++;
      }
      last_ctcp = now;
    }
  }
  if (msg[0]) {
    if ((to[0] == '$') || (strchr(to, '.') != NULL)) {
      /* Msg from oper */
      if (!ignoring) {
	detect_flood(nick, uhost, from, FLOOD_PRIVMSG);
	/* Do not interpret as command */
	putlog(LOG_MSGS | LOG_SERV, "*", "[%s!%s to %s] %s",
	       nick, uhost, to, msg);
      }
    } else {
      char *code;
      struct userrec *u;

      detect_flood(nick, uhost, from, FLOOD_PRIVMSG);
      u = get_user_by_host(from);
      if (!ignoring || trigger_on_ignore) check_tcl_msgm(nick, uhost, u, msg);
      code = newsplit(&msg);
      rmspace(msg);
      if (!ignoring)
	if (!check_tcl_msg(code, nick, uhost, u, msg))
	  putlog(LOG_MSGS, "*", "[%s] %s %s", from, code, msg);
    }
  }
  return 0;
}

/* Got a private notice.
 */
static int gotnotice(char *from, char *ignore, char *msg)
{
  char *to, buf[UHOSTLEN], *nick, *uhost, ctcpbuf[512], *ctcp, *p, *p1;
  char *code;
  struct userrec *u;
  int ignoring;

  if (msg[0] && ((strchr(CHANMETA, *msg) != NULL) ||
      (*msg == '@')))           /* Notice to a channel, not handled here */
    return 0;
  ignoring = match_ignore(from);
  to = newsplit(&msg);
  fixcolon(msg);
  strncpyz(buf, from, sizeof buf);
  nick = strtok(buf, "!");
  uhost = strtok(NULL, "!");
  if (flud_ctcp_thr && detect_avalanche(msg)) {
    /* Discard -- kick user if it was to the channel */
    if (!ignoring)
      putlog(LOG_MODES, "*", "Avalanche from %s", from);
    return 0;
  }
  /* Check for CTCP: */
  p = strchr(msg, 1);
  while ((p != NULL) && (*p)) {
    p++;
    p1 = p;
    while ((*p != 1) && (*p != 0))
      p++;
    if (*p == 1) {
      *p = 0;
      ctcp = strcpy(ctcpbuf, p1);
      strcpy(p1 - 1, p + 1);
      if (!ignoring)
	detect_flood(nick, uhost, from, FLOOD_CTCP);
      p = strchr(msg, 1);
      if (ctcp[0] != ' ') {
	code = newsplit(&ctcp);
	if ((to[0] == '$') || strchr(to, '.')) {
	  if (!ignoring)
	    putlog(LOG_PUBLIC, "*",
		   "CTCP reply %s: %s from %s (%s) to %s", code, ctcp,
		   nick, uhost, to);
	} else {
	  u = get_user_by_host(from);
	  if (!ignoring || trigger_on_ignore) {
	    check_tcl_ctcr(nick, uhost, u, to, code, ctcp);
	    if (!ignoring)
	      /* Who cares? */
	      putlog(LOG_MSGS, "*",
		     "CTCP reply %s: %s from %s (%s) to %s",
		     code, ctcp, nick, uhost, to);
	  }
	}
      }
    }
  }
  if (msg[0]) {
    if (((to[0] == '$') || strchr(to, '.')) && !ignoring) {
      detect_flood(nick, uhost, from, FLOOD_NOTICE);
      putlog(LOG_MSGS | LOG_SERV, "*", "-%s (%s) to %s- %s",
	     nick, uhost, to, msg);
    } else {
      /* Server notice? */
      if (!nick || !uhost) {
	/* Hidden `250' connection count message from server */
	if (strncmp(msg, "Highest connection count:", 25))
	  putlog(LOG_SERV, "*", "-NOTICE- %s", msg);
      } else {
        detect_flood(nick, uhost, from, FLOOD_NOTICE);
        u = get_user_by_host(from);
        if (!ignoring || trigger_on_ignore)
          check_tcl_notc(nick, uhost, u, botname, msg);
        if (!ignoring)
  	      putlog(LOG_MSGS, "*", "-%s (%s)- %s", nick, uhost, msg);
      }
    }
  }
  return 0;
}

/* WALLOPS: oper's nuisance
 */
static int gotwall(char *from, char *ignore, char *msg)
{
  char *p, buf[UHOSTLEN], *nick, *uhost;
  int r;

  fixcolon(msg);
  p = strchr(from, '!');
  if (p && (p == strrchr(from, '!'))) {
    strncpyz(buf, from, sizeof buf);
    nick = strtok(buf, "!");
    uhost = strtok(NULL, "!");
    r = check_tcl_wall(nick, msg);
    if (r == 0)
      putlog(LOG_WALL, "*", "!%s(%s)! %s", nick, uhost, msg);
  } else {
    r = check_tcl_wall(from, msg);
    if (r == 0)
      putlog(LOG_WALL, "*", "!%s! %s", from, msg);
  }
  return 0;
}

/* Called once a minute... but if we're the only one on the
 * channel, we only wanna send out "lusers" once every 5 mins.
 */
static void minutely_checks()
{
  char *alt;

  /* Only check if we have already successfully logged in.  */
  if (!server_online)
    return;
  if (keepnick) {
    /* NOTE: now that botname can but upto NICKLEN bytes long,
     * check that it's not just a truncation of the full nick.
     */
    if (strncmp(botname, origbotname, strlen(botname))) {
      /* See if my nickname is in use and if if my nick is right.  */
	alt = get_altbotnick();
	if (alt[0] && strcasecmp (botname, alt))
	  dprintf(DP_SERVER, "ISON :%s %s %s\n", botname, origbotname, alt);
	else
          dprintf(DP_SERVER, "ISON :%s %s\n", botname, origbotname);
    }
  }
}

/* Pong from server.
 */
static int gotpong(char *from, char *ignore, char *msg)
{
  unsigned int arg;

  newsplit(&msg);
  fixcolon(msg);		/* Scrap server name */
  waiting_for_awake = 0;
  sscanf(msg, "%u", &arg);
  server_lag = now - arg;
  if (server_lag > 99999) {
    /* IRCnet lagmeter support by drummer */
    server_lag = now - lastpingtime;
  }
  return 0;
}

/* This is a reply on ISON :<current> <orig> [<alt>]
 */
static void got303(char *from, char *ignore, char *msg)
{
  char *tmp, *alt;
  int ison_orig = 0, ison_alt = 0;

  if (!keepnick ||
      !strncmp(botname, origbotname, strlen(botname))) {
    return;
  }
  newsplit(&msg);
  fixcolon(msg);
  alt = get_altbotnick();
  tmp = newsplit(&msg);
  if (tmp[0] && !irccmp(botname, tmp)) {
    while ((tmp = newsplit(&msg))[0]) { /* no, it's NOT == */
      if (!irccmp(tmp, origbotname))
        ison_orig = 1;
      else if (alt[0] && !irccmp(tmp, alt))
        ison_alt = 1;
    }
    if (!ison_orig) {
      if (!nick_juped)
        putlog(LOG_MISC, "*", _("Switching back to nick %s"), origbotname);
      dprintf(DP_SERVER, "NICK %s\n", origbotname);
    } else if (alt[0] && !ison_alt && irccmp(botname, alt)) {
      putlog(LOG_MISC, "*", _("Switching back to altnick %s"), alt);
      dprintf(DP_SERVER, "NICK %s\n", alt);
    }
  }
}

/* 432 : Bad nickname
 */
static int got432(char *from, char *ignore, char *msg)
{
  char *erroneus;

  newsplit(&msg);
  erroneus = newsplit(&msg);
  if (server_online)
    putlog(LOG_MISC, "*", "NICK IS INVALID: %s (keeping '%s').", erroneus,
	   botname);
  else {
    putlog(LOG_MISC, "*", _("Server says my nickname is invalid."));
    if (!keepnick) {
      makepass(erroneus);
      erroneus[NICKMAX] = 0;
      dprintf(DP_MODE, "NICK %s\n", erroneus);
    }
    return 0;
  }
  return 0;
}

/* 433 : Nickname in use
 * Change nicks till we're acceptable or we give up
 */
static int got433(char *from, char *ignore, char *msg)
{
  char *tmp;

  if (server_online) {
    /* We are online and have a nickname, we'll keep it */
    newsplit(&msg);
    tmp = newsplit(&msg);
    putlog(LOG_MISC, "*", "NICK IN USE: %s (keeping '%s').", tmp, botname);
    nick_juped = 0;
    return 0;
  }
  gotfake433(from);
  return 0;
}

/* 437 : Nickname juped (IRCnet)
 */
static int got437(char *from, char *ignore, char *msg)
{
  char *s;
  struct chanset_t *chan;

  newsplit(&msg);
  s = newsplit(&msg);
  if (s[0] && (strchr(CHANMETA, s[0]) != NULL)) {
    chan = findchan(s);
    if (chan) {
      if (chan->status & CHAN_ACTIVE) {
	putlog(LOG_MISC, "*", _("Cant change nickname on %s.  Is my nickname banned?"), s);
      } else {
	if (!channel_juped(chan)) {
	  putlog(LOG_MISC, "*", _("Channel %s is juped. :("), s);
	  chan->status |= CHAN_JUPED;
	}
      }
    }
  } else if (server_online) {
    if (!nick_juped)
      putlog(LOG_MISC, "*", "NICK IS JUPED: %s (keeping '%s').", s, botname);
    if (!irccmp(s, origbotname))
      nick_juped = 1;
  } else {
    putlog(LOG_MISC, "*", "%s: %s", _("Nickname has been juped"), s);
    gotfake433(from);
  }
  return 0;
}

/* 438 : Nick change too fast
 */
static int got438(char *from, char *ignore, char *msg)
{
  newsplit(&msg);
  newsplit(&msg);
  fixcolon(msg);
  putlog(LOG_MISC, "*", "%s", msg);
  return 0;
}

static int got451(char *from, char *ignore, char *msg)
{
  /* Usually if we get this then we really messed up somewhere
   * or this is a non-standard server, so we log it and kill the socket
   * hoping the next server will work :) -poptix
   */
  /* Um, this does occur on a lagged anti-spoof server connection if the
   * (minutely) sending of joins occurs before the bot does its ping reply.
   * Probably should do something about it some time - beldin
   */
  putlog(LOG_MISC, "*", _("%s says Im not registered, trying next one."), from);
  nuke_server(_("You have a fucked up server."));
  return 0;
}

/* Got error notice
 */
static int goterror(char *from, char *ignore, char *msg)
{
  fixcolon(msg);
  putlog(LOG_SERV | LOG_MSGS, "*", "-ERROR from server- %s", msg);
  if (serverror_quit) {
    putlog(LOG_SERV, "*", "Disconnecting from server.");
    nuke_server("Bah, stupid error messages.");
  }
  return 1;
}

/* Got nick change.
 */
static int gotnick(char *from, char *ignore, char *msg)
{
  char *nick, *alt = get_altbotnick();
  struct userrec *u;

  fixcolon(msg);
  u = get_user_by_host(from);
  nick = strtok(from, "!");
  check_queues(nick, msg);
  if (match_my_nick(nick)) {
    /* Regained nick! */
    strncpyz(botname, msg, NICKLEN);
    altnick_char = 0;
    waiting_for_awake = 0;
    if (!strcmp(msg, origbotname)) {
      putlog(LOG_SERV | LOG_MISC, "*", "Regained nickname '%s'.", msg);
      nick_juped = 0;
    } else if (alt[0] && !strcmp(msg, alt))
      putlog(LOG_SERV | LOG_MISC, "*", "Regained alternate nickname '%s'.",
	     msg);
    else if (keepnick && strcmp(nick, msg)) {
      putlog(LOG_SERV | LOG_MISC, "*", "Nickname changed to '%s'???", msg);
      if (!irccmp(nick, origbotname)) {
        putlog(LOG_MISC, "*", _("Switching back to nick %s"), origbotname);
        dprintf(DP_SERVER, "NICK %s\n", origbotname);
      } else if (alt[0] && !irccmp(nick, alt)
		 && strcasecmp(botname, origbotname)) {
        putlog(LOG_MISC, "*", _("Switching back to altnick %s"), alt);
        dprintf(DP_SERVER, "NICK %s\n", alt);
      }
    } else
      putlog(LOG_SERV | LOG_MISC, "*", "Nickname changed to '%s'???", msg);
  } else if ((keepnick) && (irccmp(nick, msg))) {
    /* Only do the below if there was actual nick change, case doesn't count */
    if (!irccmp(nick, origbotname)) {
      putlog(LOG_MISC, "*", _("Switching back to nick %s"), origbotname);
      dprintf(DP_SERVER, "NICK %s\n", origbotname);
    } else if (alt[0] && !irccmp(nick, alt) &&
	    strcasecmp(botname, origbotname)) {
      putlog(LOG_MISC, "*", _("Switching back to altnick %s"), altnick);
      dprintf(DP_SERVER, "NICK %s\n", altnick);
    }
  }
  return 0;
}

static int gotmode(char *from, char *ignore, char *msg)
{
  char *ch;

  ch = newsplit(&msg);
  /* Usermode changes? */
  if (strchr(CHANMETA, ch[0]) == NULL) {
    if (match_my_nick(ch) && check_mode_r) {
      /* umode +r? - D0H dalnet uses it to mean something different */
      fixcolon(msg);
      if ((msg[0] == '+') && strchr(msg, 'r')) {
	int servidx = findanyidx(serv);

	putlog(LOG_MISC | LOG_JOIN, "*",
	       "%s has me i-lined (jumping)", dcc[servidx].host);
	nuke_server("i-lines suck");
      }
    }
  }
  return 0;
}

static void disconnect_server(int idx)
{
  if (server_online > 0)
    check_bind_event("disconnect-server");
  server_online = 0;
  if (dcc[idx].sock >= 0)
    killsock(dcc[idx].sock);
  dcc[idx].sock = (-1);
  serv = (-1);
  botuserhost[0] = 0;
}

static void eof_server(int idx)
{
  putlog(LOG_SERV, "*", "%s %s", _("Disconnected from"), dcc[idx].host);
  disconnect_server(idx);
  lostdcc(idx);
}

static void display_server(int idx, char *buf)
{
  sprintf(buf, "%s  (lag: %d)", trying_server ? "conn" : "serv",
	  server_lag);
}

static void connect_server(void);

static void kill_server(int idx, void *x)
{
  module_entry *me;

  disconnect_server(idx);
  if ((me = module_find("channels", 0, 0)) && me->funcs) {
    struct chanset_t *chan;

    for (chan = chanset; chan; chan = chan->next)
      (me->funcs[CHANNEL_CLEAR]) (chan, 1);
  }
  /* A new server connection will be automatically initiated in
     about 2 seconds. */
}

static void timeout_server(int idx)
{
  putlog(LOG_SERV, "*", "Timeout: connect to %s", dcc[idx].host);
  disconnect_server(idx);
  lostdcc(idx);
}

static void server_activity(int idx, char *msg, int len);

static struct dcc_table SERVER_SOCKET =
{
  "SERVER",
  0,
  eof_server,
  server_activity,
  NULL,
  timeout_server,
  display_server,
  kill_server,
  NULL
};

static void server_activity(int idx, char *msg, int len)
{
  char *from, *code;

  if (trying_server) {
    strcpy(dcc[idx].nick, "(server)");
    putlog(LOG_SERV, "*", "Connected to %s", dcc[idx].host);
    trying_server = 0;
    waiting_for_awake = 0;
    SERVER_SOCKET.timeout_val = 0;
  }
  from = "";
  if (msg[0] == ':') {
    msg++;
    from = newsplit(&msg);
  }
  code = newsplit(&msg);
  if (debug_output)
    putlog(LOG_RAW, "*", "[@] %s %s %s", from, code, msg);

  /* This has GOT to go into the raw binding table, * merely because this
   * is less effecient.
   */
  check_tcl_raw(from, code, msg);
}

static int gotping(char *from, char *ignore, char *msg)
{
  fixcolon(msg);
  dprintf(DP_MODE, "PONG :%s\n", msg);
  return 0;
}

static int gotkick(char *from, char *ignore, char *msg)
{
  char *nick;

  nick = from;
  if (irccmp(nick, botname))
    /* Not my kick, I don't need to bother about it. */
    return 0;
  if (use_penalties) {
    last_time += 2;
    if (debug_output)
      putlog(LOG_SRVOUT, "*", "adding 2secs penalty (successful kick)");
  }
  return 0;
}

/* Another sec penalty if bot did a whois on another server.
 */
static int whoispenalty(char *from, char *msg)
{
  struct server_list *x = serverlist;
  int i, ii;

  if (x && use_penalties) {
    i = ii = 0;
    for (; x; x = x->next) {
      if (i == curserv) {
        if ((strict_servernames == 1) || !x->realname) {
          if (strcmp(x->name, from))
            ii = 1;
        } else {
          if (strcmp(x->realname, from))
            ii = 1;
        }
      }
      i++;
    }
    if (ii) {
      last_time += 1;
      if (debug_output)
        putlog(LOG_SRVOUT, "*", "adding 1sec penalty (remote whois)");
    }
  }
  return 0;
}

static int got311(char *from, char *ignore, char *msg)
{
  char *n1, *n2, *u, *h;
  
  n1 = newsplit(&msg);
  n2 = newsplit(&msg);
  u = newsplit(&msg);
  h = newsplit(&msg);
  
  if (!n1 || !n2 || !u || !h)
    return 0;
    
  if (match_my_nick(n2))
    snprintf(botuserhost, sizeof botuserhost, "%s@%s", u, h);
  
  return 0;
}


static cmd_t my_raw_binds[] =
{
  {"PRIVMSG",	"",	(Function) gotmsg,		NULL},
  {"NOTICE",	"",	(Function) gotnotice,		NULL},
  {"MODE",	"",	(Function) gotmode,		NULL},
  {"PING",	"",	(Function) gotping,		NULL},
  {"PONG",	"",	(Function) gotpong,		NULL},
  {"WALLOPS",	"",	(Function) gotwall,		NULL},
  {"001",	"",	(Function) got001,		NULL},
  {"303",	"",	(Function) got303,		NULL},
  {"432",	"",	(Function) got432,		NULL},
  {"433",	"",	(Function) got433,		NULL},
  {"437",	"",	(Function) got437,		NULL},
  {"438",	"",	(Function) got438,		NULL},
  {"451",	"",	(Function) got451,		NULL},
  {"442",	"",	(Function) got442,		NULL},
  {"NICK",	"",	(Function) gotnick,		NULL},
  {"ERROR",	"",	(Function) goterror,		NULL},
  {"KICK",	"",	(Function) gotkick,		NULL},
  {"318",	"",	(Function) whoispenalty,	NULL},
  {"311",	"",	(Function) got311,		NULL},
  {NULL,	NULL,	NULL,				NULL}
};

static void server_resolve_success(int);
static void server_resolve_failure(int);

/* Hook up to a server
 */
static void connect_server(void)
{
  char pass[121], botserver[UHOSTLEN];
  static int oldserv = -1;
  int servidx;
  unsigned int botserverport = 0;

  waiting_for_awake = 0;
  trying_server = now;
  empty_msgq();
  /* Start up the counter (always reset it if "never-give-up" is on) */
  if ((oldserv < 0) || (never_give_up))
    oldserv = curserv;
  if (newserverport) {		/* Jump to specified server */
    curserv = (-1);		/* Reset server list */
    strcpy(botserver, newserver);
    botserverport = newserverport;
    strcpy(pass, newserverpass);
    newserver[0] = 0;
    newserverport = 0;
    newserverpass[0] = 0;
    oldserv = (-1);
  } else
    pass[0] = 0;
  if (!cycle_time) {
    struct chanset_t *chan;
    struct server_list *x = serverlist;

    if (!x) {
      putlog(LOG_SERV, "*", "No servers in server list");
      cycle_time = 300;
      return;
    }
 
    servidx = new_dcc(&DCC_DNSWAIT, sizeof(struct dns_info));
    if (servidx < 0) {
      putlog(LOG_SERV, "*",
	     "NO MORE DCC CONNECTIONS -- Can't create server connection.");
      return;
    }

    check_bind_event("connect-server");
    next_server(&curserv, botserver, &botserverport, pass);
    putlog(LOG_SERV, "*", "%s %s %d", _("Trying server"), botserver, botserverport);

    dcc[servidx].port = botserverport;
    strcpy(dcc[servidx].nick, "(server)");
    strncpyz(dcc[servidx].host, botserver, UHOSTLEN);

    botuserhost[0] = 0;

    nick_juped = 0;
    for (chan = chanset; chan; chan = chan->next)
      chan->status &= ~CHAN_JUPED;

    dcc[servidx].timeval = now;
    dcc[servidx].sock = -1;
    dcc[servidx].u.dns->host = calloc(1, strlen(dcc[servidx].host) + 1);
    strcpy(dcc[servidx].u.dns->host, dcc[servidx].host);
    dcc[servidx].u.dns->cbuf = calloc(1, strlen(pass) + 1);
    strcpy(dcc[servidx].u.dns->cbuf, pass);
    dcc[servidx].u.dns->dns_success = server_resolve_success;
    dcc[servidx].u.dns->dns_failure = server_resolve_failure;
    dcc[servidx].u.dns->dns_type = RES_IPBYHOST;
    dcc[servidx].u.dns->type = &SERVER_SOCKET;

    if (server_cycle_wait)
      /* Back to 1st server & set wait time.
       * Note: Put it here, just in case the server quits on us quickly
       */
      cycle_time = server_cycle_wait;
    else
      cycle_time = 0;

    /* I'm resolving... don't start another server connect request */
    resolvserv = 1;
    /* Resolve the hostname. */
    dcc_dnsipbyhost(dcc[servidx].host);
  }
}

static void server_resolve_failure(int servidx)
{
  serv = -1;
  resolvserv = 0;
  putlog(LOG_SERV, "*", "%s %s (%s)", _("Failed connect to"), dcc[servidx].host,
	 _("DNS lookup failed"));
  lostdcc(servidx);
}

static void server_resolve_success(int servidx)
{
  int oldserv = dcc[servidx].u.dns->ibuf;
  char s[121], pass[121];

  resolvserv = 0;
  
  strcpy(dcc[servidx].addr, dcc[servidx].u.dns->host);
  strcpy(pass, dcc[servidx].u.dns->cbuf);
  changeover_dcc(servidx, &SERVER_SOCKET, 0);
  serv = open_telnet(dcc[servidx].addr, dcc[servidx].port);
  if (serv < 0) {
    neterror(s);
    putlog(LOG_SERV, "*", "%s %s (%s)", _("Failed connect to"), dcc[servidx].host,
	   s);
    lostdcc(servidx);
    if (oldserv == curserv && !never_give_up)
      fatal("NO SERVERS WILL ACCEPT MY CONNECTION.", 0);
  } else {
    dcc[servidx].sock = serv;
    /* Queue standard login */
    dcc[servidx].timeval = now;
    SERVER_SOCKET.timeout_val = &server_timeout;
    /* Another server may have truncated it, so use the original */
    strcpy(botname, origbotname);
    /* Start alternate nicks from the beginning */
    altnick_char = 0;
    if (pass[0])
      dprintf(DP_MODE, "PASS %s\n", pass);
    dprintf(DP_MODE, "NICK %s\n", botname);
    dprintf(DP_MODE, "USER %s localhost %s :%s\n", botuser, dcc[servidx].host, botrealname);
    /* Wait for async result now */
  }
}