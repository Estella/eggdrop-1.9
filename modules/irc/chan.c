/*
 * chan.c -- part of irc.mod
 *   almost everything to do with channel manipulation
 *   telling channel status
 *   'who' response
 *   user kickban, kick, op, deop
 *   idle kicking
 *
 * $Id: chan.c,v 1.1 2001/10/27 16:34:50 ite Exp $
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

static time_t last_ctcp = (time_t) 0L;
static int    count_ctcp = 0;
static time_t last_invtime = (time_t) 0L;
static char   last_invchan[300] = "";

/* ID length for !channels.
 */
#define CHANNEL_ID_LEN 5


/* Returns a pointer to a new channel member structure.
 */
static memberlist *newmember(struct chanset_t *chan)
{
  memberlist *x;

  for (x = chan->channel.member; x && x->nick[0]; x = x->next); 
  x->next = calloc(1, sizeof(memberlist));
  x->next->next = NULL;
  x->next->nick[0] = 0;
  x->next->split = 0L;
  x->next->last = 0L;
  x->next->delay = 0L;
  chan->channel.members++;
  return x;
}

/* Always pass the channel dname (display name) to this function <cybah>
 */
static void update_idle(char *chname, char *nick)
{
  memberlist *m;
  struct chanset_t *chan;

  chan = findchan_by_dname(chname);
  if (chan) {
    m = ismember(chan, nick);
    if (m)
      m->last = now;
  }
}

/* Returns the current channel mode.
 */
static char *getchanmode(struct chanset_t *chan)
{
  static char s[121];
  int atr, i;

  s[0] = '+';
  i = 1;
  atr = chan->channel.mode;
  if (atr & CHANINV)
    s[i++] = 'i';
  if (atr & CHANPRIV)
    s[i++] = 'p';
  if (atr & CHANSEC)
    s[i++] = 's';
  if (atr & CHANMODER)
    s[i++] = 'm';
  if (atr & CHANNOCLR)
    s[i++] = 'c';
  if (atr & CHANREGON)
    s[i++] = 'R';
  if (atr & CHANTOPIC)
    s[i++] = 't';
  if (atr & CHANNOMSG)
    s[i++] = 'n';
  if (atr & CHANANON)
    s[i++] = 'a';
  if (atr & CHANKEY)
    s[i++] = 'k';
  if (chan->channel.maxmembers != 0)
    s[i++] = 'l';
  s[i] = 0;
  if (chan->channel.key[0])
    i += sprintf(s + i, " %s", chan->channel.key);
  if (chan->channel.maxmembers != 0)
    sprintf(s + i, " %d", chan->channel.maxmembers);
  return s;
}

static void check_exemptlist(struct chanset_t *chan, char *from)
{
  masklist *e;
  int ok = 0;

  if (!use_exempts)
    return;

  for (e = chan->channel.exempt; e->mask[0]; e = e->next)
    if (wild_match(e->mask, from)) {
      add_mode(chan, '-', 'e', e->mask);
      ok = 1;
    }
  if (prevent_mixing && ok)
    flush_mode(chan, QUICK);
}

/* Check a channel and clean-out any more-specific matching masks.
 *
 * Moved all do_ban(), do_exempt() and do_invite() into this single function
 * as the code bloat is starting to get rediculous <cybah>
 */
static void do_mask(struct chanset_t *chan, masklist *m, char *mask, char Mode)
{
  for (; m && m->mask[0]; m = m->next)
    if (wild_match(mask, m->mask) && irccmp(mask, m->mask))
      add_mode(chan, '-', Mode, m->mask);
  add_mode(chan, '+', Mode, mask);
  flush_mode(chan, QUICK);
}

/* This is a clone of detect_flood, but works for channel specificity now
 * and handles kick & deop as well.
 */
static int detect_chan_flood(char *floodnick, char *floodhost, char *from,
			     struct chanset_t *chan, int which, char *victim)
{
  char h[UHOSTLEN], ftype[12], *p;
  struct userrec *u;
  memberlist *m;
  int thr = 0, lapse = 0;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};

  if (!chan || (which < 0) || (which >= FLOOD_CHAN_MAX))
    return 0;
  m = ismember(chan, floodnick);
  /* Do not punish non-existant channel members and IRC services like
   * ChanServ
   */
  if (!m && (which != FLOOD_JOIN))
    return 0;

  get_user_flagrec(get_user_by_host(from), &fr, chan->dname);
  if (glob_bot(fr) ||
      ((which == FLOOD_DEOP) && (glob_master(fr) || chan_master(fr))) ||
      ((which != FLOOD_DEOP) && (glob_friend(fr) || chan_friend(fr))) ||
      (channel_dontkickops(chan) &&
       (chan_op(fr) || (glob_op(fr) && !chan_deop(fr)))))	/* arthur2 */
    return 0;

  /* Determine how many are necessary to make a flood. */
  switch (which) {
  case FLOOD_PRIVMSG:
  case FLOOD_NOTICE:
    thr = chan->flood_pub_thr;
    lapse = chan->flood_pub_time;
    strcpy(ftype, "pub");
    break;
  case FLOOD_CTCP:
    thr = chan->flood_ctcp_thr;
    lapse = chan->flood_ctcp_time;
    strcpy(ftype, "pub");
    break;
  case FLOOD_NICK:
    thr = chan->flood_nick_thr;
    lapse = chan->flood_nick_time;
    strcpy(ftype, "nick");
    break;
  case FLOOD_JOIN:
    thr = chan->flood_join_thr;
    lapse = chan->flood_join_time;
      strcpy(ftype, "join");
    break;
  case FLOOD_DEOP:
    thr = chan->flood_deop_thr;
    lapse = chan->flood_deop_time;
    strcpy(ftype, "deop");
    break;
  case FLOOD_KICK:
    thr = chan->flood_kick_thr;
    lapse = chan->flood_kick_time;
    strcpy(ftype, "kick");
    break;
  }
  if ((thr == 0) || (lapse == 0))
    return 0;			/* no flood protection */
  /* Okay, make sure i'm not flood-checking myself */
  if (match_my_nick(floodnick))
    return 0;
  if (!strcasecmp(floodhost, botuserhost))
    return 0;
  /* My user@host (?) */
  if ((which == FLOOD_KICK) || (which == FLOOD_DEOP))
    p = floodnick;
  else {
    p = strchr(floodhost, '@');
    if (p) {
      p++;
    }
    if (!p)
      return 0;
  }
  if (irccmp(chan->floodwho[which], p)) {	/* new */
    strncpy(chan->floodwho[which], p, 81);
    chan->floodwho[which][81] = 0;
    chan->floodtime[which] = now;
    chan->floodnum[which] = 1;
    return 0;
  }
  if (chan->floodtime[which] < now - lapse) {
    /* Flood timer expired, reset it */
    chan->floodtime[which] = now;
    chan->floodnum[which] = 1;
    return 0;
  }
  /* Deop'n the same person, sillyness ;) - so just ignore it */
  if (which == FLOOD_DEOP) {
    if (!irccmp(chan->deopd, victim))
      return 0;
    else
      strcpy(chan->deopd, victim);
  }
  chan->floodnum[which]++;
  if (chan->floodnum[which] >= thr) {	/* FLOOD */
    /* Reset counters */
    chan->floodnum[which] = 0;
    chan->floodtime[which] = 0;
    chan->floodwho[which][0] = 0;
    if (which == FLOOD_DEOP)
      chan->deopd[0] = 0;
    u = get_user_by_host(from);
    if (check_tcl_flud(floodnick, floodhost, u, ftype, chan->dname))
      return 0;
    switch (which) {
    case FLOOD_PRIVMSG:
    case FLOOD_NOTICE:
    case FLOOD_CTCP:
      /* Flooding chan! either by public or notice */
      if (me_op(chan) && !chan_sentkick(m)) {
	putlog(LOG_MODES, chan->dname, _("Channel flood from %s -- kicking"), floodnick);
	dprintf(DP_MODE, "KICK %s %s :%s\n", chan->name, floodnick,
		_("flood"));
	m->flags |= SENTKICK;
      }
      return 1;
    case FLOOD_JOIN:
    case FLOOD_NICK:
      if (use_exempts &&
	  (u_match_mask(global_exempts, from) ||
	   u_match_mask(chan->exempts, from)))
	return 1;
      simple_sprintf(h, "*!*@%s", p);
      if (!isbanned(chan, h) && me_op(chan)) {
	check_exemptlist(chan, from);
	do_mask(chan, chan->channel.ban, h, 'b');
      }
      if ((u_match_mask(global_bans, from))
	  || (u_match_mask(chan->bans, from)))
	return 1;		/* Already banned */
      if (which == FLOOD_JOIN)
	putlog(LOG_MISC | LOG_JOIN, chan->dname, _("JOIN flood from @%s!  Banning."), p);
      else
	putlog(LOG_MISC | LOG_JOIN, chan->dname, _("NICK flood from @%s!  Banning."), p);
      strcpy(ftype + 4, " flood");
      u_addban(chan, h, origbotname, ftype, now + (60 * ban_time), 0);
      if (!channel_enforcebans(chan) && me_op(chan)) {
	  char s[UHOSTLEN];
	  for (m = chan->channel.member; m && m->nick[0]; m = m->next) {	  
	    sprintf(s, "%s!%s", m->nick, m->userhost);
	    if (wild_match(h, s) &&
		(m->joined >= chan->floodtime[which]) &&
		   !chan_sentkick(m) && !match_my_nick(m->nick)) {
	      m->flags |= SENTKICK;
	      if (which == FLOOD_JOIN)
	      dprintf(DP_SERVER, "KICK %s %s :%s\n", chan->name, m->nick,
		      _("join flood"));
	      else
	        dprintf(DP_SERVER, "KICK %s %s :%s\n", chan->name, m->nick,
		      _("nick flood"));
	    }
	  }
	}
      return 1;
    case FLOOD_KICK:
      if (me_op(chan) && !chan_sentkick(m)) {
	putlog(LOG_MODES, chan->dname, "Kicking %s, for mass kick.", floodnick);
	dprintf(DP_MODE, "KICK %s %s :%s\n", chan->name, floodnick,
		_("mass kick, go sit in a corner"));
	m->flags |= SENTKICK;
      }
    return 1;
    case FLOOD_DEOP:
      if (me_op(chan) && !chan_sentkick(m)) {
	putlog(LOG_MODES, chan->dname,
	       _("Mass deop on %s by %s"), chan->dname, from);
	dprintf(DP_MODE, "KICK %s %s :%s\n",
		chan->name, floodnick, _("Mass deop.  Go sit in a corner."));
	m->flags |= SENTKICK;
      }
      return 1;
    }
  }
  return 0;
}

/* Given a [nick!]user@host, place a quick ban on them on a chan.
 */
static char *quickban(struct chanset_t *chan, char *uhost)
{
  static char s1[512];

  maskhost(uhost, s1);
  if ((strlen(s1) != 1) && (strict_host == 0))
    s1[2] = '*';		/* arthur2 */
  do_mask(chan, chan->channel.ban, s1, 'b');
  return s1;
}

/* Kick any user (except friends/masters) with certain mask from channel
 * with a specified comment.  Ernst 18/3/1998
 */
static void kick_all(struct chanset_t *chan, char *hostmask, char *comment, int bantype)
{
  memberlist *m;
  char kicknick[512], s[UHOSTLEN];
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  int k, l, flushed;

  if (!me_op(chan))
    return;
  k = 0;
  flushed = 0;
  kicknick[0] = 0;
  for (m = chan->channel.member; m && m->nick[0]; m = m->next) {
    sprintf(s, "%s!%s", m->nick, m->userhost);
    get_user_flagrec(m->user ? m->user : get_user_by_host(s), &fr, chan->dname);
    if (wild_match(hostmask, s) && !chan_sentkick(m) &&
	!match_my_nick(m->nick) && !chan_issplit(m) &&
	!glob_friend(fr) && !chan_friend(fr) &&
	!(use_exempts &&
	  ((bantype && isexempted(chan, s)) ||
	   (u_match_mask(global_exempts,s) ||
	    u_match_mask(chan->exempts, s)))) &&
	!(channel_dontkickops(chan) &&
	  (chan_op(fr) || (glob_op(fr) && !chan_deop(fr))))) {	/* arthur2 */
      if (!flushed) {
	/* We need to kick someone, flush eventual bans first */
	flush_mode(chan, QUICK);
	flushed += 1;
      }
      m->flags |= SENTKICK;	/* Mark as pending kick */
      if (kicknick[0])
	strcat(kicknick, ",");
      strcat(kicknick, m->nick);
      k += 1;
      l = strlen(chan->name) + strlen(kicknick) + strlen(comment) + 5;
      if ((kick_method != 0 && k == kick_method) || (l > 480)) {
	dprintf(DP_SERVER, "KICK %s %s :%s\n", chan->name, kicknick, comment);
	k = 0;
	kicknick[0] = 0;
      }
    }
  }
  if (k > 0)
    dprintf(DP_SERVER, "KICK %s %s :%s\n", chan->name, kicknick, comment);
}

/* If any bans match this wildcard expression, refresh them on the channel.
 */
static void refresh_ban_kick(struct chanset_t *chan, char *user, char *nick)
{
  register maskrec	*b;
  memberlist		*m;
  int			 cycle;

  m = ismember(chan, nick);
  if (!m)
    return;
  /* Check channel bans in first cycle and global bans
     in second cycle. */
  for (cycle = 0; cycle < 2; cycle++) {
    for (b = cycle ? chan->bans : global_bans; b; b = b->next) {
      if (wild_match(b->mask, user)) {
	struct flag_record	fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
	char			c[512];		/* The ban comment.	*/
	char			s[UHOSTLEN];

	sprintf(s, "%s!%s", m->nick, m->userhost);
	get_user_flagrec(m->user ? m->user : get_user_by_host(s), &fr,
			 chan->dname);
	if (!glob_friend(fr) && !chan_friend(fr))
	  add_mode(chan, '-', 'o', nick);	/* Guess it can't hurt.	*/
	check_exemptlist(chan, user);
	do_mask(chan, chan->channel.ban, b->mask, 'b');
	b->lastactive = now;
	if (b->desc && b->desc[0] != '@')
	  snprintf(c, sizeof c, "%s%s", _("banned: "), b->desc);
	else
	  c[0] = 0;
	kick_all(chan, b->mask, c[0] ? c : _("You are banned"), 0);
	return;					/* Drop out on 1st ban.	*/
      }
    }
  }
}

/* This is a bit cumbersome at the moment, but it works... Any improvements
 * then feel free to have a go.. Jason
 */
static void refresh_exempt(struct chanset_t *chan, char *user)
{
  maskrec	*e;
  masklist	*b;
  int		 cycle;

  /* Check channel exempts in first cycle and global exempts
     in second cycle. */
  for (cycle = 0; cycle < 2; cycle++) {
    for (e = cycle ? chan->exempts : global_exempts; e; e = e->next) {
      if (wild_match(user, e->mask) || wild_match(e->mask,user)) {
        for (b = chan->channel.ban; b && b->mask[0]; b = b->next) {
          if (wild_match(b->mask, user) || wild_match(user, b->mask)) {
            if (e->lastactive < now - 60 && !isexempted(chan, e->mask)) {
              do_mask(chan, chan->channel.exempt, e->mask, 'e');
              e->lastactive = now;
            }
          }
        }
      }
    }
  }
}

static void refresh_invite(struct chanset_t *chan, char *user)
{
  maskrec	*i;
  int		 cycle;

  /* Check channel invites in first cycle and global invites
     in second cycle. */
  for (cycle = 0; cycle < 2; cycle++) {
    for (i = cycle ? chan->invites : global_invites; i; i = i->next) {
      if (wild_match(i->mask, user) &&
	  ((i->flags & MASKREC_STICKY) || (chan->channel.mode & CHANINV))) {
        if (i->lastactive < now - 60 && !isinvited(chan, i->mask)) {
          do_mask(chan, chan->channel.invite, i->mask, 'I');
	  i->lastactive = now;
	  return;
	}
      }
    }
  }
}

/* Enforce all channel bans in a given channel.  Ernst 18/3/1998
 */
static void enforce_bans(struct chanset_t *chan)
{
  char		 me[UHOSTLEN];
  masklist	*b;

  if (!me_op(chan))
    return;			/* Can't do it :( */
  simple_sprintf(me, "%s!%s", botname, botuserhost);
  /* Go through all bans, kicking the users. */
  for (b = chan->channel.ban; b && b->mask[0]; b = b->next) {
    if (!wild_match(b->mask, me))
      if (!isexempted(chan, b->mask))
	kick_all(chan, b->mask, _("You are banned"), 1);
  }
}

/* Make sure that all who are 'banned' on the userlist are actually in fact
 * banned on the channel.
 *
 * Note: Since i was getting a ban list, i assume i'm chop.
 */
static void recheck_bans(struct chanset_t *chan)
{
  maskrec	*u;
  int		 cycle;

  /* Check channel bans in first cycle and global bans
     in second cycle. */
  for (cycle = 0; cycle < 2; cycle++) {
    for (u = cycle ? chan->bans : global_bans; u; u = u->next)
      if (!isbanned(chan, u->mask) && (!channel_dynamicbans(chan) ||
				       (u->flags & MASKREC_STICKY)))
	add_mode(chan, '+', 'b', u->mask);
  }
}

/* Make sure that all who are exempted on the userlist are actually in fact
 * exempted on the channel.
 *
 * Note: Since i was getting an excempt list, i assume i'm chop.
 */
static void recheck_exempts(struct chanset_t *chan)
{
  maskrec	*e;
  masklist	*b;
  int		 cycle;

  /* Check channel exempts in first cycle and global exempts
     in second cycle. */
  for (cycle = 0; cycle < 2; cycle++) {
    for (e = cycle ? chan->exempts : global_exempts; e; e = e->next) {
      if (!isexempted(chan, e->mask) &&
          (!channel_dynamicexempts(chan) || (e->flags & MASKREC_STICKY)))
        add_mode(chan, '+', 'e', e->mask);
      for (b = chan->channel.ban; b && b->mask[0]; b = b->next) {
        if ((wild_match(b->mask, e->mask) || wild_match(e->mask, b->mask)) &&
            !isexempted(chan, e->mask))
	  add_mode(chan,'+','e',e->mask);
	/* do_mask(chan, chan->channel.exempt, e->mask, 'e');*/
      }
    }
  }
}

/* Make sure that all who are invited on the userlist are actually in fact
 * invited on the channel.
 *
 * Note: Since i was getting an invite list, i assume i'm chop.
 */
static void recheck_invites(struct chanset_t *chan)
{
  maskrec	*ir;
  int		 cycle;

  /* Check channel invites in first cycle and global invites
     in second cycle. */
  for (cycle = 0; cycle < 2; cycle++)  {
    for (ir = cycle ? chan->invites : global_invites; ir; ir = ir->next) {
      /* If invite isn't set and (channel is not dynamic invites and not invite
       * only) or invite is sticky.
       */
      if (!isinvited(chan, ir->mask) && ((!channel_dynamicinvites(chan) &&
          !(chan->channel.mode & CHANINV)) || ir->flags & MASKREC_STICKY))
	add_mode(chan, '+', 'I', ir->mask);
	/* do_mask(chan, chan->channel.invite, ir->mask, 'I');*/
    }
  }
}

/* Resets the masks on the channel.
 */
static void resetmasks(struct chanset_t *chan, masklist *m, maskrec *mrec,
		       maskrec *global_masks, char mode)
{
  if (!me_op(chan))
    return;                     /* Can't do it */

  /* Remove masks we didn't put there */
  for (; m && m->mask[0]; m = m->next) {
    if (!u_equals_mask(global_masks, m->mask) && !u_equals_mask(mrec, m->mask))
      add_mode(chan, '-', mode, m->mask);
  }

  /* Make sure the intended masks are still there */
  switch (mode) {
    case 'b':
      recheck_bans(chan);
      break;
    case 'e':
      recheck_exempts(chan);
      break;
    case 'I':
      recheck_invites(chan);
      break;
    default:
      putlog(LOG_MISC, "*", "(!) Invalid mode '%c' in resetmasks()", mode);
      break;
  }
}
static void check_this_ban(struct chanset_t *chan, char *banmask, int sticky)
{
  memberlist *m;
  char user[UHOSTLEN];

  if (!me_op(chan))
    return;
  for (m = chan->channel.member; m && m->nick[0]; m = m->next) {
    sprintf(user, "%s!%s", m->nick, m->userhost);
    if (wild_match(banmask, user) &&
        !(use_exempts &&
          (u_match_mask(global_exempts, user) ||
           u_match_mask(chan->exempts, user))))
      refresh_ban_kick(chan, user, m->nick);
  }
  if (!isbanned(chan, banmask) &&
      (!channel_dynamicbans(chan) || sticky))
    add_mode(chan, '+', 'b', banmask);
}

static void recheck_channel_modes(struct chanset_t *chan)
{
  int cur = chan->channel.mode,
      mns = chan->mode_mns_prot,
      pls = chan->mode_pls_prot;

  if (!(chan->status & CHAN_ASKEDMODES)) {
    if (pls & CHANINV && !(cur & CHANINV))
      add_mode(chan, '+', 'i', "");
    else if (mns & CHANINV && cur & CHANINV)
      add_mode(chan, '-', 'i', "");
    if (pls & CHANPRIV && !(cur & CHANPRIV))
      add_mode(chan, '+', 'p', "");
    else if (mns & CHANPRIV && cur & CHANPRIV)
      add_mode(chan, '-', 'p', "");
    if (pls & CHANSEC && !(cur & CHANSEC))
      add_mode(chan, '+', 's', "");
    else if (mns & CHANSEC && cur & CHANSEC)
      add_mode(chan, '-', 's', "");
    if (pls & CHANMODER && !(cur & CHANMODER))
      add_mode(chan, '+', 'm', "");
    else if (mns & CHANMODER && cur & CHANMODER)
      add_mode(chan, '-', 'm', "");
    if (pls & CHANNOCLR && !(cur & CHANNOCLR))
      add_mode(chan, '+', 'c', "");
    else if (mns & CHANNOCLR && cur & CHANNOCLR)
      add_mode(chan, '-', 'c', "");
    if (pls & CHANREGON && !(cur & CHANREGON))
      add_mode(chan, '+', 'R', "");
    else if (mns & CHANREGON && cur & CHANREGON)
      add_mode(chan, '-', 'R', "");
    if (pls & CHANTOPIC && !(cur & CHANTOPIC))
      add_mode(chan, '+', 't', "");
    else if (mns & CHANTOPIC && cur & CHANTOPIC)
      add_mode(chan, '-', 't', "");
    if (pls & CHANNOMSG && !(cur & CHANNOMSG))
      add_mode(chan, '+', 'n', "");
    else if ((mns & CHANNOMSG) && (cur & CHANNOMSG))
      add_mode(chan, '-', 'n', "");
    if ((pls & CHANANON) && !(cur & CHANANON))
      add_mode(chan, '+', 'a', "");
    else if ((mns & CHANANON) && (cur & CHANANON))
      add_mode(chan, '-', 'a', "");
    if ((pls & CHANQUIET) && !(cur & CHANQUIET))
      add_mode(chan, '+', 'q', "");
    else if ((mns & CHANQUIET) && (cur & CHANQUIET))
      add_mode(chan, '-', 'q', "");
    if ((chan->limit_prot != 0) && (chan->channel.maxmembers == 0)) {
      char s[50];

      sprintf(s, "%d", chan->limit_prot);
      add_mode(chan, '+', 'l', s);
    } else if ((mns & CHANLIMIT) && (chan->channel.maxmembers != 0))
      add_mode(chan, '-', 'l', "");
    if (chan->key_prot[0]) {
      if (irccmp(chan->channel.key, chan->key_prot) != 0) {
        if (chan->channel.key[0])
	  add_mode(chan, '-', 'k', chan->channel.key);
        add_mode(chan, '+', 'k', chan->key_prot);
      }
    } else if ((mns & CHANKEY) && (chan->channel.key[0]))
      add_mode(chan, '-', 'k', chan->channel.key);
  }
}

/* Things to do when i just became a chanop:
 */
static void recheck_channel(struct chanset_t *chan, int dobans)
{
  memberlist *m;
  char s[UHOSTLEN], *p;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  static int stacking = 0;
  int stop_reset = 0;

  if (stacking)
    return;			/* wewps */
  if (!userlist)                /* Bot doesnt know anybody */
    return;                     /* ... it's better not to deop everybody */
  stacking++;
  /* Okay, sort through who needs to be deopped. */
  for (m = chan->channel.member; m && m->nick[0]; m = m->next) { 
    sprintf(s, "%s!%s", m->nick, m->userhost);
    if (!m->user)
      m->user = get_user_by_host(s);
    get_user_flagrec(m->user, &fr, chan->dname);
    /* ignore myself */
    if (!match_my_nick(m->nick)) {
      /* if channel user is current a chanop */
      if (chan_hasop(m)) {
	if (glob_bot(fr))
	  stop_reset = 1;
	/* if user is channel deop */
	if (chan_deop(fr) ||
	/* OR global deop and NOT channel op */
	    (glob_deop(fr) && !chan_op(fr))) {
	  /* de-op! */
	  add_mode(chan, '-', 'o', m->nick);
	/* if channel mode is bitch */
	} else if (channel_bitch(chan) &&
	  /* AND the user isnt a channel op */
		   (!chan_op(fr) &&
	  /* AND the user isnt a global op, (or IS a chan deop) */
		   !(glob_op(fr) && !chan_deop(fr)))) {
	  /* de-op! mmmbop! */
	  add_mode(chan, '-', 'o', m->nick);
	}
      }
      /* check vs invites */
      if (use_invites &&
	  (u_match_mask(global_invites,s) ||
	   u_match_mask(chan->invites, s)))
	refresh_invite(chan, s);
      /* don't kickban if permanent exempted */
      if (!(use_exempts &&
	    (u_match_mask(global_exempts,s) ||
	     u_match_mask(chan->exempts, s)))) {
        /* if match a ban */
        if (u_match_mask(global_bans, s) ||
            u_match_mask(chan->bans, s)) {
	  /* bewm */
	  refresh_ban_kick(chan, s, m->nick);
	/* ^ will use the ban comment */
	}
	/* are they +k ? */
	if (chan_kick(fr) || glob_kick(fr)) {
	  check_exemptlist(chan, s);
	  quickban(chan, m->userhost);
	  p = get_user(&USERENTRY_COMMENT, m->user);
	  dprintf(DP_SERVER, "KICK %s %s :%s\n", chan->name, m->nick,
		  p ? p : _("...and thank you for playing."));
	  m->flags |= SENTKICK;
	}
      }
      /* now lets look at de-op'd ppl */
      if (!chan_hasop(m) &&
	  /* if they're an op, channel or global (without channel +d) */
	  (chan_op(fr) || (glob_op(fr) && !chan_deop(fr))) &&
	  /* and the channel is op on join, or they are auto-opped */
	  (channel_autoop(chan) || (glob_autoop(fr) || chan_autoop(fr)))) {
	/* op them! */
	add_mode(chan, '+', 'o', m->nick);
	  /* otherwise, lets check +v stuff if the llamas want it */
      } else if (!chan_hasvoice(m) && !chan_hasop(m)) {
	if ((channel_autovoice(chan) && !chan_quiet(fr) &&
	     (chan_voice(fr) || glob_voice(fr))) ||
	    (!chan_quiet(fr) && (glob_gvoice(fr) || chan_gvoice(fr)))) {
	  add_mode(chan, '+', 'v', m->nick);
	}
	/* do they have a voice on the channel */
	if (chan_hasvoice(m) &&
	    /* do they have the +q & no +v */
	    (chan_quiet(fr) || (glob_quiet(fr) && !chan_voice(fr)))) {
	  add_mode(chan, '-', 'v', m->nick);
	}
      }
    }
  }
  if (dobans) {
    if (channel_nouserbans(chan) && !stop_reset)
      resetbans(chan);
    else
      recheck_bans(chan);
    if (use_invites) {
      if (channel_nouserinvites(chan) && !stop_reset)
	resetinvites(chan);
      else
	recheck_invites(chan);
    }
    if (use_exempts) {
      if (channel_nouserexempts(chan) && !stop_reset)
	resetexempts(chan);
      else
	recheck_exempts(chan);
    }
    if (channel_enforcebans(chan))
      enforce_bans(chan);
    if ((chan->status & CHAN_ASKEDMODES) &&
	!channel_inactive(chan)) /* Spot on guppy, this just keeps the
	                          * checking sane */
      dprintf(DP_SERVER, "MODE %s\n", chan->name);
    recheck_channel_modes(chan);
  }
  stacking--;
}

/* got 324: mode status
 * <server> 324 <to> <channel> <mode>
 */
static int got324(char *from, char *ignore, char *msg)
{
  int i = 1, ok =0;
  char *p, *q, *chname;
  struct chanset_t *chan;

  newsplit(&msg);
  chname = newsplit(&msg);
  chan = findchan(chname);
  if (!chan) {
    putlog(LOG_MISC, "*", "%s: %s", _("Hmm, mode info from a channel Im not on"), chname);
    dprintf(DP_SERVER, "PART %s\n", chname);
    return 0;
  }
  if (chan->status & CHAN_ASKEDMODES)
    ok = 1;
  chan->status &= ~CHAN_ASKEDMODES;
  chan->channel.mode = 0;
  while (msg[i] != 0) {
    if (msg[i] == 'i')
      chan->channel.mode |= CHANINV;
    if (msg[i] == 'p')
      chan->channel.mode |= CHANPRIV;
    if (msg[i] == 's')
      chan->channel.mode |= CHANSEC;
    if (msg[i] == 'm')
      chan->channel.mode |= CHANMODER;
    if (msg[i] == 'c')
      chan->channel.mode |= CHANNOCLR;
    if (msg[i] == 'R')
      chan->channel.mode |= CHANREGON;
    if (msg[i] == 't')
      chan->channel.mode |= CHANTOPIC;
    if (msg[i] == 'n')
      chan->channel.mode |= CHANNOMSG;
    if (msg[i] == 'a')
      chan->channel.mode |= CHANANON;
    if (msg[i] == 'q')
      chan->channel.mode |= CHANQUIET;
    if (msg[i] == 'k') {
      chan->channel.mode |= CHANKEY;
      p = strchr(msg, ' ');
      if (p != NULL) {		/* Test for null key assignment */
	p++;
	q = strchr(p, ' ');
	if (q != NULL) {
	  *q = 0;
	  set_key(chan, p);
	  strcpy(p, q + 1);
	} else {
	  set_key(chan, p);
	  *p = 0;
	}
      }
      if ((chan->channel.mode & CHANKEY) && !(chan->channel.key[0]))
        chan->status |= CHAN_ASKEDMODES;
    }
    if (msg[i] == 'l') {
      p = strchr(msg, ' ');
      if (p != NULL) {		/* test for null limit assignment */
	p++;
	q = strchr(p, ' ');
	if (q != NULL) {
	  *q = 0;
	  chan->channel.maxmembers = atoi(p);
	  strcpy(p, q + 1);
	} else {
	  chan->channel.maxmembers = atoi(p);
	  *p = 0;
	}
      }
    }
    i++;
  }
  if (ok)
    recheck_channel_modes(chan);
  return 0;
}

static int got352or4(struct chanset_t *chan, char *user, char *host,
		     char *nick, char *flags)
{
  char userhost[UHOSTLEN];
  memberlist *m;

  m = ismember(chan, nick);	/* In my channel list copy? */
  if (!m) {			/* Nope, so update */
    m = newmember(chan);	/* Get a new channel entry */
    m->joined = m->split = m->delay = 0L;	/* Don't know when he joined */
    m->flags = 0;		/* No flags for now */
    m->last = now;		/* Last time I saw him */
  }
  strcpy(m->nick, nick);	/* Store the nick in list */
  /* Store the userhost */
  simple_sprintf(m->userhost, "%s@%s", user, host);
  simple_sprintf(userhost, "%s!%s", nick, m->userhost);
  /* Combine n!u@h */
  m->user = NULL;		/* No handle match (yet) */
  if (match_my_nick(nick)) {	/* Is it me? */
    strcpy(botuserhost, m->userhost);	/* Yes, save my own userhost */
    m->joined = now;		/* set this to keep the whining masses happy */
  }
  if (strchr(flags, '@') != NULL)	/* Flags say he's opped? */
    m->flags |= (CHANOP | WASOP);	/* Yes, so flag in my table */
  else
    m->flags &= ~(CHANOP | WASOP);
  if (strchr(flags, '+') != NULL)	/* Flags say he's voiced? */
    m->flags |= CHANVOICE;	/* Yes */
  else
    m->flags &= ~CHANVOICE;
  if (!(m->flags & (CHANVOICE | CHANOP)))
    m->flags |= STOPWHO;
  if (match_my_nick(nick) && any_ops(chan) && !me_op(chan))
    check_tcl_need(chan->dname, "op");
  m->user = get_user_by_host(userhost);
  return 0;
}

/* got a 352: who info!
 */
static int got352(char *from, char *ignore, char *msg)
{
  char *nick, *user, *host, *chname, *flags;
  struct chanset_t *chan;

  newsplit(&msg);		/* Skip my nick - effeciently */
  chname = newsplit(&msg);	/* Grab the channel */
  chan = findchan(chname);	/* See if I'm on channel */
  if (chan) {			/* Am I? */
    user = newsplit(&msg);	/* Grab the user */
    host = newsplit(&msg);	/* Grab the host */
    newsplit(&msg);		/* Skip the server */
    nick = newsplit(&msg);	/* Grab the nick */
    flags = newsplit(&msg);	/* Grab the flags */
    got352or4(chan, user, host, nick, flags);
  }
  return 0;
}

/* got a 354: who info! - iru style
 */
static int got354(char *from, char *ignore, char *msg)
{
  char *nick, *user, *host, *chname, *flags;
  struct chanset_t *chan;

  if (use_354) {
    newsplit(&msg);		/* Skip my nick - effeciently */
    if (msg[0] && (strchr(CHANMETA, msg[0]) != NULL)) {
      chname = newsplit(&msg);	/* Grab the channel */
      chan = findchan(chname);	/* See if I'm on channel */
      if (chan) {		/* Am I? */
	user = newsplit(&msg);	/* Grab the user */
	host = newsplit(&msg);	/* Grab the host */
	nick = newsplit(&msg);	/* Grab the nick */
	flags = newsplit(&msg);	/* Grab the flags */
	got352or4(chan, user, host, nick, flags);
      }
    }
  }
  return 0;
}

/* got 315: end of who
 * <server> 315 <to> <chan> :End of /who
 */
static int got315(char *from, char *ignore, char *msg)
{
  char *chname;
  struct chanset_t *chan;

  newsplit(&msg);
  chname = newsplit(&msg);
  chan = findchan(chname);
  /* May have left the channel before the who info came in */
  if (!chan || !channel_pending(chan))
    return 0;
  /* Finished getting who list, can now be considered officially ON CHANNEL */
  chan->status |= CHAN_ACTIVE;
  chan->status &= ~CHAN_PEND;
  /* Am *I* on the channel now? if not, well d0h. */
  if (!ismember(chan, botname)) {
    putlog(LOG_MISC | LOG_JOIN, chan->dname, "Oops, I'm not really on %s",
	   chan->dname);
    clear_channel(chan, 1);
    chan->status &= ~CHAN_ACTIVE;
    dprintf(DP_MODE, "JOIN %s %s\n",
	    (chan->name[0]) ? chan->name : chan->dname,
	    chan->channel.key[0] ? chan->channel.key : chan->key_prot);
  }
  else if (me_op(chan))
    recheck_channel(chan, 1);
  else if (chan->channel.members == 1)
    chan->status |= CHAN_STOP_CYCLE;
  /* do not check for i-lines here. */
  return 0;
}

/* got 367: ban info
 * <server> 367 <to> <chan> <ban> [placed-by] [timestamp]
 */
static int got367(char *from, char *ignore, char *origmsg)
{
  char s[UHOSTLEN], *ban, *who, *chname, buf[511], *msg;
  struct chanset_t *chan;
  struct userrec *u;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};

  strncpy(buf, origmsg, 510);
  buf[510] = 0;
  msg = buf;
  newsplit(&msg);
  chname = newsplit(&msg);
  chan = findchan(chname);
  if (!chan || !(channel_pending(chan) || channel_active(chan)))
    return 0;
  ban = newsplit(&msg);
  who = newsplit(&msg);
  /* Extended timestamp format? */
  if (who[0])
    newban(chan, ban, who);
  else
    newban(chan, ban, "existent");
  simple_sprintf(s, "%s!%s", botname, botuserhost);
  if (wild_match(ban, s))
    add_mode(chan, '-', 'b', ban);
  u = get_user_by_host(ban);
  if (u) {		/* Why bother check no-user :) - of if Im not an op */
    get_user_flagrec(u, &fr, chan->dname);
    if (chan_op(fr) || (glob_op(fr) && !chan_deop(fr)))
      add_mode(chan, '-', 'b', ban);
    /* These will be flushed by 368: end of ban info */
  }
  return 0;
}

/* got 368: end of ban list
 * <server> 368 <to> <chan> :etc
 */
static int got368(char *from, char *ignore, char *msg)
{
  struct chanset_t *chan;
  char *chname;

  /* Okay, now add bans that i want, which aren't set yet */
  newsplit(&msg);
  chname = newsplit(&msg);
  chan = findchan(chname);
  if (chan)
    chan->status &= ~CHAN_ASKEDBANS;
  /* If i sent a mode -b on myself (deban) in got367, either
   * resetbans() or recheck_bans() will flush that.
   */
  return 0;
}

/* got 348: ban exemption info
 * <server> 348 <to> <chan> <exemption>
 */
static int got348(char *from, char *ignore, char *origmsg)
{
  char *exempt, *who, *chname, buf[511], *msg;
  struct chanset_t *chan;

  if (use_exempts == 0)
    return 0;

  strncpy(buf, origmsg, 510);
  buf[510] = 0;
  msg = buf;
  newsplit(&msg);
  chname = newsplit(&msg);
  chan = findchan(chname);
  if (!chan || !(channel_pending(chan) || channel_active(chan)))
    return 0;
  exempt = newsplit(&msg);
  who = newsplit(&msg);
  /* Extended timestamp format? */
  if (who[0])
    newexempt(chan, exempt, who);
  else
    newexempt(chan, exempt, "existent");
  return 0;
}

/* got 349: end of ban exemption list
 * <server> 349 <to> <chan> :etc
 */
static int got349(char *from, char *ignore, char *msg)
{
  struct chanset_t *chan;
  char *chname;

  if (use_exempts == 1) {
    newsplit(&msg);
    chname = newsplit(&msg);
    chan = findchan(chname);
    if (chan)
      chan->ircnet_status &= ~CHAN_ASKED_EXEMPTS;
  }
  return 0;
}

/* got 346: invite exemption info
 * <server> 346 <to> <chan> <exemption>
 */
static int got346(char *from, char *ignore, char *origmsg)
{
  char *invite, *who, *chname, buf[511], *msg;
  struct chanset_t *chan;

  strncpy(buf, origmsg, 510);
  buf[510] = 0;
  msg = buf;
  if (use_invites == 0)
    return 0;
  newsplit(&msg);
  chname = newsplit(&msg);
  chan = findchan(chname);
  if (!chan || !(channel_pending(chan) || channel_active(chan)))
    return 0;
  invite = newsplit(&msg);
  who = newsplit(&msg);
  /* Extended timestamp format? */
  if (who[0])
    newinvite(chan, invite, who);
  else
    newinvite(chan, invite, "existent");
  return 0;
}

/* got 347: end of invite exemption list
 * <server> 347 <to> <chan> :etc
 */
static int got347(char *from, char *ignore, char *msg)
{
  struct chanset_t *chan;
  char *chname;

  if (use_invites == 1) {
    newsplit(&msg);
    chname = newsplit(&msg);
    chan = findchan(chname);
    if (chan)
      chan->ircnet_status &= ~CHAN_ASKED_INVITED;
  }
  return 0;
}

/* Too many channels.
 */
static int got405(char *from, char *ignore, char *msg)
{
  char *chname;

  newsplit(&msg);
  chname = newsplit(&msg);
  putlog(LOG_MISC, "*", _("Im on too many channels--cant join: %s"), chname);
  return 0;
}

/* This is only of use to us with !channels. We get this message when
 * attempting to join a non-existant !channel... The channel must be
 * created by sending 'JOIN !!<channel>'. <cybah>
 *
 * 403 - ERR_NOSUCHCHANNEL
 */
static int got403(char *from, char *ignore, char *msg)
{
  char *chname;
  struct chanset_t *chan;

  newsplit(&msg);
  chname = newsplit(&msg);
  if (chname && chname[0]=='!') {
    chan = findchan_by_dname(chname);
    if (!chan) {
      chan = findchan(chname);
      if (!chan)
        return 0;       /* Ignore it */
      /* We have the channel unique name, so we have attempted to join
       * a specific !channel that doesnt exist. Now attempt to join the
       * channel using it's short name.
       */
      putlog(LOG_MISC, "*",
             "Unique channel %s does not exist... Attempting to join with "
             "short name.", chname);
      dprintf(DP_SERVER, "JOIN %s\n", chan->dname);
    } else {
      /* We have found the channel, so the server has given us the short
       * name. Prefix another '!' to it, and attempt the join again...
       */
      putlog(LOG_MISC, "*",
             "Channel %s does not exist... Attempting to create it.", chname);
      dprintf(DP_SERVER, "JOIN !%s\n", chan->dname);
    }
  }
  return 0;
}

/* got 471: can't join channel, full
 */
static int got471(char *from, char *ignore, char *msg)
{
  char *chname;
  struct chanset_t *chan;

  newsplit(&msg);
  chname = newsplit(&msg);
  /* !channel short names (also referred to as 'description names'
   * can be received by skipping over the unique ID.
   */
  if ((chname[0] == '!') && (strlen(chname) > CHANNEL_ID_LEN)) {
    chname += CHANNEL_ID_LEN;
    chname[0] = '!';
  }
  /* We use dname because name is first set on JOIN and we might not
   * have joined the channel yet.
   */
  chan = findchan_by_dname(chname);
  if (chan) {
    putlog(LOG_JOIN, chan->dname, _("Channel full--cant join: %s"), chan->dname);
    check_tcl_need(chan->dname, "limit");
  } else
    putlog(LOG_JOIN, chname, _("Channel full--cant join: %s"), chname);
  return 0;
}

/* got 473: can't join channel, invite only
 */
static int got473(char *from, char *ignore, char *msg)
{
  char *chname;
  struct chanset_t *chan;

  newsplit(&msg);
  chname = newsplit(&msg);
  /* !channel short names (also referred to as 'description names'
   * can be received by skipping over the unique ID.
   */
  if ((chname[0] == '!') && (strlen(chname) > CHANNEL_ID_LEN)) {
    chname += CHANNEL_ID_LEN;
    chname[0] = '!';
  }
  /* We use dname because name is first set on JOIN and we might not
   * have joined the channel yet.
   */
  chan = findchan_by_dname(chname);
  if (chan) {
    putlog(LOG_JOIN, chan->dname, _("Channel invite only--cant join: %s"), chan->dname);
    check_tcl_need(chan->dname, "invite");
  } else
    putlog(LOG_JOIN, chname, _("Channel invite only--cant join: %s"), chname);
  return 0;
}

/* got 474: can't join channel, banned
 */
static int got474(char *from, char *ignore, char *msg)
{
  char *chname;
  struct chanset_t *chan;

  newsplit(&msg);
  chname = newsplit(&msg);
  /* !channel short names (also referred to as 'description names'
   * can be received by skipping over the unique ID.
   */
  if ((chname[0] == '!') && (strlen(chname) > CHANNEL_ID_LEN)) {
    chname += CHANNEL_ID_LEN;
    chname[0] = '!';
  }
  /* We use dname because name is first set on JOIN and we might not
   * have joined the channel yet.
   */
  chan = findchan_by_dname(chname);
  if (chan) {
    putlog(LOG_JOIN, chan->dname, _("Banned from channel--can't join: %s"), chan->dname);
    check_tcl_need(chan->dname, "unban");
  } else
    putlog(LOG_JOIN, chname, _("Banned from channel--can't join: %s"), chname);
  return 0;
}

/* got 475: can't goin channel, bad key
 */
static int got475(char *from, char *ignore, char *msg)
{
  char *chname;
  struct chanset_t *chan;

  newsplit(&msg);
  chname = newsplit(&msg);
  /* !channel short names (also referred to as 'description names'
   * can be received by skipping over the unique ID.
   */
  if ((chname[0] == '!') && (strlen(chname) > CHANNEL_ID_LEN)) {
    chname += CHANNEL_ID_LEN;
    chname[0] = '!';
  }
  /* We use dname because name is first set on JOIN and we might not
   * have joined the channel yet.
   */
  chan = findchan_by_dname(chname);
  if (chan) {
    putlog(LOG_JOIN, chan->dname, _("Bad key--cant join: %s"), chan->dname);
    if (chan->channel.key[0]) {
      free(chan->channel.key);
      chan->channel.key = calloc(1, 1);
      dprintf(DP_MODE, "JOIN %s %s\n", chan->dname, chan->key_prot);
    } else
      check_tcl_need(chan->dname, "key");
  } else
    putlog(LOG_JOIN, chname, _("Bad key--cant join: %s"), chname);
  return 0;
}

/* got invitation
 */
static int gotinvite(char *from, char *ignore, char *msg)
{
  char buf[UHOSTLEN], *nick, *uhost;
  struct chanset_t *chan;

  newsplit(&msg);
  fixcolon(msg);
  strncpyz(buf, from, sizeof buf);
  nick = strtok(buf, "!");
  uhost = strtok(NULL, "!");
  if (!irccmp(last_invchan, msg))
    if (now - last_invtime < 30)
      return 0;		/* Two invites to the same channel in 30 seconds? */
  putlog(LOG_MISC, "*", "%s!%s invited me to %s", nick, uhost, msg);
  strncpy(last_invchan, msg, 299);
  last_invchan[299] = 0;
  last_invtime = now;
  chan = findchan(msg);
  if (!chan)
    /* Might be a short-name */
    chan = findchan_by_dname(msg);
  if (chan && (channel_pending(chan) || channel_active(chan)))
    dprintf(DP_HELP, "NOTICE %s :I'm already here.\n", nick);
  else if (chan && !channel_inactive(chan))
    dprintf(DP_MODE, "JOIN %s %s\n", (chan->name[0]) ? chan->name : chan->dname,
            chan->channel.key[0] ? chan->channel.key : chan->key_prot);
  return 0;
}

/* Set the topic.
 */
static void set_topic(struct chanset_t *chan, char *k)
{
  if (chan->channel.topic)
    free(chan->channel.topic);
  if (k && k[0]) {
    chan->channel.topic = calloc(1, strlen(k) + 1);
    strcpy(chan->channel.topic, k);
  } else
    chan->channel.topic = NULL;
}

/* Topic change.
 */
static int gottopic(char *from, char *ignore, char *msg)
{
  char buf[UHOSTLEN], *nick, *uhost, *chname;
  memberlist *m;
  struct chanset_t *chan;
  struct userrec *u;

  chname = newsplit(&msg);
  fixcolon(msg);
  u = get_user_by_host(from);
  strncpyz(buf, from, sizeof buf);
  nick = strtok(buf, "!");
  uhost = strtok(NULL, "!");
  chan = findchan(chname);
  if (chan) {
    putlog(LOG_JOIN, chan->dname, "Topic changed on %s by %s!%s: %s",
	   chan->dname, nick, uhost, msg);
    m = ismember(chan, nick);
    if (m != NULL)
      m->last = now;
    set_topic(chan, msg);
    check_tcl_topc(nick, uhost, u, chan->dname, msg);
  }
  return 0;
}

/* 331: no current topic for this channel
 * <server> 331 <to> <chname> :etc
 */
static int got331(char *from, char *ignore, char *msg)
{
  char *chname;
  struct chanset_t *chan;

  newsplit(&msg);
  chname = newsplit(&msg);
  chan = findchan(chname);
  if (chan) {
    set_topic(chan, NULL);
    check_tcl_topc("*", "*", NULL, chan->dname, "");
  }
  return 0;
}

/* 332: topic on a channel i've just joined
 * <server> 332 <to> <chname> :topic goes here
 */
static int got332(char *from, char *ignore, char *msg)
{
  struct chanset_t *chan;
  char *chname;

  newsplit(&msg);
  chname = newsplit(&msg);
  chan = findchan(chname);
  if (chan) {
    fixcolon(msg);
    set_topic(chan, msg);
    check_tcl_topc("*", "*", NULL, chan->dname, msg);
  }
  return 0;
}

static void set_delay(struct chanset_t *chan, char *nick)
{
  time_t a_delay;
  int aop_min = chan->aop_min, aop_max = chan->aop_max, count = 0;
  memberlist *m, *m2;

  m = ismember(chan, nick);
  if (!m)
    return;
  if (aop_min >= aop_max)
    a_delay = now + aop_min;
  else
    a_delay = now + (random() % (aop_max - aop_min)) + aop_min + 1;
  for (m2 = chan->channel.member; m2 && m2->nick[0]; m2 = m2->next)
    if (m2->delay && !(m2->flags & FULL_DELAY))
      count++;
  if (count)
    for (m2 = chan->channel.member; m2 && m2->nick[0]; m2 = m2->next)
      if (m2->delay && !(m2->flags & FULL_DELAY)) {
 m2->delay = a_delay;
 if (count + 1 >=  modesperline)
   m2->flags |= FULL_DELAY;
      }
  if (count + 1 >=modesperline)
    m->flags |= FULL_DELAY;
  m->delay = a_delay;
}

/* Got a join
 */
static int gotjoin(char *from, char *ignore, char *chname)
{
  char buf[UHOSTLEN], *nick, *uhost, *p;
  char *ch_dname = NULL;
  struct chanset_t *chan;
  memberlist *m;
  masklist *b;
  struct userrec *u;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};

  fixcolon(chname);
  chan = findchan(chname);
  if (!chan && chname[0] == '!') {
    /* As this is a !channel, we need to search for it by display (short)
     * name now. This will happen when we initially join the channel, as we
     * dont know the unique channel name that the server has made up. <cybah>
     */
    int	l_chname = strlen(chname);

    if (l_chname > (CHANNEL_ID_LEN + 1)) {
      ch_dname = malloc(l_chname + 1);
      if (ch_dname) {
	snprintf(ch_dname, l_chname + 2, "!%s",
		     chname + (CHANNEL_ID_LEN + 1));
	chan = findchan_by_dname(ch_dname);
	if (!chan) {
	  /* Hmm.. okay. Maybe the admin's a genius and doesn't know the
	   * difference between id and descriptive channel names. Search
	   * the channel name in the dname list using the id-name.
	   */
	   chan = findchan_by_dname(chname);
	   if (chan) {
	     /* Duh, I was right. Mark this channel as inactive and log
	      * the incident.
	      */
	     chan->status |= CHAN_INACTIVE;
	     putlog(LOG_MISC, "*", "Deactivated channel %s, because it uses "
		    "an ID channel-name. Use the descriptive name instead.",
		    chname);
	     dprintf(DP_SERVER, "PART %s\n", chname);
	     goto exit;
	   }
	}
      }
    }
  } else if (!chan) {
    /* As this is not a !chan, we need to search for it by display name now.
     * Unlike !chan's, we dont need to remove the unique part.
     */
    chan = findchan_by_dname(chname);
  }

  if (!chan || channel_inactive(chan)) {
    putlog(LOG_MISC, "*", "joined %s but didn't want to!", chname);
    dprintf(DP_MODE, "PART %s\n", chname);
  } else if (!channel_pending(chan)) {
    chan->status &= ~CHAN_STOP_CYCLE;
    strncpyz(buf, from, sizeof buf);
    nick = strtok(buf, "!");
    uhost = strtok(NULL, "!");
    detect_chan_flood(nick, uhost, from, chan, FLOOD_JOIN, NULL);
    /* Grab last time joined before we update it */
    u = get_user_by_host(from);
    get_user_flagrec(u, &fr, chname);
    if (!channel_active(chan) && !match_my_nick(nick)) {
      /* uh, what?!  i'm on the channel?! */
      putlog(LOG_MISC, chan->dname,
	     "confused bot: guess I'm on %s and didn't realize it",
	     chan->dname);
      chan->status |= CHAN_ACTIVE;
      chan->status &= ~CHAN_PEND;
      reset_chan_info(chan);
    } else {
      m = ismember(chan, nick);
      if (m && m->split && !strcasecmp(m->userhost, uhost)) {
	check_tcl_rejn(nick, uhost, u, chan->dname);
	/* The tcl binding might have deleted the current user. Recheck. */
	u = get_user_by_host(from);
	m->split = 0;
	m->last = now;
	m->delay = 0L;
        m->flags = (chan_hasop(m) ? WASOP : 0);
	m->user = u;
	set_handle_laston(chan->dname, u, now);
	m->flags |= STOPWHO;
	putlog(LOG_JOIN, chan->dname, "%s (%s) returned to %s.", nick, uhost,
	       chan->dname);
      } else {
	if (m)
	  killmember(chan, nick);
	m = newmember(chan);
	m->joined = now;
	m->split = 0L;
	m->flags = 0;
	m->last = now;
	m->delay = 0L;
        strcpy(m->nick, nick);
	strcpy(m->userhost, uhost);
	m->user = u;
	m->flags |= STOPWHO;

	check_tcl_join(nick, uhost, u, chan->dname);

	/* The tcl binding might have deleted the current user and the
	 * current channel, so we'll now have to re-check whether they
	 * both still exist.
	 */
	chan = findchan(chname);
	if (!chan) {
	  if (ch_dname)
	    chan = findchan_by_dname(ch_dname);
	  else
	    chan = findchan_by_dname(chname);
	}
	if (!chan)
	  /* The channel doesn't exist anymore, so get out of here. */
	  goto exit;

	/* The record saved in the channel record always gets updated,
	   so we can use that. */
	u = m->user;

	if (match_my_nick(nick)) {
	  /* It was me joining! Need to update the channel record with the
	   * unique name for the channel (as the server see's it). <cybah>
	   */
	  strncpy(chan->name, chname, 81);
	  chan->name[80] = 0;
	  chan->status &= ~CHAN_JUPED;

          /* ... and log us joining. Using chan->dname for the channel is
	   * important in this case. As the config file will never contain
	   * logs with the unique name.
           */
	  if (chname[0] == '!')
	    putlog(LOG_JOIN | LOG_MISC, chan->dname, "%s joined %s (%s)",
	           nick, chan->dname, chname);
	  else
	    putlog(LOG_JOIN | LOG_MISC, chan->dname, "%s joined %s.", nick,
	           chname);
	  reset_chan_info(chan);
	} else {
	  struct chanuserrec *cr;

	  putlog(LOG_JOIN, chan->dname,
		 "%s (%s) joined %s.", nick, uhost, chan->dname);
	  /* Don't re-display greeting if they've been on the channel
	   * recently.
	   */
	  if (u) {
	    struct laston_info *li = 0;

	    cr = get_chanrec(m->user, chan->dname);
	    if (!cr && no_chanrec_info)
	      li = get_user(&USERENTRY_LASTON, m->user);
	    if (channel_greet(chan) && use_info &&
		((cr && now - cr->laston > wait_info) ||
		 (no_chanrec_info &&
		  (!li || now - li->laston > wait_info)))) {
	      char s1[512], *s;

	      if (!(u->flags & USER_BOT)) {
		s = get_user(&USERENTRY_INFO, u);
		get_handle_chaninfo(u->handle, chan->dname, s1);
		/* Locked info line overides non-locked channel specific
		 * info line.
		 */
		if (!s || (s1[0] && (s[0] != '@' || s1[0] == '@')))
		  s = s1;
		if (s[0] == '@')
		  s++;
		if (s && s[0])
		  dprintf(DP_HELP, "PRIVMSG %s :[%s] %s\n",
			  chan->name, nick, s);
	      }
	    }
	  }
	  set_handle_laston(chan->dname, u, now);
	}
      }
      /* ok, the op-on-join,etc, tests...first only both if Im opped */
      if (me_op(chan)) {
	/* Check for and reset exempts and invites.
	 *
	 * This will require further checking to account for when to use the
	 * various modes.
	 */
	if (u_match_mask(global_invites,from) ||
	    u_match_mask(chan->invites, from))
	  refresh_invite(chan, from);
	if (!(use_exempts &&
	      (u_match_mask(global_exempts,from) ||
	       u_match_mask(chan->exempts, from)))) {
          if (channel_enforcebans(chan) && !chan_op(fr) && !glob_op(fr) &&
              !glob_friend(fr) && !chan_friend(fr) && !chan_sentkick(m) &&
              !(use_exempts && isexempted(chan, from))) {
            for (b = chan->channel.ban; b->mask[0]; b = b->next) {
              if (wild_match(b->mask, from)) {
                dprintf(DP_SERVER, "KICK %s %s :%s\n", chname, m->nick,
                        _("You are banned"));
                m->flags |= SENTKICK;
                goto exit;
              }
            }
          }
	  /* If it matches a ban, dispose of them. */
	  if (u_match_mask(global_bans, from) ||
	      u_match_mask(chan->bans, from)) {
	    refresh_ban_kick(chan, from, nick);
	  /* Likewise for kick'ees */
	  } else if (glob_kick(fr) || chan_kick(fr)) {
	    check_exemptlist(chan, from);
	    quickban(chan, from);
	    p = get_user(&USERENTRY_COMMENT, m->user);
	    dprintf(DP_MODE, "KICK %s %s :%s\n", chname, nick,
		    (p && (p[0] != '@')) ? p : _("...and dont come back."));
	    m->flags |= SENTKICK;
	  }
	}
	/* Are they a chan op, or global op without chan deop? */
	if ((chan_op(fr) || (glob_op(fr) && !chan_deop(fr))) &&
	   /* ... and is it op-on-join or is the use marked auto-op? */
	    (channel_autoop(chan) || glob_autoop(fr) || chan_autoop(fr))) {
	  /* Yes! do the honors. */
	  if (!chan->aop_min)
	    add_mode(chan, '+', 'o', nick);
	  else {
            set_delay(chan, nick);
            m->flags |= SENTOP;
	  }
	} else if ((channel_autovoice(chan) &&
		    (chan_voice(fr) || (glob_voice(fr) && !chan_quiet(fr)))) ||
                   ((glob_gvoice(fr) || chan_gvoice(fr)) && !chan_quiet(fr))) {
           if (!chan->aop_min)
             add_mode(chan, '+', 'v', nick);
           else {
             set_delay(chan, nick);
             m->flags |= SENTVOICE;
           }
         }
      }
    }
  }

exit:
  if (ch_dname)
    free(ch_dname);
  return 0;
}

/* Got a part
 */
static int gotpart(char *from, char *ignore, char *msg)
{
  char buf[UHOSTLEN], *nick, *uhost, *chname;
  struct chanset_t *chan;
  struct userrec *u;

  chname = newsplit(&msg);
  fixcolon(msg);
  chan = findchan(chname);
  if (chan && channel_inactive(chan)) {
    clear_channel(chan, 1);
    chan->status &= ~(CHAN_ACTIVE | CHAN_PEND);
    return 0;
  }
  if (chan && !channel_pending(chan)) {
    if (!channel_active(chan)) {
      /* whoa! */
      putlog(LOG_MISC, chan->dname,
	  "confused bot: guess I'm on %s and didn't realize it", chan->dname);
      chan->status |= CHAN_ACTIVE;
      chan->status &= ~CHAN_PEND;
      reset_chan_info(chan);
    }
    u = get_user_by_host(from);
    set_handle_laston(chan->dname, u, now);
    strncpyz(buf, from, sizeof buf);
    nick = strtok(buf, "!");
    uhost = strtok(NULL, "!");
    check_tcl_part(nick, uhost, u, chan->dname, msg); /* This must be directly above the killmember, in case
    							we're doing anything to the record that would affect
							the above */
    killmember(chan, nick);
    if (msg[0])
      putlog(LOG_JOIN, chan->dname, "%s (%s) left %s (%s).", nick, uhost, chan->dname, msg);
    else
      putlog(LOG_JOIN, chan->dname, "%s (%s) left %s.", nick, uhost, chan->dname);
    /* If it was me, all hell breaks loose... */
    if (match_my_nick(nick)) {
      clear_channel(chan, 1);
      chan->status &= ~(CHAN_ACTIVE | CHAN_PEND);
      if (!channel_inactive(chan))
	dprintf(DP_MODE, "JOIN %s %s\n",
	        (chan->name[0]) ? chan->name : chan->dname,
	        chan->channel.key[0] ? chan->channel.key : chan->key_prot);
    } else
      check_lonely_channel(chan);
  }
  return 0;
}

/* Got a kick
 */
static int gotkick(char *from, char *ignore, char *origmsg)
{
  char buf[UHOSTLEN], *nick, *uhost, *kicked, *chname, s1[UHOSTLEN];
  char buf2[511], *msg;
  memberlist *m;
  struct chanset_t *chan;
  struct userrec *u;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};

  strncpyz(buf2, origmsg, sizeof buf2);
  msg = buf2;
  chname = newsplit(&msg);
  chan = findchan(chname);
  if (chan && channel_active(chan)) {
    kicked = newsplit(&msg);
    fixcolon(msg);
    u = get_user_by_host(from);
    strncpyz(buf, from, sizeof buf);
    nick = strtok(buf, "!");
    uhost = strtok(NULL, "!");
    detect_chan_flood(nick, uhost, from, chan, FLOOD_KICK, kicked);
    m = ismember(chan, nick);
    if (m)
      m->last = now;
    /* This _needs_ to use chan->dname <cybah> */
    get_user_flagrec(u, &fr, chan->dname);
    set_handle_laston(chan->dname, u, now);
    check_tcl_kick(nick, uhost, u, chan->dname, kicked, msg);
    m = ismember(chan, kicked);
    if (m) {
      struct userrec *u2;

      simple_sprintf(s1, "%s!%s", m->nick, m->userhost);
      u2 = get_user_by_host(s1);
      set_handle_laston(chan->dname, u2, now);
      maybe_revenge(chan, from, s1, REVENGE_KICK);
    }
    putlog(LOG_MODES, chan->dname, "%s kicked from %s by %s: %s", s1,
	   chan->dname, from, msg);
    /* Kicked ME?!? the sods! */
    if (match_my_nick(kicked)) {
      chan->status &= ~(CHAN_ACTIVE | CHAN_PEND);
      dprintf(DP_MODE, "JOIN %s %s\n",
              (chan->name[0]) ? chan->name : chan->dname,
              chan->channel.key[0] ? chan->channel.key : chan->key_prot);
      clear_channel(chan, 1);
    } else {
      killmember(chan, kicked);
      check_lonely_channel(chan);
    }
  }
  return 0;
}

/* Got a nick change
 */
static int gotnick(char *from, char *ignore, char *msg)
{
  char buf[UHOSTLEN], *nick, *uhost, s1[UHOSTLEN];
  memberlist *m, *mm;
  struct chanset_t *chan;
  struct userrec *u;

  fixcolon(msg);
  strncpyz(buf, from, sizeof buf);
  nick = strtok(buf, "!");
  uhost = strtok(NULL, "!");
  for (chan = chanset; chan; chan = chan->next) { 
    m = ismember(chan, nick);
    if (m) {
      putlog(LOG_JOIN, chan->dname, "Nick change: %s -> %s", nick, msg);
      m->last = now;
      if (irccmp(nick, msg)) {
	/* Not just a capitalization change */
	mm = ismember(chan, msg);
	if (mm) {
	  /* Someone on channel with old nick?! */
	  if (mm->split)
	    putlog(LOG_JOIN, chan->dname,
		   "Possible future nick collision: %s", mm->nick);
	  else
	    putlog(LOG_MISC, chan->dname,
		   "* Bug: nick change to existing nick");
	  killmember(chan, mm->nick);
	}
      }
      /*
       * Banned?
       */
      /* Compose a nick!user@host for the new nick */
      sprintf(s1, "%s!%s", msg, uhost);
      /* Enforcing bans & haven't already kicked them? */
      if (channel_enforcebans(chan) && chan_sentkick(m) &&
	  (u_match_mask(global_bans, s1) ||
	   u_match_mask(chan->bans, s1)) &&
	  !(use_exempts &&
	   (u_match_mask(global_exempts,s1) ||
	    u_match_mask(chan->exempts, s1))))
	refresh_ban_kick(chan, s1, msg);
      strcpy(m->nick, msg);
      detect_chan_flood(msg, uhost, from, chan, FLOOD_NICK, NULL);
      /* Any pending kick to the old nick is lost. Ernst 18/3/1998 */
      if (chan_sentkick(m))
	m->flags &= ~SENTKICK;
      u = get_user_by_host(from); /* make sure this is in the loop, someone could have changed the record
                                     in an earlier iteration of the loop */
      check_tcl_nick(nick, uhost, u, chan->dname, msg);
    }
  }
  clear_chanlist_member(msg);	/* Cache for nick 'msg' is meaningless now. */
  return 0;
}

/* Signoff, similar to part.
 */
static int gotquit(char *from, char *ignore, char *msg)
{
  char buf[UHOSTLEN], *nick, *uhost, *p, *alt;
  int split = 0;
  memberlist *m;
  struct chanset_t *chan;
  struct userrec *u;

  fixcolon(msg);
  strncpyz(buf, from, sizeof buf);
  nick = strtok(from, "!");
  uhost = strtok(NULL, "!");
  /* Fred1: Instead of expensive wild_match on signoff, quicker method.
   *        Determine if signoff string matches "%.% %.%", and only one
   *        space.
   */
  p = strchr(msg, ' ');
  if (p && (p == strrchr(msg, ' '))) {
    char *z1, *z2;

    *p = 0;
    z1 = strchr(p + 1, '.');
    z2 = strchr(msg, '.');
    if (z1 && z2 && (*(z1 + 1) != 0) && (z1 - 1 != p) &&
	(z2 + 1 != p) && (z2 != msg)) {
      /* Server split, or else it looked like it anyway (no harm in
       * assuming)
       */
      split = 1;
    } else
      *p = ' ';
  }
  for (chan = chanset; chan; chan = chan->next) {
    m = ismember(chan, nick);
    if (m) {
      u = get_user_by_host(from);
      if (u) {
        set_handle_laston(chan->dname, u, now); /* If you remove this, the bot will crash when the user record in question
						   is removed/modified during the tcl binds below, and the users was on more
						   than one monitored channel */
      }
      if (split) {
	m->split = now;
	check_tcl_splt(nick, uhost, u, chan->dname);
	putlog(LOG_JOIN, chan->dname, "%s (%s) got netsplit.", nick,
	       uhost);
      } else {
	check_tcl_sign(nick, uhost, u, chan->dname, msg);
	putlog(LOG_JOIN, chan->dname, "%s (%s) left irc: %s", nick,
	       uhost, msg);
	killmember(chan, nick);
	check_lonely_channel(chan);
      }
    }
  }
  /* Our nick quit? if so, grab it. Heck, our altnick quit maybe, maybe
   * we want it.
   */
  if (keepnick) {
    alt = get_altbotnick();
    if (!irccmp(nick, origbotname)) {
      putlog(LOG_MISC, "*", _("Switching back to nick %s"), origbotname);
      dprintf(DP_SERVER, "NICK %s\n", origbotname);
    } else if (alt[0]) {
      if (!irccmp(nick, alt) && strcmp(botname, origbotname)) {
	putlog(LOG_MISC, "*", _("Switching back to altnick %s"), alt);
	dprintf(DP_SERVER, "NICK %s\n", alt);
      }
    }
  }
  return 0;
}

/* Got a private message.
 */
static int gotmsg(char *from, char *ignore, char *msg)
{
  char buf[UHOSTLEN], *nick, *uhost, *to, *realto, buf2[512], *p, *p1;
  char *code, *ctcp;
  int ctcp_count = 0;
  struct chanset_t *chan;
  int ignoring;
  struct userrec *u;
  memberlist *m;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};

  if (!strchr("&#!+@$", msg[0]))
    return 0;
  ignoring = match_ignore(from);
  to = newsplit(&msg);
  realto = (to[0] == '@') ? to + 1 : to;
  chan = findchan(realto);
  if (!chan)
    return 0;			/* Private msg to an unknown channel?? */
  fixcolon(msg);
  strncpyz(buf, from, sizeof buf);
  nick = strtok(buf, "!");
  uhost = strtok(NULL, "!");
  /* Only check if flood-ctcp is active */
  if (flud_ctcp_thr && detect_avalanche(msg)) {
    u = get_user_by_host(from);
    get_user_flagrec(u, &fr, chan->dname);
    m = ismember(chan, nick);
    /* Discard -- kick user if it was to the channel */
    if (me_op(chan) && m && !chan_sentkick(m) &&
	!chan_friend(fr) && !glob_friend(fr) &&
	!(channel_dontkickops(chan) &&
	  (chan_op(fr) || (glob_op(fr) && !chan_deop(fr)))) &&	/* arthur2 */
	!(use_exempts && ban_fun &&
	  /* don't kickban if permanent exempted -- Eule */
	  (u_match_mask(global_exempts, from) ||
	   u_match_mask(chan->exempts, from)))) {
      if (ban_fun) {
	check_exemptlist(chan, from);
	u_addban(chan, quickban(chan, uhost), origbotname,
		_("that was fun, lets do it again!"), now + (60 * ban_time), 0);
      }
      if (kick_fun) {
	/* This can induce kickflood - arthur2 */
	dprintf(DP_SERVER, "KICK %s %s :%s\n", chan->name, nick,
		_("that was fun, lets do it again!"));
	m->flags |= SENTKICK;
      }
    }
    if (!ignoring) {
      putlog(LOG_MODES, "*", "Avalanche from %s!%s in %s - ignoring",
	     nick, uhost, chan->dname);
      /* FIXME: get rid of this mess */
      p = strchr(uhost, '@');
      if (p)
	p++;
      else
	p = uhost;
      simple_sprintf(buf2, "*!*@%s", p);
      addignore(buf2, origbotname, "ctcp avalanche", now + (60 * ignore_time));
    }
    return 0;
  }
  /* Check for CTCP: */
  ctcp_reply[0] = 0;
  p = strchr(msg, 1);
  while (p && *p) {
    p++;
    p1 = p;
    while ((*p != 1) && *p)
      p++;
    if (*p == 1) {
      *p = 0;
      ctcp = buf2;
      strcpy(ctcp, p1);
      strcpy(p1 - 1, p + 1);
      detect_chan_flood(nick, uhost, from, chan,
			strncmp(ctcp, "ACTION ", 7) ?
			FLOOD_CTCP : FLOOD_PRIVMSG, NULL);
      /* Respond to the first answer_ctcp */
      p = strchr(msg, 1);
      if (ctcp_count < answer_ctcp) {
	ctcp_count++;
	if (ctcp[0] != ' ') {
	  code = newsplit(&ctcp);
	  u = get_user_by_host(from);
	  if (!ignoring || trigger_on_ignore) {
	    if (!check_tcl_ctcp(nick, uhost, u, to, code, ctcp))
	      update_idle(chan->dname, nick);
	    if (!ignoring) {
	      /* Log DCC, it's to a channel damnit! */
	      if (!strcmp(code, "ACTION")) {
		putlog(LOG_PUBLIC, chan->dname, "Action: %s %s", nick, ctcp);
	      } else {
		putlog(LOG_PUBLIC, chan->dname,
		       "CTCP %s: %s from %s (%s) to %s", code, ctcp, nick,
		       from, to);
	      }
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
    /* Check even if we're ignoring the host. (modified by Eule 17.7.99) */
    detect_chan_flood(nick, uhost, from, chan, FLOOD_PRIVMSG, NULL);
    if (!ignoring || trigger_on_ignore) {
      if (check_tcl_pub(nick, uhost, chan->dname, msg))
	return 0;
      check_tcl_pubm(nick, uhost, chan->dname, msg);
    }
    if (!ignoring) {
      if (to[0] == '@')
	putlog(LOG_PUBLIC, chan->dname, "@<%s> %s", nick, msg);
      else
	putlog(LOG_PUBLIC, chan->dname, "<%s> %s", nick, msg);
    }
    update_idle(chan->dname, nick);
  }
  return 0;
}

/* Got a private notice.
 */
static int gotnotice(char *from, char *ignore, char *msg)
{
  char buf[UHOSTLEN], *nick, *uhost, *to, *realto, buf2[512], *p, *p1;
  char *ctcp, *code;
  struct userrec *u;
  memberlist *m;
  struct chanset_t *chan;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  int ignoring;

  if (!strchr(CHANMETA "@", *msg))
    return 0;
  ignoring = match_ignore(from);
  to = newsplit(&msg);
  realto = (*to == '@') ? to + 1 : to;
  chan = findchan(realto);
  if (!chan)
    return 0;			/* Notice to an unknown channel?? */
  fixcolon(msg);
  strncpyz(buf, from, sizeof buf);
  nick = strtok(buf, "!");
  uhost = strtok(NULL, "!");
  u = get_user_by_host(from);
  if (flud_ctcp_thr && detect_avalanche(msg)) {
    get_user_flagrec(u, &fr, chan->dname);
    m = ismember(chan, nick);
    /* Discard -- kick user if it was to the channel */
    if (me_op(chan) && m && !chan_sentkick(m) &&
	!chan_friend(fr) && !glob_friend(fr) &&
	!(channel_dontkickops(chan) &&
	  (chan_op(fr) || (glob_op(fr) && !chan_deop(fr)))) &&	/* arthur2 */
	!(use_exempts && ban_fun &&
	  /* don't kickban if permanent exempted -- Eule */
	  (u_match_mask(global_exempts,from) ||
	   u_match_mask(chan->exempts, from)))) {
      if (ban_fun) {
	check_exemptlist(chan, from);
	u_addban(chan, quickban(chan, uhost), origbotname,
		_("that was fun, lets do it again!"), now + (60 * ban_time), 0);
      }
      if (kick_fun) {
	/* This can induce kickflood - arthur2 */
	dprintf(DP_SERVER, "KICK %s %s :%s\n", chan->name, nick,
		_("that was fun, lets do it again!"));
	m->flags |= SENTKICK;
      }
    }
    if (!ignoring)
      putlog(LOG_MODES, "*", "Avalanche from %s", from);
    return 0;
  }
  /* Check for CTCP: */
  p = strchr(msg, 1);
  while (p && *p) {
    p++;
    p1 = p;
    while ((*p != 1) && *p)
      p++;
    if (*p == 1) {
      *p = 0;
      ctcp = buf2;
      strcpy(ctcp, p1);
      strcpy(p1 - 1, p + 1);
      p = strchr(msg, 1);
      detect_chan_flood(nick, uhost, from, chan,
			strncmp(ctcp, "ACTION ", 7) ?
			FLOOD_CTCP : FLOOD_PRIVMSG, NULL);
      if (ctcp[0] != ' ') {
	code = newsplit(&ctcp);
	if (!ignoring || trigger_on_ignore) {
	  check_tcl_ctcr(nick, uhost, u, chan->dname, code, msg);
	  if (!ignoring) {
	    putlog(LOG_PUBLIC, chan->dname, "CTCP reply %s: %s from %s (%s) to %s",
		   code, msg, nick, from, chan->dname);
	    update_idle(chan->dname, nick);
	  }
	}
      }
    }
  }
  if (msg[0]) {
    /* Check even if we're ignoring the host. (modified by Eule 17.7.99) */
    detect_chan_flood(nick, uhost, from, chan, FLOOD_NOTICE, NULL);
    if (!ignoring || trigger_on_ignore)
      check_tcl_notc(nick, uhost, u, to, msg);
    if (!ignoring)
      putlog(LOG_PUBLIC, chan->dname, "-%s:%s- %s", nick, to, msg);
    update_idle(chan->dname, nick);
  }
  return 0;
}

static cmd_t irc_raw[] =
{
  {"324",	"",	(Function) got324,	"irc:324"},
  {"352",	"",	(Function) got352,	"irc:352"},
  {"354",	"",	(Function) got354,	"irc:354"},
  {"315",	"",	(Function) got315,	"irc:315"},
  {"367",	"",	(Function) got367,	"irc:367"},
  {"368",	"",	(Function) got368,	"irc:368"},
  {"403",	"",	(Function) got403,	"irc:403"},
  {"405",	"",	(Function) got405,	"irc:405"},
  {"471",	"",	(Function) got471,	"irc:471"},
  {"473",	"",	(Function) got473,	"irc:473"},
  {"474",	"",	(Function) got474,	"irc:474"},
  {"475",	"",	(Function) got475,	"irc:475"},
  {"INVITE",	"",	(Function) gotinvite,	"irc:invite"},
  {"TOPIC",	"",	(Function) gottopic,	"irc:topic"},
  {"331",	"",	(Function) got331,	"irc:331"},
  {"332",	"",	(Function) got332,	"irc:332"},
  {"JOIN",	"",	(Function) gotjoin,	"irc:join"},
  {"PART",	"",	(Function) gotpart,	"irc:part"},
  {"KICK",	"",	(Function) gotkick,	"irc:kick"},
  {"NICK",	"",	(Function) gotnick,	"irc:nick"},
  {"QUIT",	"",	(Function) gotquit,	"irc:quit"},
  {"PRIVMSG",	"",	(Function) gotmsg,	"irc:msg"},
  {"NOTICE",	"",	(Function) gotnotice,	"irc:notice"},
  {"MODE",	"",	(Function) gotmode,	"irc:mode"},
  {"346",	"",	(Function) got346,	"irc:346"},
  {"347",	"",	(Function) got347,	"irc:347"},
  {"348",	"",	(Function) got348,	"irc:348"},
  {"349",	"",	(Function) got349,	"irc:349"},
  {NULL,	NULL,	NULL,			NULL}
};