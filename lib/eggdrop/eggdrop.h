/*
 * eggdrop.h --
 */

#ifndef _EGGDROP_H
#define _EGGDROP_H

#include "../egglib/egglib.h"
#include <eggdrop/common.h>
#include <eggdrop/flags.h>
#include <eggdrop/ircmasks.h>
#include <eggdrop/users.h>

#include <eggdrop/base64.h>
#include <eggdrop/binds.h>
#include <eggdrop/botnetutil.h>
#include <eggdrop/eggconfig.h>
#include <eggdrop/eggdns.h>
#include <eggdrop/eggident.h>
#include <eggdrop/egglog.h>
#include <eggdrop/eggmod.h>
#include <eggdrop/eggnet.h>
#include <eggdrop/eggowner.h>
#include <eggdrop/eggtimer.h>
#include <eggdrop/fileutil.h>
#include <eggdrop/garbage.h>
#include <eggdrop/hash_table.h>
#include <eggdrop/irccmp.h>
#include <eggdrop/ircparse.h>
#include <eggdrop/linemode.h>
#include <eggdrop/match.h>
#include <eggdrop/md5.h>
#include <eggdrop/memutil.h>
#include <eggdrop/my_socket.h>
#include <eggdrop/partyline.h>
#include <eggdrop/sockbuf.h>
#include <eggdrop/script.h>
#include <eggdrop/throttle.h>
#include <eggdrop/xml.h>

/* Gettext macros */
#ifdef ENABLE_NLS
#include <libintl.h>
#define _(x) gettext(x)
#define N_(x) gettext_noop(x)
#define P_(x1, x2, n) ngettext(x1, x2, n)
#else
#define _(x) (x)
#define N_(x) (x)
#define P_(x1, x2, n) ( ((n) == 1) ? (x1) : (x2) )
#endif

BEGIN_C_DECLS

typedef struct eggdrop {
  Function *global;		/* FIXME: this field will be removed once the
				   global_funcs mess is cleaned up */
} eggdrop_t;

extern int eggdrop_init();
extern int eggdrop_event(const char *event);
extern eggdrop_t *eggdrop_new(void);
extern eggdrop_t *eggdrop_delete(eggdrop_t *);

END_C_DECLS

#endif				/* !_EGGDROP_H */
