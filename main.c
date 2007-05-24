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
#include <errno.h>
#include <signal.h>

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

sigfunc *
setup_signal(int signr, sigfunc *shandler) {
    struct sigaction nh, oh;

    nh.sa_handler = shandler;
    sigemptyset(&nh.sa_mask);
    nh.sa_flags = 0;

    if(sigaction(signr, &nh, &oh) < 0)
        return SIG_ERR;

    return NULL;
}

static void
chomp(char *buf, unsigned int len) {
    if(buf && (buf[len-1] == '\n'))
        buf[len-1] = '\0';
}

void
free_buffer(void) {
    int i;
    for(i=0; i<BUF_SIZE; i++)
        free(dzen.slave_win.tbuf[i]);
    dzen.slave_win.tcnt = 0;
    last_cnt = 0;
}

void *
read_stdin(void *ptr) {
    char buf[1024], *text = NULL;

    /* draw background until data is available */
    drawheader("");

    while(dzen.running) {
        text = fgets(buf, sizeof buf, stdin);
        if(feof(stdin) && !dzen.slave_win.ispersistent) {
            dzen.running = False;
            break;
        }
        if(feof(stdin) && dzen.slave_win.ispersistent) 
            break;

        if(text) {
            chomp(text, strlen(text));

            if(!dzen.cur_line || !dzen.slave_win.max_lines) {
                drawheader(text);
            }
            else 
                drawbody(text);
            dzen.cur_line++;
        }
    }
    return NULL;
}

static void
x_resize_header(int width, int height) {
    XResizeWindow(dzen.dpy, dzen.title_win.win, width, height);
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

    pthread_mutex_lock(&dzen.mt);
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
    pthread_mutex_unlock(&dzen.mt);
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
event_loop(void *ptr) {
    XEvent ev;
    XWindowAttributes wa;
    int i;

    while(dzen.running) {
        if(dzen.slave_win.max_lines && (dzen.slave_win.tcnt > last_cnt)) {
            if (XGetWindowAttributes(dzen.dpy, dzen.slave_win.win, &wa),
                    wa.map_state != IsUnmapped) {
                dzen.slave_win.first_line_vis = 0;
                dzen.slave_win.last_line_vis = 0;
                do_action(exposeslave);
            }
            last_cnt = dzen.slave_win.tcnt;
        }

        if(XPending(dzen.dpy)) {
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
            }
            XFlush(dzen.dpy);
        } else
            usleep(10000);
    }
}

static void 
clean_up(void) {
    int i;

    if(!dzen.running) 
        pthread_cancel(dzen.read_thread);

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
            dzen.slave_win.ispersistent = True;
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
            eprint("usage: dzen2 [-v] [-p] [-m] [-ta <l|c|r>] [-sa <l|c|r>] [-tw <pixel>]\n"
                   "             [-e <string>] [-x <pixel>] [-y <pixel>]  [-w <pixel>]    \n"
                   "             [-l <lines>]  [-fn <font>] [-bg <color>] [-fg <color>]   \n"
#ifdef DZEN_XINERAMA
                   "             [-xs <screen>]\n"
#endif
                   );

    if(!dzen.title_win.width)
        dzen.title_win.width = dzen.slave_win.width;

    if(!XInitThreads())
        eprint("dzen: no multithreading support in xlib.\n");
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

    set_alignment();
    x_create_windows();
    x_map_window(dzen.title_win.win);

    /* reader */
    pthread_create(&dzen.read_thread, NULL, read_stdin, NULL);

    do_action(onstart);
    
    /* main event loop */
    event_loop(NULL);

    do_action(onexit);
    clean_up();
    
    if(dzen.ret_val)
        return dzen.ret_val;

    return EXIT_SUCCESS;
}
