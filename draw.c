
/* 
* (C)opyright MMVII Robert Manea <rob dot manea at gmail dot com>
* See LICENSE file for license details.
*
*/

#include "dzen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef DZEN_XPM
#include <X11/xpm.h>
#endif

#define ARGLEN 256

int otx;

/* command types for the in-text parser */
enum ctype {bg, fg, icon, rect, recto, circle, circleo, pos, abspos, titlewin, ibg, sa};

int get_tokval(const char* line, char **retdata);
int get_token(const char*  line, int * t, char **tval);

static unsigned int
textnw(const char *text, unsigned int len) {
	XRectangle r;

	if(dzen.font.set) {
		XmbTextExtents(dzen.font.set, text, len, NULL, &r);
		return r.width;
	}
	return XTextWidth(dzen.font.xfont, text, len);
}


void
drawtext(const char *text, int reverse, int line, int align) {
	if(!reverse) {
		XSetForeground(dzen.dpy, dzen.gc, dzen.norm[ColBG]);
		XFillRectangle(dzen.dpy, dzen.slave_win.drawable[line], dzen.gc, 0, 0, dzen.w, dzen.h);
		XSetForeground(dzen.dpy, dzen.gc, dzen.norm[ColFG]);
	}
	else {
		XSetForeground(dzen.dpy, dzen.rgc, dzen.norm[ColFG]);
		XFillRectangle(dzen.dpy, dzen.slave_win.drawable[line], dzen.rgc, 0, 0, dzen.w, dzen.h);
		XSetForeground(dzen.dpy, dzen.rgc, dzen.norm[ColBG]);
	}

	parse_line(text, line, align, reverse, 0);
}

long
getcolor(const char *colstr) {
	Colormap cmap = DefaultColormap(dzen.dpy, dzen.screen);
	XColor color;

	if(!XAllocNamedColor(dzen.dpy, cmap, colstr, &color, &color))
		return -1;

	return color.pixel;
}

void
setfont(const char *fontstr) {
	char *def, **missing;
	int i, n;

	missing = NULL;
	if(dzen.font.set)
		XFreeFontSet(dzen.dpy, dzen.font.set);
	dzen.font.set = XCreateFontSet(dzen.dpy, fontstr, &missing, &n, &def);
	if(missing)
		XFreeStringList(missing);
	if(dzen.font.set) {
		XFontSetExtents *font_extents;
		XFontStruct **xfonts;
		char **font_names;
		dzen.font.ascent = dzen.font.descent = 0;
		font_extents = XExtentsOfFontSet(dzen.font.set);
		n = XFontsOfFontSet(dzen.font.set, &xfonts, &font_names);
		for(i = 0, dzen.font.ascent = 0, dzen.font.descent = 0; i < n; i++) {
			if(dzen.font.ascent < (*xfonts)->ascent)
				dzen.font.ascent = (*xfonts)->ascent;
			if(dzen.font.descent < (*xfonts)->descent)
				dzen.font.descent = (*xfonts)->descent;
			xfonts++;
		}
	}
	else {
		if(dzen.font.xfont)
			XFreeFont(dzen.dpy, dzen.font.xfont);
		dzen.font.xfont = NULL;
		if(!(dzen.font.xfont = XLoadQueryFont(dzen.dpy, fontstr)))
			eprint("dzen: error, cannot load font: '%s'\n", fontstr);
		dzen.font.ascent = dzen.font.xfont->ascent;
		dzen.font.descent = dzen.font.xfont->descent;
	}
	dzen.font.height = dzen.font.ascent + dzen.font.descent;
}

unsigned int
textw(const char *text) {
	return textnw(text, strlen(text)) + dzen.font.height;
}

int
get_tokval(const char* line, char **retdata) {
	int i;
	char tokval[ARGLEN];

	for(i=0; i < ARGLEN && (*(line+i) != ')'); i++)
		tokval[i] = *(line+i);

	tokval[i] = '\0';
	*retdata = strdup(tokval);

	return i+1;
}

int
get_token(const char *line, int * t, char **tval) {
	int off=0, next_pos=0;
	char *tokval = NULL;

	if(*(line+1) == ESC_CHAR)
		return 0;

	line++;
	/* ^bg(#rrggbb) background color, type: bg */
	if( (*line == 'b') && (*(line+1) == 'g') && (*(line+2) == '(')) {
		off = 3;
		next_pos = get_tokval(line+off, &tokval);
		*t = bg;
	}
	/* ^ib(bool) ignore background color, type: ibg */
	else if((*line == 'i') && (*(line+1) == 'b') && (*(line+2) == '(')) {
		off=3;
		next_pos = get_tokval(line+off, &tokval);
		*t = ibg;
	}
	/* ^fg(#rrggbb) foreground color, type: fg */
	else if((*line == 'f') && (*(line+1) == 'g') && (*(line+2) == '(')) {
		off=3;
		next_pos = get_tokval(line+off, &tokval);
		*t = fg;
	}
	/* ^tw() draw to title window, type: titlewin */
	else if((*line == 't') && (*(line+1) == 'w') && (*(line+2) == '(')) {
		off=3;
		next_pos = get_tokval(line+off, &tokval);
		*t = titlewin;
	}
	/* ^i(iconname) bitmap icon, type: icon */
	else if((*line == 'i') && (*(line+1) == '(')) {
		off = 2;
		next_pos = get_tokval(line+off, &tokval);
		*t = icon;
	}
	/* ^r(widthxheight) filled rectangle, type: rect */
	else if((*line == 'r') && (*(line+1) == '(')) {
		off = 2;
		next_pos = get_tokval(line+off, &tokval);
		*t = rect;
	}
	/* ^ro(widthxheight) outlined rectangle, type: recto */
	else if((*line == 'r') && (*(line+1) == 'o') && (*(line+2) == '(')) {
		off = 3;
		next_pos = get_tokval(line+off, &tokval);
		*t = recto;
	}
	/* ^p(pixel) relative position, type: pos */
	else if((*line == 'p') && (*(line+1) == '(')) {
		off = 2;
		next_pos = get_tokval(line+off, &tokval);
		*t = pos;
	}
	/* ^pa(pixel) absolute position, type: pos */
	else if((*line == 'p') && (*(line+1) == 'a') && (*(line+2) == '(')) {
		off = 3;
		next_pos = get_tokval(line+off, &tokval);
		*t = abspos;
	}
	/*^c(radius) filled circle, type: circle */
	else if((*line == 'c') && (*(line+1) == '(')) {
		off = 2;
		next_pos = get_tokval(line+off, &tokval);
		*t = circle;
	}
	/* ^co(radius) outlined circle, type: circleo */
	else if((*line == 'c') && (*(line+1) == 'o') && (*(line+2) == '(')) {
		off = 3;
		next_pos = get_tokval(line+off, &tokval);
		*t = circleo;
	}
	/* ^sa(string) sensitive areas, type: sa */
	else if((*line == 's') && (*(line+1) == 'a') && (*(line+2) == '(')) {
		off = 3;
		next_pos = get_tokval(line+off, &tokval);
		*t = sa;
	}

	*tval = tokval;
	return next_pos+off;
}

static void
setcolor(Drawable *pm, int x, int width, long tfg, long tbg, int reverse, int nobg) {

	if(nobg)
		return;

	XSetForeground(dzen.dpy, dzen.tgc, reverse ? tfg : tbg);
	XFillRectangle(dzen.dpy, *pm, dzen.tgc, x, 0, width, dzen.line_height);

	XSetForeground(dzen.dpy, dzen.tgc, reverse ? tbg : tfg);
	XSetBackground(dzen.dpy, dzen.tgc, reverse ? tfg : tbg);
}

static void
get_rect_vals(char *s, int *w, int *h, int *x, int *y) {
	int i, j;
	char buf[128];

	*w=*h=*x=*y=0;

	for(i=0; s[i] && s[i] != 'x' && i<128; i++) {
		buf[i] = s[i];
	}
	buf[i] = '\0';
	*w = atoi(buf);

	for(j=0, ++i; s[i] && s[i] != '+' && s[i] != '-' && j<128; j++, i++)
		buf[j] = s[i];
	buf[j] = '\0';
	*h = atoi(buf);

	for(j=0, ++i; s[i] && s[i] != '+' && s[i] != '-' && j<128; j++, i++)
		buf[j] = s[i];
	if(j<2) {
		buf[j] = '\0';
		*x = atoi(buf);
		*y = atoi(s+i);
	}
			
}

static int
get_circle_vals(char *s, int *d, int *a) {
	int i, ret;
	char buf[128];
	*d=*a=ret=0;

	for(i=0; s[i] && i<128; i++) {
		if(s[i] == '+' || s[i] == '-') {
			ret=1;
			break;
		} else 
			buf[i]=s[i];
	}

	buf[i+1]='\0';
	*d=atoi(buf);
	*a=atoi(s+i);

	return ret;
}

static int
get_pos_vals(char *s, int *d, int *a) {
	int i=0, ret=3;
	char buf[128];
	*d=*a=0;

	for(i=0; s[i] && i<128; i++) {
		if(s[i] == ';') {
			break;
		} else 
			buf[i]=s[i];
	}

	if(i) {
		buf[i+2]='\0';
		*d=atoi(buf);
		
	} else 
		ret=2;

	if(s[++i]) {
		*a=atoi(s+i);
	} else
		ret = 1;

	return ret;
}

char *
parse_line(const char *line, int lnr, int align, int reverse, int nodraw) {
	unsigned int bm_w, bm_h; 
	int bm_xh, bm_yh;
	int rectw, recth, rectx, recty;
	int n_posx, n_posy, set_posy=0;
	int i, next_pos=0, j=0, px=0, py=0, xorig, h=0, tw, ow;
	char lbuf[MAX_LINE_LEN], *rbuf = NULL;
	int t=-1, nobg=0;
	char *tval=NULL;
	long lastfg = dzen.norm[ColFG], lastbg = dzen.norm[ColBG];
	XGCValues gcv;
	Drawable *pm, bm;
	Drawable opm=0;
#ifdef DZEN_XPM
	int free_xpm_attrib = 0;
	Pixmap xpm_pm;
	XpmAttributes xpma;
	XpmColorSymbol xpms;
#endif

	/* sensitive areas */
	Window sa_win=0;
	Drawable sapm=0;
	int opx=0;
	XSetWindowAttributes sawa;

	sawa.background_pixmap = ParentRelative;
	sawa.event_mask = ExposureMask | ButtonReleaseMask | EnterWindowMask | LeaveWindowMask | KeyPressMask;

	/* parse line and return the text without control commands */
	if(nodraw) {
		rbuf = emalloc(MAX_LINE_LEN);
		rbuf[0] = '\0';
		if( (lnr + dzen.slave_win.first_line_vis) >= dzen.slave_win.tcnt) 
			line = NULL;
		else
			line = dzen.slave_win.tbuf[dzen.slave_win.first_line_vis+lnr];

	}
	/* parse line and render text */
	else {
		h = dzen.font.height;
		//py = dzen.font.ascent + (dzen.line_height - h) / 2;
		py = (dzen.line_height - h) / 2 - dzen.font.descent;
		xorig = 0; 


		if(lnr != -1) {
			opm = XCreatePixmap(dzen.dpy, RootWindow(dzen.dpy, DefaultScreen(dzen.dpy)), dzen.slave_win.width, 
					dzen.line_height, DefaultDepth(dzen.dpy, dzen.screen));
		}
		else {
			opm = XCreatePixmap(dzen.dpy, RootWindow(dzen.dpy, DefaultScreen(dzen.dpy)), dzen.title_win.width, 
					dzen.line_height, DefaultDepth(dzen.dpy, dzen.screen));
		}
		sapm = XCreatePixmap(dzen.dpy, RootWindow(dzen.dpy, DefaultScreen(dzen.dpy)), dzen.slave_win.width, 
				dzen.line_height, DefaultDepth(dzen.dpy, dzen.screen));
		pm = &opm;

		if(!reverse) {
			XSetForeground(dzen.dpy, dzen.tgc, dzen.norm[ColBG]);
#ifdef DZEN_XPM
			xpms.pixel = dzen.norm[ColBG];
#endif
		}
		else {
			XSetForeground(dzen.dpy, dzen.tgc, dzen.norm[ColFG]);
#ifdef DZEN_XPM
			xpms.pixel = dzen.norm[ColFG];
#endif
		}
		XFillRectangle(dzen.dpy, *pm, dzen.tgc, 0, 0, dzen.w, dzen.h);

		if(!reverse) {
			XSetForeground(dzen.dpy, dzen.tgc, dzen.norm[ColFG]);
		}
		else {
			XSetForeground(dzen.dpy, dzen.tgc, dzen.norm[ColBG]);
		}

#ifdef DZEN_XPM
		xpms.name = NULL;
		xpms.value = (char *)"none";

		xpma.colormap = DefaultColormap(dzen.dpy, dzen.screen);
		xpma.depth = DefaultDepth(dzen.dpy, dzen.screen);
		xpma.visual = DefaultVisual(dzen.dpy, dzen.screen);
		xpma.colorsymbols = &xpms;
		xpma.numsymbols = 1;
		xpma.valuemask = XpmColormap|XpmDepth|XpmVisual|XpmColorSymbols;
#endif

		if(!dzen.font.set){ 
			gcv.font = dzen.font.xfont->fid;
			XChangeGC(dzen.dpy, dzen.tgc, GCFont, &gcv);
		}

		if( lnr != -1 && (lnr + dzen.slave_win.first_line_vis >= dzen.slave_win.tcnt)) {
			XCopyArea(dzen.dpy, *pm, dzen.slave_win.drawable[lnr], dzen.gc,
					0, 0, px, dzen.line_height, xorig, 0);
			XFreePixmap(dzen.dpy, *pm);
			return NULL;
		}
	}


	for(i=0; (unsigned)i < strlen(line); i++) {
		if(*(line+i) == ESC_CHAR) {
			lbuf[j] = '\0';

			if(nodraw) {
				strcat(rbuf, lbuf);
			}
			else {
				if(t != -1 && tval) {
					switch(t) {
						case icon:
							if(XReadBitmapFile(dzen.dpy, *pm, tval, &bm_w, 
										&bm_h, &bm, &bm_xh, &bm_yh) == BitmapSuccess 
									&& (h/2 + px + (signed)bm_w < dzen.w)) {
								setcolor(pm, px, bm_w, lastfg, lastbg, reverse, nobg);
								XCopyPlane(dzen.dpy, bm, *pm, dzen.tgc, 
										0, 0, bm_w, bm_h, px, 
										dzen.line_height >= (signed)bm_h ? (dzen.line_height - bm_h)/2 : 0, 1);
								XFreePixmap(dzen.dpy, bm);
								px += bm_w;
							}
#ifdef DZEN_XPM
							else if(XpmReadFileToPixmap(dzen.dpy, dzen.title_win.win, tval, &xpm_pm, NULL, &xpma) == XpmSuccess) {
								setcolor(pm, px, xpma.width, lastfg, lastbg, reverse, nobg);
								XCopyArea(dzen.dpy, xpm_pm, *pm, dzen.tgc, 
										0, 0, xpma.width, xpma.height, px, 
										dzen.line_height >= (signed)xpma.height ? (dzen.line_height - xpma.height)/2 : 0);
								px += xpma.width;

								XFreePixmap(dzen.dpy, xpm_pm);
								free_xpm_attrib = 1;
							}
#endif
							break;

						case rect:
							get_rect_vals(tval, &rectw, &recth, &rectx, &recty);
							recth = recth > dzen.line_height ? dzen.line_height : recth;
							recty =	(recty == 0) ? (dzen.line_height - recth)/2 : recty;
							px = (rectx == 0) ? px : rectx+px;
							setcolor(pm, px, rectw, lastfg, lastbg, reverse, nobg);
							XFillRectangle(dzen.dpy, *pm, dzen.tgc, (int)px, 
									set_posy ? py : ((int)recty<0 ? dzen.line_height + recty : recty), rectw, recth);

							px += rectw;
							break;

						case recto:
							get_rect_vals(tval, &rectw, &recth, &rectx, &recty);
							if (!rectw) break;

							recth = recth > dzen.line_height ? dzen.line_height-2 : recth-1;
							recty =	recty == 0 ? (dzen.line_height - recth)/2 : recty;
							px = (rectx == 0) ? px : rectx+px;
							/* prevent from stairs effect when rounding recty */
							if (!((dzen.line_height - recth) % 2)) recty--;
							setcolor(pm, px, rectw, lastfg, lastbg, reverse, nobg);
							XDrawRectangle(dzen.dpy, *pm, dzen.tgc, px, 
									set_posy ? py : ((int)recty<0 ? dzen.line_height + recty : recty), rectw-1, recth);
							px += rectw;
							break;

						case circle:
							rectx = get_circle_vals(tval, &rectw, &recth);
							setcolor(pm, px, rectw, lastfg, lastbg, reverse, nobg);
							XFillArc(dzen.dpy, *pm, dzen.tgc, px, set_posy ? py :(dzen.line_height - rectw)/2, 
									rectw, rectw, 90*64, rectx?recth*64:64*360);
							px += rectw;
							break;

						case circleo:
							rectx = get_circle_vals(tval, &rectw, &recth);
							setcolor(pm, px, rectw, lastfg, lastbg, reverse, nobg);
							XDrawArc(dzen.dpy, *pm, dzen.tgc, px, set_posy ? py : (dzen.line_height - rectw)/2, 
									rectw, rectw, 90*64, rectx?recth*64:64*360);
							px += rectw;
							break;

						case pos:
							if(tval[0]) {
								set_posy = get_pos_vals(tval, &n_posx, &n_posy); 

								if(set_posy != 2)
									px = px+n_posx<0? 0 : px + n_posx; 
								if(set_posy != 1)
									py += n_posy;
								set_posy = set_posy == 0 || set_posy == 2 ? 1 : 0;
							} else {
								set_posy = 0;
								py = (dzen.line_height - h) / 2 - dzen.font.descent;
							}
							break;

						case abspos:
							if(tval[0]) {
								set_posy = get_pos_vals(tval, &n_posx, &n_posy);
								n_posx = n_posx < 0 ? n_posx*-1 : n_posx;
								if(set_posy != 2)
									px = n_posx;
								if(set_posy != 1)
									py = n_posy;
								set_posy = set_posy == 0 || set_posy == 2 ? 1 : 0;
							} else {
								set_posy = 0;
								py = (dzen.line_height - h) / 2 - dzen.font.descent;
							}
							
							break;

						case ibg:
							nobg = atoi(tval);
							break;

						case bg:
							lastbg = tval[0] ? (unsigned)getcolor(tval) : dzen.norm[ColBG];
							break;

						case fg:
							lastfg = tval[0] ? (unsigned)getcolor(tval) : dzen.norm[ColFG];
							XSetForeground(dzen.dpy, dzen.tgc, lastfg);
							break;
						/*
						case sa:
							if(tval[0]) {
								opx = px;
								pm = &sapm;
							}
							else {
								pm = &opm;
								dzen.sa_win = XCreateWindow(dzen.dpy, dzen.slave_win.line[lnr], 
										opx, 0, px-opx, dzen.line_height,
										0, DefaultDepth(dzen.dpy, dzen.screen), 
										CopyFromParent, DefaultVisual(dzen.dpy, dzen.screen), 
										CWEventMask, &sawa);
								XCopyArea(dzen.dpy, sapm, dzen.sa_win, dzen.gc,
										0, 0, opx-px, dzen.line_height, 0, 0);

							}
							break;
						*/

					}
					free(tval);
				}

				/* check if text is longer than window's width */
				ow = j; tw = textnw(lbuf, strlen(lbuf));
				while( ((tw + px) > (dzen.w - h)) && j>=0) {
					lbuf[--j] = '\0';
					tw = textnw(lbuf, strlen(lbuf));
				}
				if(j < ow) {
					if(j > 1)
						lbuf[j - 1] = '.';
					if(j > 2)
						lbuf[j - 2] = '.';
					if(j > 3)
						lbuf[j - 3] = '.';
				}


				if(!nobg)
					setcolor(pm, px, tw, lastfg, lastbg, reverse, nobg);
				if(dzen.font.set) 
					XmbDrawString(dzen.dpy, *pm, dzen.font.set,
							dzen.tgc, px, py+dzen.font.ascent, lbuf, strlen(lbuf));
				else 
					XDrawString(dzen.dpy, *pm, dzen.tgc, px, py+dzen.font.ascent, lbuf, strlen(lbuf)); 
				px += tw;
			}

			j=0; t=-1; tval=NULL;
			next_pos = get_token(line+i, &t, &tval);
			i += next_pos;

			/* ^^ escapes */
			if(next_pos == 0) 
				lbuf[j++] = line[i++];
		} 
		else 
			lbuf[j++] = line[i];
	}

	lbuf[j] = '\0';
	if(nodraw) {
		strcat(rbuf, lbuf);
	}
	else {
		if(t != -1 && tval) {
			switch(t) {
				case icon:
					if(XReadBitmapFile(dzen.dpy, *pm, tval, &bm_w, 
								&bm_h, &bm, &bm_xh, &bm_yh) == BitmapSuccess 
							&& (h/2 + px + (signed)bm_w < dzen.w)) {
						setcolor(pm, px, bm_w, lastfg, lastbg, reverse, nobg);
						XCopyPlane(dzen.dpy, bm, *pm, dzen.tgc, 
								0, 0, bm_w, bm_h, px, 
								dzen.line_height >= (signed)bm_h ? (dzen.line_height - bm_h)/2 : 0, 1);
						XFreePixmap(dzen.dpy, bm);
						px += bm_w;
					}
#ifdef DZEN_XPM
					else if(XpmReadFileToPixmap(dzen.dpy, dzen.title_win.win, tval, &xpm_pm, NULL, &xpma) == XpmSuccess) {
						setcolor(pm, px, xpma.width, lastfg, lastbg, reverse, nobg);
						XCopyArea(dzen.dpy, xpm_pm, *pm, dzen.tgc, 
								0, 0, xpma.width, xpma.height, px, 
								dzen.line_height >= (signed)xpma.height ? (dzen.line_height - xpma.height)/2 : 0);
						px += xpma.width;

						XFreePixmap(dzen.dpy, xpm_pm);
						free_xpm_attrib = 1;
					}
#endif
					break;

				case rect:
					get_rect_vals(tval, &rectw, &recth, &rectx, &recty);
					recth = recth > dzen.line_height ? dzen.line_height : recth;
					recty =	(recty == 0) ? (dzen.line_height - recth)/2 : recty;
					px = (rectx == 0) ? px : rectx+px;
					setcolor(pm, px, rectw, lastfg, lastbg, reverse, nobg);
					XFillRectangle(dzen.dpy, *pm, dzen.tgc, (int)px,
							set_posy ? py : ((int)recty<0 ? dzen.line_height + recty : recty), rectw, recth);

					px += rectw;
					break;

				case recto:
					get_rect_vals(tval, &rectw, &recth, &rectx, &recty);
					if (!rectw) break;

					recth = recth > dzen.line_height ? dzen.line_height-2 : recth-1;
					recty =	recty == 0 ? (dzen.line_height - recth)/2 : recty;
					px = (rectx == 0) ? px : rectx+px;
					/* prevent from stairs effect when rounding recty */
					if (!((dzen.line_height - recth) % 2)) recty--;
					setcolor(pm, px, rectw, lastfg, lastbg, reverse, nobg);
					XDrawRectangle(dzen.dpy, *pm, dzen.tgc, px, 
							set_posy ? py : ((int)recty<0 ? dzen.line_height + recty : recty), rectw-1, recth);
					px += rectw;
					break;

				case circle:
					rectx = get_circle_vals(tval, &rectw, &recth);
					setcolor(pm, px, rectw, lastfg, lastbg, reverse, nobg);
					XFillArc(dzen.dpy, *pm, dzen.tgc, px, set_posy ? py :(dzen.line_height - rectw)/2, 
							rectw, rectw, 90*64, rectx?recth*64:64*360);
					px += rectw;
					break;

				case circleo:
					rectx = get_circle_vals(tval, &rectw, &recth);
					setcolor(pm, px, rectw, lastfg, lastbg, reverse, nobg);
					XDrawArc(dzen.dpy, *pm, dzen.tgc, px, set_posy ? py : (dzen.line_height - rectw)/2, 
							rectw, rectw, 90*64, rectx?recth*64:64*360);
					px += rectw;
					break;

				case pos:
					if(tval[0]) {
						set_posy = get_pos_vals(tval, &n_posx, &n_posy); 

						if(set_posy != 2)
							px = px+n_posx<0? 0 : px + n_posx; 
						if(set_posy != 1)
							py += n_posy;
						set_posy = set_posy == 0 || set_posy == 2 ? 1 : 0;
					} else {
						set_posy = 0;
						py = (dzen.line_height - h) / 2 - dzen.font.descent;
					}
					break;

				case abspos:
					if(tval[0]) {
						set_posy = get_pos_vals(tval, &n_posx, &n_posy);
						n_posx = n_posx < 0 ? n_posx*-1 : n_posx;
						if(set_posy != 2)
							px = n_posx;
						if(set_posy != 1)
							py = n_posy;
						set_posy = set_posy == 0 || set_posy == 2 ? 1 : 0;
					} else {
						set_posy = 0;
						py = (dzen.line_height - h) / 2 - dzen.font.descent;
					}

					break;

				case ibg:
					nobg = atoi(tval);
					break;

				case bg:
					lastbg = tval[0] ? (unsigned)getcolor(tval) : dzen.norm[ColBG];
					break;

				case fg:
					lastfg = tval[0] ? (unsigned)getcolor(tval) : dzen.norm[ColFG];
					XSetForeground(dzen.dpy, dzen.tgc, lastfg);
					break;
				/*
				case sa:
					if(tval[0]) {
						opx = px;
						pm = &sapm;
					}
					else {
						pm = &opm;
						dzen.sa_win = XCreateWindow(dzen.dpy, dzen.slave_win.line[lnr], 
								opx, 0, px-opx, dzen.line_height,
								0, DefaultDepth(dzen.dpy, dzen.screen), 
								CopyFromParent, DefaultVisual(dzen.dpy, dzen.screen), 
								CWEventMask, &sawa);
						XCopyArea(dzen.dpy, sapm, dzen.sa_win, dzen.rgc,
								0, 0, opx-px, dzen.line_height, 0, 0);

					}
					break;
				*/
			}
			free(tval);
		}

		/* check if text is longer than window's width */
		ow = j; tw = textnw(lbuf, strlen(lbuf));
		while( ((tw + px) > (dzen.w - h)) && j>=0) {
			lbuf[--j] = '\0';
			tw = textnw(lbuf, strlen(lbuf));
		}
		if(j < ow) {
			if(j > 1)
				lbuf[j - 1] = '.';
			if(j > 2)
				lbuf[j - 2] = '.';
			if(j > 3)
				lbuf[j - 3] = '.';
		}


		if(!nobg)
			setcolor(pm, px, tw, lastfg, lastbg, reverse, nobg);
		if(dzen.font.set) 
			XmbDrawString(dzen.dpy, *pm, dzen.font.set,
					dzen.tgc, px, py+dzen.font.ascent, lbuf, strlen(lbuf));
		else 
			XDrawString(dzen.dpy, *pm, dzen.tgc, px, py+dzen.font.ascent, lbuf, strlen(lbuf)); 
		px += tw;

		/* expand/shrink dynamically */
		if(dzen.title_win.expand && lnr == -1){
			i = px;
			switch(dzen.title_win.expand) {
				case left:
					/* grow left end */
					otx = dzen.title_win.x_right_corner - i > dzen.title_win.x ?
						dzen.title_win.x_right_corner - i : dzen.title_win.x; 
					XMoveResizeWindow(dzen.dpy, dzen.title_win.win, otx, dzen.title_win.y, px, dzen.line_height);
					break;
				case right:
					XResizeWindow(dzen.dpy, dzen.title_win.win, px, dzen.line_height);
					break;
			}

		} else {
			if(align == ALIGNLEFT)
				xorig = 0;
			if(align == ALIGNCENTER) {
				xorig = (lnr != -1) ?
					(dzen.slave_win.width - px)/2 :
					(dzen.title_win.width - px)/2;
			}
			else if(align == ALIGNRIGHT) {
				xorig = (lnr != -1) ?
					(dzen.slave_win.width - px) :
					(dzen.title_win.width - px); 
			}
		}
		

		if(lnr != -1) {
			XCopyArea(dzen.dpy, *pm, dzen.slave_win.drawable[lnr], dzen.gc,
					0, 0, dzen.w, dzen.line_height, xorig, 0);
		}
		else {
			XCopyArea(dzen.dpy, *pm, dzen.title_win.drawable, dzen.gc,
					0, 0, dzen.w, dzen.line_height, xorig, 0);
		}
		XFreePixmap(dzen.dpy, *pm);

#ifdef DZEN_XPM
		if(free_xpm_attrib) {
			XFreeColors(dzen.dpy, xpma.colormap, xpma.pixels, xpma.npixels, xpma.depth);
			XpmFreeAttributes(&xpma);
		}
#endif
	}

	return nodraw ? rbuf : NULL;
}

void
drawheader(const char * text) {
	if(text){ 
		dzen.w = dzen.title_win.width;
		dzen.h = dzen.line_height;

		XFillRectangle(dzen.dpy, dzen.title_win.drawable, dzen.rgc, 0, 0, dzen.w, dzen.h);
		parse_line(text, -1, dzen.title_win.alignment, 0, 0);
	}

	XCopyArea(dzen.dpy, dzen.title_win.drawable, dzen.title_win.win, 
			dzen.gc, 0, 0, dzen.title_win.width, dzen.line_height, 0, 0);
}

void
drawbody(char * text) {
	char *ec;
	int i;

	/* draw to title window
	   this routine should be better integrated into
	   the actual parsing process
	   */
	if((ec = strstr(text, "^tw()")) && (*(ec-1) != '^')) {
		dzen.w = dzen.title_win.width;
		dzen.h = dzen.line_height;

		XFillRectangle(dzen.dpy, dzen.title_win.drawable, dzen.rgc, 0, 0, dzen.w, dzen.h);
		parse_line(ec+5, -1, dzen.title_win.alignment, 0, 0);
		XCopyArea(dzen.dpy, dzen.title_win.drawable, dzen.title_win.win, 
				dzen.gc, 0, 0, dzen.w, dzen.h, 0, 0);
		return;
	}

	if(dzen.slave_win.tcnt == dzen.slave_win.tsize) 
		free_buffer();
	if(text[0] == '^' && text[1] == 'c' && text[2] == 's') {
		free_buffer();
		for(i=0; i < dzen.slave_win.max_lines; i++)
			XFillRectangle(dzen.dpy, dzen.slave_win.drawable[i], dzen.rgc, 0, 0, dzen.slave_win.width, dzen.line_height);
		x_draw_body();
		return;
	}
	if(dzen.slave_win.tcnt < dzen.slave_win.tsize) {
		dzen.slave_win.tbuf[dzen.slave_win.tcnt] = estrdup(text);
		dzen.slave_win.tcnt++;
	}
}
