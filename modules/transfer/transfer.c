/*
 * transfer.c -- part of transfer.mod
 *
 * $Id: transfer.c,v 1.1 2001/10/27 16:34:53 ite Exp $
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
/*
 * Small code snippets related to REGET/RESEND support were taken from
 * BitchX, copyright by panasync.
 */

#define MODULE_NAME "transfer"
#define MAKING_TRANSFER

/* sigh sunos */
#include <sys/types.h>
#include <sys/stat.h>
#include "lib/eggdrop/module.h"
#include "src/tandem.h"

#include "src/users.h"
#include "transfer.h"

#include <netinet/in.h>
#include <arpa/inet.h>

#define start transfer_LTX_start

static Function *global = NULL;

static int copy_to_tmp = 1;	/* Copy files to /tmp before transmitting? */
static int wait_dcc_xfer = 300;	/* Timeout time on DCC xfers */
static bind_table_t *BT_rcvd, *BT_sent, *BT_lost, *BT_tout;
static int dcc_limit = 3;	/* Maximum number of simultaneous file
				   downloads allowed */
static int dcc_block = 1024;	/* Size of one dcc block */
static int quiet_reject;        /* Quietly reject dcc chat or sends from
                                   users without access? */

static bind_table_t *BT_load;

/*
 * Prototypes
 */
static void stats_add_dnload(struct userrec *, unsigned long);
static void stats_add_upload(struct userrec *, unsigned long);
static void wipe_tmp_filename(char *, int);
static int at_limit(char *);
static void dcc_get_pending(int, char *, int);
static struct dcc_table DCC_SEND;
static struct dcc_table DCC_GET;
static struct dcc_table DCC_GET_PENDING;

static fileq_t *fileq = NULL;


/*
 *   Misc functions
 */

#undef MATCH
#define MATCH (match+sofar)

/* This function SHAMELESSLY :) pinched from match.c in the original
 * source, see that file for info about the author etc.
 */

#define QUOTE '\\'
#define WILDS '*'
#define WILDQ '?'
#define NOMATCH 0
/*
 * wild_match_file(char *ma, char *na)
 *
 * Features:  Forward, case-sensitive, ?, *
 * Best use:  File mask matching, as it is case-sensitive
 */
static int wild_match_file(register char *m, register char *n)
{
  char *ma = m, *lsm = 0, *lsn = 0;
  int match = 1;
  register unsigned int sofar = 0;

  /* Take care of null strings (should never match) */
  if ((m == 0) || (n == 0) || (!*n))
    return NOMATCH;
  /* (!*m) test used to be here, too, but I got rid of it.  After all, If
   * (!*n) was false, there must be a character in the name (the second
   * string), so if the mask is empty it is a non-match.  Since the
   * algorithm handles this correctly without testing for it here and this
   * shouldn't be called with null masks anyway, it should be a bit faster
   * this way.
   */
  while (*n) {
    /* Used to test for (!*m) here, but this scheme seems to work better */
    switch (*m) {
    case 0:
      do
	m--;			/* Search backwards      */
      while ((m > ma) && (*m == '?'));	/* For first non-? char  */
      if ((m > ma) ? ((*m == '*') && (m[-1] != QUOTE)) : (*m == '*'))
	return MATCH;		/* nonquoted * = match   */
      break;
    case WILDS:
      do
	m++;
      while (*m == WILDS);	/* Zap redundant wilds   */
      lsm = m;
      lsn = n;			/* Save * fallback spot  */
      match += sofar;
      sofar = 0;
      continue;			/* Save tally count      */
    case WILDQ:
      m++;
      n++;
      continue;			/* Match one char        */
    case QUOTE:
      m++;			/* Handle quoting        */
    }
    if (*m == *n) {		/* If matching           */
      m++;
      n++;
      sofar++;
      continue;			/* Tally the match       */
    }
    if (lsm) {			/* Try to fallback on *  */
      n = ++lsn;
      m = lsm;			/* Restore position      */
      /* Used to test for (!*n) here but it wasn't necessary so it's gone */
      sofar = 0;
      continue;			/* Next char, please     */
    }
    return NOMATCH;		/* No fallbacks=No match */
  }
  while (*m == WILDS)
    m++;			/* Zap leftover *s       */
  return (*m) ? NOMATCH : MATCH;	/* End of both = match   */
}

static void wipe_tmp_filename(char *fn, int idx)
{
  int i, ok = 1;

  if (!copy_to_tmp)
    return;
  for (i = 0; i < dcc_total; i++)
    if (i != idx)
      if (dcc[i].type == &DCC_GET || dcc[i].type == &DCC_GET_PENDING)
	if (!strcmp(dcc[i].u.xfer->filename, fn)) {
	  ok = 0;
	  break;
	}
  if (ok)
    unlink(fn);
}

/* Return true if this user has >= the maximum number of file xfers allowed.
 */
static int at_limit(char *nick)
{
  int i, x = 0;

  for (i = 0; i < dcc_total; i++)
    if (dcc[i].type == &DCC_GET || dcc[i].type == &DCC_GET_PENDING)
      if (!strcasecmp(dcc[i].nick, nick))
	x++;
  return (x >= dcc_limit);
}

/* Replaces all spaces with underscores (' ' -> '_').  The returned buffer
 * needs to be freed after use.
 */
static char *replace_spaces(char *fn)
{
  register char *ret, *p;

  malloc_strcpy(ret, fn);
  p = ret;
  while ((p = strchr(p, ' ')) != NULL)
    *p = '_';
  return ret;
}

static void check_tcl_sentrcvd(struct userrec *u, char *nick, char *path,
			       bind_table_t *table)
{
  struct flag_record fr = {FR_GLOBAL | FR_CHAN | FR_ANYWH, 0, 0, 0, 0, 0};
  char *hand = u ? u->handle : "*";	/* u might be NULL. */

  get_user_flagrec(u, &fr, NULL);
  check_bind(table, hand, &fr, u, nick, path);
}

static void check_tcl_toutlost(struct userrec *u, char *nick, char *path,
			       unsigned long acked, unsigned long length,
				bind_table_t *table)
{
  struct flag_record fr = {FR_GLOBAL | FR_CHAN | FR_ANYWH, 0, 0, 0, 0, 0};
  char *hand = u ? u->handle : "*";	/* u might be NULL. */

  get_user_flagrec(u, &fr, NULL);
  check_bind(table, hand, &fr, u, nick, path, acked, length);
}

/*
 *    File queue functions
 */

static void queue_file(char *dir, char *file, char *from, char *to)
{
  fileq_t *q = fileq;

  fileq = (fileq_t *) malloc(sizeof(fileq_t));
  fileq->next = q;
  malloc_strcpy(fileq->dir, dir);
  malloc_strcpy(fileq->file, file);
  strcpy(fileq->nick, from);
  strcpy(fileq->to, to);
}

static void deq_this(fileq_t *this)
{
  fileq_t *q = fileq, *last = NULL;

  while (q && q != this) {
    last = q;
    q = q->next;
  }
  if (!q)
    return;			/* Bogus ptr */
  if (last)
    last->next = q->next;
  else
    fileq = q->next;
  free(q->dir);
  free(q->file);
  free(q);
}

/* Remove all files queued to a certain user.
 */
static void flush_fileq(char *to)
{
  fileq_t *q = fileq;
  int fnd = 1;

  while (fnd) {
    q = fileq;
    fnd = 0;
    while (q != NULL) {
      if (!strcasecmp(q->to, to)) {
	deq_this(q);
	q = NULL;
	fnd = 1;
      }
      if (q != NULL)
	q = q->next;
    }
  }
}

static void send_next_file(char *to)
{
  fileq_t *q, *this = NULL;
  char *s, *s1;
  int x;

  for (q = fileq; q; q = q->next)
    if (!strcasecmp(q->to, to))
      this = q;
  if (this == NULL)
    return;			/* None */
  /* Copy this file to /tmp */
  if (this->dir[0] == '*') {	/* Absolute path */
    s = malloc(strlen(&this->dir[1]) + strlen(this->file) + 2);
    sprintf(s, "%s/%s", &this->dir[1], this->file);
  } else {
    char *p = strchr(this->dir, '*');

    if (p == NULL) {		/* if it's messed up */
      send_next_file(to);
      return;
    }
    p++;
    s = malloc(strlen(p) + strlen(this->file) + 2);
    sprintf(s, "%s%s%s", p, p[0] ? "/" : "", this->file);
    strcpy(this->dir, &(p[atoi(this->dir)]));
  }
  if (copy_to_tmp) {
    s1 = malloc(strlen(tempdir) + strlen(this->file) + 1);
    sprintf(s1, "%s%s", tempdir, this->file);
    if (copyfile(s, s1) != 0) {
      putlog(LOG_FILES | LOG_MISC, "*",
	     "Refused dcc get %s: copy to %s FAILED!",
	     this->file, tempdir);
      dprintf(DP_HELP,
	      "NOTICE %s :File system is broken; aborting queued files.\n",
	      this->to);
      strcpy(s, this->to);
      flush_fileq(s);
      free(s1);
      free(s);
      return;
    }
  } else
    malloc_strcpy(s1, s);
  if (this->dir[0] == '*') {
    s = realloc(s, strlen(&this->dir[1]) + strlen(this->file) + 2);
    sprintf(s, "%s/%s", &this->dir[1], this->file);
  } else {
    s = realloc(s, strlen(this->dir) + strlen(this->file) + 2);
    sprintf(s, "%s%s%s", this->dir, this->dir[0] ? "/" : "", this->file);
  }
  x = raw_dcc_send(s1, this->to, this->nick, s, 0);
  if (x == DCCSEND_OK) {
    if (strcasecmp(this->to, this->nick))
      dprintf(DP_HELP, "NOTICE %s :Here is a file from %s ...\n", this->to,
	      this->nick);
    deq_this(this);
    free(s);
    free(s1);
    return;
  }
  wipe_tmp_filename(s1, -1);
  if (x == DCCSEND_FULL) {
    putlog(LOG_FILES, "*", "DCC connections full: GET %s [%s]", s1, this->nick);
    dprintf(DP_HELP,
	    "NOTICE %s :DCC connections full; aborting queued files.\n",
	    this->to);
    strcpy(s, this->to);
    flush_fileq(s);
  } else if (x == DCCSEND_NOSOCK) {
    putlog(LOG_FILES, "*", "DCC socket error: GET %s [%s]", s1, this->nick);
    dprintf(DP_HELP, "NOTICE %s :DCC socket error; aborting queued files.\n",
	    this->to);
    strcpy(s, this->to);
    flush_fileq(s);
  } else {
    if (x == DCCSEND_FEMPTY) {
      putlog(LOG_FILES, "*", "Aborted dcc get %s: File is empty!", this->file);
      dprintf(DP_HELP, "NOTICE %s :File %s is empty, aborting transfer.\n",
	      this->to, this->file);
    }
    deq_this(this);
  }
  free(s);
  free(s1);
  return;
}

static void show_queued_files(int idx)
{
  int i, cnt = 0, len;
  char spaces[] = "                                 ";
  fileq_t *q;

  for (q = fileq; q; q = q->next) {
    if (!strcasecmp(q->nick, dcc[idx].nick)) {
      if (!cnt) {
	spaces[HANDLEN - 9] = 0;
	dprintf(idx, "  Send to  %s  Filename\n", spaces);
	dprintf(idx, "  ---------%s  --------------------\n", spaces);
	spaces[HANDLEN - 9] = ' ';
      }
      cnt++;
      spaces[len = HANDLEN - strlen(q->to)] = 0;
      if (q->dir[0] == '*')
	dprintf(idx, "  %s%s  %s/%s\n", q->to, spaces, &q->dir[1],
		q->file);
      else
	dprintf(idx, "  %s%s  /%s%s%s\n", q->to, spaces, q->dir,
		q->dir[0] ? "/" : "", q->file);
      spaces[len] = ' ';
    }
  }
  for (i = 0; i < dcc_total; i++) {
    if ((dcc[i].type == &DCC_GET_PENDING || dcc[i].type == &DCC_GET) &&
	(!strcasecmp(dcc[i].nick, dcc[idx].nick) ||
	 !strcasecmp(dcc[i].u.xfer->from, dcc[idx].nick))) {
      char *nfn;

      if (!cnt) {
	spaces[HANDLEN - 9] = 0;
	dprintf(idx, "  Send to  %s  Filename\n", spaces);
	dprintf(idx, "  ---------%s  --------------------\n", spaces);
	spaces[HANDLEN - 9] = ' ';
      }
      nfn = strrchr(dcc[i].u.xfer->origname, '/');
      if (nfn == NULL)
	nfn = dcc[i].u.xfer->origname;
      else
	nfn++;
      cnt++;
      spaces[len = HANDLEN - strlen(dcc[i].nick)] = 0;
      if (dcc[i].type == &DCC_GET_PENDING)
	dprintf(idx, "  %s%s  %s  [WAITING]\n", dcc[i].nick, spaces,
		nfn);
      else
	dprintf(idx, "  %s%s  %s  (%.1f%% done)\n", dcc[i].nick, spaces,
		nfn, (100.0 * ((float) dcc[i].status /
			       (float) dcc[i].u.xfer->length)));
      spaces[len] = ' ';
    }
  }
  if (!cnt)
    dprintf(idx, "No files queued up.\n");
  else
    dprintf(idx, "Total: %d\n", cnt);
}

static void fileq_cancel(int idx, char *par)
{
  int fnd = 1, matches = 0, atot = 0, i;
  fileq_t *q;
  char *s = NULL;

  while (fnd) {
    q = fileq;
    fnd = 0;
    while (q != NULL) {
      if (!strcasecmp(dcc[idx].nick, q->nick)) {
	s = realloc(s, strlen(q->dir) + strlen(q->file) + 3);
	if (q->dir[0] == '*')
	  sprintf(s, "%s/%s", &q->dir[1], q->file);
	else
	  sprintf(s, "/%s%s%s", q->dir, q->dir[0] ? "/" : "", q->file);
	if (wild_match_file(par, s)) {
	  dprintf(idx, "Cancelled: %s to %s\n", s, q->to);
	  fnd = 1;
	  deq_this(q);
	  q = NULL;
	  matches++;
	}
	if (!fnd && wild_match_file(par, q->file)) {
	  dprintf(idx, "Cancelled: %s to %s\n", s, q->to);
	  fnd = 1;
	  deq_this(q);
	  q = NULL;
	  matches++;
	}
      }
      if (q != NULL)
	q = q->next;
    }
  }
  if (s)
    free(s);
  for (i = 0; i < dcc_total; i++) {
    if ((dcc[i].type == &DCC_GET_PENDING || dcc[i].type == &DCC_GET) &&
	(!strcasecmp(dcc[i].nick, dcc[idx].nick) ||
	 !strcasecmp(dcc[i].u.xfer->from, dcc[idx].nick))) {
      char *nfn = strrchr(dcc[i].u.xfer->origname, '/');

      if (nfn == NULL)
	nfn = dcc[i].u.xfer->origname;
      else
	nfn++;
      if (wild_match_file(par, nfn)) {
	dprintf(idx, "Cancelled: %s  (aborted dcc send)\n", nfn);
	if (strcasecmp(dcc[i].nick, dcc[idx].nick))
	  dprintf(DP_HELP, "NOTICE %s :Transfer of %s aborted by %s\n",
		  dcc[i].nick, nfn, dcc[idx].nick);
	if (dcc[i].type == &DCC_GET)
	  putlog(LOG_FILES, "*", "DCC cancel: GET %s (%s) at %lu/%lu", nfn,
		 dcc[i].nick, dcc[i].status, dcc[i].u.xfer->length);
	wipe_tmp_filename(dcc[i].u.xfer->filename, i);
	atot++;
	matches++;
	killsock(dcc[i].sock);
	lostdcc(i);
      }
    }
  }
  if (!matches)
    dprintf(idx, "No matches.\n");
  else
    dprintf(idx, "Cancelled %d file%s.\n", matches, matches > 1 ? "s" : "");
  for (i = 0; i < atot; i++)
    if (!at_limit(dcc[idx].nick))
      send_next_file(dcc[idx].nick);
}

static int tcl_getfileq STDVAR
{
  char *s = NULL;
  fileq_t *q;

  BADARGS(2, 2, " handle");
  for (q = fileq; q; q = q->next) {
    if (!strcasecmp(q->nick, argv[1])) {
      s = realloc(s, strlen(q->to) + strlen(q->dir) + strlen(q->file) + 4);
      if (q->dir[0] == '*')
	sprintf(s, "%s %s/%s", q->to, &q->dir[1], q->file);
      else
	sprintf(s, "%s /%s%s%s", q->to, q->dir, q->dir[0] ? "/" : "", q->file);
      Tcl_AppendElement(irp, s);
    }
  }
  if (s)
    free(s);
  return TCL_OK;
}


/*
 *    Misc Tcl functions
 */

static int tcl_dccsend STDVAR
{
  char s[10], *sys, *nfn;
  int i;
  FILE *f;

  BADARGS(3, 3, " filename ircnick");
  f = fopen(argv[1], "r");
  if (f == NULL) {
    /* File not found */
    Tcl_AppendResult(irp, "3", NULL);
    return TCL_OK;
  }
  fclose(f);
  nfn = strrchr(argv[1], '/');
  if (nfn == NULL)
    nfn = argv[1];
  else
    nfn++;
  if (at_limit(argv[2])) {
    /* Queue that mother */
    if (nfn == argv[1])
      queue_file("*", nfn, "(script)", argv[2]);
    else {
      nfn--;
      *nfn = 0;
      nfn++;
      sys = malloc(strlen(argv[1]) + 2);
      sprintf(sys, "*%s", argv[1]);
      queue_file(sys, nfn, "(script)", argv[2]);
      free(sys);
    }
    Tcl_AppendResult(irp, "4", NULL);
    return TCL_OK;
  }
  if (copy_to_tmp) {
    sys = malloc(strlen(tempdir) + strlen(nfn) + 1);
    sprintf(sys, "%s%s", tempdir, nfn);		/* New filename, in /tmp */
    copyfile(argv[1], sys);
  } else
    malloc_strcpy(sys, argv[1]);
  i = raw_dcc_send(sys, argv[2], "*", argv[1], 0);
  if (i > 0)
    wipe_tmp_filename(sys, -1);
  snprintf(s, sizeof s, "%d", i);
  Tcl_AppendResult(irp, s, NULL);
  free(sys);
  return TCL_OK;
}

static int tcl_getfilesendtime STDVAR
{
  int	sock, i;
  char	s[15];

  BADARGS(2, 2, " idx");
  /* Btw, what the tcl interface refers to as `idx' is the socket number
     for the C part. */
  sock = atoi(argv[1]);

  for (i = 0; i < dcc_total; i++)
    if (dcc[i].sock == sock) {
      if (dcc[i].type == &DCC_SEND || dcc[i].type == &DCC_GET) {
	snprintf(s, sizeof s, "%lu", dcc[i].u.xfer->start_time);
	Tcl_AppendResult(irp, s, NULL);
      } else
	Tcl_AppendResult(irp, "-2", NULL);  /* Not a valid file transfer,
					       honey. */
      return TCL_OK;
    }
  Tcl_AppendResult(irp, "-1", NULL);	/* No matching entry found.	*/
  return TCL_OK;
}

static tcl_cmds mytcls[] =
{
  {"dccsend",		tcl_dccsend},
  {"getfileq",		tcl_getfileq},
  {"getfilesendtime", 	tcl_getfilesendtime},
  {NULL,		NULL}
};


/*
 *    DCC routines
 */

/* Instead of reading all data intended to go into the DCC block
 * in one go, we read it in PMAX_SIZE chunks, feed it to tputs and
 * continue until we get to know that the network buffer only
 * buffers the data instead of sending it.
 *
 * In that case, we delay further sending until we receive the
 * dcc outdone event.
 *
 * Note: To optimise buffer sizes, we default to PMAX_SIZE, but
 *       allocate a smaller buffer for smaller pending_data sizes.
 */
#define	PMAX_SIZE	4096
static unsigned long pump_file_to_sock(FILE *file, long sock,
				       register unsigned long pending_data)
{
  const unsigned long		 buf_len = pending_data >= PMAX_SIZE ?
	  					PMAX_SIZE : pending_data;
  char				*bf = malloc(buf_len);
  register unsigned long	 actual_size;

  if (bf) {
    do {
      actual_size = pending_data >= buf_len ? buf_len : pending_data;
      fread(bf, actual_size, 1, file);
      tputs(sock, bf, actual_size);
      pending_data -= actual_size;
    } while (!sock_has_data(SOCK_DATA_OUTGOING, sock) && pending_data != 0);
    free(bf);
  }
  return pending_data;
}

static void eof_dcc_fork_send(int idx)
{
  char s1[121];
  char *s2;

  fclose(dcc[idx].u.xfer->f);
  if (!strcmp(dcc[idx].nick, "*users")) {
    int x, y = 0;

    for (x = 0; x < dcc_total; x++)
      if ((!strcasecmp(dcc[x].nick, dcc[idx].host)) &&
	  (dcc[x].type->flags & DCT_BOT)) {
	y = x;
	break;
      }
    if (y != 0) {
      dcc[y].status &= ~STAT_GETTING;
      dcc[y].status &= ~STAT_SHARE;
      debug0("(!) Could not find bot responsible for sending us the userfile "
	     "for which the transfer failed.");
    }
    putlog(LOG_BOTS, "*", _("Failed connection; aborted userfile transfer."));
    unlink(dcc[idx].u.xfer->filename);
  } else {
    neterror(s1);
    if (!quiet_reject)
      dprintf(DP_HELP, "NOTICE %s :%s (%s)\n", dcc[idx].nick,
	      _("Failed to connect"), s1);
    putlog(LOG_MISC, "*", "%s: SEND %s (%s!%s)", _("DCC connection failed"),
	   dcc[idx].u.xfer->origname, dcc[idx].nick, dcc[idx].host);
    putlog(LOG_MISC, "*", "    (%s)", s1);
    s2 = malloc(strlen(tempdir) + strlen(dcc[idx].u.xfer->filename) + 1);
    sprintf(s2, "%s%s", tempdir, dcc[idx].u.xfer->filename);
    unlink(s2);
    free(s2);
  }
  killsock(dcc[idx].sock);
  lostdcc(idx);
}

static void eof_dcc_send(int idx)
{
  int ok, j;
  char *ofn, *nfn, s[1024], *hand;
  struct userrec *u;

  fclose(dcc[idx].u.xfer->f);
  if (dcc[idx].u.xfer->length == dcc[idx].status) {
    int l;

    /* Success */
    ok = 0;
    if (!strcmp(dcc[idx].nick, "*users")) {
      module_entry *me = module_find("share", 0, 0);

      if (me && me->funcs) {
	Function f = me->funcs[SHARE_FINISH];

	(f) (idx);
      }
      killsock(dcc[idx].sock);
      lostdcc(idx);
      return;
    }
    putlog(LOG_FILES, "*", "Completed dcc send %s from %s!%s",
	   dcc[idx].u.xfer->origname, dcc[idx].nick, dcc[idx].host);
    snprintf(s, sizeof s, "%s!%s", dcc[idx].nick, dcc[idx].host);
    u = get_user_by_host(s);
    hand = u ? u->handle : "*";

    l = strlen(dcc[idx].u.xfer->filename);
    if (l > NAME_MAX) {
      /* The filename is to long... blow it off */
      putlog(LOG_FILES, "*", "Filename %d length. Way To LONG.", l);
      dprintf(DP_HELP, "NOTICE %s :Filename %d length Way To LONG!\n",
              dcc[idx].nick, l);
      putlog(LOG_FILES, "*", "To Bad So Sad Your Dad!");
      dprintf(DP_HELP, "NOTICE %s :To Bad So Sad Your Dad!\n",
              dcc[idx].nick);
      killsock(dcc[idx].sock);
      lostdcc(idx);
      return;
    }
    /* Move the file from /tmp */
    ofn = malloc(strlen(tempdir) + strlen(dcc[idx].u.xfer->filename) + 1);
    nfn = malloc(strlen(dcc[idx].u.xfer->dir)
		  + strlen(dcc[idx].u.xfer->origname) + 1);
    sprintf(ofn, "%s%s", tempdir, dcc[idx].u.xfer->filename);
    sprintf(nfn, "%s%s", dcc[idx].u.xfer->dir, dcc[idx].u.xfer->origname);
    if (movefile(ofn, nfn))
      putlog(LOG_MISC | LOG_FILES, "*",
	     "FAILED move `%s' from `%s'! File lost!", nfn, ofn);
    else {
      /* Add to file database */
      module_entry *fs = module_find("filesys", 0, 0);

      if (fs != NULL) {
	Function f = fs->funcs[FILESYS_ADDFILE];

	f(dcc[idx].u.xfer->dir, dcc[idx].u.xfer->origname, hand);
      }
      stats_add_upload(u, dcc[idx].u.xfer->length);
      check_tcl_sentrcvd(u, dcc[idx].nick, nfn, BT_rcvd);
    }
    free(ofn);
    free(nfn);
    for (j = 0; j < dcc_total; j++)
      if (!ok && (dcc[j].type->flags & (DCT_GETNOTES | DCT_FILES)) &&
	  !strcasecmp(dcc[j].nick, hand)) {
	ok = 1;
	dprintf(j, "Thanks for the file!\n");
      }
    if (!ok)
      dprintf(DP_HELP, "NOTICE %s :Thanks for the file!\n",
	      dcc[idx].nick);
    killsock(dcc[idx].sock);
    lostdcc(idx);
    return;
  }
  /* Failure :( */
  if (!strcmp(dcc[idx].nick, "*users")) {
    int x, y = 0;

    for (x = 0; x < dcc_total; x++)
      if ((!strcasecmp(dcc[x].nick, dcc[idx].host)) &&
	  (dcc[x].type->flags & DCT_BOT))
	y = x;
    if (y) {
      putlog(LOG_BOTS, "*", "Lost userfile transfer from %s; aborting.",
	     dcc[y].nick);
      unlink(dcc[idx].u.xfer->filename);
      /* Drop that bot */
      dprintf(y, "bye\n");
      snprintf(s, sizeof s, "Disconnected %s (aborted userfile transfer)",
		   dcc[y].nick);
      botnet_send_unlinked(y, dcc[y].nick, s);
      chatout("*** %s\n", dcc[y].nick, s);
      if (y < idx) {
	int t = y;

	y = idx;
	idx = t;
      }
      killsock(dcc[y].sock);
      lostdcc(y);
    }
  } else {
    putlog(LOG_FILES, "*", "Lost dcc send %s from %s!%s (%lu/%lu)",
	   dcc[idx].u.xfer->origname, dcc[idx].nick, dcc[idx].host,
	   dcc[idx].status, dcc[idx].u.xfer->length);
    ofn = malloc(strlen(tempdir) + strlen(dcc[idx].u.xfer->filename) + 1);
    sprintf(ofn, "%s%s", tempdir, dcc[idx].u.xfer->filename);
    unlink(ofn);
    free(ofn);
    killsock(dcc[idx].sock);
    lostdcc(idx);
  }
}

/* Determine byte order. Used for resend DCC startup packets.
 */
static inline u_8bit_t byte_order_test(void)
{
  u_16bit_t test = TRANSFER_REGET_PACKETID;

  if (*((u_8bit_t *)&test) == ((TRANSFER_REGET_PACKETID & 0xff00) >> 8))
    return 0;
  if (*((u_8bit_t *)&test) == (TRANSFER_REGET_PACKETID & 0x00ff))
    return 1;
  return 0;
}

/* Parse and handle resend DCC startup packets.
 */
inline static void handle_resend_packet(int idx, transfer_reget *reget_data)
{
  if (byte_order_test() != reget_data->byte_order) {
    /* The sender's byte order does not match our's so we need to switch the
     * bytes first, before we can make use of them.
     */
    reget_data->packet_id = ((reget_data->packet_id & 0x00ff) << 8) |
	    		    ((reget_data->packet_id & 0xff00) >> 8);
    reget_data->byte_offset = ((reget_data->byte_offset & 0xff000000) >> 24) |
	   		      ((reget_data->byte_offset & 0x00ff0000) >> 8)  |
			      ((reget_data->byte_offset & 0x0000ff00) << 8)  |
			      ((reget_data->byte_offset & 0x000000ff) << 24);
  }
  if (reget_data->packet_id != TRANSFER_REGET_PACKETID)
    putlog(LOG_FILES, "*", "(!) reget packet from %s for %s is invalid!",
	   dcc[idx].nick, dcc[idx].u.xfer->origname);
  else
    dcc[idx].u.xfer->offset = reget_data->byte_offset;
  dcc[idx].u.xfer->type = XFER_RESEND;
}

/* Handles DCC packets the client sends us. As soon as the last sent dcc
 * block is fully acknowledged we send the next block.
 *
 * Note: The first received packet during reget is a special 8 bit packet
 *       containing special information.
 */
static void dcc_get(int idx, char *buf, int len)
{
  char xnick[NICKLEN];
  unsigned char bbuf[4];
  unsigned long cmp, l;
  int w = len + dcc[idx].u.xfer->sofar, p = 0;

  dcc[idx].timeval = now;		/* Mark as active		*/

  /* Add bytes to our buffer if we don't have a complete response yet.
   * This is either a 4 bit ack or the 8 bit reget packet.
   */
  if (w < 4 ||
      (w < 8 && dcc[idx].u.xfer->type == XFER_RESEND_PEND)) {
    memcpy(&(dcc[idx].u.xfer->buf[dcc[idx].u.xfer->sofar]), buf, len);
    dcc[idx].u.xfer->sofar += len;
    return;
  /* Waiting for the 8 bit reget packet? */
  } else if (dcc[idx].u.xfer->type == XFER_RESEND_PEND) {
    /* The 8 bit packet is complete now. Parse it. */
    if (w == 8) {
      transfer_reget reget_data;

      memcpy(&reget_data, dcc[idx].u.xfer->buf, dcc[idx].u.xfer->sofar);
      memcpy(&reget_data + dcc[idx].u.xfer->sofar, buf, len);
      handle_resend_packet(idx, &reget_data);
      cmp = dcc[idx].u.xfer->offset;
    } else
      return;
    /* Fall through! */
  /* No, only want 4 bit ack responses. */
  } else {
    /* Complete packet? */
    if (w == 4) {
      memcpy(bbuf, dcc[idx].u.xfer->buf, dcc[idx].u.xfer->sofar);
      memcpy(&(bbuf[dcc[idx].u.xfer->sofar]), buf, len);
    } else {
      p = ((w - 1) & ~3) - dcc[idx].u.xfer->sofar;
      w = w - ((w - 1) & ~3);
      if (w < 4) {
	memcpy(dcc[idx].u.xfer->buf, &(buf[p]), w);
	return;
      }
      memcpy(bbuf, &(buf[p]), w);
    }
    /* This is more compatible than ntohl for machines where an int
     * is more than 4 bytes:
     */
    cmp = ((unsigned int) (bbuf[0]) << 24) +
	  ((unsigned int) (bbuf[1]) << 16) +
	  ((unsigned int) (bbuf[2]) << 8) + bbuf[3];
    dcc[idx].u.xfer->acked = cmp;
  }

  dcc[idx].u.xfer->sofar = 0;
  if (cmp > dcc[idx].u.xfer->length && cmp > dcc[idx].status) {
    /* Attempt to resume, but file is not as long as requested... */
    putlog(LOG_FILES, "*",
	   "!! Resuming file transfer behind file end for %s to %s",
	   dcc[idx].u.xfer->origname, dcc[idx].nick);
  } else if (cmp > dcc[idx].status) {
    /* Attempt to resume */
    if (!strcmp(dcc[idx].nick, "*users"))
      putlog(LOG_BOTS, "*", "!!! Trying to skip ahead on userfile transfer");
    else {
      fseek(dcc[idx].u.xfer->f, cmp, SEEK_SET);
      dcc[idx].status = cmp;
      putlog(LOG_FILES, "*", "Resuming file transfer at %dk for %s to %s",
	     (int) (cmp / 1024), dcc[idx].u.xfer->origname,
	     dcc[idx].nick);
    }
  } else {
    if (dcc[idx].u.xfer->ack_type == XFER_ACK_UNKNOWN) {
      if (cmp < dcc[idx].u.xfer->offset)
	/* If we don't start at the top of the file, some clients only tell
	 * us the really received bytes (e.g. bitchx). This seems to be the
	 * case here.
	 */
	dcc[idx].u.xfer->ack_type = XFER_ACK_WITHOUT_OFFSET;
      else
	dcc[idx].u.xfer->ack_type = XFER_ACK_WITH_OFFSET;
    }
    if (dcc[idx].u.xfer->ack_type == XFER_ACK_WITHOUT_OFFSET)
      cmp += dcc[idx].u.xfer->offset;
  }

  if (cmp != dcc[idx].status)
    return;
  if (dcc[idx].status == dcc[idx].u.xfer->length) {
    /* Successful send, we are done */
    killsock(dcc[idx].sock);
    fclose(dcc[idx].u.xfer->f);
    if (!strcmp(dcc[idx].nick, "*users")) {
      module_entry *me = module_find("share", 0, 0);
      int x, y = 0;

      for (x = 0; x < dcc_total; x++)
	if (!strcasecmp(dcc[x].nick, dcc[idx].host) &&
	    (dcc[x].type->flags & DCT_BOT))
	  y = x;
      if (y != 0)
	dcc[y].status &= ~STAT_SENDING;
      putlog(LOG_BOTS, "*", "Completed userfile transfer to %s.",
	     dcc[y].nick);
      unlink(dcc[idx].u.xfer->filename);
      /* Any sharebot things that were queued: */
      if (me && me->funcs[SHARE_DUMP_RESYNC])
	((me->funcs)[SHARE_DUMP_RESYNC]) (y);
      xnick[0] = 0;
    } else {
      module_entry *fs = module_find("filesys", 0, 0);
      struct userrec *u = get_user_by_handle(userlist,
					     dcc[idx].u.xfer->from);
      check_tcl_sentrcvd(u, dcc[idx].nick,
			 dcc[idx].u.xfer->dir, BT_sent);
      if (fs != NULL) {
	Function f = fs->funcs[FILESYS_INCRGOTS];

	f(dcc[idx].u.xfer->dir);
      }
      /* Download is credited to the user who requested it
       * (not the user who actually received it)
       */
      stats_add_dnload(u, dcc[idx].u.xfer->length);
      putlog(LOG_FILES, "*", "Finished dcc send %s to %s",
	     dcc[idx].u.xfer->origname, dcc[idx].nick);
      wipe_tmp_filename(dcc[idx].u.xfer->filename, idx);
      strcpy((char *) xnick, dcc[idx].nick);
    }
    lostdcc(idx);
    /* Any to dequeue? */
    if (!at_limit(xnick))
      send_next_file(xnick);
    return;
  }
  /* Note:  No fseek() needed here, because the file position is kept from
   *        the last run.
   */
  l = dcc_block;
  if (l == 0 || dcc[idx].status + l > dcc[idx].u.xfer->length)
    l = dcc[idx].u.xfer->length - dcc[idx].status;
  dcc[idx].u.xfer->block_pending = pump_file_to_sock(dcc[idx].u.xfer->f,
						     dcc[idx].sock, l);
  dcc[idx].status += l;
}

static void eof_dcc_get(int idx)
{
  char xnick[NICKLEN], s[1024];

  fclose(dcc[idx].u.xfer->f);
  if (!strcmp(dcc[idx].nick, "*users")) {
    int x, y = 0;

    for (x = 0; x < dcc_total; x++)
      if (!strcasecmp(dcc[x].nick, dcc[idx].host) &&
	  (dcc[x].type->flags & DCT_BOT))
	y = x;
    putlog(LOG_BOTS, "*", "Lost userfile transfer; aborting.");
    /* Note: no need to unlink the xfer file, as it's already unlinked. */
    xnick[0] = 0;
    /* Drop that bot */
    dprintf(-dcc[y].sock, "bye\n");
    snprintf(s, sizeof s, "Disconnected %s (aborted userfile transfer)",
		 dcc[y].nick);
    botnet_send_unlinked(y, dcc[y].nick, s);
    chatout("*** %s\n", s);
    if (y < idx) {
      int t = y;

      y = idx;
      idx = t;
    }
    killsock(dcc[y].sock);
    lostdcc(y);
    return;
  } else {
    struct userrec *u;

    /* Call `lost' DCC trigger now.
     */
    snprintf(s, sizeof s, "%s!%s", dcc[idx].nick, dcc[idx].host);
    u = get_user_by_host(s);
    check_tcl_toutlost(u, dcc[idx].nick, dcc[idx].u.xfer->dir,
		       dcc[idx].u.xfer->acked, dcc[idx].u.xfer->length,
		       BT_lost);

    putlog(LOG_FILES, "*", "Lost dcc get %s from %s!%s",
	   dcc[idx].u.xfer->origname, dcc[idx].nick, dcc[idx].host);
    wipe_tmp_filename(dcc[idx].u.xfer->filename, idx);
    strcpy(xnick, dcc[idx].nick);
  }
  killsock(dcc[idx].sock);
  lostdcc(idx);
  /* Send next queued file if there is one */
  if (!at_limit(xnick))
    send_next_file(xnick);
}

static void dcc_send(int idx, char *buf, int len)
{
  char s[512], *b;
  unsigned long sent;

  fwrite(buf, len, 1, dcc[idx].u.xfer->f);
  dcc[idx].status += len;
  /* Put in network byte order */
  sent = dcc[idx].status;
  s[0] = (sent / (1 << 24));
  s[1] = (sent % (1 << 24)) / (1 << 16);
  s[2] = (sent % (1 << 16)) / (1 << 8);
  s[3] = (sent % (1 << 8));
  tputs(dcc[idx].sock, s, 4);
  dcc[idx].timeval = now;
  if (dcc[idx].status > dcc[idx].u.xfer->length &&
      dcc[idx].u.xfer->length > 0) {
    dprintf(DP_HELP, "NOTICE %s :Bogus file length.\n", dcc[idx].nick);
    putlog(LOG_FILES, "*",
	   "File too long: dropping dcc send %s from %s!%s",
	   dcc[idx].u.xfer->origname, dcc[idx].nick, dcc[idx].host);
    fclose(dcc[idx].u.xfer->f);
    b = malloc(strlen(tempdir) + strlen(dcc[idx].u.xfer->filename) + 1);
    sprintf(b, "%s%s", tempdir, dcc[idx].u.xfer->filename);
    unlink(b);
    free(b);
    killsock(dcc[idx].sock);
    lostdcc(idx);
  }
}

static void transfer_get_timeout(int i)
{
  char xx[1024];

  fclose(dcc[i].u.xfer->f);
  if (strcmp(dcc[i].nick, "*users") == 0) {
    int x, y = 0;

    for (x = 0; x < dcc_total; x++)
      if ((!strcasecmp(dcc[x].nick, dcc[i].host)) &&
	  (dcc[x].type->flags & DCT_BOT))
	y = x;
    if (y != 0) {
      dcc[y].status &= ~STAT_SENDING;
      dcc[y].status &= ~STAT_SHARE;
    }
    unlink(dcc[i].u.xfer->filename);
    putlog(LOG_BOTS, "*", "Timeout on userfile transfer.");
    dprintf(y, "bye\n");
    snprintf(xx, sizeof xx, "Disconnected %s (timed-out userfile transfer)",
		 dcc[y].nick);
    botnet_send_unlinked(y, dcc[y].nick, xx);
    chatout("*** %s\n", xx);
    if (y < i) {
      int t = y;

      y = i;
      i = t;
    }
    killsock(dcc[y].sock);
    lostdcc(y);
    xx[0] = 0;
  } else {
    char *p;
    struct userrec *u;

    p = strrchr(dcc[i].u.xfer->origname, '/');
    dprintf(DP_HELP, "NOTICE %s :Timeout during transfer, aborting %s.\n",
	    dcc[i].nick, p ? p + 1 : dcc[i].u.xfer->origname);

    /* Call DCC `timeout' trigger now.
     */
    snprintf(xx, sizeof xx, "%s!%s", dcc[i].nick, dcc[i].host);
    u = get_user_by_host(xx);
    check_tcl_toutlost(u, dcc[i].nick, dcc[i].u.xfer->dir,
		       dcc[i].u.xfer->acked, dcc[i].u.xfer->length, BT_tout);

    putlog(LOG_FILES, "*", "DCC timeout: GET %s (%s) at %lu/%lu",
	   p ? p + 1 : dcc[i].u.xfer->origname, dcc[i].nick, dcc[i].status,
	   dcc[i].u.xfer->length);
    wipe_tmp_filename(dcc[i].u.xfer->filename, i);
    strcpy(xx, dcc[i].nick);
  }
  killsock(dcc[i].sock);
  lostdcc(i);
  if (!at_limit(xx))
    send_next_file(xx);
}

static void tout_dcc_send(int idx)
{
  if (!strcmp(dcc[idx].nick, "*users")) {
    int x, y = 0;

    for (x = 0; x < dcc_total; x++)
      if (!strcasecmp(dcc[x].nick, dcc[idx].host) &&
	  (dcc[x].type->flags & DCT_BOT))
	y = x;
    if (y != 0) {
      dcc[y].status &= ~STAT_GETTING;
      dcc[y].status &= ~STAT_SHARE;
    }
    unlink(dcc[idx].u.xfer->filename);
    putlog(LOG_BOTS, "*", "Timeout on userfile transfer.");
  } else {
    char *buf;

    dprintf(DP_HELP, "NOTICE %s :Timeout during transfer, aborting %s.\n",
	    dcc[idx].nick, dcc[idx].u.xfer->origname);
    putlog(LOG_FILES, "*", "DCC timeout: SEND %s (%s) at %lu/%lu",
	   dcc[idx].u.xfer->origname, dcc[idx].nick, dcc[idx].status,
	   dcc[idx].u.xfer->length);
    buf = malloc(strlen(tempdir) + strlen(dcc[idx].u.xfer->filename) + 1);
    sprintf(buf, "%s%s", tempdir, dcc[idx].u.xfer->filename);
    unlink(buf);
    free(buf);
  }
  killsock(dcc[idx].sock);
  lostdcc(idx);
}

static void display_dcc_get(int idx, char *buf)
{
  if (dcc[idx].status == dcc[idx].u.xfer->length)
    sprintf(buf, "send  (%lu)/%lu\n    Filename: %s\n", dcc[idx].u.xfer->acked,
	    dcc[idx].u.xfer->length, dcc[idx].u.xfer->origname);
  else
    sprintf(buf, "send  %lu/%lu\n    Filename: %s\n", dcc[idx].status,
	    dcc[idx].u.xfer->length, dcc[idx].u.xfer->origname);
}

static void display_dcc_get_p(int idx, char *buf)
{
  sprintf(buf, "send  waited %lus\n    Filename: %s\n", now - dcc[idx].timeval,
	  dcc[idx].u.xfer->origname);
}

static void display_dcc_send(int idx, char *buf)
{
  sprintf(buf, "send  %lu/%lu\n    Filename: %s\n", dcc[idx].status,
	  dcc[idx].u.xfer->length, dcc[idx].u.xfer->origname);
}

static void display_dcc_fork_send(int idx, char *buf)
{
  sprintf(buf, "conn  send");
}

static void kill_dcc_xfer(int idx, void *x)
{
  register struct xfer_info *p = (struct xfer_info *) x;

  if (p->filename)
    free(p->filename);
  /* We need to check if origname points to filename before
   * attempting to free the memory.
   */
  if (p->origname && p->origname != p->filename)
    free(p->origname);
  free(x);
}

static void out_dcc_xfer(int idx, char *buf, void *x)
{
}

static void outdone_dcc_xfer(int idx)
{
  if (dcc[idx].u.xfer->block_pending)
    dcc[idx].u.xfer->block_pending =
	    pump_file_to_sock(dcc[idx].u.xfer->f, dcc[idx].sock,
			      dcc[idx].u.xfer->block_pending);
}

static struct dcc_table DCC_SEND =
{
  "SEND",
  DCT_FILETRAN | DCT_FILESEND | DCT_VALIDIDX,
  eof_dcc_send,
  dcc_send,
  &wait_dcc_xfer,
  tout_dcc_send,
  display_dcc_send,
  kill_dcc_xfer,
  out_dcc_xfer
};

static void dcc_fork_send(int idx, char *x, int y);

static struct dcc_table DCC_FORK_SEND =
{
  "FORK_SEND",
  DCT_FILETRAN | DCT_FORKTYPE | DCT_FILESEND | DCT_VALIDIDX,
  eof_dcc_fork_send,
  dcc_fork_send,
  &wait_dcc_xfer,
  eof_dcc_fork_send,
  display_dcc_fork_send,
  kill_dcc_xfer,
  out_dcc_xfer
};

static void dcc_fork_send(int idx, char *x, int y)
{
  char s1[121];

  if (dcc[idx].type != &DCC_FORK_SEND)
    return;
  dcc[idx].type = &DCC_SEND;
  dcc[idx].u.xfer->start_time = now;
  snprintf(s1, sizeof s1, "%s!%s", dcc[idx].nick, dcc[idx].host);
  if (strcmp(dcc[idx].nick, "*users"))
    putlog(LOG_MISC, "*", "DCC connection: SEND %s (%s)",
	   dcc[idx].u.xfer->origname, s1);
}

static struct dcc_table DCC_GET =
{
  "GET",
  DCT_FILETRAN | DCT_VALIDIDX,
  eof_dcc_get,
  dcc_get,
  &wait_dcc_xfer,
  transfer_get_timeout,
  display_dcc_get,
  kill_dcc_xfer,
  out_dcc_xfer,
  outdone_dcc_xfer
};

static struct dcc_table DCC_GET_PENDING =
{
  "GET_PENDING",
  DCT_FILETRAN | DCT_VALIDIDX,
  eof_dcc_get,
  dcc_get_pending,
  &wait_dcc_xfer,
  transfer_get_timeout,
  display_dcc_get_p,
  kill_dcc_xfer,
  out_dcc_xfer
};

static void dcc_get_pending(int idx, char *buf, int len)
{
  char ip[ADDRLEN];
  unsigned short port;
  int i;
  char s[UHOSTLEN];

  i = answer(dcc[idx].sock, s, ip, &port, 1);
  killsock(dcc[idx].sock);
  dcc[idx].sock = i;
  strcpy(dcc[idx].addr, ip);
  dcc[idx].port = (int) port;
  if (dcc[idx].sock == -1) {
    neterror(s);
    dprintf(DP_HELP, "NOTICE %s :Bad connection (%s)\n", dcc[idx].nick, s);
    putlog(LOG_FILES, "*", "DCC bad connection: GET %s (%s!%s)",
	   dcc[idx].u.xfer->origname, dcc[idx].nick, dcc[idx].host);
    fclose(dcc[idx].u.xfer->f);
    lostdcc(idx);
    return;
  }
  dcc[idx].type = &DCC_GET;
  dcc[idx].u.xfer->ack_type = XFER_ACK_UNKNOWN;

  /*
   * Note: The file was already opened and dcc[idx].u.xfer->f may be
   *       used immediately. Leave it opened until the file transfer
   *       is complete.
   */

  /* Are we resuming? */
  if (dcc[idx].u.xfer->type == XFER_RESUME_PEND) {
    int l;

    if (dcc_block == 0 || dcc[idx].u.xfer->length < dcc_block) {
      l = dcc[idx].u.xfer->length - dcc[idx].u.xfer->offset;
      dcc[idx].status = dcc[idx].u.xfer->length;
    } else {
      l = dcc_block;
      dcc[idx].status = dcc[idx].u.xfer->offset + dcc_block;
    }

    /* Seek forward ... */
    fseek(dcc[idx].u.xfer->f, dcc[idx].u.xfer->offset, SEEK_SET);
    dcc[idx].u.xfer->block_pending = pump_file_to_sock(dcc[idx].u.xfer->f,
						       dcc[idx].sock, l);
    dcc[idx].u.xfer->type = XFER_RESUME;
  } else {
    dcc[idx].u.xfer->offset = 0;

    /* If we're resending the data, wait for the client's response first,
     * before sending anything ourself.
     */
    if (dcc[idx].u.xfer->type != XFER_RESEND_PEND) {
      if (dcc_block == 0 || dcc[idx].u.xfer->length < dcc_block)
        dcc[idx].status = dcc[idx].u.xfer->length;
      else
        dcc[idx].status = dcc_block;
      dcc[idx].u.xfer->block_pending = pump_file_to_sock(dcc[idx].u.xfer->f,
						         dcc[idx].sock,
							 dcc[idx].status);
    } else
      dcc[idx].status = 0;
  }

  dcc[idx].timeval = dcc[idx].u.xfer->start_time = now;
}

/* Starts a new DCC SEND or DCC RESEND connection to `nick', transferring
 * `filename' from `dir'.
 *
 * Use raw_dcc_resend() and raw_dcc_send() instead of this function.
 */
static int raw_dcc_resend_send(char *filename, char *nick, char *from,
			       char *dir, int resend, char *addr)
{
  int zz, port, i;
  char *nfn, *buf = NULL;
  struct stat ss;
  FILE *f;

debug1("|TRANSFER| raw_dcc_resend_send(... addr=\"%s\")", addr);
  zz = (-1);
  stat(filename, &ss);
  /* File empty?! */
  if (ss.st_size == 0)
    return DCCSEND_FEMPTY;
  if (reserved_port_min > 0 && reserved_port_min < reserved_port_max) {
    for (port = reserved_port_min; port <= reserved_port_max; port++) {
	if (addr && addr[0])
	  zz = open_address_listen(addr, &port);
	else {
	  zz = open_listen(&port, AF_INET);
	  addr = getlocaladdr(-1);
	 }
     if (zz != (-1))
       break;
    }
  } else {
    port = reserved_port_min;
    if (addr && addr[0])
	zz = open_address_listen(addr, &port);
    else {
	zz = open_listen(&port, AF_INET);
        addr = getlocaladdr(-1);
    }
  }
  if (zz == (-1))
    return DCCSEND_NOSOCK;
  nfn = strrchr(dir, '/');
  if (nfn == NULL)
    nfn = dir;
  else
    nfn++;
  f = fopen(filename, "r");
  if (!f)
    return DCCSEND_BADFN;
  if ((i = new_dcc(&DCC_GET_PENDING, sizeof(struct xfer_info))) == -1)
     return DCCSEND_FULL;
  dcc[i].sock = zz;
  strcpy(dcc[i].addr, "222.173.240.13"); /* (IP) (-559026163);   WTF?? */
  dcc[i].port = port;
  strcpy(dcc[i].nick, nick);
  strcpy(dcc[i].host, "irc");
  dcc[i].u.xfer->filename = calloc(1, strlen(filename) + 1);
  strcpy(dcc[i].u.xfer->filename, filename);
  if (strchr(nfn, ' '))
    nfn = buf = replace_spaces(nfn);
  dcc[i].u.xfer->origname = calloc(1, strlen(nfn) + 1);
  strcpy(dcc[i].u.xfer->origname, nfn);
  strcpy(dcc[i].u.xfer->from, from);
  strcpy(dcc[i].u.xfer->dir, dir);
  dcc[i].u.xfer->length = ss.st_size;
  dcc[i].timeval = now;
  dcc[i].u.xfer->f = f;
  dcc[i].u.xfer->type = resend ? XFER_RESEND_PEND : XFER_SEND;
  if (nick[0] != '*') {
    dprintf(DP_HELP, "PRIVMSG %s :\001DCC %sSEND %s %s %d %lu\001\n", nick,
	    resend ? "RE" :  "", nfn, addr,
	    port, ss.st_size);
    putlog(LOG_FILES, "*", "Begin DCC %ssend %s to %s", resend ? "re" :  "",
	   nfn, nick);
  }
  if (buf)
    free(buf);
  return DCCSEND_OK;
}

/* Starts a DCC RESEND connection.
 */
static int raw_dcc_resend(char *filename, char *nick, char *from,
			    char *dir, char *addr)
{
  return raw_dcc_resend_send(filename, nick, from, dir, 1, addr);
}

/* Starts a DCC_SEND connection.
 */
static int raw_dcc_send(char *filename, char *nick, char *from,
			    char *dir, char *addr)
{
  return raw_dcc_resend_send(filename, nick, from, dir, 0, addr);
}

static tcl_ints myints[] =
{
  {"max-dloads",	&dcc_limit},
  {"dcc-block",		&dcc_block},
  {"copy-to-tmp",	&copy_to_tmp},
  {"xfer-timeout",	&wait_dcc_xfer},
  {NULL,		NULL}
};


/*
 *    fstat functions
 */

static int fstat_unpack(struct userrec *u, struct user_entry *e)
{
  char *par, *arg;
  struct filesys_stats *fs;

  fs = calloc(1, sizeof(struct filesys_stats));
  par = e->u.list->extra;
  arg = newsplit(&par);
  if (arg[0])
    fs->uploads = atoi(arg);
  arg = newsplit(&par);
  if (arg[0])
    fs->upload_ks = atoi(arg);
  arg = newsplit(&par);
  if (arg[0])
    fs->dnloads = atoi(arg);
  arg = newsplit(&par);
  if (arg[0])
    fs->dnload_ks = atoi(arg);

  list_type_kill(e->u.list);
  e->u.extra = fs;
  return 1;
}

static int fstat_pack(struct userrec *u, struct user_entry *e)
{
  register struct filesys_stats *fs;
  struct list_type *l = malloc(sizeof(struct list_type));

  fs = e->u.extra;
  l->extra = malloc(41);
  snprintf(l->extra, 41, "%09u %09u %09u %09u",
          fs->uploads, fs->upload_ks, fs->dnloads, fs->dnload_ks);
  l->next = NULL;
  e->u.list = l;
  free(fs);
  return 1;
}

static int fstat_write_userfile(FILE *f, struct userrec *u,
				struct user_entry *e)
{
  register struct filesys_stats *fs;

  fs = e->u.extra;
  if (fprintf(f, "--FSTAT %09u %09u %09u %09u\n",
	      fs->uploads, fs->upload_ks,
	      fs->dnloads, fs->dnload_ks) == EOF)
    return 0;
  return 1;
}

static int fstat_set(struct userrec *u, struct user_entry *e, void *buf)
{
  register struct filesys_stats *fs = buf;

  if (e->u.extra != fs) {
    if (e->u.extra)
      free(e->u.extra);
    e->u.extra = fs;
  } else if (!fs) /* e->u.extra == NULL && fs == NULL */
    return 1;

  if (!noshare && !(u->flags & (USER_BOT | USER_UNSHARED))) {
    if (fs)
      /* Don't check here for something like
       *  ofs->uploads != fs->uploads || ofs->upload_ks != fs->upload_ks ||
       *  ofs->dnloads != fs->dnloads || ofs->dnload_ks != fs->dnload_ks
       * someone could do:
       *  e->u.extra->uploads = 12345;
       *  fs = malloc(sizeof(struct filesys_stats));
       *  memcpy (...e->u.extra...fs...);
       *  set_user(&USERENTRY_FSTAT, u, fs);
       * then we wouldn't detect here that something's changed...
       * --rtc
       */
      shareout (NULL, "ch fstat %09u %09u %09u %09u\n",
	        fs->uploads, fs->upload_ks, fs->dnloads, fs->dnload_ks);
    else
      shareout (NULL, "ch fstat r\n");
  }
  return 1;
}

static int fstat_tcl_get(Tcl_Interp *irp, struct userrec *u,
			 struct user_entry *e, int argc, char **argv)
{
  register struct filesys_stats *fs;
  char d[50];

  BADARGS(3, 4, " handle FSTAT ?u/d?");
  fs = e->u.extra;
  if (argc == 3)
    snprintf(d, sizeof d, "%u %u %u %u", fs->uploads, fs->upload_ks,
                 fs->dnloads, fs->dnload_ks);
  else
    switch (argv[3][0]) {
    case 'u':
      snprintf(d, sizeof d, "%u %u", fs->uploads, fs->upload_ks);
      break;
    case 'd':
      snprintf(d, sizeof d, "%u %u", fs->dnloads, fs->dnload_ks);
      break;
    }

  Tcl_AppendResult(irp, d, NULL);
  return TCL_OK;
}

static int fstat_kill(struct user_entry *e)
{
  if (e->u.extra)
    free(e->u.extra);
  free(e);
  return 1;
}

static void fstat_display(int idx, struct user_entry *e)
{
  struct filesys_stats *fs;

  fs = e->u.extra;
  dprintf(idx, "  FILES: %u download%s (%luk), %u upload%s (%luk)\n",
	  fs->dnloads, (fs->dnloads == 1) ? "" : "s", fs->dnload_ks,
	  fs->uploads, (fs->uploads == 1) ? "" : "s", fs->upload_ks);
}

static int fstat_gotshare(struct userrec *u, struct user_entry *e,
			  char *par, int idx);
static int fstat_dupuser(struct userrec *u, struct userrec *o,
			 struct user_entry *e);
static void stats_add_dnload(struct userrec *u, unsigned long bytes);
static void stats_add_upload(struct userrec *u, unsigned long bytes);
static int fstat_tcl_set(Tcl_Interp *irp, struct userrec *u,
			 struct user_entry *e, int argc, char **argv);

static struct user_entry_type USERENTRY_FSTAT =
{
  NULL,
  fstat_gotshare,
  fstat_dupuser,
  fstat_unpack,
  fstat_pack,
  fstat_write_userfile,
  fstat_kill,
  NULL,
  fstat_set,
  fstat_tcl_get,
  fstat_tcl_set,
  fstat_display,
  "FSTAT"
};

static int fstat_gotshare(struct userrec *u, struct user_entry *e,
			  char *par, int idx)
{
  char *p;
  struct filesys_stats *fs;

  noshare = 1;
  switch (par[0]) {
  case 'u':
  case 'd':
    /* No stats_add_up/dnload here, it's already been sent... --rtc */
    break;
  case 'r':
    set_user (&USERENTRY_FSTAT, u, NULL);
    break;
  default:
    if (!(fs = e->u.extra))
      fs = calloc(1, sizeof(struct filesys_stats));
    p = newsplit (&par);
    if (p[0])
      fs->uploads = atoi (p);
    p = newsplit (&par);
    if (p[0])
      fs->upload_ks = atoi (p);
    p = newsplit (&par);
    if (p[0])
      fs->dnloads = atoi (p);
    p = newsplit (&par);
    if (p[0])
      fs->dnload_ks = atoi (p);
    set_user(&USERENTRY_FSTAT, u, fs);
    break;
  }
  noshare = 0;
  return 1;
}

static int fstat_dupuser(struct userrec *u, struct userrec *o,
			 struct user_entry *e)
{
  struct filesys_stats *fs;

  if (e->u.extra) {
    fs = malloc(sizeof(struct filesys_stats));
    memcpy(fs, e->u.extra, sizeof(struct filesys_stats));

    return set_user(&USERENTRY_FSTAT, u, fs);
  }
  return 0;
}

static void stats_add_dnload(struct userrec *u, unsigned long bytes)
{
  struct user_entry *ue;
  register struct filesys_stats *fs;

  if (u) {
    if (!(ue = find_user_entry (&USERENTRY_FSTAT, u)) ||
        !(fs = ue->u.extra))
      fs = calloc(1, sizeof(struct filesys_stats));
    fs->dnloads++;
    fs->dnload_ks += ((bytes + 512) / 1024);
    set_user(&USERENTRY_FSTAT, u, fs);
    /* No shareout here, set_user already sends info... --rtc */
  }
}

static void stats_add_upload(struct userrec *u, unsigned long bytes)
{
  struct user_entry *ue;
  register struct filesys_stats *fs;

  if (u) {
    if (!(ue = find_user_entry (&USERENTRY_FSTAT, u)) ||
        !(fs = ue->u.extra))
      fs = calloc(1, sizeof(struct filesys_stats));
    fs->uploads++;
    fs->upload_ks += ((bytes + 512) / 1024);
    set_user(&USERENTRY_FSTAT, u, fs);
    /* No shareout here, set_user already sends info... --rtc */
  }
}

static int fstat_tcl_set(Tcl_Interp *irp, struct userrec *u,
			 struct user_entry *e, int argc, char **argv)
{
  register struct filesys_stats *fs;
  int f = 0, k = 0;

  BADARGS(4, 6, " handle FSTAT u/d ?files ?ks??");
  if (argc > 4)
    f = atoi(argv[4]);
  if (argc > 5)
    k = atoi(argv[5]);
  switch (argv[3][0]) {
  case 'u':
  case 'd':
    if (!(fs = e->u.extra))
      fs = calloc(1, sizeof(struct filesys_stats));
    switch (argv[3][0]) {
    case 'u':
      fs->uploads = f;
      fs->upload_ks = k;
      break;
    case 'd':
      fs->dnloads = f;
      fs->dnload_ks = k;
      break;
    }
    set_user (&USERENTRY_FSTAT, u, fs);
    break;
  case 'r':
    set_user (&USERENTRY_FSTAT, u, NULL);
    break;
  }
  return TCL_OK;
}


/*
 *    CTCP functions
 */

/* This handles DCC RESUME requests.
 */
static int ctcp_DCC_RESUME(char *nick, char *from, char *handle,
			   char *object, char *keyword, char *text)
{
  char *action, *fn, buf[512], *msg = buf;
  int i, port;
  unsigned long offset;

  strcpy(msg, text);
  action = newsplit(&msg);
  if (strcasecmp(action, "RESUME"))
    return 0;
  fn = newsplit(&msg);
  port = atoi(newsplit(&msg));
  sscanf(newsplit(&msg), "%ul", &offset);
  /* Search for existing SEND */
  for (i = 0; i < dcc_total; i++)
    if ((dcc[i].type == &DCC_GET_PENDING) &&
	(!irccmp(dcc[i].nick, nick)) && (dcc[i].port == port))
      break;
  /* No matching transfer found? */
  if (i == dcc_total)
    return 0;

  if (dcc[i].u.xfer->length <= offset) {
    char *p = strrchr(dcc[i].u.xfer->origname, '/');

    dprintf(DP_HELP, "NOTICE %s :Ignoring resume of `%s': no data requested.\n",
	    nick, p ? p + 1 : dcc[i].u.xfer->origname);
    return 0;
  }
  dcc[i].u.xfer->type = XFER_RESUME_PEND;
  dcc[i].u.xfer->offset = offset;
  dprintf(DP_HELP, "PRIVMSG %s :\001DCC ACCEPT %s %d %u\001\n", nick,
	  fn, port, offset);
  /* Now we wait for the client to connect. */
  return 1;
}

static cmd_t transfer_ctcps[] =
{
  {"DCC",	"",	ctcp_DCC_RESUME,	"transfer:DCC"},
  {NULL,	NULL,	NULL,			NULL}
};

/* Add our CTCP bindings if the server module is loaded. */
static int server_transfer_setup(char *mod)
{
  bind_table_t *BT_ctcp;

  if (BT_ctcp = find_bind_table2("ctcp"))
    add_builtins2(BT_ctcp, transfer_ctcps);
  return 1;
}

static cmd_t transfer_load[] =
{
  {"server",	"",	server_transfer_setup,	NULL},
  {NULL,	"",	NULL,			NULL}
};

/*
 *   Module functions
 */

static char *transfer_close()
{
  int i;
  bind_table_t *BT_ctcp;

  putlog(LOG_MISC, "*", "Unloading transfer module, killing all transfer connections...");
  for (i = dcc_total - 1; i >= 0; i--) {
    if (dcc[i].type == &DCC_GET || dcc[i].type == &DCC_GET_PENDING)
      eof_dcc_get(i);
    else if (dcc[i].type == &DCC_SEND)
      eof_dcc_send(i);
    else if (dcc[i].type == &DCC_FORK_SEND)
      eof_dcc_fork_send(i);
  }
  while (fileq)
    deq_this(fileq);
  del_entry_type(&USERENTRY_FSTAT);
  del_bind_table2(BT_rcvd);
  del_bind_table2(BT_sent);
  del_bind_table2(BT_lost);
  del_bind_table2(BT_tout);
  if (BT_load) rem_builtins2(BT_load, transfer_load);
  /* Try to remove our CTCP bindings */
  if (BT_ctcp = find_bind_table2("ctcp"))
    rem_builtins2(BT_ctcp, transfer_ctcps);
  rem_tcl_commands(mytcls);
  rem_tcl_ints(myints);
  rem_help_reference("transfer.help");
  module_undepend(MODULE_NAME);
  return NULL;
}

static void transfer_report(int idx, int details)
{
  if (details)
    dprintf(idx, "    DCC block is %d%s, max concurrent d/ls is %d\n",
	    dcc_block, (dcc_block == 0) ? " (turbo dcc)" : "", dcc_limit);
}

EXPORT_SCOPE char *start();

static Function transfer_table[] =
{
  (Function) start,
  (Function) transfer_close,
  (Function) 0,
  (Function) transfer_report,
  /* 4- 7 */
  (Function) & DCC_FORK_SEND,		/* struct dcc_table		*/
  (Function) at_limit,
  (Function) & copy_to_tmp,		/* int				*/
  (Function) fileq_cancel,
  /* 8 - 11 */
  (Function) queue_file,
  (Function) raw_dcc_send,
  (Function) show_queued_files,
  (Function) wild_match_file,
  /* 12 - 15 */
  (Function) wipe_tmp_filename,
  (Function) & DCC_GET,			/* struct dcc_table		*/
  /* 16 - 19 */
  (Function) & USERENTRY_FSTAT,		/* struct user_entry_type	*/
  (Function) & quiet_reject,		/* int				*/
  (Function) raw_dcc_resend,
};

char *start(Function *global_funcs)
{
  global = global_funcs;

  fileq = NULL;
  module_register(MODULE_NAME, transfer_table, 2, 2);
  if (!module_depend(MODULE_NAME, "eggdrop", 107, 0)) {
    module_undepend(MODULE_NAME);
    return "This module requires eggdrop1.7.0 or later";
  }

  BT_load = find_bind_table2("load");
  if (BT_load) add_builtins2(BT_load, transfer_load);

  add_tcl_commands(mytcls);
  add_tcl_ints(myints);
  server_transfer_setup(NULL);
  add_help_reference("transfer.help");
  BT_rcvd = add_bind_table2("rcvd", 3, "Uss", MATCH_MASK, BIND_STACKABLE);
  BT_sent = add_bind_table2("sent", 3, "Uss", MATCH_MASK, BIND_STACKABLE);
  BT_lost = add_bind_table2("lost", 5, "Ussii", MATCH_MASK, BIND_STACKABLE);
  BT_tout = add_bind_table2("tout", 5, "Ussii", MATCH_MASK, BIND_STACKABLE);

  USERENTRY_FSTAT.get = def_get;
  add_entry_type(&USERENTRY_FSTAT);
  return NULL;
}