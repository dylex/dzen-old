/*
* (C)opyright 2007-2009 Robert Manea <rob dot manea at gmail dot com>
* See LICENSE file for license details.
*
*/

#include "dzen.h"
#include "action.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


struct event_lookup ev_lookup_table[] = {
	{ "onstart",        onstart},
	{ "onexit",         onexit},
	{ "onnewinput",     onnewinput},
	{ "button1",        button1},
	{ "button2",        button2},
	{ "button3",        button3},
	{ "button4",        button4},
	{ "button5",        button5},
	{ "button6",        button6},
	{ "button7",        button7},
	{ "entertitle",     entertitle},
	{ "leavetitle",     leavetitle},
	{ "enterslave",     enterslave},
	{ "leaveslave",     leaveslave},
	{ "sigusr1",        sigusr1},
	{ "sigusr2",        sigusr2},
	{ "keymarker",      keymarker},
	{ 0, 0 }
};

struct action_lookup  ac_lookup_table[] = {
	{ "print",          a_print },
	{ "exec",           a_exec},
	{ "exit",           a_exit},
	{ "xevent",         a_xevent},
	{ "collapse",       a_collapse},
	{ "uncollapse",     a_uncollapse},
	{ "togglecollapse",	a_togglecollapse},
	{ "stick",          a_stick},
	{ "unstick",        a_unstick},
	{ "togglestick",    a_togglestick},
	{ "hide",           a_hide},
	{ "unhide",         a_unhide},
	{ "togglehide",     a_togglehide},
	{ "scrollup",       a_scrollup},
	{ "scrolldown",     a_scrolldown},
	{ "menuprint",      a_menuprint},
	{ "menuprint_noparse",      a_menuprint_noparse},
	{ "menuexec",       a_menuexec},
	{ "raise",          a_raise},
	{ "lower",          a_lower},
	{ "scrollhome",     a_scrollhome},
	{ "scrollend",      a_scrollend},
	{ "grabkeys",       a_grabkeys},
	{ "ungrabkeys",     a_ungrabkeys},
	{ "grabmouse",       a_grabmouse},
	{ "ungrabmouse",     a_ungrabmouse},
	{ 0, 0 }
};

ev_list *head = NULL;

static ev_list *
alloc_event(long evid)
{
	ev_list *item;

	item = emalloc(sizeof(ev_list));
	item->id = evid;
	item->next = NULL;
	memset(item->action, 0, sizeof(item->action));
	return item;
}

static void 
free_action_list(ev_list *item)
{
	int i;
	for(i=0; i < MAXACTIONS && item->action[i]; i++)
	{
		free(item->action[i]->options[0]);
		free(item->action[i]);
		item->action[i] = NULL;
	}
}

static ev_list *
new_event(long evid) {
	ev_list *item;

	if(!head) {
		return head = alloc_event(evid);
	}
	item = head;
	/* check if we already handle this event */
	while(1) {
		if(item->id == evid)
		{
			free_action_list(item);
			return item;
		}
		if (!item->next)
			break;
		item = item->next;
	}

	return item->next = alloc_event(evid);
}

static As *
add_handler(ev_list *item, int hpos, handlerf* hcb) {
	if(hpos >= MAXACTIONS)
		return NULL;

	item->action[hpos] = emalloc(sizeof(As));
	item->action[hpos]->handler = hcb;
	item->action[hpos]->options[0] = NULL;
	return item->action[hpos];
}

static void
fill_options(As *as, char *opts) {
	char *dptoken, *saveptr4=NULL;
	int i;

	if (!opts || !*opts)
	{
		as->options[0] = NULL;
		return;
	}

	as->options[0] = estrdup(opts);
	dptoken = strtok_r(as->options[0], ":", &saveptr4);
	for (i = 1; dptoken && i < MAXOPTIONS-1; i++) {
		dptoken = strtok_r(NULL, ":", &saveptr4);
		as->options[i] = dptoken;
	}
}

int
find_event(long evid) {
	ev_list *item;

	item = head;
	while(item) {
		if(item->id == evid)
			return item->id;
		item = item->next;
	}

	return -1;
}

void
do_action(long evid) {
	int i;
	ev_list *item;

	item = head;
	while(item) {
		if(item->id == evid)
			break;
		item = item->next;
	}

	if(item) {
		for(i=0; i < MAXACTIONS && item->action[i]->handler; i++) {
			item->action[i]->handler(item->action[i]->options);
		}
	}
}

int
get_ev_id(const char *evname) {
	int i;
	KeySym ks;

	/* check for keyboard event */
	if((!strncmp(evname, "key_", 4))
			&& ((ks = XStringToKeysym(evname+4)) != NoSymbol)) {
		return ks+keymarker;
	}

	/* own events */
	for(i=0; ev_lookup_table[i].name; i++) {
		if(strncmp(ev_lookup_table[i].name, evname, strlen(ev_lookup_table[i].name)) == 0)
			return ev_lookup_table[i].id;
	}
	return -1;
}

handlerf *
get_action_handler(const char *acname) {
	int i;

	for(i=0; ac_lookup_table[i].name; i++) {
		if(strcmp(ac_lookup_table[i].name, acname) == 0)
			return ac_lookup_table[i].handler;
	}
	return NULL;
}

void
free_event_list(void) {
	ev_list *item;

	while(head) {
		item = head;
		head = item->next;
		free_action_list(item);
		free(item);
	}
}

void
fill_ev_action(long eid, char *action)
{
	char *str3,
		 *kommatoken, *dptoken;
	char *saveptr3=NULL;
	int i=0;
	handlerf *ah=0;
	ev_list *item;
	As *as;

	item = new_event(eid);

	if (action) for (str3 = action; ; str3 = NULL) {
		kommatoken = strtok_r(str3, ",", &saveptr3);
		if (kommatoken == NULL)
			break;

		if ((dptoken = strchr(kommatoken, ':')))
			*dptoken++ = 0;

		ah = get_action_handler(kommatoken);
		if (!ah) /* unknown action */
			continue;

		as = add_handler(item, i++, ah);
		if (as)
			fill_options(as, dptoken);
	}
	add_handler(item, i++, NULL);
}

void
fill_ev_table(char *input) {
	char *str1, *token, *value;
	char *saveptr1=NULL;
	long eid=0;

	for (str1 = input; ; str1 = NULL) {
		token = strtok_r(str1, ";", &saveptr1);
		if (token == NULL)
			break;

		if ((value = strchr(token, '=')))
			*value++ = 0;
		eid = get_ev_id(token);
		if (eid == -1) /* unknown event */
			continue;
		fill_ev_action(eid, value);
	}
}


/* actions */
int
a_exit(char * opt[]) {
	if(opt && opt[0])
		dzen.ret_val = atoi(opt[0]);
	dzen.running = False;
	return 0;
}

int
a_collapse(char * opt[]){
	(void)opt;
	if(!dzen.slave_win.ishmenu
			&& dzen.slave_win.max_lines
			&& !dzen.slave_win.issticky) {
		XUnmapWindow(dzen.dpy, dzen.slave_win.win);
	}
	return 0;
}

int
a_uncollapse(char * opt[]){
	int i;
	(void)opt;
	if(!dzen.slave_win.ishmenu
			&& dzen.slave_win.max_lines
			&& !dzen.slave_win.issticky) {
		XMapRaised(dzen.dpy, dzen.slave_win.win);
		for(i=0; i < dzen.slave_win.max_lines; i++)
			XMapWindow(dzen.dpy, dzen.slave_win.line[i]);
	}
	return 0;
}

int
a_togglecollapse(char * opt[]){
	XWindowAttributes wa;
	(void)opt;

	if(dzen.slave_win.max_lines &&
			(XGetWindowAttributes(dzen.dpy, dzen.slave_win.win, &wa), wa.map_state == IsUnmapped))
		a_uncollapse(NULL);
	else
		a_collapse(NULL);

	return 0;
}

int
a_stick(char * opt[]) {
	(void)opt;
	if(!dzen.slave_win.ishmenu
			&& dzen.slave_win.max_lines)
		dzen.slave_win.issticky = True;
	return 0;
}

int
a_unstick(char * opt[]) {
	(void)opt;
	if(!dzen.slave_win.ishmenu
			&& dzen.slave_win.max_lines)
		dzen.slave_win.issticky = False;
	return 0;
}

int
a_togglestick(char * opt[]) {
	(void)opt;
	if(!dzen.slave_win.ishmenu
			&& dzen.slave_win.max_lines)
		dzen.slave_win.issticky = dzen.slave_win.issticky ? False : True;
	return 0;
}

static void
scroll(int n) {
	if(dzen.slave_win.tcnt <= dzen.slave_win.max_lines)
		return;
	if(dzen.slave_win.first_line_vis + n < 0) {
		dzen.slave_win.first_line_vis = 0;
		dzen.slave_win.last_line_vis = dzen.slave_win.max_lines;
	}
	else if(dzen.slave_win.last_line_vis + n > dzen.slave_win.tcnt) {
		dzen.slave_win.first_line_vis = dzen.slave_win.tcnt - dzen.slave_win.max_lines;
		dzen.slave_win.last_line_vis = dzen.slave_win.tcnt;
	}
	else {
		dzen.slave_win.first_line_vis += n;
		dzen.slave_win.last_line_vis  += n;
	}

	x_draw_body();
}

int
a_scrollup(char * opt[]) {
	int n=1;

	if(opt && opt[0])
		n = atoi(opt[0]);
	if(dzen.slave_win.max_lines)
			scroll(-1*n);

	return 0;
}

int
a_scrolldown(char * opt[]) {
	int n=1;

	if(opt && opt[0])
		n = atoi(opt[0]);
	if(dzen.slave_win.max_lines)
		scroll(n);

	return 0;
}

int
a_hide(char * opt[]) {
	int n=1;
	
	
	printf("n:%d\n", n);
	if(!dzen.title_win.ishidden) {
		if(!dzen.slave_win.ishmenu)
			XResizeWindow(dzen.dpy, dzen.title_win.win, dzen.title_win.width, 1);
		else
			XResizeWindow(dzen.dpy, dzen.slave_win.win, dzen.title_win.width, 1);

		dzen.title_win.ishidden = True;
	}
	return 0;
}

int
a_unhide(char * opt[]) {
	(void)opt;
	if(dzen.title_win.ishidden) {
		if(!dzen.slave_win.ishmenu)
			XResizeWindow(dzen.dpy, dzen.title_win.win, dzen.title_win.width, dzen.line_height);
		else
			XResizeWindow(dzen.dpy, dzen.slave_win.win, dzen.title_win.width, dzen.line_height);

		dzen.title_win.ishidden = False;
	}
	return 0;
}

int
a_togglehide(char * opt[]) {

	dzen.title_win.ishidden ?
		a_unhide(NULL) :
		a_hide(opt);

	return 0;
}

int
a_exec(char * opt[]) {
	int i;

	if(opt)
		for(i=0; opt[i]; i++)
			if(opt[i]) spawn(opt[i]);
	return 0;
}

int
a_xevent(char * opt[]) {
	XClientMessageEvent e = { ClientMessage };
	int i;

	if(!opt || !opt[0] || !*opt[0])
		return 0;

	e.display = dzen.dpy;
	e.window = RootWindow(dzen.dpy, dzen.screen);
	e.message_type = XInternAtom(dzen.dpy, opt[0], True);
	if(e.message_type == None)
		return 0;
	e.format = 32;
	for(i=0; i < 5 && opt[i+1]; i++)
		e.data.l[i] = atoi(opt[i+1]);

	XSendEvent(e.display, e.window, False, StructureNotifyMask, (XEvent *)&e);

	return 0;
}

int
a_print(char * opt[]) {
	int i;

	if(opt)
		for(i=0; opt[i]; i++)
			puts(opt[i]);
	return 0;
}

int
a_menuprint(char * opt[]) {
	char *text;
	int i;

	if(dzen.slave_win.ismenu && dzen.slave_win.sel_line != -1
			&& (dzen.slave_win.sel_line + dzen.slave_win.first_line_vis) < dzen.slave_win.tcnt) {
		text = parse_line(NULL, dzen.slave_win.sel_line, 0, 0, 1);
		printf("%s", text);
		if(opt)
			for(i=0; opt[i]; ++i)
				printf("%s", opt[i]);
		puts("");
		fflush(stdout);
		dzen.slave_win.sel_line = -1;
		free(text);
	}
	return 0;
}

int
a_menuprint_noparse(char * opt[]) {
	int i;

	if(dzen.slave_win.ismenu && dzen.slave_win.sel_line != -1
			&& (dzen.slave_win.sel_line + dzen.slave_win.first_line_vis) < dzen.slave_win.tcnt) {
		printf("%s", dzen.slave_win.tbuf[dzen.slave_win.sel_line]);
		if(opt)
			for(i=0; opt[i]; ++i)
				printf("%s", opt[i]);
		puts("");
		fflush(stdout);
		dzen.slave_win.sel_line = -1;
	}
	return 0;
}

int
a_menuexec(char * opt[]) {
	char *text;
	(void)opt;

	if(dzen.slave_win.ismenu && dzen.slave_win.sel_line != -1
			&& (dzen.slave_win.sel_line + dzen.slave_win.first_line_vis) < dzen.slave_win.tcnt) {
		text = parse_line(NULL, dzen.slave_win.sel_line, 0, 0, 1);
		spawn(text);
		dzen.slave_win.sel_line = -1;
		free(text);
	}
	return 0;
}

int
a_raise(char * opt[]) {
	(void)opt;
	XRaiseWindow(dzen.dpy, dzen.title_win.win);

	if(dzen.slave_win.max_lines)
		XRaiseWindow(dzen.dpy, dzen.slave_win.win);
	return 0;
}

int
a_lower(char * opt[]) {
	(void)opt;
	XLowerWindow(dzen.dpy, dzen.title_win.win);

	if(dzen.slave_win.max_lines)
		XLowerWindow(dzen.dpy, dzen.slave_win.win);
	return 0;
}

int
a_scrollhome(char * opt[]) {
	(void)opt;
	if(dzen.slave_win.max_lines) {
		dzen.slave_win.first_line_vis = 0;
		dzen.slave_win.last_line_vis  = dzen.slave_win.max_lines;

		x_draw_body();
	}
	return 0;
}

int
a_scrollend(char * opt[]) {
	(void)opt;
	if(dzen.slave_win.max_lines) {
		dzen.slave_win.first_line_vis = dzen.slave_win.tcnt - dzen.slave_win.max_lines ;
		dzen.slave_win.last_line_vis  = dzen.slave_win.tcnt;

		x_draw_body();
	}
	return 0;
}

int
a_grabkeys(char * opt[]) {
	(void)opt;
	XGrabKeyboard(dzen.dpy, RootWindow(dzen.dpy, dzen.screen),
			True, GrabModeAsync, GrabModeAsync, CurrentTime);
	return 0;
}

int
a_ungrabkeys(char * opt[]) {
	(void)opt;
	XUngrabKeyboard(dzen.dpy, CurrentTime);
	return 0;
}

int
a_grabmouse(char * opt[]) {
	(void)opt;
	XGrabPointer(dzen.dpy, RootWindow(dzen.dpy, dzen.screen),
			True, ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	return 0;
}

int
a_ungrabmouse(char * opt[]) {
	(void)opt;
	XUngrabPointer(dzen.dpy, CurrentTime);
	return 0;
}

