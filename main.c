/*  
*  (C)opyright MMVII Robert Manea <rob dot manea at gmail dot com>
*  See LICENSE file for license details.
*
*/
#include "dzen.h"
#include "action.h"

#include <ctype.h>
#include <locale.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>

Dzen dzen = {0};
static int last_cnt = 0;
typedef void sigfunc(int);


static void
catch_sigusr1() {
    do_action(sigusr1);
}

static void
catch_sigusr2() {
    do_action(sigusr2);
}

static void
catch_sigterm() {
    do_action(onexit);
}

static void
catch_alrm() {
    do_action(onexit);
    exit(0);
}

static sigfunc *
setup_signal(int signr, sigfunc *shandler) {
    struct sigaction nh, oh;

    nh.sa_handler = shandler;
    sigemptyset(&nh.sa_mask);
    nh.sa_flags = 0;

    if(sigaction(signr, &nh, &oh) < 0)
        return SIG_ERR;

    return NULL;
}

char *rem=NULL;
static int
chomp(char *inbuf, char *outbuf, int start, int len) {
    int i=0;
    int off=start;

    if(rem) {
        strncpy(outbuf, rem, strlen(rem));
        i += strlen(rem);
        free(rem);
        rem = NULL;
    }
    while(off < len) {
        if(inbuf[off] != '\n') {
            outbuf[i++] = inbuf[off++];
        } else if(inbuf[off] == '\n') {
            outbuf[i] = '\0';
            return ++off;
        }
    }

    outbuf[i] = '\0';
    rem = estrdup(outbuf);
    return 0;
}

void
free_buffer(void) {
    int i;
    for(i=0; i<BUF_SIZE; i++)
        free(dzen.slave_win.tbuf[i]);
    dzen.slave_win.tcnt = 0;
    last_cnt = 0;
}

static int 
read_stdin(void *ptr) {
    char buf[1024], retbuf[2048];
    ssize_t n, n_off=0;

    if(!(n = read(STDIN_FILENO, buf, sizeof buf))) {
        if(!dzen.ispersistent) {
            dzen.running = False;
            return -1;
        } else 
            return -2;
    } else {
        while((n_off = chomp(buf, retbuf, n_off, n))) {
            if(!dzen.cur_line || !dzen.slave_win.max_lines) {
                drawheader(retbuf);
            }
            else 
                drawbody(retbuf);
            dzen.cur_line++;
        }
    }
    return 0;
}

static void
x_highlight_line(int line) {
    drawtext(dzen.slave_win.tbuf[line + dzen.slave_win.first_line_vis], 1, line+1, dzen.slave_win.alignment);
    XCopyArea(dzen.dpy, dzen.slave_win.drawable, dzen.slave_win.line[line], dzen.rgc,
            0, 0, dzen.slave_win.width, dzen.line_height, 0, 0);
}

static void
x_unhighlight_line(int line) {
    drawtext(dzen.slave_win.tbuf[line + dzen.slave_win.first_line_vis], 0, line+1, dzen.slave_win.alignment);
    XCopyArea(dzen.dpy, dzen.slave_win.drawable, dzen.slave_win.line[line], dzen.gc,
            0, 0, dzen.slave_win.width, dzen.line_height, 0, 0);
}

void 
x_draw_body(void) {
    dzen.x = 0;
    dzen.y = 0;
    dzen.w = dzen.slave_win.width;
    dzen.h = dzen.line_height;
    int i;

    if(!dzen.slave_win.last_line_vis) {
        if(dzen.slave_win.tcnt < dzen.slave_win.max_lines) {
            dzen.slave_win.first_line_vis = 0;
            dzen.slave_win.last_line_vis  = dzen.slave_win.tcnt;
        }
        if(dzen.slave_win.tcnt >= dzen.slave_win.max_lines) {
            dzen.slave_win.first_line_vis = dzen.slave_win.tcnt - dzen.slave_win.max_lines;
            dzen.slave_win.last_line_vis  = dzen.slave_win.tcnt;
        }
    }

    for(i=0; i < dzen.slave_win.max_lines; i++) {
        if(i < dzen.slave_win.last_line_vis) {
            drawtext(dzen.slave_win.tbuf[i + dzen.slave_win.first_line_vis], 0, i, dzen.slave_win.alignment);
            XCopyArea(dzen.dpy, dzen.slave_win.drawable, dzen.slave_win.line[i], dzen.gc, 
                    0, 0, dzen.slave_win.width, dzen.line_height, 0, 0);
        }
        else if(i < dzen.slave_win.max_lines) {
            drawtext("", 0, i, dzen.slave_win.alignment);
            XCopyArea(dzen.dpy, dzen.slave_win.drawable, dzen.slave_win.line[i], dzen.gc,
                    0, 0, dzen.slave_win.width, dzen.line_height, 0, 0);
        }
    }
}

static void
x_check_geometry(XRectangle si) {
        if(dzen.title_win.x > si.width)
            dzen.title_win.x = si.x;
        if (dzen.title_win.x < si.x)
            dzen.title_win.x = si.x;

        if(!dzen.title_win.width)
            dzen.title_win.width = si.width;

        if((dzen.title_win.x + dzen.title_win.width) > (si.x + si.width))
            dzen.title_win.width = si.width - (dzen.title_win.x - si.x);

        if(!dzen.slave_win.width) {
            dzen.slave_win.x = si.x;
            dzen.slave_win.width = si.width;
        }
        if( dzen.title_win.width == dzen.slave_win.width) {
            dzen.slave_win.x = dzen.title_win.x;
            dzen.slave_win.width = dzen.title_win.width;
        }
        if(dzen.slave_win.width != si.width) {
            dzen.slave_win.x = dzen.title_win.x + (dzen.title_win.width - dzen.slave_win.width)/2;
            if(dzen.slave_win.x < si.x)
                dzen.slave_win.x = si.x;
            if(dzen.slave_win.width > si.width)
                dzen.slave_win.width = si.width;
            if(dzen.slave_win.x + dzen.slave_win.width >  si.width)
                dzen.slave_win.x = si.x + (si.width - dzen.slave_win.width);
        }
        dzen.line_height = dzen.font.height + 2;
        dzen.title_win.y = si.y + ((dzen.title_win.y + dzen.line_height) > si.height ? 0 : dzen.title_win.y); 
    }

static void
qsi_no_xinerama(Display *dpy, XRectangle *rect) {
    rect->x = 0;
    rect->y = 0;
    rect->width  = DisplayWidth( dpy, DefaultScreen(dpy));
    rect->height = DisplayHeight(dpy, DefaultScreen(dpy));
}

#ifdef DZEN_XINERAMA
static void
queryscreeninfo(Display *dpy, XRectangle *rect, int screen) {
    XineramaScreenInfo *xsi = NULL;
    int nscreens = 1;

    if(XineramaIsActive(dpy))
        xsi = XineramaQueryScreens(dpy, &nscreens);

    if(xsi == NULL || screen > nscreens || screen <= 0) {
        qsi_no_xinerama(dpy, rect);
    } else {
        rect->x      = xsi[screen-1].x_org;
        rect->y      = xsi[screen-1].y_org;
        rect->width  = xsi[screen-1].width;
        rect->height = xsi[screen-1].height;
    }
}
#endif

static void
x_create_windows(void) {
    XSetWindowAttributes wa;
    Window root;
    int i;
    XRectangle si;

    dzen.dpy = XOpenDisplay(0);
    if(!dzen.dpy) 
        eprint("dzen: cannot open display\n");

    dzen.screen = DefaultScreen(dzen.dpy);
    root = RootWindow(dzen.dpy, dzen.screen);

    /* style */
    dzen.norm[ColBG] = getcolor(dzen.bg);
    dzen.norm[ColFG] = getcolor(dzen.fg);
    setfont(dzen.fnt);

    /* window attributes */
    wa.override_redirect = 1;
    wa.background_pixmap = ParentRelative;
    wa.event_mask = ExposureMask | ButtonReleaseMask | EnterWindowMask | LeaveWindowMask;

#ifdef DZEN_XINERAMA
    queryscreeninfo(dzen.dpy, &si, dzen.xinescreen);
#else
    qsi_no_xinerama(dzen.dpy, &si);
#endif

    x_check_geometry(si);

    /* title window */
    dzen.title_win.win = XCreateWindow(dzen.dpy, root, 
            dzen.title_win.x, dzen.title_win.y, dzen.title_win.width, dzen.line_height, 0,
            DefaultDepth(dzen.dpy, dzen.screen), CopyFromParent,
            DefaultVisual(dzen.dpy, dzen.screen),
            CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
    dzen.title_win.drawable = XCreatePixmap(dzen.dpy, root, dzen.title_win.width, 
            dzen.line_height, DefaultDepth(dzen.dpy, dzen.screen));

    /* slave window */
    if(dzen.slave_win.max_lines) {
        dzen.slave_win.first_line_vis = 0;
        dzen.slave_win.last_line_vis  = 0;
        dzen.slave_win.issticky = False;
        dzen.slave_win.y = dzen.title_win.y + dzen.line_height;

        if(dzen.title_win.y + dzen.line_height*dzen.slave_win.max_lines > si.height)
            dzen.slave_win.y = (dzen.title_win.y - dzen.line_height) - dzen.line_height*(dzen.slave_win.max_lines) + dzen.line_height;

        dzen.slave_win.win = XCreateWindow(dzen.dpy, root, 
                dzen.slave_win.x, dzen.slave_win.y, dzen.slave_win.width, dzen.slave_win.max_lines * dzen.line_height, 0,
                DefaultDepth(dzen.dpy, dzen.screen), CopyFromParent,
                DefaultVisual(dzen.dpy, dzen.screen),
                CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);

        dzen.slave_win.drawable = XCreatePixmap(dzen.dpy, root, dzen.slave_win.width, 
                dzen.line_height, DefaultDepth(dzen.dpy, dzen.screen));

        /* windows holding the lines */
        dzen.slave_win.line = emalloc(sizeof(Window) * dzen.slave_win.max_lines);
        for(i=0; i < dzen.slave_win.max_lines; i++) {
            dzen.slave_win.line[i] = XCreateWindow(dzen.dpy, dzen.slave_win.win, 
                    0, i*dzen.line_height, dzen.slave_win.width, dzen.line_height, 0,
                    DefaultDepth(dzen.dpy, dzen.screen), CopyFromParent,
                    DefaultVisual(dzen.dpy, dzen.screen),
                    CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
        }

    }
    /* normal GC */
    dzen.gc  = XCreateGC(dzen.dpy, root, 0, 0);
    XSetForeground(dzen.dpy, dzen.gc, dzen.norm[ColFG]);
    XSetBackground(dzen.dpy, dzen.gc, dzen.norm[ColBG]);
    /* reverse GC */
    dzen.rgc = XCreateGC(dzen.dpy, root, 0, 0);
    XSetForeground(dzen.dpy, dzen.rgc, dzen.norm[ColBG]);
    XSetBackground(dzen.dpy, dzen.rgc, dzen.norm[ColFG]);
}

static void 
x_map_window(Window win) {
    XMapRaised(dzen.dpy, win);
    XSync(dzen.dpy, False);
}

static void
handle_xev(void) {
    XEvent ev;
    int i;

    XNextEvent(dzen.dpy, &ev);
    switch(ev.type) {
        case Expose:
            if(ev.xexpose.count == 0) {
                if(ev.xexpose.window == dzen.title_win.win) 
                    drawheader(NULL);
                if(ev.xexpose.window == dzen.slave_win.win)
                    do_action(exposeslave);
                for(i=0; i < dzen.slave_win.max_lines; i++) 
                    if(ev.xcrossing.window == dzen.slave_win.line[i])
                        do_action(exposeslave);
            }
            XSync(dzen.dpy, False);
            break;
        case EnterNotify:
            if(dzen.slave_win.ismenu) { 
                for(i=0; i < dzen.slave_win.max_lines; i++) 
                    if(ev.xcrossing.window == dzen.slave_win.line[i])
                        x_highlight_line(i);
            }
            if(ev.xcrossing.window == dzen.title_win.win)
                do_action(entertitle);
            if(ev.xcrossing.window == dzen.slave_win.win)
                do_action(enterslave);
            XSync(dzen.dpy, False);
            break;
        case LeaveNotify:
            if(dzen.slave_win.ismenu) {
                for(i=0; i < dzen.slave_win.max_lines; i++)
                    if(ev.xcrossing.window == dzen.slave_win.line[i])
                        x_unhighlight_line(i);
            }
            if(ev.xcrossing.window == dzen.title_win.win)
                do_action(leavetitle);
            if(ev.xcrossing.window == dzen.slave_win.win) {
                do_action(leaveslave);
            }
            XSync(dzen.dpy, False);
            break;
        case ButtonRelease:
            if(dzen.slave_win.ismenu) {
                for(i=0; i < dzen.slave_win.max_lines; i++) 
                    if(ev.xbutton.window == dzen.slave_win.line[i]) 
                        dzen.slave_win.sel_line = i;
            }
            switch(ev.xbutton.button) {
                case Button1:
                    do_action(button1);
                    break;
                case Button2:
                    do_action(button2);
                    break;
                case Button3:
                    do_action(button3);
                    break;
                case Button4:
                    do_action(button4);
                    break;
                case Button5:
                    do_action(button5);
                    break;
            }
            XSync(dzen.dpy, False);
            break;
    }
    XFlush(dzen.dpy);
}

static void
handle_newl(void) {
    XWindowAttributes wa;

    if(dzen.slave_win.max_lines && (dzen.slave_win.tcnt > last_cnt)) {
        if (XGetWindowAttributes(dzen.dpy, dzen.slave_win.win, &wa),
                wa.map_state != IsUnmapped
                /* scroll only if we're viewing the last line of input */
                && (dzen.slave_win.last_line_vis == last_cnt)) {
            dzen.slave_win.first_line_vis = 0;
            dzen.slave_win.last_line_vis = 0;
        } else if(wa.map_state == IsUnmapped || !dzen.slave_win.last_line_vis) {
            dzen.slave_win.first_line_vis = 0;
            dzen.slave_win.last_line_vis = 0;
        }
        do_action(exposeslave);
        last_cnt = dzen.slave_win.tcnt;
    }
}

static int
event_loop(void *ptr) {
    int xfd, ret, dr=0;
    fd_set rmask;

    /* fill background until data is available */
    drawheader("");

    xfd = ConnectionNumber(dzen.dpy);
    while(dzen.running) {
        FD_ZERO(&rmask);
        FD_SET(xfd, &rmask);
        if(dr != -2)
            FD_SET(STDIN_FILENO, &rmask);

        while(XPending(dzen.dpy))
            handle_xev();

        ret = select(xfd+1, &rmask, NULL, NULL, NULL);
        if(ret) {
            if(dr != -2 && FD_ISSET(STDIN_FILENO, &rmask)) {
                if((dr = read_stdin(NULL)) == -1)
                    return;
                handle_newl();
            }
            if(dr == -2 && dzen.timeout > 0) {
                /* Set an alarm to kill us after the timeout */
                struct itimerval t;
                memset(&t, 0, sizeof t);
                t.it_value.tv_sec = dzen.timeout;
                t.it_value.tv_usec = 0;
                setitimer(ITIMER_REAL, &t, NULL);
            }
            if(FD_ISSET(xfd, &rmask))
                handle_xev();
        }
    }
    return; 
}

static void 
clean_up(void) {
    int i;

    free_ev_table();
    if(dzen.font.set)
        XFreeFontSet(dzen.dpy, dzen.font.set);
    else
        XFreeFont(dzen.dpy, dzen.font.xfont);

    XFreePixmap(dzen.dpy, dzen.title_win.drawable);
    if(dzen.slave_win.max_lines) {
        XFreePixmap(dzen.dpy, dzen.slave_win.drawable);
        for(i=0; i < dzen.slave_win.max_lines; i++) 
            XDestroyWindow(dzen.dpy, dzen.slave_win.line[i]);
        free(dzen.slave_win.line);
        XDestroyWindow(dzen.dpy, dzen.slave_win.win);
    }
    XFreeGC(dzen.dpy, dzen.gc);
    XDestroyWindow(dzen.dpy, dzen.title_win.win);
    XCloseDisplay(dzen.dpy);
}

static void
set_alignment(void)
{
    if(dzen.title_win.alignment) {
        switch(dzen.title_win.alignment) {
            case 'l': 
                dzen.title_win.alignment = ALIGNLEFT;
                break;
            case 'c':
                dzen.title_win.alignment = ALIGNCENTER;
                break;
            case 'r':
                dzen.title_win.alignment = ALIGNRIGHT;
                break;
            default:
                dzen.title_win.alignment = ALIGNCENTER;
        }
    }
    if(dzen.slave_win.alignment) {
        switch(dzen.slave_win.alignment) {
            case 'l': 
                dzen.slave_win.alignment = ALIGNLEFT;
                break;
            case 'c':
                dzen.slave_win.alignment = ALIGNCENTER;
                break;
            case 'r':
                dzen.slave_win.alignment = ALIGNRIGHT;
                break;
            default:
                dzen.slave_win.alignment = ALIGNLEFT;
        }
    }
}

int
main(int argc, char *argv[]) {
    int i;
    char *action_string = NULL;
    char *endptr;

    /* default values */
    dzen.cur_line  = 0;
    dzen.ret_val   = 0;
    dzen.title_win.x = dzen.slave_win.x = 0;
    dzen.title_win.y = 0;
    dzen.title_win.width = dzen.slave_win.width = 0;
    dzen.title_win.alignment = ALIGNCENTER;
    dzen.slave_win.alignment = ALIGNLEFT;
    dzen.fnt = FONT;
    dzen.bg  = BGCOLOR;
    dzen.fg  = FGCOLOR;
    dzen.slave_win.max_lines  = 0;
    dzen.running = True;
    dzen.xinescreen = 0;

    /* cmdline args */
    for(i = 1; i < argc; i++)
        if(!strncmp(argv[i], "-l", 3)){
            if(++i < argc) dzen.slave_win.max_lines = atoi(argv[i]);
        }
        else if(!strncmp(argv[i], "-p", 3)) {
            dzen.ispersistent = True;
            /* see if the next argument looks like a valid number */
            if (i + 1 < argc) {
                dzen.timeout = strtol(argv[i + 1], &endptr, 10);
                if (*endptr) {
                    dzen.timeout = 0;
                } else {
                    i++;
                }
            }
        }
        else if(!strncmp(argv[i], "-ta", 4)) {
            if(++i < argc) dzen.title_win.alignment = argv[i][0];
        }
        else if(!strncmp(argv[i], "-sa", 4)) {
            if(++i < argc) dzen.slave_win.alignment = argv[i][0];
        }
        else if(!strncmp(argv[i], "-m", 3)) {
            dzen.slave_win.ismenu = True;
        }
        else if(!strncmp(argv[i], "-fn", 4)) {
            if(++i < argc) dzen.fnt = argv[i];
        }
        else if(!strncmp(argv[i], "-e", 3)) {
            if(++i < argc) action_string = argv[i];
        }
        else if(!strncmp(argv[i], "-bg", 4)) {
            if(++i < argc) dzen.bg = argv[i];
        }
        else if(!strncmp(argv[i], "-fg", 4)) {
            if(++i < argc) dzen.fg = argv[i];
        }
        else if(!strncmp(argv[i], "-x", 3)) {
            if(++i < argc) dzen.title_win.x = dzen.slave_win.x = atoi(argv[i]);
        }
        else if(!strncmp(argv[i], "-y", 3)) {
            if(++i < argc) dzen.title_win.y = atoi(argv[i]);
        }
        else if(!strncmp(argv[i], "-w", 3)) {
            if(++i < argc) dzen.slave_win.width = atoi(argv[i]);
        }
        else if(!strncmp(argv[i], "-tw", 3)) {
            if(++i < argc) dzen.title_win.width = atoi(argv[i]);
        }
#ifdef DZEN_XINERAMA
        else if(!strncmp(argv[i], "-xs", 4)) {
            if(++i < argc) dzen.xinescreen = atoi(argv[i]);
        }
#endif
        else if(!strncmp(argv[i], "-v", 3)) 
            eprint("dzen-"VERSION", (C)opyright 2007 Robert Manea\n");
        else
            eprint("usage: dzen2 [-v] [-p [timeout]] [-m] [-ta <l|c|r>] [-sa <l|c|r>] [-tw <pixel>]\n"
                    "             [-e <string>] [-x <pixel>] [-y <pixel>]  [-w <pixel>]    \n"
                    "             [-l <lines>]  [-fn <font>] [-bg <color>] [-fg <color>]   \n"
#ifdef DZEN_XINERAMA
                    "             [-xs <screen>]\n"
#endif
                  );

    if(!dzen.title_win.width)
        dzen.title_win.width = dzen.slave_win.width;

    if(!setlocale(LC_ALL, "") || !XSupportsLocale())
        puts("dzen: locale not available, expect problems with fonts.\n");

    if(action_string) {
        char edef[] = "exposet=exposetitle;exposes=exposeslave";
        fill_ev_table(edef);
        fill_ev_table(action_string);
    } else {
        char edef[] = "exposet=exposetitle;exposes=exposeslave;"
            "entertitle=uncollapse;leaveslave=collapse;"
            "button1=menuexec;button2=togglestick;button3=exit:13;"
            "button4=scrollup;button5=scrolldown";
        fill_ev_table(edef);
    }

    if(ev_table[onexit].isset && (setup_signal(SIGTERM, catch_sigterm) == SIG_ERR))
        fprintf(stderr, "dzen: error hooking SIGTERM\n");
    if(ev_table[sigusr1].isset && (setup_signal(SIGUSR1, catch_sigusr1) == SIG_ERR))
        fprintf(stderr, "dzen: error hooking SIGUSR1\n");
    if(ev_table[sigusr2].isset && (setup_signal(SIGUSR2, catch_sigusr2) == SIG_ERR))
        fprintf(stderr, "dzen: error hooking SIGUSR2\n");
    if(setup_signal(SIGALRM, catch_alrm) == SIG_ERR)
        fprintf(stderr, "dzen: error hooking SIGALARM\n");

    set_alignment();
    x_create_windows();
    x_map_window(dzen.title_win.win);

    do_action(onstart);

    /* main loop */
    event_loop(NULL);

    do_action(onexit);
    clean_up();

    if(dzen.ret_val)
        return dzen.ret_val;

    return EXIT_SUCCESS;
}
