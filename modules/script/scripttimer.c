/* scripttimer.c: timer-related scripting commands
 *
 * Copyright (C) 2002, 2003, 2004 Eggheads Development Team
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
static const char rcsid[] = "$Id: scripttimer.c,v 1.4 2003/12/18 06:50:47 wcc Exp $";
#endif

#include <eggdrop/eggdrop.h>
#include <stdio.h>
#include <stdlib.h>

static int script_single_timer(int nargs, int sec, int usec, script_callback_t *callback);
static int script_repeat_timer(int nargs, int sec, int usec, script_callback_t *callback);
static int script_timers(script_var_t *retval);
static int script_timer_info(script_var_t *retval, int timer_id);

static int script_timer(int sec, int usec, script_callback_t *callback, int flags)
{
	egg_timeval_t howlong;
	int id;

	howlong.sec = sec;
	howlong.usec = usec;
	callback->syntax = (char *)malloc(1);
	callback->syntax[0] = 0;
	id = timer_create_complex(&howlong, callback->name, callback->callback, callback, flags);
	return(id);
}

static int script_single_timer(int nargs, int sec, int usec, script_callback_t *callback)
{
	if (nargs < 3) {
		sec = usec;
		usec = 0;
	}

	callback->flags |= SCRIPT_CALLBACK_ONCE;
	return script_timer(sec, usec, callback, 0);
}

static int script_repeat_timer(int nargs, int sec, int usec, script_callback_t *callback)
{
	if (nargs < 3) {
		sec = usec;
		usec = 0;
	}

	return script_timer(sec, usec, callback, TIMER_REPEAT);
}

static int script_timers(script_var_t *retval)
{
	int *timers, ntimers;

	ntimers = timer_list(&timers);

	/* A malloc'd array of ints. */
	retval->type = SCRIPT_ARRAY | SCRIPT_FREE | SCRIPT_INTEGER;
	retval->value = timers;
	retval->len = ntimers;
	return(0);
}

static int script_timer_info(script_var_t *retval, int timer_id)
{
	egg_timeval_t howlong, trigger_time, start_time, diff, now;
	char *name;

	retval->type = SCRIPT_VAR | SCRIPT_FREE | SCRIPT_ARRAY;
	retval->len = 0;

	if (timer_info(timer_id, &name, &howlong, &trigger_time)) return(0);

	timer_get_now(&now);

	/* Name, when it started, initial timer length,
		how long it's run already, how long until it triggers,
		absolute time when it triggers. */

	script_list_append(retval, script_string(name, -1));

	timer_diff(&howlong, &trigger_time, &start_time);
	script_list_append(retval, script_int(start_time.sec));
	script_list_append(retval, script_int(start_time.usec));

	script_list_append(retval, script_int(howlong.sec));
	script_list_append(retval, script_int(howlong.usec));

	timer_diff(&start_time, &now, &diff);
	script_list_append(retval, script_int(diff.sec));
	script_list_append(retval, script_int(diff.usec));

	timer_diff(&now, &trigger_time, &diff);
	script_list_append(retval, script_int(diff.sec));
	script_list_append(retval, script_int(diff.usec));

	script_list_append(retval, script_int(trigger_time.sec));
	script_list_append(retval, script_int(trigger_time.usec));

	return(0);
}

script_command_t script_timer_cmds[] = {
	{"", "timer", script_single_timer, NULL, 2, "iic", "seconds ?microseconds? callback", SCRIPT_INTEGER, SCRIPT_VAR_ARGS | SCRIPT_VAR_FRONT | SCRIPT_PASS_COUNT},
	{"", "rtimer", script_repeat_timer, NULL, 2, "iic", "seconds ?microseconds? callback", SCRIPT_INTEGER, SCRIPT_VAR_ARGS | SCRIPT_VAR_FRONT | SCRIPT_PASS_COUNT},
	{"", "killtimer", timer_destroy, NULL, 1, "i", "timer-id", SCRIPT_INTEGER, 0},
	{"", "timers", script_timers, NULL, 0, "", "", 0, SCRIPT_PASS_RETVAL},
	{"", "timer_info", script_timer_info, NULL, 1, "i", "timer-id", 0, SCRIPT_PASS_RETVAL},
	{0}
};
