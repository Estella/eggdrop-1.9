/*
 * channels.c --
 *
 *	support for channels within the bot
 */
/*
 * Copyright (C) 1997 Robey Pointer
 * Copyright (C) 1999, 2000, 2001, 2002, 2003 Eggheads Development Team
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

#ifndef lint
static const char rcsid[] = "$Id: channels.c,v 1.23 2003/01/30 01:15:49 wcc Exp $";
#endif

#define MODULE_NAME "channels"
#define MAKING_CHANNELS
#include <sys/stat.h>
#include "lib/eggdrop/module.h"

#define start channels_LTX_start

static eggdrop_t *egg = NULL;

static int  setstatic;
static int  use_info;
static char chanfile[121];
static int  chan_hack;
static int  quiet_save;
static char glob_chanmode[64];		/* Default chanmode (drummer,990731) */
static struct udef_struct *udef;
static int global_stopnethack_mode;
static int global_revenge_mode;
static int global_idle_kick;		/* Default idle_kick setting. */
static int global_aop_min;
static int global_aop_max;
static int global_ban_time;
static int global_exempt_time;
static int global_invite_time;

/* Global channel settings (drummer/dw) */
static char glob_chanset[512];

/* Global flood settings */
static int gfld_chan_thr;
static int gfld_chan_time;
static int gfld_deop_thr;
static int gfld_deop_time;
static int gfld_kick_thr;
static int gfld_kick_time;
static int gfld_join_thr;
static int gfld_join_time;
static int gfld_ctcp_thr;
static int gfld_ctcp_time;
static int gfld_nick_thr;
static int gfld_nick_time;

#include "channels.h"
#include "cmdschan.c"
#include "tclchan.c"
#include "userchan.c"
#include "udefchan.c"


static void set_mode_protect(struct chanset_t *chan, char *set)
{
  int i, pos = 1;
  char *s, *s1;

  /* Clear old modes */
  chan->mode_mns_prot = chan->mode_pls_prot = 0;
  chan->limit_prot = 0;
  chan->key_prot[0] = 0;
  for (s = newsplit(&set); *s; s++) {
    i = 0;
    switch (*s) {
    case '+':
      pos = 1;
      break;
    case '-':
      pos = 0;
      break;
    case 'i':
      i = CHANINV;
      break;
    case 'p':
      i = CHANPRIV;
      break;
    case 's':
      i = CHANSEC;
      break;
    case 'm':
      i = CHANMODER;
      break;
    case 'c':
      i = CHANNOCLR;
      break;
    case 'R':
      i = CHANREGON;
      break;
    case 'M':
      i = CHANMODREG;
      break;
    case 't':
      i = CHANTOPIC;
      break;
    case 'n':
      i = CHANNOMSG;
      break;
    case 'a':
      i = CHANANON;
      break;
    case 'q':
      i = CHANQUIET;
      break;
    case 'l':
      i = CHANLIMIT;
      chan->limit_prot = 0;
      if (pos) {
	s1 = newsplit(&set);
	if (s1[0])
	  chan->limit_prot = atoi(s1);
      }
      break;
    case 'k':
      i = CHANKEY;
      chan->key_prot[0] = 0;
      if (pos) {
	s1 = newsplit(&set);
	if (s1[0])
	  strcpy(chan->key_prot, s1);
      }
      break;
    }
    if (i) {
      if (pos) {
	chan->mode_pls_prot |= i;
	chan->mode_mns_prot &= ~i;
      } else {
	chan->mode_pls_prot &= ~i;
	chan->mode_mns_prot |= i;
      }
    }
  }
  /* Prevents a +s-p +p-s flood  (fixed by drummer) */
  if (chan->mode_pls_prot & CHANSEC)
    chan->mode_pls_prot &= ~CHANPRIV;
}

static void get_mode_protect(struct chanset_t *chan, char *s)
{
  char *p = s, s1[121];
  int i, tst;

  s1[0] = 0;
  for (i = 0; i < 2; i++) {
    if (i == 0) {
      tst = chan->mode_pls_prot;
      if ((tst) || (chan->limit_prot != 0) || (chan->key_prot[0]))
	*p++ = '+';
      if (chan->limit_prot != 0) {
	*p++ = 'l';
	sprintf(&s1[strlen(s1)], "%d ", chan->limit_prot);
      }
      if (chan->key_prot[0]) {
	*p++ = 'k';
	sprintf(&s1[strlen(s1)], "%s ", chan->key_prot);
      }
    } else {
      tst = chan->mode_mns_prot;
      if (tst)
	*p++ = '-';
      if (tst & CHANKEY)
	*p++ = 'k';
      if (tst & CHANLIMIT)
	*p++ = 'l';
    }
    if (tst & CHANINV)
      *p++ = 'i';
    if (tst & CHANPRIV)
      *p++ = 'p';
    if (tst & CHANSEC)
      *p++ = 's';
    if (tst & CHANMODER)
      *p++ = 'm';
    if (tst & CHANNOCLR)
      *p++ = 'c';
    if (tst & CHANREGON)
      *p++ = 'R';
    if (tst & CHANMODREG)
      *p++ = 'M';
    if (tst & CHANTOPIC)
      *p++ = 't';
    if (tst & CHANNOMSG)
      *p++ = 'n';
    if (tst & CHANANON)
      *p++ = 'a';
    if (tst & CHANQUIET)
      *p++ = 'q';
  }
  *p = 0;
  if (s1[0]) {
    s1[strlen(s1) - 1] = 0;
    strcat(s, " ");
    strcat(s, s1);
  }
}

/* Returns true if this is one of the channel masks
 */
static int ismodeline(masklist *m, char *user)
{
  for (; m && m->mask[0]; m = m->next)  
    if (!irccmp(m->mask, user))
      return 1;
  return 0;
}

/* Returns true if user matches one of the masklist -- drummer
 */
static int ismasked(masklist *m, char *user)
{
  for (; m && m->mask[0]; m = m->next)
    if (wild_match(m->mask, user))
      return 1;
  return 0;
}

/* Unlink chanset element from chanset list.
 */
inline static int chanset_unlink(struct chanset_t *chan)
{
  struct chanset_t	*c, *c_old = NULL;

  for (c = chanset; c; c_old = c, c = c->next) {
    if (c == chan) {
      if (c_old)
	c_old->next = c->next;
      else
	chanset = c->next;
      return 1;
    }
  }
  return 0;
}

/* Completely removes a channel.
 *
 * This includes the removal of all channel-bans, -exempts and -invites, as
 * well as all user flags related to the channel.
 */
static void remove_channel(struct chanset_t *chan)
{
   int		 i;
   module_entry	*me;

   /* Remove the channel from the list, so that noone can pull it
      away from under our feet during the check_tcl_part() call. */
   (void) chanset_unlink(chan);

   if ((me = module_find("irc", 1, 3)) != NULL)
     (me->funcs[IRC_DO_CHANNEL_PART])(chan);

   clear_channel(chan, 0);
   noshare = 1;
   /* Remove channel-bans */
   while (chan->bans)
     u_delmask('b', chan, chan->bans->mask, 1);
   /* Remove channel-exempts */
   while (chan->exempts)
     u_delmask('e', chan, chan->exempts->mask, 1);
   /* Remove channel-invites */
   while (chan->invites)
     u_delmask('I', chan, chan->invites->mask, 1);
   /* Remove channel specific user flags */
   user_del_chan(chan->dname);
   noshare = 0;
   free(chan->channel.key);
   for (i = 0; i < 6 && chan->cmode[i].op; i++)
     free(chan->cmode[i].op);
   if (chan->key)
     free(chan->key);
   if (chan->rmkey)
     free(chan->rmkey);
   free(chan);
}

/* Bind this to chon and *if* the users console channel == ***
 * then set it to a specific channel
 */
static int channels_chon(char *handle, int idx)
{
  struct flag_record fr = {FR_CHAN | FR_ANYWH | FR_GLOBAL, 0, 0, 0, 0, 0};
  int find, found = 0;
  struct chanset_t *chan = chanset;

  if (dcc[idx].type == &DCC_CHAT) {
    if (!findchan_by_dname(dcc[idx].u.chat->con_chan) &&
	((dcc[idx].u.chat->con_chan[0] != '*') ||
	 (dcc[idx].u.chat->con_chan[1] != 0))) {
      get_user_flagrec(dcc[idx].user, &fr, NULL);
      if (glob_op(fr))
	found = 1;
      if (chan_owner(fr))
	find = USER_OWNER;
      else if (chan_master(fr))
	find = USER_MASTER;
      else
	find = USER_OP;
      fr.match = FR_CHAN;
      while (chan && !found) {
	get_user_flagrec(dcc[idx].user, &fr, chan->dname);
	if (fr.chan & find)
	  found = 1;
	else
	  chan = chan->next;
      }
      if (!chan)
	chan = chanset;
      if (chan)
	strcpy(dcc[idx].u.chat->con_chan, chan->dname);
      else
	strcpy(dcc[idx].u.chat->con_chan, "*");
    }
  }
  return 0;
}

static char *convert_element(char *src, char *dst)
{
  int flags;

  Tcl_ScanElement(src, &flags);
  Tcl_ConvertElement(src, dst, flags);
  return dst;
}

#define PLSMNS(x) (x ? '+' : '-')

/*
 * Note:
 *  - We write chanmode "" too, so that the bot won't use default-chanmode
 *    instead of ""
 */
static void write_channels()
{
  FILE *f;
  char s[121], w[1024], w2[1024], name[163];
  struct chanset_t *chan;
  struct udef_struct *ul;

  if (!chanfile[0])
    return;
  sprintf(s, "%s~new", chanfile);
  f = fopen(s, "w");
  chmod(s, userfile_perm);
  if (f == NULL) {
    putlog(LOG_MISC, "*", "ERROR writing channel file.");
    return;
  }
  if (!quiet_save)
    putlog(LOG_MISC, "*", "Writing channel file...");
  fprintf(f, "#Dynamic Channel File for %s (%s) -- written %s\n",
	  botnetnick, ver, ctime(&now));
  for (chan = chanset; chan; chan = chan->next) {
    convert_element(chan->dname, name);
    get_mode_protect(chan, w);
    convert_element(w, w2);
    fprintf(f, "channel %s %s%schanmode %s idle_kick %d stopnethack_mode %d \
revenge_mode %d flood_chan %d:%d flood_ctcp %d:%d flood_join %d:%d \
flood_kick %d:%d flood_deop %d:%d flood_nick %d:%d aop_delay %d:%d \
ban_time %d exempt_time %d invite_time %d \
%cenforcebans %cdynamicbans %cuserbans %cautoop %cbitch \
%cgreet %cprotectops %cprotectfriends %cdontkickops \
%cstatuslog %crevenge %crevengebot %cautovoice %csecret \
%cshared %ccycle %cinactive %cdynamicexempts %cuserexempts \
%chonor-global-bans %chonor-global-exempts %chonor-global-invites \
%cdynamicinvites %cuserinvites %cnodesynch ",
	channel_static(chan) ? "set" : "add",
	name,
	channel_static(chan) ? " " : " { ",
	w2,
	chan->idle_kick,
	chan->stopnethack_mode,
	chan->revenge_mode,
	chan->flood_pub_thr, chan->flood_pub_time,
	chan->flood_ctcp_thr, chan->flood_ctcp_time,
	chan->flood_join_thr, chan->flood_join_time,
	chan->flood_kick_thr, chan->flood_kick_time,
	chan->flood_deop_thr, chan->flood_deop_time,
	chan->flood_nick_thr, chan->flood_nick_time,
	chan->aop_min, chan->aop_max,
	chan->ban_time,
	chan->exempt_time,
	chan->invite_time,
	PLSMNS(channel_enforcebans(chan)),
	PLSMNS(channel_dynamicbans(chan)),
	PLSMNS(!channel_nouserbans(chan)),
	PLSMNS(channel_autoop(chan)),
	PLSMNS(channel_bitch(chan)),
	PLSMNS(channel_greet(chan)),
	PLSMNS(channel_protectops(chan)),
	PLSMNS(channel_protectfriends(chan)),
	PLSMNS(channel_dontkickops(chan)),
	PLSMNS(channel_logstatus(chan)),
	PLSMNS(channel_revenge(chan)),
	PLSMNS(channel_revengebot(chan)),
	PLSMNS(channel_autovoice(chan)),
	PLSMNS(channel_secret(chan)),
	PLSMNS(channel_shared(chan)),
	PLSMNS(channel_cycle(chan)),
	PLSMNS(channel_inactive(chan)),
	PLSMNS(channel_dynamicexempts(chan)),
	PLSMNS(!channel_nouserexempts(chan)),
	PLSMNS(channel_honor_global_bans(chan)),
	PLSMNS(channel_honor_global_exempts(chan)),
	PLSMNS(channel_honor_global_invites(chan)),
	PLSMNS(channel_dynamicinvites(chan)),
	PLSMNS(!channel_nouserinvites(chan)),
	PLSMNS(channel_nodesynch(chan)));
    for (ul = udef; ul; ul = ul->next) {
      if (ul->defined && ul->name) {
	if (ul->type == UDEF_FLAG)
	  fprintf(f, "%c%s%s ", getudef(ul->values, chan->dname) ? '+' : '-',
		  "udef_flag_", ul->name);
	else if (ul->type == UDEF_INT)
	  fprintf(f, "%s%s %d ", "udef_int_", ul->name, getudef(ul->values,
		  chan->dname));
	else if (ul->type == UDEF_STR) {
		char *p;
		p = (char *)getudef(ul->values, chan->dname);
		if (!p) strcpy(s, "{}");
		fprintf(f, "udef_str_%s %s ", ul->name, p);
	}
	else
	  debug1("UDEF-ERROR: unknown type %d", ul->type);
      }
    }
    fprintf(f, "%s\n", channel_static(chan) ? "" : "}");
    if (fflush(f)) {
      putlog(LOG_MISC, "*", "ERROR writing channel file.");
      fclose(f);
      return;
    }
  }
  fclose(f);
  unlink(chanfile);
  movefile(s, chanfile);
}

static void read_channels(int create)
{
  struct chanset_t *chan, *chan_next;

  if (!chanfile[0])
    return;
  for (chan = chanset; chan; chan = chan->next)
    if (!channel_static(chan))
      chan->status |= CHAN_FLAGGED;
  chan_hack = 1;
  if (!readtclprog(chanfile) && create) {
    FILE *f;

    /* Assume file isnt there & therfore make it */
    putlog(LOG_MISC, "*", "Creating channel file");
    f = fopen(chanfile, "w");
    if (!f)
      putlog(LOG_MISC, "*", "Couldn't create channel file: %s.  Dropping",
	     chanfile);
    else
      fclose(f);
  }
  chan_hack = 0;
  for (chan = chanset; chan; chan = chan_next) {
    chan_next = chan->next;
    if (chan->status & CHAN_FLAGGED) {
      putlog(LOG_MISC, "*", "No longer supporting channel %s", chan->dname);
      remove_channel(chan);
    }
  }
}

static void backup_chanfile()
{
  char s[125];

  putlog(LOG_MISC, "*", "Backing up channel file...");
  snprintf(s, sizeof s, "%s~bak", chanfile);
  copyfile(chanfile, s);
}

static void channels_prerehash()
{
  struct chanset_t *chan;

  /* Flag will be cleared as the channels are re-added by the
   * config file. Any still flagged afterwards will be removed.
   */
  for (chan = chanset; chan; chan = chan->next) {
    chan->status |= CHAN_FLAGGED;
    /* Flag is only added to channels read from config file */
    if (chan->status & CHAN_STATIC)
      chan->status &= ~CHAN_STATIC;
  }
  setstatic = 1;
}

static void channels_rehash()
{
  struct chanset_t *chan;

  setstatic = 0;
  read_channels(1);
  /* Remove any extra channels, by checking the flag. */
  chan = chanset;
  for (chan = chanset; chan;) {
    if (chan->status & CHAN_FLAGGED) {
      putlog(LOG_MISC, "*", "No longer supporting channel %s", chan->dname);
      remove_channel(chan);
      chan = chanset;
    } else
      chan = chan->next;
  }
}

static cmd_t my_chon[] =
{
  {"*",		"",	(Function) channels_chon,	"channels:chon"},
  {NULL,	NULL,	NULL,				NULL}
};

static void channels_report(int idx, int details)
{
  struct chanset_t *chan;
  int i;
  char s[1024], s2[100];
  struct flag_record fr = {FR_CHAN | FR_GLOBAL, 0, 0, 0, 0, 0};

  for (chan = chanset; chan; chan = chan->next) {
    if (idx != DP_STDOUT)
      get_user_flagrec(dcc[idx].user, &fr, chan->dname);
    if ((idx == DP_STDOUT) || glob_master(fr) || chan_master(fr)) {
      s[0] = 0;
      if (channel_greet(chan))
	strcat(s, "greet, ");
      if (channel_autoop(chan))
	strcat(s, "auto-op, ");
      if (channel_bitch(chan))
	strcat(s, "bitch, ");
      if (s[0])
	s[strlen(s) - 2] = 0;
      if (!s[0])
	strcpy(s, _("lurking"));
      get_mode_protect(chan, s2);
      if (!channel_inactive(chan)) {
	if (channel_active(chan)) {
	  /* If it's a !chan, we want to display it's unique name too <cybah> */
	  if (chan->dname[0]=='!') {
	    dprintf(idx, "    %-10s: %2d member%s enforcing \"%s\" (%s), "
	            "unique name %s\n", chan->dname, chan->channel.members,
	            (chan->channel.members==1) ? "," : "s,", s2, s, chan->name);
	  } else {
	    dprintf(idx, "    %-10s: %2d member%s enforcing \"%s\" (%s)\n",
	            chan->dname, chan->channel.members,
	            chan->channel.members == 1 ? "," : "s,", s2, s);
	  }
	} else {
	  dprintf(idx, "    %-10s: (%s), enforcing \"%s\"  (%s)\n", chan->dname,
		  channel_pending(chan) ? "pending" : "not on channel", s2, s);
	}
      } else {
	dprintf(idx, "    %-10s: channel is set +inactive\n",
		chan->dname);
      }
      if (details) {
	s[0] = 0;
	i = 0;
	if (channel_enforcebans(chan))
	  i += my_strcpy(s + i, "enforce-bans ");
	if (channel_dynamicbans(chan))
	  i += my_strcpy(s + i, "dynamic-bans ");
	if (channel_nouserbans(chan))
	  i += my_strcpy(s + i, "forbid-user-bans ");
	if (channel_autoop(chan))
	  i += my_strcpy(s + i, "op-on-join ");
	if (channel_bitch(chan))
	  i += my_strcpy(s + i, "bitch ");
	if (channel_greet(chan))
	  i += my_strcpy(s + i, "greet ");
	if (channel_protectops(chan))
	  i += my_strcpy(s + i, "protect-ops ");
        if (channel_protectfriends(chan))
          i += my_strcpy(s + i, "protect-friends ");
	if (channel_dontkickops(chan))
	  i += my_strcpy(s + i, "dont-kick-ops ");
	if (channel_logstatus(chan))
	  i += my_strcpy(s + i, "log-status ");
	if (channel_revenge(chan))
	  i += my_strcpy(s + i, "revenge ");
	if (channel_secret(chan))
	  i += my_strcpy(s + i, "secret ");
	if (channel_shared(chan))
	  i += my_strcpy(s + i, "shared ");
	if (!channel_static(chan))
	  i += my_strcpy(s + i, "dynamic ");
	if (channel_autovoice(chan))
	  i += my_strcpy(s + i, "autovoice ");
	if (channel_cycle(chan))
	  i += my_strcpy(s + i, "cycle ");
	if (channel_dynamicexempts(chan))
	  i += my_strcpy(s + i, "dynamic-exempts ");
	if (channel_nouserexempts(chan))
	  i += my_strcpy(s + i, "forbid-user-exempts ");
	if (channel_dynamicinvites(chan))
	  i += my_strcpy(s + i, "dynamic-invites ");
	if (channel_nouserinvites(chan))
	  i += my_strcpy(s + i, "forbid-user-invites ");
	if (channel_inactive(chan))
	  i += my_strcpy(s + i, "inactive ");
	if (channel_nodesynch(chan))
	  i += my_strcpy(s + i, "nodesynch ");
	if (channel_honor_global_bans(chan))
	  i += my_strcpy(s + i, "honor-global-bans ");
	if (channel_honor_global_exempts(chan))
	  i += my_strcpy(s + i, "honor-global-exempts ");
	if (channel_honor_global_invites(chan))
	  i += my_strcpy(s + i, "honor-global-invites ");
	dprintf(idx, "      Options: %s\n", s);
	if (chan->idle_kick)
	  dprintf(idx, "      Kicking idle users after %d min\n",
		  chan->idle_kick);
	if (chan->stopnethack_mode)
	  dprintf(idx, "      stopnethack_mode %d\n",
		  chan->stopnethack_mode);
	if (chan->revenge_mode)
	  dprintf(idx, "      revenge_mode %d\n",
                  chan->revenge_mode);
	if (details) {
		dprintf(idx, "    Bans last %d mins.\n", chan->ban_time);
		dprintf(idx, "    Exemptions last %d mins.\n", chan->exempt_time);
		dprintf(idx, "    Invitations last %d mins.\n", chan->invite_time);
	}
      }
    }
  }
}

static char *traced_globchanset(ClientData cdata, Tcl_Interp * irp,
				char *name1, char *name2, int flags)
{
  char *s;
  char *t;
  int i;
  int items;
  char **item;

  if (flags & (TCL_TRACE_READS | TCL_TRACE_UNSETS)) {
    Tcl_SetVar2(interp, name1, name2, glob_chanset, TCL_GLOBAL_ONLY);
    if (flags & TCL_TRACE_UNSETS)
      Tcl_TraceVar(interp, "global-chanset",
	    TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
	    traced_globchanset, NULL);
  } else { /* Write */
    s = Tcl_GetVar2(interp, name1, name2, TCL_GLOBAL_ONLY);
    Tcl_SplitList(interp, s, &items, &item);
    for (i = 0; i<items; i++) {
      if (!(item[i]) || (strlen(item[i]) < 2)) continue;
      s = glob_chanset;
      while (s[0]) {
	t = strchr(s, ' '); /* Can't be NULL coz of the extra space */
	t[0] = 0;
	if (!strcmp(s + 1, item[i] + 1)) {
	  s[0] = item[i][0]; /* +- */
	  t[0] = ' ';
	  break;
	}
	t[0] = ' ';
	s = t + 1;
      }
    }
    if (item) /* hmm it cant be 0 */
      Tcl_Free((char *) item);
    Tcl_SetVar2(interp, name1, name2, glob_chanset, TCL_GLOBAL_ONLY);
  }
  return NULL;
}

static tcl_ints my_tcl_ints[] =
{
  {"share_greet",		NULL,				0},
  {"use_info",			&use_info,			0},
  {"quiet_save",		&quiet_save,			0},
  {"global_stopnethack_mode",	&global_stopnethack_mode,	0},
  {"global_revenge_mode",       &global_revenge_mode,           0},
  {"global_idle_kick",		&global_idle_kick,		0},
  {"global_ban_time",		&global_ban_time,		0},
  {"global_exempt_time",	&global_exempt_time,		0},
  {"global_invite_time",	&global_invite_time,		0},
  {NULL,			NULL,				0}
};

static tcl_coups mychan_tcl_coups[] =
{
  {"global_flood_chan",		&gfld_chan_thr,		&gfld_chan_time},
  {"global_flood_deop",		&gfld_deop_thr,		&gfld_deop_time},
  {"global_flood_kick",		&gfld_kick_thr,		&gfld_kick_time},
  {"global_flood_join",		&gfld_join_thr,		&gfld_join_time},
  {"global_flood_ctcp",		&gfld_ctcp_thr,		&gfld_ctcp_time},
  {"global_flood_nick",		&gfld_nick_thr, 	&gfld_nick_time},
  {"global_aop_delay",		&global_aop_min,	&global_aop_max},
  {NULL,			NULL,			NULL}
};

static tcl_strings my_tcl_strings[] =
{
  {"chanfile",		chanfile,	120,	STR_PROTECT},
  {"global_chanmode",	glob_chanmode,	64,	0},
  {NULL,		NULL,		0,	0}
};

static char *channels_close()
{
  write_channels();
  free_udef(udef);
  rem_builtins("chon", my_chon);
  rem_builtins("dcc", C_dcc_irc);
  script_delete_commands(channel_script_cmds);
  rem_tcl_commands(channels_cmds);
  rem_tcl_strings(my_tcl_strings);
  rem_tcl_ints(my_tcl_ints);
  rem_tcl_coups(mychan_tcl_coups);
  del_hook(HOOK_USERFILE, (Function) channels_writeuserfile);
  del_hook(HOOK_BACKUP, (Function) backup_chanfile);
  del_hook(HOOK_REHASH, (Function) channels_rehash);
  del_hook(HOOK_PRE_REHASH, (Function) channels_prerehash);
  del_hook(HOOK_MINUTELY, (Function) check_expired_bans);
  del_hook(HOOK_MINUTELY, (Function) check_expired_exempts);
  del_hook(HOOK_MINUTELY, (Function) check_expired_invites);
  Tcl_UntraceVar(interp, "global-chanset",
		 TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		 traced_globchanset, NULL);
  rem_help_reference("channels.help");
  rem_help_reference("chaninfo.help");
  module_undepend(MODULE_NAME);
  return NULL;
}

EXPORT_SCOPE char *start();

static Function channels_table[] =
{
  /* 0 - 3 */
  (Function) start,
  (Function) channels_close,
  (Function) 0,
  (Function) channels_report,
  /* 4 - 7 */
  (Function) u_setsticky_mask,
  (Function) u_delmask,
  (Function) u_addmask,
  (Function) write_bans,
  /* 8 - 11 */
  (Function) get_chanrec,
  (Function) add_chanrec,
  (Function) del_chanrec,
  (Function) set_handle_chaninfo,
  /* 12 - 15 */
  (Function) 0,
  (Function) u_match_mask,
  (Function) u_equals_mask,
  (Function) clear_channel,
  /* 16 - 19 */
  (Function) set_handle_laston,
  (Function) NULL,
  (Function) & use_info,
  (Function) get_handle_chaninfo,
  /* 20 - 23 */
  (Function) u_sticky_mask,
  (Function) ismasked,
  (Function) add_chanrec_by_handle,
  (Function) NULL, /* [23] used to be isexempted() <cybah> */
  /* 24 - 27 */
  (Function) NULL,
  (Function) NULL, /* [25] used to be isinvited() <cybah> */
  (Function) NULL,
  (Function) NULL,
  /* 28 - 31 */
  (Function) NULL, /* [28] used to be u_setsticky_exempt() <cybah> */
  (Function) NULL,
  (Function) NULL,
  (Function) NULL,
  /* 32 - 35 */
  (Function) NULL,/* [32] used to be u_sticky_exempt() <cybah> */
  (Function) NULL,
  (Function) NULL,	/* [34] used to be killchanset().	*/
  (Function) NULL,
  /* 36 - 39 */
  (Function) NULL,
  (Function) tcl_channel_add,
  (Function) tcl_channel_modify,
  (Function) write_exempts,
  /* 40 - 43 */
  (Function) write_invites,
  (Function) ismodeline,
  (Function) initudef,
  (Function) ngetudef,
  /* 44 - 47 */
  (Function) expired_mask,
  (Function) remove_channel,
  (Function) & global_ban_time,
  (Function) & global_exempt_time,
  /* 48 - 51 */
  (Function) & global_invite_time
};

char *start(eggdrop_t *eggdrop)
{
  egg = eggdrop;

  gfld_chan_thr = 10;
  gfld_chan_time = 60;
  gfld_deop_thr = 3;
  gfld_deop_time = 10;
  gfld_kick_thr = 3;
  gfld_kick_time = 10;
  gfld_join_thr = 5;
  gfld_join_time = 60;
  gfld_ctcp_thr = 5;
  gfld_ctcp_time = 60;
  global_idle_kick = 0;
  global_aop_min = 5;
  global_aop_max = 30;
  setstatic = 0;
  use_info = 1;
  strcpy(chanfile, "chanfile");
  chan_hack = 0;
  quiet_save = 0;
  strcpy(glob_chanmode, "nt");
  udef = NULL;
  global_stopnethack_mode = 0;
  global_revenge_mode = 0;
  global_ban_time = 120;
  global_exempt_time = 60;
  global_invite_time = 60;
  strcpy(glob_chanset,
         "-enforcebans "
	 "+dynamicbans "
	 "+userbans "
	 "-autoop "
	 "-bitch "
	 "+greet "
	 "+protectops "
	 "+statuslog "
	 "-revenge "
	 "-secret "
	 "-autovoice "
	 "+cycle "
	 "+dontkickops "
	 "-inactive "
	 "-protectfriends "
	 "+shared "
	 "+userexempts "
	 "+dynamicexempts "
	 "+userinvites "
	 "+dynamicinvites "
	 "-revengebot "
	 "+honor-global-bans "
         "+honor-global-exempts "
         "+honor-global-invites "
	 "-nodesynch ");
  module_register(MODULE_NAME, channels_table, 1, 0);
  if (!module_depend(MODULE_NAME, "eggdrop", 107, 0)) {
    module_undepend(MODULE_NAME);
    return "This module needs eggdrop1.7.0 or later";
  }
  add_hook(HOOK_MINUTELY, (Function) check_expired_bans);
  add_hook(HOOK_MINUTELY, (Function) check_expired_exempts);
  add_hook(HOOK_MINUTELY, (Function) check_expired_invites);
  add_hook(HOOK_USERFILE, (Function) channels_writeuserfile);
  add_hook(HOOK_BACKUP, (Function) backup_chanfile);
  add_hook(HOOK_REHASH, (Function) channels_rehash);
  add_hook(HOOK_PRE_REHASH, (Function) channels_prerehash);
  Tcl_TraceVar(interp, "global-chanset",
	       TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
	       traced_globchanset, NULL);
  add_builtins("chon", my_chon);
  add_builtins("dcc", C_dcc_irc);
  script_create_commands(channel_script_cmds);
  add_tcl_commands(channels_cmds);
  add_tcl_strings(my_tcl_strings);
  add_help_reference("channels.help");
  add_help_reference("chaninfo.help");
  my_tcl_ints[0].val = &share_greet;
  add_tcl_ints(my_tcl_ints);
  add_tcl_coups(mychan_tcl_coups);
  read_channels(0);
  setstatic = 1;
  return NULL;
}
