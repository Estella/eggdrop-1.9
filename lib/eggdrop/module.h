/*
 * module.h
 *
 * $Id: module.h,v 1.1 2001/10/27 16:34:47 ite Exp $
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

#ifndef _EGG_MOD_MODULE_H
#define _EGG_MOD_MODULE_H

/* FIXME: remove this ugliness ASAP! */
#define MAKING_MODS

/* Just include *all* the include files...it's slower but EASIER */
#include "src/main.h"		/* NOTE: when removing this, include config.h */
#include "modvals.h"
#include "src/tandem.h"
#include "src/registry.h"

/*
 * This file contains all the orrible stuff required to do the lookup
 * table for symbols, rather than getting the OS to do it, since most
 * OS's require all symbols resolved, this can cause a problem with
 * some modules.
 *
 * This is intimately related to the table in `modules.c'. Don't change
 * the files unless you have flamable underwear.
 *
 * Do not read this file whilst unless heavily sedated, I will not be
 * held responsible for mental break-downs caused by this file <G>
 */

/* #undef feof */
#undef dprintf
#undef Context
#undef ContextNote

#if defined (__CYGWIN__) && !defined(STATIC)
#  define EXPORT_SCOPE	__declspec(dllexport)
#else
#  define EXPORT_SCOPE
#endif

/* Redefine for module-relevance */

/* 0 - 3 */
/* 0: nmalloc -- UNUSED (Tothwolf) */
/* 1: nfree -- UNUSED (Tothwolf) */
#ifdef DEBUG_CONTEXT
#  define Context (global[2](__FILE__, __LINE__, MODULE_NAME))
#else
#  define Context {}
#endif
#define module_rename ((int (*)(char *, char *))global[3])
/* 4 - 7 */
#define module_register ((int (*)(char *, Function *, int, int))global[4])
#define module_find ((module_entry * (*)(char *,int,int))global[5])
#define module_depend ((Function *(*)(char *,char *,int,int))global[6])
#define module_undepend ((int(*)(char *))global[7])
/* 8 - 11 */
/* #define add_bind_table ((p_tcl_bind_list(*)(const char *,int,Function))global[8]) */
/* #define del_bind_table ((void (*) (p_tcl_bind_list))global[9]) */
/* #define find_bind_table ((p_tcl_bind_list(*)(const char *))global[10]) */
/* #define check_tcl_bind ((int (*) (p_tcl_bind_list,const char *,struct flag_record *,const char *, int))global[11]) */
/* 12 - 15 */
#define add_builtins ((int (*) (p_tcl_bind_list, cmd_t *))global[12])
#define rem_builtins ((int (*) (p_tcl_bind_list, cmd_t *))global[13])
#define add_tcl_commands ((void (*) (tcl_cmds *))global[14])
#define rem_tcl_commands ((void (*) (tcl_cmds *))global[15])
/* 16 - 19 */
#define add_tcl_ints ((void (*) (tcl_ints *))global[16])
#define rem_tcl_ints ((void (*) (tcl_ints *))global[17])
#define add_tcl_strings ((void (*) (tcl_strings *))global[18])
#define rem_tcl_strings ((void (*) (tcl_strings *))global[19])
/* 20 - 23 */
#define base64_to_int ((int (*) (char *))global[20])
#define int_to_base64 ((char * (*) (int))global[21])
#define int_to_base10 ((char * (*) (int))global[22])
#define simple_sprintf ((int (*)())global[23])
/* 24 - 27 */
#define botnet_send_zapf ((void (*)(int, char *, char *, char *))global[24])
#define botnet_send_zapf_broad ((void (*)(int, char *, char *, char *))global[25])
#define botnet_send_unlinked ((void (*)(int, char *, char *))global[26])
#define botnet_send_bye ((void(*)(void))global[27])
/* 28 - 31 */
#define botnet_send_chat ((void(*)(int,char*,char*))global[28])
#define botnet_send_filereject ((void(*)(int,char*,char*,char*))global[29])
#define botnet_send_filesend ((void(*)(int,char*,char*,char*))global[30])
#define botnet_send_filereq ((void(*)(int,char*,char*,char*))global[31])
/* 32 - 35 */
#define botnet_send_join_idx ((void(*)(int,int))global[32])
#define botnet_send_part_idx ((void(*)(int,char *))global[33])
#define updatebot ((void(*)(int,char*,char,int))global[34])
#define nextbot ((int (*)(char *))global[35])
/* 36 - 39 */
#define zapfbot ((void (*)(int))global[36])
/* 37: n_free -- UNUSED (Tothwolf) */
#define u_pass_match ((int (*)(struct userrec *,char *))global[38])
/* 39: user_malloc -- UNUSED (Tothwolf) */
/* 40 - 43 */
#define get_user ((void *(*)(struct user_entry_type *,struct userrec *))global[40])
#define set_user ((int(*)(struct user_entry_type *,struct userrec *,void *))global[41])
#define add_entry_type ((int (*) ( struct user_entry_type * ))global[42])
#define del_entry_type ((int (*) ( struct user_entry_type * ))global[43])
/* 44 - 47 */
#define get_user_flagrec ((void (*)(struct userrec *, struct flag_record *, const char *))global[44])
#define set_user_flagrec ((void (*)(struct userrec *, struct flag_record *, const char *))global[45])
#define get_user_by_host ((struct userrec * (*)(char *))global[46])
#define get_user_by_handle ((struct userrec *(*)(struct userrec *,char *))global[47])
/* 48 - 51 */
#define find_entry_type ((struct user_entry_type * (*) ( char * ))global[48])
#define find_user_entry ((struct user_entry * (*)( struct user_entry_type *, struct userrec *))global[49])
#define adduser ((struct userrec *(*)(struct userrec *,char*,char*,char*,int))global[50])
#define deluser ((int (*)(char *))global[51])
/* 52 - 55 */
#define addhost_by_handle ((void (*) (char *, char *))global[52])
#define delhost_by_handle ((int(*)(char *,char *))global[53])
#define readuserfile ((int (*)(char *,struct userrec **))global[54])
#define write_userfile ((void(*)(int))global[55])
/* 56 - 59 */
#define geticon ((char (*) (int))global[56])
#define clear_chanlist ((void (*)(void))global[57])
#define reaffirm_owners ((void (*)(void))global[58])
#define change_handle ((int(*)(struct userrec *,char*))global[59])
/* 60 - 63 */
#define write_user ((int (*)(struct userrec *, FILE *,int))global[60])
#define clear_userlist ((void (*)(struct userrec *))global[61])
#define count_users ((int(*)(struct userrec *))global[62])
#define sanity_check ((int(*)(int))global[63])
/* 64 - 67 */
#define break_down_flags ((void (*)(const char *,struct flag_record *,struct flag_record *))global[64])
#define build_flags ((void (*)(char *, struct flag_record *, struct flag_record *))global[65])
#define flagrec_eq ((int(*)(struct flag_record*,struct flag_record *))global[66])
#define flagrec_ok ((int(*)(struct flag_record*,struct flag_record *))global[67])
/* 68 - 71 */
#define shareout (*(Function *)(global[68]))
#define dprintf (global[69])
#define chatout (global[70])
#define chanout_but ((void(*)())global[71])
/* 72 - 75 */
#define check_validity ((int (*) (char *,Function))global[72])
#define list_delete ((int (*)( struct list_type **, struct list_type *))global[73])
#define list_append ((int (*) ( struct list_type **, struct list_type *))global[74])
#define list_contains ((int (*) (struct list_type *, struct list_type *))global[75])
/* 76 - 79 */
#define answer ((int (*) (int,char *,char *,unsigned short *,int))global[76])
#define getmyip ((IP (*) (void))global[77])
#define neterror ((void (*) (char *))global[78])
#define tputs ((void (*) (int, char *,unsigned int))global[79])
/* 80 - 83 */
#define new_dcc ((int (*) (struct dcc_table *, int))global[80])
#define lostdcc ((void (*) (int))global[81])
#define getsock ((int (*) (int))global[82])
#define killsock ((void (*) (int))global[83])
/* 84 - 87 */
#define open_listen ((int (*) (int *,int))global[84])
#define open_telnet_dcc ((int (*) (int,char *,char *))global[85])
/* 86: get_data_ptr -- UNUSED (Tothwolf) */
#define open_telnet ((int (*) (char *, int))global[87])
/* 88 - 91 */
#define check_bind_event ((void * (*) (const char *))global[88])
#ifndef HAVE_MEMCPY
# define memcpy ((void * (*) (void *, const void *, size_t))global[89])
#endif
/* #define my_atoul ((IP(*)(char *))global[90]) */
#define my_strcpy ((int (*)(char *, const char *))global[91])
/* 92 - 95 */
#define dcc (*(struct dcc_t **)global[92])
#define chanset (*(struct chanset_t **)(global[93]))
#define userlist (*(struct userrec **)global[94])
#define lastuser (*(struct userrec **)(global[95]))
/* 96 - 99 */
#define global_bans (*(maskrec **)(global[96]))
#define global_ign (*(struct igrec **)(global[97]))
#define password_timeout (*(int *)(global[98]))
#define share_greet (*(int *)global[99])
/* 100 - 103 */
#define max_dcc (*(int *)global[100])
#define require_p (*(int *)global[101])
#define ignore_time (*(int *)(global[102]))
/* #define use_console_r (*(int *)(global[103])) */
/* 104 - 107 */
#define reserved_port_min (*(int *)(global[104]))
#define reserved_port_max (*(int *)(global[105]))
#define debug_output (*(int *)(global[106]))
#define noshare (*(int *)(global[107]))
/* 108 - 111 */
#define gban_total (*(int*)global[108])
#define make_userfile (*(int*)global[109])
#define default_flags (*(int*)global[110])
#define dcc_total (*(int*)global[111])
/* 112 - 115 */
#define tempdir ((char *)(global[112]))
#define natip ((char *)(global[113]))
/* 114: hostname -- UNUSED (drummer) */
#define origbotname ((char *)(global[115]))
/* 116 - 119 */
#define botuser ((char *)(global[116]))
#define admin ((char *)(global[117]))
#define userfile ((char *)global[118])
#define ver ((char *)global[119])
/* 120 - 123 */
#define notify_new ((char *)global[120])
#define helpdir ((char *)global[121])
#define Version ((char *)global[122])
#define botnetnick ((char *)global[123])
/* 124 - 127 */
#define DCC_CHAT_PASS (*(struct dcc_table *)(global[124]))
#define DCC_BOT (*(struct dcc_table *)(global[125]))
#define DCC_LOST (*(struct dcc_table *)(global[126]))
#define DCC_CHAT (*(struct dcc_table *)(global[127]))
/* 128 - 131 */
#define interp (*(Tcl_Interp **)(global[128]))
#define now (*(time_t*)global[129])
#define findanyidx ((int (*)(int))global[130])
#define findchan ((struct chanset_t *(*)(char *))global[131])
/* 132 - 135 */
#define cmd_die (global[132])
#define days ((void (*)(time_t,time_t,char *))global[133])
#define daysago ((void (*)(time_t,time_t,char *))global[134])
#define daysdur ((void (*)(time_t,time_t,char *))global[135])
/* 136 - 139 */
#define ismember ((memberlist * (*) (struct chanset_t *, char *))global[136])
#define newsplit ((char *(*)(char **))global[137])
/* 138: splitnick -- UNUSED (Tothwolf) */
#define splitc ((void (*)(char *,char *,char))global[139])
/* 140 - 143 */
#define addignore ((void (*) (char *, char *, char *,time_t))global[140])
#define match_ignore ((int (*)(char *))global[141])
#define delignore ((int (*)(char *))global[142])
#define fatal (global[143])
/* 144 - 147 */
#define xtra_kill ((void (*)(struct user_entry *))global[144])
#define xtra_unpack ((void (*)(struct userrec *, struct user_entry *))global[145])
#define movefile ((int (*) (char *, char *))global[146])
#define copyfile ((int (*) (char *, char *))global[147])
/* 148 - 151 */
#define do_tcl ((void (*)(char *, char *))global[148])
#define readtclprog ((int (*)(const char *))global[149])
/* 150: get_language() -- UNUSED */
#define def_get ((void *(*)(struct userrec *, struct user_entry *))global[151])
/* 152 - 155 */
#define makepass ((void (*) (char *))global[152])
#define wild_match ((int (*)(const char *, const char *))global[153])
#define maskhost ((void (*)(const char *, char *))global[154])
#define show_motd ((void(*)(int))global[155])
/* 156 - 159 */
#define tellhelp ((void(*)(int, char *, struct flag_record *, int))global[156])
#define showhelp ((void(*)(char *, char *, struct flag_record *, int))global[157])
#define add_help_reference ((void(*)(char *))global[158])
#define rem_help_reference ((void(*)(char *))global[159])
/* 160 - 163 */
#define touch_laston ((void (*)(struct userrec *,char *,time_t))global[160])
#define add_mode ((void (*)(struct chanset_t *,char,char,char *))(*(Function**)(global[161])))
#define rmspace ((void (*)(char *))global[162])
#define in_chain ((int (*)(char *))global[163])
/* 164 - 167 */
#define add_note ((int (*)(char *,char*,char*,int,int))global[164])
/* 165: del_lang_section() -- UNUSED */
#define detect_dcc_flood ((int (*) (time_t *,struct chat_info *,int))global[166])
#define flush_lines ((void(*)(int,struct chat_info*))global[167])
/* 168 - 171 */
/* 168: expected_memory -- UNUSED (Tothwolf) */
/* 169: tell_mem_status -- UNUSED (Tothwolf) */
#define do_restart (*(int *)(global[170]))
#define check_tcl_filt ((const char *(*)(int, const char *))global[171])
/* 172 - 175 */
#define add_hook(a,b) (((void (*) (int, Function))global[172])(a,b))
#define del_hook(a,b) (((void (*) (int, Function))global[173])(a,b))
/* 174: H_dcc -- UNUSED (stdarg) */
/* 175: H_filt -- UNUSED (oskar) */
/* 176 - 179 */
/* 176: H_chon -- UNUSED (oskar) */
/* 177: H_chof -- UNUSED (oskar) */
/*#define H_load (*(p_tcl_bind_list *)(global[178])) */
/*#define H_unld (*(p_tcl_bind_list *)(global[179])) */
/* 180 - 183 */
/* 180: H_chat -- UNUSED (oskar) */
/* 181: H_act -- UNUSED (oskar) */
/* 182: H_bcst -- UNUSED (oskar) */
/* 183: H_bot -- UNUSED (oskar) */
/* 184 - 187 */
/* 184: H_link -- UNUSED (oskar) */
/* 185: H_disc -- UNUSED (oskar) */
/* 186: H_away -- UNUSED (oskar) */
/* 187: H_nkch -- UNUSED (oskar) */
/* 188 - 191 */
#define USERENTRY_BOTADDR (*(struct user_entry_type *)(global[188]))
#define USERENTRY_BOTFL (*(struct user_entry_type *)(global[189]))
#define USERENTRY_HOSTS (*(struct user_entry_type *)(global[190]))
#define USERENTRY_PASS (*(struct user_entry_type *)(global[191]))
/* 192 - 195 */
#define USERENTRY_XTRA (*(struct user_entry_type *)(global[192]))
#define user_del_chan ((void(*)(char *))(global[193]))
#define USERENTRY_INFO (*(struct user_entry_type *)(global[194]))
#define USERENTRY_COMMENT (*(struct user_entry_type *)(global[195]))
/* 196 - 199 */
#define USERENTRY_LASTON (*(struct user_entry_type *)(global[196]))
#define putlog (global[197])
#define botnet_send_chan ((void(*)(int,char*,char*,int,char*))global[198])
#define list_type_kill ((void(*)(struct list_type *))global[199])
/* 200 - 203 */
#define logmodes ((int(*)(char *))global[200])
#define masktype ((const char *(*)(int))global[201])
#define stripmodes ((int(*)(char *))global[202])
#define stripmasktype ((const char *(*)(int))global[203])
/* 204 - 207 */
#define sub_lang ((void(*)(int,char *))global[204])
#define online_since (*(int *)(global[205]))
/* 206: cmd_loadlanguage() -- UNUSED */
#define check_dcc_attrs ((int (*)(struct userrec *,int))global[207])
/* 208 - 211 */
#define check_dcc_chanattrs ((int (*)(struct userrec *,char *,int,int))global[208])
#define add_tcl_coups ((void (*) (tcl_coups *))global[209])
#define rem_tcl_coups ((void (*) (tcl_coups *))global[210])
#define botname ((char *)(global[211]))
/* 212 - 215 */
/* 212: remove_gunk() -- UNUSED (drummer) */
#define check_tcl_chjn ((void (*) (const char *,const char *,int,char,int,const char *))global[213])
/* 214: sanitycheck_dcc() -- UNUSED (guppy) */
#define isowner ((int (*)(char *))global[215])
/* 216 - 219 */
/* 216: min_dcc_port -- UNUSED (guppy) */
/* 217: max_dcc_port -- UNUSED (guppy) */
#define irccmp ((int(*)(char *, char *))(*(Function**)(global[218])))
#define ircncmp ((int(*)(char *, char *, int *))(*(Function**)(global[219])))
/* 220 - 223 */
#define global_exempts (*(maskrec **)(global[220]))
#define global_invites (*(maskrec **)(global[221]))
#define ginvite_total (*(int*)global[222])
#define gexempt_total (*(int*)global[223])
/* 224 - 227 */
/* 224: H_event -- UNUSED (stdarg) */
#define use_exempts (*(int *)(global[225]))	/* drummer/Jason */
#define use_invites (*(int *)(global[226]))	/* drummer/Jason */
#define force_expire (*(int *)(global[227]))	/* Rufus */
/* 228 - 231 */
/* 228: add_lang_section() -- UNUSED */
/* 229: user_realloc -- UNUSED (Tothwolf) */
/* 230: nrealloc -- UNUSED (Tothwolf) */
#define xtra_set ((int(*)(struct userrec *,struct user_entry *, void *))global[231])
/* 232 - 235 */
#ifdef DEBUG_CONTEXT
#  define ContextNote(note) (global[232](__FILE__, __LINE__, MODULE_NAME, note))
#else
#  define ContextNote(note)	do {	} while (0)
#endif
/* 233: Assert -- UNUSED (Tothwolf) */
#define allocsock ((int(*)(int sock,int options))global[234])
#define call_hostbyip ((void(*)(char *, char *, int))global[235])
/* 236 - 239 */
#define call_ipbyhost ((void(*)(char *, char *, int))global[236])
#define iptostr ((char *(*)(IP))global[237])
#define DCC_DNSWAIT (*(struct dcc_table *)(global[238]))
/* 239: hostsanitycheck_dcc() -- UNUSED (guppy) */
/* 240 - 243 */
#define dcc_dnsipbyhost ((void (*)(char *))global[240])
#define dcc_dnshostbyip ((void (*)(char *))global[241])
#define changeover_dcc ((void (*)(int, struct dcc_table *, int))global[242])
#define make_rand_str ((void (*) (char *, int))global[243])
/* 244 - 247 */
#define protect_readonly (*(int *)(global[244]))
#define findchan_by_dname ((struct chanset_t *(*)(char *))global[245])
#define removedcc ((void (*) (int))global[246])
#define userfile_perm (*(int *)global[247])
/* 248 - 251 */
#define sock_has_data ((int(*)(int, int))global[248])
#define bots_in_subtree ((int (*)(tand_t *))global[249])
#define users_in_subtree ((int (*)(tand_t *))global[250])
#ifndef HAVE_INET_ATON
# define inet_aton ((int (*)(const char *cp, struct in_addr *addr))global[251])
#endif
/* 252 - 255 */
#ifndef HAVE_SNPRINTF
# define snprintf (global[252])
#endif
#ifndef HAVE_VSNPRINTF
# define vsnprintf ((int (*)(char *, size_t, const char *, va_list))global[253])
#endif
#ifndef HAVE_MEMSET
# define memset ((void *(*)(void *, int, size_t))global[254])
#endif
#ifndef HAVE_STRCASECMP
# define strcasecmp ((int (*)(const char *, const char *))global[255])
#endif
/* 256 - 259 */
#ifndef HAVE_STRNCASECMP
# define strncasecmp ((int (*)(const char *, const char *, size_t))global[256])
#endif
#define is_file ((int (*)(const char *))global[257])
#define must_be_owner (*(int *)(global[258]))
#define tandbot (*(tand_t **)(global[259]))
/* 260 - 263 */
#define party (*(party_t **)(global[260]))
#define open_address_listen ((int (*)(char *addr, int *port))global[261])
#define str_escape ((char *(*)(const char *, const char, const char))global[262])
#define strchr_unescape ((char *(*)(char *, const char, register const char))global[263])
/* 264 - 267 */
#define str_unescape ((void (*)(char *, register const char))global[264])
#define egg_strcatn ((int (*)(char *dst, const char *src, size_t max))global[265])
#define clear_chanlist_member ((void (*)(const char *nick))global[266])
#if (TCL_MAJOR_VERSION >= 8 && TCL_MINOR_VERSION >= 1) || (TCL_MAJOR_VERSION >= 9)
#define str_nutf8tounicode ((int (*)(char *str, int len))global[267])
#endif
/* 268 - 271 */
/* Please don't modify socklist directly, unless there's no other way.
 * Its structure might be changed, or it might be completely removed,
 * so you can't rely on it without a version-check.
 */
#define socklist (*(struct sock_list **)global[268])
#define sockoptions ((int (*)(int, int, int))global[269])
#define flush_inbuf ((int (*)(int))global[270])
#ifdef IPV6
#  define getmyip6 ((struct in6_addr (*) (void))global[271])
#endif
/* 272 - 275 */
#define getlocaladdr ((char* (*) (int))global[272])
#define kill_bot ((void (*)(char *, char *))global[273])
#define quit_msg ((char *)(global[274]))
#define add_bind_table2 ((bind_table_t *(*)(const char *, int, char *, int, int))global[275])
/* 276 - 279 */
#define del_bind_table2 ((void (*)(bind_table_t *))global[276])
#define add_builtins2 ((void (*)(bind_table_t *, cmd_t *))global[277])
#define rem_builtins2 ((void (*)(bind_table_t *, cmd_t *))global[278])
#define find_bind_table2 ((bind_table_t *(*)(const char *))global[279])
/* 280 - 283 */
#define check_bind ((int (*)(bind_table_t *, const char *, struct flag_record *, ...))global[280])
#define registry_lookup ((int (*)(const char *, const char *, Function *, void **))global[281])
#define registry_add_simple_chains ((int (*)(registry_simple_chain_t *))global[282])
#ifndef HAVE_STRFTIME
# define strftime ((size_t (*)(char *, size_t, const char *, const struct tm *))global[283])
#endif
/* 284 - 287 */
#ifndef HAVE_INET_NTOP
# define inet_ntop ((const char (*)(int, const void *, char *, socklen_t size))global[284])
#endif
#ifndef HAVE_INET_PTON
# define inet_pton ((int (*)(int, const char *, void *))global[285])
#endif
#ifndef HAVE_VASPRINTF
# define vasprintf ((int (*)(char **, const char *, va_list))global[286])
#endif
#ifndef HAVE_ASPRINTF
# define asprintf ((int (*)(char **, const char *, ...))global[287])
#endif

/* 288 - 291 */
#define msprintf ((char *(*)())global[288])
#define mstack_new ((mstack_t *(*)())global[289])
#define mstack_push ((void *(*)())global[290])
#define mstack_destroy ((void *(*)())global[291])

/* This is for blowfish module, couldnt be bothered making a whole new .h
 * file for it ;)
 */
#ifndef MAKING_ENCRYPTION

#  define encrypt_string(a, b)						\
	(((char *(*)(char *,char*))encryption_funcs[4])(a,b))
#  define decrypt_string(a, b)						\
	(((char *(*)(char *,char*))encryption_funcs[5])(a,b))
#endif

#endif				/* _EGG_MOD_MODULE_H */