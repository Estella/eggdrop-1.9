/*
 * cmdsserv.c -- part of server.mod
 *   handles commands from a user via dcc that cause server interaction
 *
 * $Id: cmdsserv.c,v 1.1 2001/10/27 16:34:52 ite Exp $
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

static void cmd_servers(struct userrec *u, int idx, char *par)
{
  struct server_list *x = serverlist;
  int i;
  char s[1024];

  putlog(LOG_CMDS, "*", "#%s# servers", dcc[idx].nick);
  if (!x) {
    dprintf(idx, "No servers.\n");
  } else {
    dprintf(idx, "My server list:\n");
    i = 0;
    for (; x; x = x->next) {
      snprintf(s, sizeof s, "%14s %20.20s:%-10d",
		   (i == curserv) ? "I am here ->" : "", x->name,
		   x->port ? x->port : default_port);
      if (x->realname)
	snprintf(s + 46, sizeof s - 46, " (%-.20s)", x->realname);
      dprintf(idx, "%s\n", s);
      i++;
    }
  }
}

static void cmd_dump(struct userrec *u, int idx, char *par)
{
  if (!(isowner(dcc[idx].nick)) && (must_be_owner == 2)) {
    dprintf(idx, _("What?  You need .help\n"));
    return;
  }
  if (!par[0]) {
    dprintf(idx, "Usage: dump <server stuff>\n");
    return;
  }
  putlog(LOG_CMDS, "*", "#%s# dump %s", dcc[idx].nick, par);
  dprintf(DP_SERVER, "%s\n", par);
}

static void cmd_jump(struct userrec *u, int idx, char *par)
{
  char *other;
  int port;

  if (par[0]) {
    other = newsplit(&par);
    port = atoi(newsplit(&par));
    if (!port)
      port = default_port;
    putlog(LOG_CMDS, "*", "#%s# jump %s %d %s", dcc[idx].nick, other,
	   port, par);
    strncpyz(newserver, other, sizeof newserver);
    newserverport = port;
    strncpyz(newserverpass, par, sizeof newserverpass);
  } else
    putlog(LOG_CMDS, "*", "#%s# jump", dcc[idx].nick);
  dprintf(idx, "%s...\n", _("Jumping servers..."));
  cycle_time = 0;
  nuke_server("changing servers");
}

static void cmd_clearqueue(struct userrec *u, int idx, char *par)
{
  int	msgs;

  if (!par[0]) {
    dprintf(idx, "Usage: clearqueue <mode|server|help|all>\n");
    return;
  }
  if (!strcasecmp(par, "all")) {
    msgs = modeq.tot + mq.tot + hq.tot;
    msgq_clear(&modeq);
    msgq_clear(&mq);
    msgq_clear(&hq);
    double_warned = burst = 0;
    dprintf(idx, "Removed %d msgs from all queues\n", msgs);
  } else if (!strcasecmp(par, "mode")) {
    msgs = modeq.tot;
    msgq_clear(&modeq);
    if (mq.tot == 0)
      burst = 0;
    double_warned = 0;
    dprintf(idx, "Removed %d msgs from the %s queue\n", msgs, "mode");
  } else if (!strcasecmp(par, "help")) {
    msgs = hq.tot;
    msgq_clear(&hq);
    double_warned = 0;
    dprintf(idx, "Removed %d msgs from the %s queue\n", msgs, "help");
  } else if (!strcasecmp(par, "server")) {
    msgs = mq.tot;
    msgq_clear(&mq);
    if (modeq.tot == 0)
      burst = 0;
    double_warned = 0;
    dprintf(idx, "Removed %d msgs from the %s queue\n", msgs, "server");
  } else {
    dprintf(idx, "Usage: clearqueue <mode|server|help|all>\n");
    return;
  }
  putlog(LOG_CMDS, "*", "#%s# clearqueue %s", dcc[idx].nick, par);
}

/* Function call should be:
 *   int cmd_whatever(idx,"parameters");
 *
 * As with msg commands, function is responsible for any logging.
 */
static cmd_t C_dcc_serv[] =
{
  {"dump",		"m",	(Function) cmd_dump,		NULL},
  {"jump",		"m",	(Function) cmd_jump,		NULL},
  {"servers",		"o",	(Function) cmd_servers,		NULL},
  {"clearqueue",	"m",	(Function) cmd_clearqueue,	NULL},
  {NULL,		NULL,	NULL,				NULL}
};