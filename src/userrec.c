/* 
 * userrec.c -- handles:
 * add_q() del_q() str2flags() flags2str() str2chflags() chflags2str()
 * a bunch of functions to find and change user records
 * change and check user (and channel-specific) flags
 * 
 * dprintf'ized, 10nov1995
 */
/*
 * This file is part of the eggdrop source code
 * copyright (c) 1997 Robey Pointer
 * and is distributed according to the GNU general public license.
 * For full details, read the top of 'main.c' or the file called
 * COPYING that was distributed with this code.
 */

#include "main.h"
#include <sys/stat.h>
#include "users.h"
#include "chan.h"
#include "modules.h"
#include "tandem.h"

extern struct dcc_t *dcc;
extern int dcc_total;
extern char userfile[];
extern int share_greet;
extern struct chanset_t *chanset;
extern char ver[];
extern char botnetnick[];
extern time_t now;
extern int default_flags, default_uflags;
extern int quiet_save;

int noshare = 1;		/* don't send out to sharebots */
int sort_users = 0;		/* sort the userlist when saving */
struct userrec *userlist = NULL;	/* user records are stored here */
struct userrec *lastuser = NULL;	/* last accessed user record */
maskrec *global_bans = NULL,
        *global_exempts = NULL,
        *global_invites = NULL;
struct igrec *global_ign = NULL;
int cache_hit = 0, cache_miss = 0;	/* temporary cache accounting */

#ifdef EBUG_MEM
void *_user_malloc(int size, char *file, int line)
{
  char x[1024];

  simple_sprintf(x, "userrec.c:%s", file);
  return n_malloc(size, x, line);
}
void *_user_realloc(void *ptr, int size, char *file, int line)
{
  char x[1024];

  simple_sprintf(x, "userrec.c:%s", file);
  return n_realloc(ptr, size, x, line);
}
#else
void *_user_malloc(int size, char *file, int line)
{
  return nmalloc(size);
}

void *_user_realloc(void *ptr, int size, char *file, int line)
{
  return nrealloc(ptr, size);
}
#endif

inline int expmem_mask(struct maskrec *m)
{
  int result = 0;
  
  while (m) {
    result += sizeof(struct maskrec);
    result += strlen(m->mask) + 1;
    if (m->user)
      result += strlen(m->user) + 1;
    if (m->desc)
      result += strlen(m->desc) + 1;
      
    m = m->next;
  }
  
  return result;
}

/* memory we should be using */
int expmem_users()
{
  int tot;
  struct userrec *u;
  struct chanuserrec *ch;
  struct chanset_t *chan;
  struct user_entry *ue;
  struct igrec *i;

  context;
  tot = 0;
  u = userlist;
  while (u != NULL) {
    ch = u->chanrec;
    while (ch) {
      tot += sizeof(struct chanuserrec);

      if (ch->info != NULL)
	tot += strlen(ch->info) + 1;
      ch = ch->next;
    }
    tot += sizeof(struct userrec);

    for (ue = u->entries; ue; ue = ue->next) {
      tot += sizeof(struct user_entry);

      if (ue->name) {
	tot += strlen(ue->name) + 1;
	tot += list_type_expmem(ue->u.list);
      } else {
	tot += ue->type->expmem(ue);
      }
    }
    u = u->next;
  }
  /* account for each channel's masks */
  for (chan = chanset; chan; chan = chan->next) {

    /* account for each channel's ban-list user */
    tot += expmem_mask(chan->bans);

    /* account for each channel's exempt-list user */
    tot += expmem_mask(chan->exempts);

    /* account for each channel's invite-list user */
    tot += expmem_mask(chan->invites);
  }
  
  tot += expmem_mask(global_bans);
  tot += expmem_mask(global_exempts);
  tot += expmem_mask(global_invites);

  for (i = global_ign; i; i = i->next) {
    tot += sizeof(struct igrec);

    tot += strlen(i->igmask) + 1;
    if (i->user)
      tot += strlen(i->user) + 1;
    if (i->msg)
      tot += strlen(i->msg) + 1;
  }
  return tot;
}

int count_users(struct userrec *bu)
{
  int tot = 0;
  struct userrec *u = bu;

  while (u != NULL) {
    tot++;
    u = u->next;
  }
  return tot;
}

struct userrec *check_dcclist_hand(char *handle)
{
  int i;

  for (i = 0; i < dcc_total; i++)
    if (!strcasecmp(dcc[i].nick, handle))
      return dcc[i].user;
  return NULL;
}

struct userrec *get_user_by_handle(struct userrec *bu, char *handle)
{
  struct userrec *u = bu, *ret;

  if (!handle)
    return NULL;
  rmspace(handle);
  if (!handle[0] || (handle[0] == '*'))
    return NULL;
  if (bu == userlist) {
    if (lastuser && !strcasecmp(lastuser->handle, handle)) {
      cache_hit++;
      return lastuser;
    }
    ret = check_dcclist_hand(handle);
    if (ret) {
      cache_hit++;
      return ret;
    }
    ret = check_chanlist_hand(handle);
    if (ret) {
      cache_hit++;
      return ret;
    }
    cache_miss++;
  }
  while (u) {
    if (!strcasecmp(u->handle, handle)) {
      if (bu == userlist)
	lastuser = u;
      return u;
    }
    u = u->next;
  }
  return NULL;
}

/* fix capitalization, etc */
void correct_handle(char *handle)
{
  struct userrec *u;

  u = get_user_by_handle(userlist, handle);
  if (u == NULL)
    return;
  strcpy(handle, u->handle);
}

/*        This will be usefull in a lot of places, much more code re-use so we
 *      endup with a smaller executable bot. <cybah> 
 */
void clear_masks(maskrec *m)
{
  maskrec *temp = NULL;

  while (m) {
    temp = m->next;
      
    if (m->mask)
      nfree(m->mask);
    if (m->user)
      nfree(m->user);
    if (m->desc)
      nfree(m->desc);
	
    nfree(m);
    m = temp;
  }
}

void clear_userlist(struct userrec *bu)
{
  struct userrec *u = bu, *v;
  int i;

  context;
  while (u != NULL) {
    v = u->next;
    freeuser(u);
    u = v;
  }
  if (userlist == bu) {
    struct chanset_t *cst;

    for (i = 0; i < dcc_total; i++)
      dcc[i].user = NULL;
    clear_chanlist();
    lastuser = NULL;
    
    while (global_ign)
      delignore(global_ign->igmask);

    clear_masks(global_bans);
    clear_masks(global_exempts);
    clear_masks(global_invites);
    global_exempts = global_invites = global_bans = NULL;

    for (cst = chanset; cst; cst = cst->next) {
      clear_masks(cst->bans);
      clear_masks(cst->exempts);
      clear_masks(cst->invites);
      
      cst->bans = cst->exempts = cst->invites = NULL;
    }
  }
  /* remember to set your userlist to NULL after calling this */
  context;
}

/* find CLOSEST host match */
/* (if "*!*@*" and "*!*@*clemson.edu" both match, use the latter!) */
/* 26feb: CHECK THE CHANLIST FIRST to possibly avoid needless search */
struct userrec *get_user_by_host(char *host)
{
  struct userrec *u = userlist, *ret;
  struct list_type *q;
  int cnt, i;

  if (host == NULL)
    return NULL;
  rmspace(host);
  if (!host[0])
    return NULL;
  ret = check_chanlist(host);
  cnt = 0;
  if (ret != NULL) {
    cache_hit++;
    return ret;
  }
  cache_miss++;
  while (u != NULL) {
    q = get_user(&USERENTRY_HOSTS, u);
    while (q != NULL) {
      i = wild_match(q->extra, host);
      if (i > cnt) {
	ret = u;
	cnt = i;
      }
      q = q->next;
    }
    u = u->next;
  }
  if (ret != NULL) {
    lastuser = ret;
    set_chanlist(host, ret);
  }
  return ret;
}

struct userrec *get_user_by_equal_host(char *host)
{
  struct userrec *u = userlist;
  struct list_type *q;

  while (u != NULL) {
    q = get_user(&USERENTRY_HOSTS, u);
    while (q != NULL) {
      if (!rfc_casecmp(q->extra, host))
	return u;
      q = q->next;
    }
    u = u->next;
  }
  return NULL;
}

/* try: pass_match_by_host("-",host)
 * will return 1 if no password is set for that host */
int u_pass_match(struct userrec *u, char *pass)
{
  char *cmp, new[32];

  if (!u)
    return 0;
  cmp = get_user(&USERENTRY_PASS, u);
  if (!cmp)
    return 1;
  if (!pass || !pass[0] || (pass[0] == '-'))
    return 0;
  if (u->flags & USER_BOT) {
    if (!strcmp(cmp, pass))
      return 1;
  } else {
    if (strlen(pass) > 15)
      pass[15] = 0;
    encrypt_pass(pass, new);
    if (!strcmp(cmp, new))
      return 1;
  }
  return 0;
}

int write_user(struct userrec *u, FILE * f, int idx)
{
  char s[181];
  struct chanuserrec *ch;
  struct chanset_t *cst;
  struct user_entry *ue;
  struct flag_record fr =
  {FR_GLOBAL, 0, 0, 0, 0, 0};

  fr.global = u->flags;

  fr.udef_global = u->flags_udef;
  build_flags(s, &fr, NULL);
  if (fprintf(f, "%-10s - %-24s\n", u->handle, s) == EOF)
    return 0;
  for (ch = u->chanrec; ch; ch = ch->next) {
    cst = findchan_by_dname(ch->channel);
    if (cst && ((idx < 0) || channel_shared(cst))) {
      if (idx >= 0) {
	fr.match = (FR_CHAN | FR_BOT);
	get_user_flagrec(dcc[idx].user, &fr, ch->channel);
      } else
	fr.chan = BOT_SHARE;
      if ((fr.chan & BOT_SHARE) || (fr.bot & BOT_GLOBAL)) {
	fr.match = FR_CHAN;
	fr.chan = ch->flags;
	fr.udef_chan = ch->flags_udef;
	build_flags(s, &fr, NULL);
	if (fprintf(f, "! %-20s %lu %-10s %s\n", ch->channel, ch->laston, s,
		    (((idx < 0) || share_greet) && ch->info) ? ch->info
		    : "") == EOF)
	  return 0;
      }
    }
  }
  for (ue = u->entries; ue; ue = ue->next) {
    if (ue->name) {
      struct list_type *lt;

      for (lt = ue->u.list; lt; lt = lt->next)
	if (fprintf(f, "--%s %s\n", ue->name, lt->extra) == EOF)
	  return 0;
    } else {
      if (!ue->type->write_userfile(f, u, ue))
	return 0;
    }
  }
  return 1;
}

int sort_compare(struct userrec *a, struct userrec *b)
{
  /* order by flags, then alphabetically
   * first bots: +h / +a / +l / other bots
   * then users: +n / +m / +o / other users
   * return true if (a > b) */
  if (a->flags & b->flags & USER_BOT) {
    if (~bot_flags(a) & bot_flags(b) & BOT_HUB)
      return 1;
    if (bot_flags(a) & ~bot_flags(b) & BOT_HUB)
      return 0;
    if (~bot_flags(a) & bot_flags(b) & BOT_ALT)
      return 1;
    if (bot_flags(a) & ~bot_flags(b) & BOT_ALT)
      return 0;
    if (~bot_flags(a) & bot_flags(b) & BOT_LEAF)
      return 1;
    if (bot_flags(a) & ~bot_flags(b) & BOT_LEAF)
      return 0;
  } else {
    if (~a->flags & b->flags & USER_BOT)
      return 1;
    if (a->flags & ~b->flags & USER_BOT)
      return 0;
    if (~a->flags & b->flags & USER_OWNER)
      return 1;
    if (a->flags & ~b->flags & USER_OWNER)
      return 0;
    if (~a->flags & b->flags & USER_MASTER)
      return 1;
    if (a->flags & ~b->flags & USER_MASTER)
      return 0;
    if (~a->flags & b->flags & USER_OP)
      return 1;
    if (a->flags & ~b->flags & USER_OP)
      return 0;
  }
  return (strcasecmp(a->handle, b->handle) > 0);
}

void sort_userlist()
{
  int again;
  struct userrec *last, *p, *c, *n;

  again = 1;
  last = NULL;
  while ((userlist != last) && (again)) {
    p = NULL;
    c = userlist;
    n = c->next;
    again = 0;
    while (n != last) {
      if (sort_compare(c, n)) {
	again = 1;
	c->next = n->next;
	n->next = c;
	if (p == NULL)
	  userlist = n;
	else
	  p->next = n;
      }
      p = c;
      c = n;
      n = n->next;
    }
    last = c;
  }
}

/* rewrite the entire user file */
void write_userfile(int idx)
{
  FILE *f;
  char s[121], s1[81];
  time_t tt;
  struct userrec *u;
  int ok;

  context;
  /* also write the channel file at the same time */
  if (userlist == NULL)
    return;			/* no point in saving userfile */
  sprintf(s, "%s~new", userfile);
  f = fopen(s, "w");
  chmod(s, 0600);		/* make it -rw------- */
  if (f == NULL) {
    putlog(LOG_MISC, "*", USERF_ERRWRITE);
    return;
  }
  if (!quiet_save)
    putlog(LOG_MISC, "*", USERF_WRITING);
  if (sort_users)
    sort_userlist();
  tt = now;
  strcpy(s1, ctime(&tt));
  fprintf(f, "#4v: %s -- %s -- written %s", ver, botnetnick, s1);
  context;
  ok = 1;
  u = userlist;
  while ((u != NULL) && (ok)) {
    ok = write_user(u, f, idx);
    u = u->next;
  }
  context;
  if (!ok || fflush(f)) {
    putlog(LOG_MISC, "*", "%s (%s)", USERF_ERRWRITE, strerror(ferror(f)));
    fclose(f);
    return;
  }
  fclose(f);
  context;
  call_hook(HOOK_USERFILE);
  context;
  unlink(userfile);
  sprintf(s, "%s~new", userfile);
  movefile(s, userfile);
}

int change_handle(struct userrec *u, char *newh)
{
  int i;
  char s[16];

  if (!u)
    return 0;
  /* nothing that will confuse the userfile */
  if ((newh[1] == 0) && strchr(BADHANDCHARS, newh[0]))
    return 0;
  check_tcl_nkch(u->handle, newh);
  /* yes, even send bot nick changes now: */
  if ((!noshare) && !(u->flags & USER_UNSHARED))
    shareout(NULL, "h %s %s\n", u->handle, newh);
  strcpy(s, u->handle);
  strcpy(u->handle, newh);
  for (i = 0; i < dcc_total; i++) {
    if (!strcasecmp(dcc[i].nick, s) &&
	(dcc[i].type != &DCC_BOT)) {
      strcpy(dcc[i].nick, newh);
      if ((dcc[i].type == &DCC_CHAT) && (dcc[i].u.chat->channel >= 0)) {
	chanout_but(-1, dcc[i].u.chat->channel,
		    "*** Nick change: %s -> %s\n", s, newh);
	if (dcc[i].u.chat->channel < 100000)
	  botnet_send_nkch(i, s);
      }
    }
  }
  return 1;
}

extern int noxtra;

struct userrec *adduser(struct userrec *bu, char *handle, char *host,
			char *pass, int flags)
{
  struct userrec *u, *x;
  struct xtra_key *xk;
  int oldshare = noshare;

  noshare = 1;
  u = (struct userrec *) nmalloc(sizeof(struct userrec));

  /* u->next=bu; bu=u; */
  strncpy(u->handle, handle, HANDLEN);
  u->handle[HANDLEN] = 0;
  u->next = NULL;
  u->chanrec = NULL;
  u->entries = NULL;
  if (flags != USER_DEFAULT) { /* drummer */
  u->flags = flags;
  u->flags_udef = 0;
  } else {
    u->flags = default_flags;
    u->flags_udef = default_uflags;
  }  
  set_user(&USERENTRY_PASS, u, pass);
  if (!noxtra) {
    xk = nmalloc(sizeof(struct xtra_key));

    xk->key = nmalloc(8);
    strcpy(xk->key, "created");
    xk->data = nmalloc(10);
    sprintf(xk->data, "%09lu", now);
    set_user(&USERENTRY_XTRA, u, xk);
  }
  /* strip out commas -- they're illegal */
  if (host && host[0]) {
    char *p = strchr(host, ',');

    while (p != NULL) {
      *p = '?';
      p = strchr(host, ',');
    }
    set_user(&USERENTRY_HOSTS, u, host);
  } else
    set_user(&USERENTRY_HOSTS, u, "none");
  if (bu == userlist)
    clear_chanlist();
  noshare = oldshare;
  if ((!noshare) && (handle[0] != '*') && (!(flags & USER_UNSHARED)) &&
      (bu == userlist)) {
    struct flag_record fr =
    {FR_GLOBAL, u->flags, u->flags_udef, 0, 0, 0};    
    char x[100];

    build_flags(x, &fr, 0);
    shareout(NULL, "n %s %s %s %s\n", handle, host, pass, x);
  }
  if (bu == NULL)
    bu = u;
  else {
    if ((bu == userlist) && (lastuser != NULL))
      x = lastuser;
    else
      x = bu;
    while (x->next != NULL)
      x = x->next;
    x->next = u;
    if (bu == userlist)
      lastuser = u;
  }
  return bu;
}

void freeuser(struct userrec *u)
{
  struct user_entry *ue, *ut;
  struct chanuserrec *ch, *z;

  if (u == NULL)
    return;
  ch = u->chanrec;
  while (ch) {
    z = ch;
    ch = ch->next;
    if (z->info != NULL)
      nfree(z->info);
    nfree(z);
  }
  u->chanrec = NULL;
  for (ue = u->entries; ue; ue = ut) {
    ut = ue->next;
    if (ue->name) {
      struct list_type *lt, *ltt;

      for (lt = ue->u.list; lt; lt = ltt) {
	ltt = lt->next;
	nfree(lt->extra);
	nfree(lt);
      }
      nfree(ue->name);
      nfree(ue);
    } else {
      ue->type->kill(ue);
    }
  }
  nfree(u);
}

int deluser(char *handle)
{
  struct userrec *u = userlist, *prev = NULL;
  int fnd = 0;

  while ((u != NULL) && (!fnd)) {
    if (!strcasecmp(u->handle, handle))
      fnd = 1;
    else {
      prev = u;
      u = u->next;
    }
  }
  if (!fnd)
    return 0;
  if (prev == NULL)
    userlist = u->next;
  else
    prev->next = u->next;
  if (!noshare && (handle[0] != '*') && !(u->flags & USER_UNSHARED))
    shareout(NULL, "k %s\n", handle);
  for (fnd = 0; fnd < dcc_total; fnd++)
    if (dcc[fnd].user == u)
      dcc[fnd].user = 0;	/* clear any dcc users for this entry,
				 * null is safe-ish */
  clear_chanlist();
  freeuser(u);
  lastuser = NULL;
  return 1;
}

int delhost_by_handle(char *handle, char *host)
{
  struct userrec *u;
  struct list_type *q, *qnext, *qprev;
  struct user_entry *e = NULL;
  int i = 0;

  context;
  u = get_user_by_handle(userlist, handle);
  if (!u)
    return 0;
  q = get_user(&USERENTRY_HOSTS, u);
  qprev = q;
  if (q) {
    if (!rfc_casecmp(q->extra, host)) {
      e = find_user_entry(&USERENTRY_HOSTS, u);
      e->u.extra = q->next;
      nfree(q->extra);
      nfree(q);
      i++;
      qprev = NULL;
      q = e->u.extra;
    } else
      q = q->next;
    while (q) {
      qnext = q->next;
      if (!rfc_casecmp(q->extra, host)) {
	if (qprev)
	  qprev->next = q->next;
	else if (e) {
	  e->u.extra = q->next;
	  qprev = NULL;
	}
	nfree(q->extra);
	nfree(q);
	i++;
      } else
        qprev = q;
      q = qnext;
    }
  }
  if (!qprev)
    set_user(&USERENTRY_HOSTS, u, "none");
  if (!noshare && i && !(u->flags & USER_UNSHARED))
    shareout(NULL, "-h %s %s\n", handle, host);
  clear_chanlist();
  return i;
}

void addhost_by_handle(char *handle, char *host)
{
  struct userrec *u = get_user_by_handle(userlist, handle);

  set_user(&USERENTRY_HOSTS, u, host);
  /* u will be cached, so really no overhead, even tho this looks dumb: */
  if ((!noshare) && !(u->flags & USER_UNSHARED)) {
    if (u->flags & USER_BOT)
      shareout(NULL, "+bh %s %s\n", handle, host);
    else
      shareout(NULL, "+h %s %s\n", handle, host);
  }
  clear_chanlist();
}

void touch_laston(struct userrec *u, char *where, time_t timeval)
{
  if (!u)
    return;
  if (timeval > 1) {
    struct laston_info *li =
    (struct laston_info *) get_user(&USERENTRY_LASTON, u);

    if (!li)
      li = nmalloc(sizeof(struct laston_info));

    else if (li->lastonplace)
      nfree(li->lastonplace);
    li->laston = timeval;
    if (where) {
      li->lastonplace = nmalloc(strlen(where) + 1);
      strcpy(li->lastonplace, where);
    } else
      li->lastonplace = NULL;
    set_user(&USERENTRY_LASTON, u, li);
  } else if (timeval == 1) {
    set_user(&USERENTRY_LASTON, u, 0);
  }
}

/*  Go through all channel records and try to find a matching
 *  nick. Will return the user's user record if that is known
 *  to the bot.  (Fabian)
 *  
 *  Warning: This is unreliable by concept!
 */
struct userrec *get_user_by_nick(char *nick)
{
  struct chanset_t *chan = chanset;
  memberlist *m;

  context;
  while (chan) {
    m = chan->channel.member;
    while (m->nick[0]) {
      if (!rfc_casecmp(nick, m->nick)) {
  	char word[512];

	sprintf(word, "%s!%s", m->nick, m->userhost);
	/* no need to check the return value ourself */
	return get_user_by_host(word);;
      }
      m = m->next;
    }
    chan = chan->next;
  }
  /* sorry, no matches */
  return NULL;
}
