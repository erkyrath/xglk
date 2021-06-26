#include <stdlib.h>
#include <string.h>
#include "xglk.h"
#include "xg_internal.h"
#include "xg_win_textgrid.h"

typedef struct sattr_struct {
  glui32 style, linkid;
  int blank; /* flag */
  int end; 
  /* style runs from the previous one to (before) here. Everything 
     after the last style is blank. (Actually, this is the start 
     of the next style.) */
} sattr_t;

typedef struct sline_struct {
  char *text; /* pointer to array of text_size chars */
  int text_size; /* allocation size; at least width, but could be more */
  sattr_t *attr; 
  int numattr;
  int attr_size; /* allocation size */
  int dirtybeg, dirtyend; /* chars; [) protocol; -1,-1 for non-dirty */
} sline_t;

typedef struct sdot_struct {
  int begline, begchar;
  int endline, endchar;
} sdot_t;

#define DOT_EXISTS(dt)   ((dt)->begline >= 0)

#define DOT_LENGTH_ZERO(dt)   \
  (DOT_EXISTS(dt) &&     \
    (dt)->begline == (dt)->endline && (dt)->begchar == (dt)->endchar)

#define collapse_dot(ctw)  \
  (ctw->dot.begline = ctw->dot.endline, \
    ctw->dot.begchar = ctw->dot.endchar)

#define collapse_dot_back(ctw)  \
  (ctw->dot.endline = ctw->dot.begline, \
    ctw->dot.endchar = ctw->dot.begchar)

#define ATTRMATCH(at1, at2)  \
  ((at1)->style == (at2)->style && (at1)->linkid == (at2)->linkid)

struct window_textgrid_struct {
  window_t *owner;
  XRectangle bbox;

  stylehints_t hints;
  fontset_t font;

  XRectangle cbox; /* contents -- the characters themselves */
  int width, height;
  sline_t *linelist;
  int height_size; /* allocation size. Lines above height have valid text, 
		      text_size, attr, attr_size fields, but the rest are 
		      garbage. */

  int cursorx, cursory;

  int dirtybeg, dirtyend; /* lines; [) protocol; -1,-1 for non-dirty */
  
  sdot_t dot;
  sdot_t lastdot;

  int isactive; /* is window active? */

  /* these are for line input */
  long buflen;
  char *buffer;
  int inputlen;
  int inputmaxlen;
  sdot_t inputdot; /* always one line */
  glui32 originalattr;
  gidispatch_rock_t inarrayrock;

  /* these are for mouse input. */
  int drag_mouseevent, drag_linkevent;
  glui32 drag_linkid;
  sdot_t drag_first, drag_linkpos;
  
};

static void win_textgrid_layout(window_textgrid_t *cutwin, int drawall);
static void flip_selection(window_textgrid_t *cutwin, sdot_t *dot);
static int dot_contains_dot(window_textgrid_t *dwin, sdot_t *bigdot, 
  sdot_t *dot);
static void find_pos_by_loc(window_textgrid_t *dwin, int xpos, int ypos, 
  int addhalf, XPoint *res);
static int compare_pos_to_dot(window_textgrid_t *dwin, sdot_t *dot, 
  int pchar, int pline);
static void kill_input(window_textgrid_t *cutwin, int beg, int end);
static void insert_input(window_textgrid_t *cutwin, int pos, char *buf, 
  int len);
static void xgrid_line_cancel(window_textgrid_t *cutwin, event_t *ev);

struct window_textgrid_struct *win_textgrid_create(window_t *win)
{
  int jx;
  window_textgrid_t *res = 
    (window_textgrid_t *)malloc(sizeof(window_textgrid_t));
  if (!res)
    return NULL;
  
  res->owner = win;

  gli_stylehints_for_window(wintype_TextGrid, &(res->hints));
  gli_styles_compute(&(res->font), &(res->hints));
  
  res->width = 0;
  res->height = 0;
  res->height_size = 4;
  res->linelist = (sline_t *)malloc(res->height_size * sizeof(sline_t)); 
  for (jx = 0; jx < res->height_size; jx++) {
    sline_t *ln = &(res->linelist[jx]);
    ln->text_size = 20;
    ln->text = (char *)malloc(ln->text_size * sizeof(char));
    ln->attr_size = 4;
    ln->attr = (sattr_t *)malloc(ln->attr_size * sizeof(sattr_t));
  }
  
  res->cursorx = 0;
  res->cursory = 0;
  
  res->dirtybeg = -1;
  res->dirtyend = -1;
  
  res->dot.begline = -1;
  res->lastdot.begline = -1;
  
  res->isactive = FALSE;

  res->buffer = NULL;
  res->buflen = 0;

  return res;
}

void win_textgrid_destroy(window_textgrid_t *win)
{
  int jx;
  
  if (win->buffer) {
    if (gli_unregister_arr) {
      (*gli_unregister_arr)(win->buffer, win->buflen, "&+#!Cn", 
	win->inarrayrock);
    }
    win->buffer = NULL;
  }

  for (jx = 0; jx < win->height_size; jx++) {
    sline_t *ln = &(win->linelist[jx]);
    if (ln->text) {
      free(ln->text);
      ln->text = NULL;
    }
    if (ln->attr) {
      free(ln->attr);
      ln->attr = NULL;
    }
  }
  
  free(win->linelist);
  win->linelist = NULL;
  
  win->owner = NULL;
  
  free(win);
}

/* Return the first style whose end is after pos. If all styles
   end <= pos, return -1. guess is a position to start scanning at (-1
   means the end). 
   Can call this with pos == -1. */
static int find_style_at(sline_t *ln, int pos, int guess)
{
  int ix;
  sattr_t *attr;
  
  if (ln->numattr <= 0)
    return -1;
  
  ix = guess; 
  if (ix < 0 || ix >= ln->numattr)
    ix = ln->numattr-1;
  
  attr = &(ln->attr[ix]);
  
  if (attr->end > pos) {
    /* scan backwards */
    for (ix--; ix >= 0; ix--) {
      if (ln->attr[ix].end <= pos)
	break;
    }
    return ix+1;
  }
  else {
    /* scan forwards */
    for (ix++; ix < ln->numattr; ix++) {
      if (ln->attr[ix].end > pos)
	break;
    }
    if (ix >= ln->numattr)
      return -1;
    else
      return ix;
  }
}

static void change_size(window_textgrid_t *cutwin, int newwid, int newhgt)
{
  int ix, jx;
  
  if (newhgt > cutwin->height) {
    
    if (newhgt > cutwin->height_size) {
      int oldheightsize = cutwin->height_size;
      while (newhgt > cutwin->height_size) 
	cutwin->height_size *= 2;
      cutwin->linelist = (sline_t *)realloc(cutwin->linelist, 
	cutwin->height_size * sizeof(sline_t)); 
      for (jx = oldheightsize; jx < cutwin->height_size; jx++) {
	sline_t *ln = &(cutwin->linelist[jx]);
	ln->text_size = newwid + 4;
	ln->text = (char *)malloc(ln->text_size * sizeof(char));
	ln->attr_size = 4;
	ln->attr = (sattr_t *)malloc(ln->attr_size * sizeof(sattr_t));
      }
    }
    
    for (jx = 0; jx < cutwin->height; jx++) {
      sline_t *ln = &(cutwin->linelist[jx]);
      /* existing valid lines */
      if (newwid > cutwin->width) {
	if (newwid > ln->text_size) {
	  while (newwid > ln->text_size)
	    ln->text_size *= 2;
	  ln->text = (char *)realloc(ln->text, ln->text_size * sizeof(char));
	}
      }
      else if (newwid < cutwin->width) {
	ix = find_style_at(ln, newwid-1, -1);
	if (ix >= 0) {
	  sattr_t *attr = &(ln->attr[ix]);
	  attr->end = newwid;
	  ln->numattr = ix+1;
	}
      }
    }
    
    for (jx = cutwin->height; jx < newhgt; jx++) {
      sline_t *ln = &(cutwin->linelist[jx]);
      /* recondition new lines (allocation is already done) */
      if (newwid > ln->text_size) {
	while (newwid > ln->text_size)
	  ln->text_size *= 2;
	ln->text = (char *)realloc(ln->text, ln->text_size * sizeof(char));
      }
      ln->numattr = 0;
      ln->dirtybeg = -1;
      ln->dirtyend = -1;
    }
    
  }
  else { /* (newhgt < or = cutwin->height) */
    
    for (jx = 0; jx < newhgt; jx++) {
      sline_t *ln = &(cutwin->linelist[jx]);
      /* existing valid lines */
      if (newwid > cutwin->width) {
	if (newwid > ln->text_size) {
	  while (newwid > ln->text_size)
	    ln->text_size *= 2;
	  ln->text = (char *)realloc(ln->text, ln->text_size * sizeof(char));
	}
      }
      else if (newwid < cutwin->width) {
	ix = find_style_at(ln, newwid-1, -1);
	if (ix >= 0) {
	  sattr_t *attr = &(ln->attr[ix]);
	  attr->end = newwid;
	  ln->numattr = ix+1;
	}
      }
    }
    
    /* everything beyond newhgt: Ignore. The valid fields are valid and
       everything else will be reconditioned if the window expands. */
  }
  
  cutwin->height = newhgt;
  cutwin->width = newwid;
  
  if (cutwin->dot.begline >= 0) {
    sdot_t *dot = &(cutwin->dot);
    if (dot->begline >= cutwin->height) {
      dot->begline = cutwin->height - 1;
      dot->begchar = cutwin->width;
    }
    if (dot->begchar > cutwin->width)
      dot->begchar = cutwin->width;
    if (dot->endline >= cutwin->height) {
      dot->endline = cutwin->height - 1;
      dot->endchar = cutwin->width;
    }
    if (dot->endchar > cutwin->width)
      dot->endchar = cutwin->width;
  }
}

static void insert_style(sline_t *ln, int pos, int num)
{
  int ix, numend;
  
  if (num <= 0)
    return;
  if (ln->numattr + num > ln->attr_size) {
    while (ln->numattr + num > ln->attr_size)
      ln->attr_size *= 2;
    ln->attr = (sattr_t *)realloc(ln->attr, ln->attr_size * sizeof(sattr_t));
  }
  numend = ln->numattr - pos;
  if (numend) {
    memmove(&ln->attr[pos+num], &ln->attr[pos], sizeof(sattr_t) * (numend));
  }
  ln->numattr += num;
  
  for (ix=pos; ix<pos+num; ix++) {
    sattr_t *attr = &ln->attr[ix];
    attr->end = -99;
    attr->style = -99;
    attr->linkid = 0;
  }
}

static void delete_style(sline_t *ln, int pos, int num)
{
  int numend;
  
  if (num <= 0)
    return;
  if (pos + num > ln->numattr)
    num = ln->numattr - pos;
  numend = ln->numattr - (pos+num);
  if (numend) {
    memmove(&ln->attr[pos], &ln->attr[pos+num], sizeof(sattr_t) * (numend));
  }
  ln->numattr -= num;
}

void win_textgrid_flush(window_t *win)
{
  window_textgrid_t *dwin = win->data;
  win_textgrid_layout(dwin, FALSE);
}

void win_textgrid_get_size(window_t *win, glui32 *width, glui32 *height)
{
  window_textgrid_t *dwin = win->data;
  *width = dwin->width;
  *height = dwin->height;
}

XRectangle *win_textgrid_get_rect(window_t *win)
{
  window_textgrid_t *dwin = win->data;
  return &dwin->bbox;
}

long win_textgrid_figure_size(window_t *win, long size, int vertical)
{
  window_textgrid_t *dwin = win->data;
  
  if (vertical) {
    /* size * charwidth */
    long textwin_w = size * dwin->font.gc[0].spacewidth; 
    return textwin_w + 2 * prefs.textgrid.marginx;
  }
  else {
    /* size * lineheight */
    long textwin_h = size * dwin->font.lineheight;
    return textwin_h + 2 * prefs.textgrid.marginy;
  }
}

fontset_t *win_textgrid_get_fontset(window_t *win)
{
  window_textgrid_t *dwin = win->data;
  return &(dwin->font);
}

stylehints_t *win_textgrid_get_stylehints(window_t *win)
{
  window_textgrid_t *dwin = win->data;
  return &(dwin->hints);
}

void win_textgrid_rearrange(window_t *win, XRectangle *box)
{
  int wid, hgt;
  window_textgrid_t *cutwin = win->data;
  
  cutwin->bbox = *box;
  
  cutwin->cbox.x = box->x + prefs.textgrid.marginx;
  cutwin->cbox.width = box->width - 2 * prefs.textgrid.marginx;
  cutwin->cbox.y = box->y + prefs.textgrid.marginy;
  cutwin->cbox.height = box->height - 2 * prefs.textgrid.marginy;
  
  wid = (cutwin->cbox.width) / cutwin->font.gc[0].spacewidth;
  hgt = (cutwin->cbox.height) / cutwin->font.lineheight;
  if (wid < 0)
    wid = 0;
  if (hgt < 0)
    hgt = 0;
  
  change_size(cutwin, wid, hgt);
}

void win_textgrid_redraw(window_t *win)
{
  window_textgrid_t *cutwin = win->data;
  
  gli_draw_window_outline(&cutwin->bbox);

  gli_draw_window_margin(&(cutwin->font.backcolor),
    cutwin->bbox.x, cutwin->bbox.y, 
    cutwin->bbox.width, cutwin->bbox.height,
    cutwin->cbox.x, cutwin->cbox.y, 
    cutwin->width * cutwin->font.gc[0].spacewidth,
    cutwin->height * cutwin->font.lineheight);

  win_textgrid_layout(cutwin, TRUE);
}

void win_textgrid_setfocus(window_t *win, int turnon)
{
  window_textgrid_t *cutwin = win->data; 
  if (turnon) {
    if (!cutwin->isactive) {
      cutwin->isactive = TRUE;
      flip_selection(cutwin, &cutwin->dot);
    }
  }
  else {
    if (cutwin->isactive) {
      flip_selection(cutwin, &cutwin->dot);
      cutwin->isactive = FALSE;
    }
  }
}

void win_textgrid_perform_click(window_t *win, int dir, XPoint *pt, 
  int butnum, int clicknum, unsigned int state)
{
  window_textgrid_t *cutwin = win->data; 
  XPoint posp, posp2;
  long px, px2;
  int ix;

  if (dir == mouse_Down) {

    find_pos_by_loc(cutwin, pt->x, pt->y, TRUE, &posp);
    find_pos_by_loc(cutwin, pt->x, pt->y, FALSE, &posp2);

    cutwin->drag_mouseevent = FALSE;
    cutwin->drag_linkevent = FALSE;
    if (cutwin->owner->mouse_request && butnum==1) {
      cutwin->drag_mouseevent = TRUE;
      cutwin->drag_linkpos.begline = posp2.y;
      cutwin->drag_linkpos.begchar = posp2.x;
    }
    else if (cutwin->owner->hyperlink_request && butnum==1) {
      sline_t *ln = &(cutwin->linelist[posp2.y]);
      cutwin->drag_linkevent = TRUE;
      cutwin->drag_linkid = 0;
      cutwin->drag_linkpos.begline = posp2.y;
      cutwin->drag_linkpos.begchar = posp2.x;
      ix = find_style_at(ln, posp2.x, -1);
      if (ix >= 0)
	cutwin->drag_linkid = ln->attr[ix].linkid;
    }

    if (butnum==1) {
      cutwin->dot.begline = posp.y;
      cutwin->dot.begchar = posp.x;
      cutwin->dot.endline = posp.y;
      cutwin->dot.endchar = posp.x;
      cutwin->drag_first = cutwin->dot;
    }
    else if (butnum==3) {
      ix = compare_pos_to_dot(cutwin, &cutwin->dot, posp.x, posp.y);
      if (ix == 0 || ix == 1) {
	cutwin->drag_first.begline = cutwin->dot.endline;
	cutwin->drag_first.begchar = cutwin->dot.endchar;
      }
      else {
	cutwin->drag_first.begline = cutwin->dot.begline;
	cutwin->drag_first.begchar = cutwin->dot.begchar;
      }
      cutwin->drag_first.endline = cutwin->drag_first.begline;
      cutwin->drag_first.endchar = cutwin->drag_first.begchar;
      
      ix = compare_pos_to_dot(cutwin, &cutwin->drag_first, posp.x, posp.y);
      if (ix == 0) {
	cutwin->dot.begline = posp.y;
	cutwin->dot.begchar = posp.x;
	cutwin->dot.endline = cutwin->drag_first.endline;
	cutwin->dot.endchar = cutwin->drag_first.endchar;
      }
      else if (ix == 3) {
	cutwin->dot.begline = cutwin->drag_first.begline;
	cutwin->dot.begchar = cutwin->drag_first.begchar;
	cutwin->dot.endline = posp.y;
	cutwin->dot.endchar = posp.x;
      }
      else {
	cutwin->dot = cutwin->drag_first;
      }
    }
    win_textgrid_layout(cutwin, FALSE);
  }
  else if (dir == mouse_Move) {

    find_pos_by_loc(cutwin, pt->x, pt->y, TRUE, &posp);
    ix = compare_pos_to_dot(cutwin, &cutwin->drag_first, posp.x, posp.y);
    if (ix == 0) {
      cutwin->dot.begline = posp.y;
      cutwin->dot.begchar = posp.x;
      cutwin->dot.endline = cutwin->drag_first.endline;
      cutwin->dot.endchar = cutwin->drag_first.endchar;
    }
    else if (ix == 3) {
      cutwin->dot.begline = cutwin->drag_first.begline;
      cutwin->dot.begchar = cutwin->drag_first.begchar;
      cutwin->dot.endline = posp.y;
      cutwin->dot.endchar = posp.x;
    }
    else {
      cutwin->dot = cutwin->drag_first;
    }
    win_textgrid_layout(cutwin, FALSE);
  }
  else if (dir == mouse_Up) {
    int singleclick = FALSE;
    find_pos_by_loc(cutwin, pt->x, pt->y, TRUE, &posp);
    if (posp.x == cutwin->drag_first.begchar
      && posp.y == cutwin->drag_first.begline) {
      singleclick = TRUE;
    }

    if (cutwin->drag_mouseevent) {
      if (singleclick) {
	cutwin->owner->mouse_request = FALSE;
	eventloop_setevent(evtype_MouseInput, cutwin->owner, 
	  cutwin->drag_linkpos.begchar, cutwin->drag_linkpos.begline);
	return;
      }
    }

    if (cutwin->drag_linkevent && cutwin->drag_linkid) {
      if (singleclick) {
	find_pos_by_loc(cutwin, pt->x, pt->y, FALSE, &posp);
	cutwin->owner->hyperlink_request = FALSE;
	eventloop_setevent(evtype_Hyperlink, cutwin->owner, 
	  cutwin->drag_linkid, 0);
	return;
      }
    }
  }

}

void win_textgrid_init_line(window_t *win, char *buffer, int buflen, 
  int readpos)
{
  window_textgrid_t *cutwin = win->data;
  int len;
  
  len = cutwin->width - cutwin->cursorx;
  if (buflen < len)
    len = buflen;
  
  cutwin->buflen = buflen;
  cutwin->buffer = buffer;
  cutwin->inputlen = 0;

  cutwin->inputmaxlen = len;
  cutwin->inputdot.begline = cutwin->cursory;
  cutwin->inputdot.endline = cutwin->cursory;
  cutwin->inputdot.begchar = cutwin->cursorx;
  cutwin->inputdot.endchar = cutwin->inputdot.begchar + len;
  cutwin->originalattr = cutwin->owner->style;
  cutwin->owner->style = style_Input;

  if (readpos) {
    /* The terp has to enter the text. */
    insert_input(cutwin, cutwin->inputdot.begchar, buffer, readpos);
    win_textgrid_layout(cutwin, FALSE);
  }

  /*cutwin->historypos = cutwin->historynum;*/

  if (gli_register_arr) {
    cutwin->inarrayrock = (*gli_register_arr)(buffer, buflen, "&+#!Cn");
  }
}

void win_textgrid_cancel_line(window_t *win, event_t *ev)
{
  window_textgrid_t *cutwin = win->data;
  xgrid_line_cancel(cutwin, ev);
}

static void find_pos_by_loc(window_textgrid_t *dwin, int xpos, int ypos, 
  int addhalf, XPoint *res)
{
  int charwidth = dwin->font.gc[0].spacewidth;
  int charheight = dwin->font.lineheight;
  
  xpos -= dwin->cbox.x;
  ypos -= dwin->cbox.y;
  
  if (ypos < 0) {
    res->x = 0;
    res->y = 0;
    return;
  }
  if (xpos < 0)
    xpos = 0;
  
  if (addhalf)
    xpos += charwidth/2;
  xpos = xpos / charwidth;
  ypos = ypos / charheight;
  
  if (xpos > dwin->width) {
    xpos = dwin->width;
  }
  if (ypos >= dwin->height) {
    ypos = dwin->height - 1;
    xpos = dwin->width;
  }
  
  res->x = xpos;
  res->y = ypos;
}

static int compare_pos_to_dot(window_textgrid_t *dwin, sdot_t *dot, 
  int pchar, int pline)
{
  long pos, dotbeg, dotend;
  long winwidth = dwin->width;
  
  pos = (long)pline * winwidth + (long)pchar;
  dotbeg = (long)dot->begline * winwidth + (long)dot->begchar;
  dotend = (long)dot->endline * winwidth + (long)dot->endchar;
  
  if (pos < dotbeg)
    return 0;
  if (pos >= dotend)
    return 3;
  
  if (pos < (dotbeg + dotend) / 2)
    return 1;
  else
    return 2;
}

static int dot_contains_dot(window_textgrid_t *dwin, sdot_t *bigdot, 
  sdot_t *dot)
{
  long bigdotbeg, bigdotend, dotbeg, dotend;
  long winwidth = dwin->width;
  
  if (!DOT_EXISTS(dot) || !DOT_EXISTS(bigdot))
    return FALSE;

  dotbeg = (long)dot->begline * winwidth + (long)dot->begchar;
  dotend = (long)dot->endline * winwidth + (long)dot->endchar;
  bigdotbeg = (long)bigdot->begline * winwidth + (long)bigdot->begchar;
  bigdotend = (long)bigdot->endline * winwidth + (long)bigdot->endchar;
  
  if (dotbeg < bigdotbeg || dotend > bigdotend)
    return FALSE;
  return TRUE;
}

static void flip_selection(window_textgrid_t *cutwin, sdot_t *dot)
{
  int charwidth = cutwin->font.gc[0].spacewidth;
  int charheight = cutwin->font.lineheight;

  if (!cutwin->isactive) {
    return; /* not the front window */
  }

  if (dot->begline < 0 || dot->endline < 0) {
    return; /* dot hidden */
  }

  if (DOT_LENGTH_ZERO(dot)) {
    if (dot->begline < 0 || dot->begline >= cutwin->height) {
      return;
    }
    xglk_draw_dot(cutwin->cbox.x + dot->begchar*charwidth, 
      cutwin->cbox.y + dot->begline*charheight + cutwin->font.lineoff,
      cutwin->font.lineoff);
    return;
  }
  else {
    int xpos, ypos, xpos2, ypos2;
    int cboxright = cutwin->cbox.x + cutwin->width * charwidth;
    xpos = cutwin->cbox.x + dot->begchar*charwidth;
    ypos = cutwin->cbox.y + dot->begline*charheight;
    xpos2 = cutwin->cbox.x + dot->endchar*charwidth;
    ypos2 = cutwin->cbox.y + dot->endline*charheight;
    if (dot->begline == dot->endline) {
      /* within one line */
      if (dot->begchar != dot->endchar 
	&& dot->begline >= 0 && dot->begline < cutwin->height) {
	XFillRectangle(xiodpy, xiowin, gcflip, xpos, ypos, xpos2-xpos, charheight);
      }
    }
    else {
      if (dot->begchar < cutwin->width 
	&& dot->begline >= 0 && dot->begline < cutwin->height) {
	/* first partial line */
	XFillRectangle(xiodpy, xiowin, gcflip, xpos, ypos, 
	  cboxright - xpos, charheight);
      }
      if (dot->begline+1 < dot->endline 
	&& dot->endline >= 0 && dot->begline+1 < cutwin->height) {
	/* now, paint from begline+1 to the top of endline. */
	int ybody = ypos+charheight;
	int ybody2 = ypos2;
	if (ybody < cutwin->cbox.y)
	  ybody = cutwin->cbox.y;
	if (ybody2 > cutwin->cbox.y+cutwin->cbox.height)
	  ybody2 = cutwin->cbox.y+cutwin->cbox.height;
	/* main body */
	XFillRectangle(xiodpy, xiowin, gcflip, cutwin->cbox.x, ybody, 
	  cboxright - cutwin->cbox.x, ybody2 - ybody);
      }
      if (dot->endchar > 0 
	&& dot->endline >= 0 && dot->endline < cutwin->height) {
	/* last partial line */
	XFillRectangle(xiodpy, xiowin, gcflip, cutwin->cbox.x, ypos2, 
	  xpos2 - cutwin->cbox.x, charheight);
      }
    }
  }
}

static void refiddle_selection(window_textgrid_t *cutwin, 
  sdot_t *olddot, sdot_t *newdot)
{
  sdot_t tmpdot;
  int ix;
  
  if (DOT_LENGTH_ZERO(olddot) || DOT_LENGTH_ZERO(newdot) 
    || olddot->begline < 0 || newdot->begline < 0) {
    flip_selection(cutwin, olddot);
    flip_selection(cutwin, newdot);
    return;
  }

  if (olddot->begline == newdot->begline && olddot->begchar == newdot->begchar) {
    /* start at same place */
    
    if (olddot->endline == newdot->endline && olddot->endchar == newdot->endchar) {
      /* identical! */
      return;
    }
    
    ix = compare_pos_to_dot(cutwin, olddot, newdot->endchar, newdot->endline);
    if (ix == 3) {
      tmpdot.begline = olddot->endline;
      tmpdot.begchar = olddot->endchar;
      tmpdot.endline = newdot->endline;
      tmpdot.endchar = newdot->endchar;
    }
    else {
      tmpdot.begline = newdot->endline;
      tmpdot.begchar = newdot->endchar;
      tmpdot.endline = olddot->endline;
      tmpdot.endchar = olddot->endchar;
    }
    flip_selection(cutwin, &tmpdot);
    return;
  }
  
  if (olddot->endline == newdot->endline && olddot->endchar == newdot->endchar) {
    /* end at same place */
    
    ix = compare_pos_to_dot(cutwin, olddot, newdot->begchar, newdot->begline);
    if (ix == 0) {
      tmpdot.begline = newdot->begline;
      tmpdot.begchar = newdot->begchar;
      tmpdot.endline = olddot->begline;
      tmpdot.endchar = olddot->begchar;
    }
    else {
      tmpdot.begline = olddot->begline;
      tmpdot.begchar = olddot->begchar;
      tmpdot.endline = newdot->begline;
      tmpdot.endchar = newdot->begchar;
    }
    flip_selection(cutwin, &tmpdot);
    return;
  }

  flip_selection(cutwin, olddot);
  flip_selection(cutwin, newdot);
}

static void win_textgrid_layout(window_textgrid_t *cutwin, int drawall)
{
  int ix, ix2, jx, sx;
  int startln, endln, startchar, endchar;
  sattr_t *attr;
  
  int charwidth = cutwin->font.gc[0].spacewidth;
  int charheight = cutwin->font.lineheight;
  fontref_t *gclist = cutwin->font.gc;

  if (drawall) {
    startln = 0;
    endln = cutwin->height;
  }
  else {
    if (cutwin->dirtybeg < 0 || cutwin->dirtyend < 0) {
      startln = 0;
      endln = 0;
    }
    else {
      startln = cutwin->dirtybeg;
      endln = cutwin->dirtyend;
      if (endln > cutwin->height)
	endln = cutwin->height;
    }
  }
  
  if (startln >= endln) {
    /* no text changes */
    if (cutwin->lastdot.begline != cutwin->dot.begline
      || cutwin->lastdot.begchar != cutwin->dot.begchar
      || cutwin->lastdot.endline != cutwin->dot.endline
      || cutwin->lastdot.endchar != cutwin->dot.endchar) {
      refiddle_selection(cutwin, &cutwin->lastdot, &cutwin->dot);
      cutwin->lastdot = cutwin->dot;
    }
    return;
  }
  
  /* flip dot off */
  flip_selection(cutwin, &cutwin->lastdot);
  cutwin->lastdot = cutwin->dot;
  
  for (jx = startln; jx < endln; jx++) {
    sline_t *ln = &(cutwin->linelist[jx]);
    
    if (drawall) {
      startchar = 0;
      endchar = cutwin->width;
    }
    else {
      if (ln->dirtybeg < 0 || ln->dirtyend < 0)
	continue;
      startchar = ln->dirtybeg;
      endchar = ln->dirtyend;
      if (endchar > cutwin->width)
	endchar = cutwin->width;
    }
    
    ix = startchar;
    sx = find_style_at(ln, ix, 0);
    while (ix < endchar) {
      if (sx >= 0 && sx < ln->numattr) {
	attr = &(ln->attr[sx]);
	ix2 = attr->end;
      }
      else { 
	attr = NULL;
	ix2 = cutwin->width;
      }
      if (ix2 > endchar)
	ix2 = endchar;
      
      if (!attr || attr->blank) {
	/* ### This may screw up styled whitespace (bkcolors) ### */
	xglk_clearfor_string(&(cutwin->font.backcolor),
	  cutwin->cbox.x+ix*charwidth, 
	  cutwin->cbox.y+jx*charheight, 
	  (ix2-ix)*charwidth, charheight);
      }
      else {
	xglk_clearfor_string(&(gclist[attr->style].backcolor),
	  cutwin->cbox.x+ix*charwidth,
          cutwin->cbox.y+jx*charheight,
          (ix2-ix)*charwidth, charheight);
	xglk_draw_string(&(gclist[attr->style]),
	  (attr->linkid != 0), (ix2-ix)*charwidth,
	  cutwin->cbox.x+ix*charwidth,
          cutwin->cbox.y+jx*charheight+cutwin->font.lineoff,
          ln->text+ix, (ix2-ix));
      }
      
      ix = ix2;
      sx++;
    }
    
    ln->dirtybeg = -1;
    ln->dirtyend = -1;
  }
  
  cutwin->dirtybeg = -1;
  cutwin->dirtyend = -1;

  /* flip dot back on */
  flip_selection(cutwin, &cutwin->lastdot);
}

void win_textgrid_add(window_textgrid_t *cutwin, char ch)
{
  sline_t *ln;
  sattr_t *oattr;
  int sx, sx2, ix, pos;
  
  if (cutwin->cursorx >= cutwin->width) {
    cutwin->cursorx = 0;
    cutwin->cursory++;
  }
  if (cutwin->cursory >= cutwin->height)
    return;
  
  if (ch == '\n') {
    cutwin->cursorx = 0;
    cutwin->cursory++;
    return;
  }
  
  if (cutwin->dirtybeg < 0 || cutwin->dirtybeg > cutwin->cursory)
    cutwin->dirtybeg = cutwin->cursory;
  if (cutwin->dirtyend < 0 || cutwin->dirtyend < cutwin->cursory+1)
    cutwin->dirtyend = cutwin->cursory+1;
  ln = &(cutwin->linelist[cutwin->cursory]);
  
  pos = cutwin->cursorx;
  if (ln->dirtybeg < 0 || ln->dirtybeg > pos)
    ln->dirtybeg = pos;
  if (ln->dirtyend < 0 || ln->dirtyend < pos+1)
    ln->dirtyend = pos+1;
  
  sx = find_style_at(ln, pos, -1);
  
  if (sx >= 0 && sx < ln->numattr) {
    oattr = &(ln->attr[sx]);
  }
  else {
    oattr = NULL;
    sx = ln->numattr;
  }
  
  if (oattr && !oattr->blank && ATTRMATCH(oattr, cutwin->owner)) {
    /* within a matching style; leave alone */
  }
  else {
    /* within non-matching style */
    sattr_t *lattr, *nattr;
    int lastendat, curendat;
    if (sx == 0) {
      lattr = NULL;
      lastendat = 0;
    }
    else {
      lattr = &(ln->attr[sx-1]);
      lastendat = lattr->end;
    }
    if (oattr)
      curendat = oattr->end;
    else
      curendat = cutwin->width + 10;
    nattr = NULL;
    if (pos == lastendat && pos == curendat-1) {
      /* current has length 1; replace. (current is not NULL) */
      sattr_t *fattr;
      if (sx+1 < ln->numattr)
	fattr = &(ln->attr[sx+1]);
      else
	fattr = NULL;
      if (lattr && !lattr->blank && ATTRMATCH(lattr, cutwin->owner)) {
	if (fattr && !fattr->blank && ATTRMATCH(fattr, cutwin->owner)) {
	  /* no, delete current *and* previous, fall into next */
	  delete_style(ln, sx-1, 2);
	}
	else {
	  /* no, delete current and extend previous. */
	  delete_style(ln, sx, 1);
	  lattr = &(ln->attr[sx-1]);
	  lattr->end = pos+1;
	}
	nattr = NULL;
      }
      else if (fattr && !fattr->blank && ATTRMATCH(fattr, cutwin->owner)) {
	/* no, delete current and fall into next */
	delete_style(ln, sx, 1);
	nattr = NULL;
      }
      else {
	nattr = oattr;
	/* nattr->end unchanged */
      }
    }
    else if (pos == lastendat) {
      /* insert at beginning, or extend previous */
      if (lattr && !lattr->blank && ATTRMATCH(lattr, cutwin->owner)) {
	/* extend previous */
	lattr->end = pos+1; /* that is, += 1 */
	nattr = NULL;
      }
      else {
	/* insert at beginning */
	insert_style(ln, sx, 1);
	nattr = &(ln->attr[sx]);
	nattr->end = pos+1;
      }
    }
    else if (pos == curendat-1) {
      /* insert at end, or retract current. (current is not NULL) */
      sattr_t *fattr;
      if (sx+1 < ln->numattr)
	fattr = &(ln->attr[sx+1]);
      else
	fattr = NULL;
      if (fattr && !fattr->blank && ATTRMATCH(fattr, cutwin->owner)) {
	/* retract current, put char in next */
	oattr->end = pos; /* that is, -= 1 */
	nattr = NULL;
      }
      else {
	/* retract current, insert at end */
	oattr->end = pos; /* that is, -= 1 */
	insert_style(ln, sx+1, 1);
	nattr = &(ln->attr[sx+1]);
	nattr->end = pos+1;
      }
    }
    else {
      /* split current (or current NULL) */
      if (oattr) {
	insert_style(ln, sx, 2);
	ln->attr[sx] = ln->attr[sx+2]; 
	ln->attr[sx].end = pos;
	nattr = &(ln->attr[sx+1]);
	nattr->end = pos+1;
      }
      else {
	insert_style(ln, ln->numattr, 2);
	nattr = &(ln->attr[ln->numattr-2]); 
	nattr->blank = TRUE;
	nattr->style = 0;
	nattr->linkid = 0;
	nattr->end = pos;
	nattr++; /* new last attr */
	nattr->end = pos+1;
      }
    }
    
    if (nattr) {
      nattr->blank = FALSE;
      nattr->style = cutwin->owner->style;
      nattr->linkid = cutwin->owner->linkid;
    }
  }
  
  ln->text[pos] = ch;
  cutwin->cursorx++; 
  /* note that this can leave the cursor out-of-bounds. It's handled at the
     beginning of this function. */
}

void win_textgrid_set_pos(window_textgrid_t *cutwin, glui32 xpos, glui32 ypos)
{
  if (xpos > 32767)
    xpos = 32767;
  if (ypos > 32767)
    ypos = 32767;
  cutwin->cursorx = xpos;
  cutwin->cursory = ypos;
}

void win_textgrid_clear_window(window_textgrid_t *cutwin)
{
  int ix;
  
  for (ix=0; ix<cutwin->height; ix++) {
    sline_t *ln;
    ln = &(cutwin->linelist[ix]);
    ln->numattr = 0;
    ln->dirtybeg = 0;
    ln->dirtyend = cutwin->width;
  }
  
  cutwin->dirtybeg = 0;
  cutwin->dirtyend = cutwin->height;
  
  cutwin->cursorx = 0;
  cutwin->cursory = 0;
}

static void insert_input(window_textgrid_t *cutwin, 
  int pos, char *buf, int len)
{
  int ix, left;
  sline_t *ln = &cutwin->linelist[cutwin->inputdot.begline];
  
  if (cutwin->inputlen >= cutwin->inputmaxlen) {
    cutwin->dot.begchar = pos;
    cutwin->dot.begline = cutwin->inputdot.begline;
    collapse_dot_back(cutwin);
    return;
  }
  if (pos > cutwin->inputdot.begchar + cutwin->inputlen) {
    pos = cutwin->inputdot.begchar + cutwin->inputlen;
  }
  if (len > cutwin->inputmaxlen - cutwin->inputlen) {
    len = cutwin->inputmaxlen - cutwin->inputlen;
  }
  
  for (ix = cutwin->inputdot.begchar + cutwin->inputlen + len - 1;
       ix >= pos + len; 
       ix--) {
    cutwin->cursorx = ix;
    win_textgrid_add(cutwin, ln->text[ix-len]);
  }
  for (; ix >= pos; ix--) {
    cutwin->cursorx = ix;
    win_textgrid_add(cutwin, buf[ix-pos]);
    cutwin->inputlen++;
  }

  cutwin->dot.begchar = pos+len;
  cutwin->dot.begline = cutwin->inputdot.begline;
  collapse_dot_back(cutwin);
}

static void kill_input(window_textgrid_t *cutwin, int beg, int end)
{
  int diff, post;
  int pos;
  sline_t *ln = &cutwin->linelist[cutwin->inputdot.begline];
  
  if (beg < cutwin->inputdot.begchar)
    beg = cutwin->inputdot.begchar;
  if (end > cutwin->inputdot.begchar + cutwin->inputlen)
    end = cutwin->inputdot.begchar + cutwin->inputlen;

  if (end <= beg)
    return;
  diff = end - beg;
  post = (cutwin->inputdot.begchar + cutwin->inputlen) - end;
  cutwin->cursorx = beg;
  cutwin->cursory = cutwin->inputdot.begline;
  for (pos = beg; pos < beg+post; pos++) {
    win_textgrid_add(cutwin, ln->text[pos+diff]);
  }
  for (; pos < end+post; pos++) {
    win_textgrid_add(cutwin, ' ');
  }
  
  cutwin->inputlen -= diff;
  
  cutwin->dot.begchar = beg;
  cutwin->dot.begline = cutwin->inputdot.begline;
  collapse_dot_back(cutwin);
}

static void xgrid_line_cancel(window_textgrid_t *cutwin, event_t *ev)
{
  long ix, len2;
  long inputlen;
  sline_t *ln;
  long buflen;
  char *buffer;
  gidispatch_rock_t inarrayrock;

  /* same as xged_enter(), but skip the unnecessary stuff. */
  
  if (!cutwin->buffer) 
    return;
  
  buffer = cutwin->buffer;
  buflen = cutwin->buflen;
  inarrayrock = cutwin->inarrayrock;

  ln = &cutwin->linelist[cutwin->inputdot.begline];

  cutwin->owner->style = cutwin->originalattr;

  inputlen = cutwin->inputlen;
  /*if (inputlen > buflen)
    inputlen = buflen;
    memmove(buffer, ln->text+cutwin->inputdot.begchar, inputlen*sizeof(char));*/

  len2 = 0;
  for (ix=0; ix < inputlen && len2 < buflen; ix++) {
    /* We could convert input to Latin-1, but we're assuming that the
       input *is* Latin-1. */
    unsigned char ch = ln->text[cutwin->inputdot.begchar+ix];
    buffer[len2] = ch;
    len2++;
  }

  /*len = cutwin->numchars - cutwin->inputfence;
    if (len) {
    xtext_replace(cutwin->inputfence, len, "", 0);
    cutwin->dotpos = cutwin->numchars;
    cutwin->dotlen = 0;
    xtext_layout();
    }*/
  
  if (cutwin->owner->echostr) {
    window_t *oldwin = cutwin->owner;
    /*gli_stream_echo_line(cutwin->owner->echostr, 
      ln->text+cutwin->inputdot.begchar, cutwin->inputlen*sizeof(char));*/
    gli_stream_echo_line(cutwin->owner->echostr, 
      buffer, len2*sizeof(char));
  }

  cutwin->cursorx = 0;
  cutwin->cursory = cutwin->inputdot.begline + 1;
  win_textgrid_layout(cutwin, FALSE);
  
  /* create event, and set everything blank. */
  ev->type = evtype_LineInput;
  ev->val1 = len2;
  ev->val2 = 0;
  ev->win = cutwin->owner;
  cutwin->owner->line_request = FALSE;
  cutwin->buffer = NULL;
  cutwin->buflen = 0;

  if (gli_unregister_arr) {
    (*gli_unregister_arr)(buffer, buflen, "&+#!Cn", inarrayrock);
  }
}

void xgc_grid_getchar(window_textgrid_t *cutwin, int ch)
{
  if (cutwin->owner->char_request) {
    glui32 key = ch;
    eventloop_setevent(evtype_CharInput, cutwin->owner, key, 0);
    cutwin->owner->char_request = FALSE;
  }
}

void xgc_grid_movecursor(window_textgrid_t *cutwin, int op)
{
  long pos;

  switch (op) {
  case op_BackChar:
    if (DOT_LENGTH_ZERO(&cutwin->dot)) {
      if (cutwin->dot.begchar > 0) {
	cutwin->dot.begchar--;
	collapse_dot_back(cutwin);
      }
      else {
	if (cutwin->dot.begline > 0) {
	  cutwin->dot.begchar = cutwin->width;
	  cutwin->dot.begline--;
	}
	else {
	  cutwin->dot.begchar = 0;
	  cutwin->dot.begline = 0;
	}
	collapse_dot_back(cutwin);
      }
    }
    else if (!DOT_EXISTS(&cutwin->dot)) {
      cutwin->dot.begline = 0;
      cutwin->dot.begchar = 0;
      collapse_dot_back(cutwin);
    }
    else {
      collapse_dot_back(cutwin);
    }
    break;
  case op_ForeChar:
    if (DOT_LENGTH_ZERO(&cutwin->dot)) {
      if (cutwin->dot.begchar < cutwin->width) {
	cutwin->dot.begchar++;
	collapse_dot_back(cutwin);
      }
      else {
	if (cutwin->dot.begline < cutwin->height-1) {
	  cutwin->dot.begchar = 0;
	  cutwin->dot.begline++;
	}
	else {
	  cutwin->dot.begchar = cutwin->width;
	  cutwin->dot.begline = cutwin->height-1;
	}
	collapse_dot_back(cutwin);
      }
    }
    else if (!DOT_EXISTS(&cutwin->dot)) {
      cutwin->dot.begline = 0;
      cutwin->dot.begchar = 0;
      collapse_dot_back(cutwin);
    }
    else {
      collapse_dot(cutwin);
    }
    break;
  case op_BeginLine:
    if (cutwin->buffer) {
      cutwin->dot.begline = cutwin->inputdot.begline;
      cutwin->dot.begchar = cutwin->inputdot.begchar;
      collapse_dot_back(cutwin);
    }
    else {
      if (DOT_EXISTS(&cutwin->dot)) {
	cutwin->dot.begchar = 0;
	collapse_dot_back(cutwin);
      }
    }
    break;
  case op_EndLine:
    if (cutwin->buffer) {
      cutwin->dot.begline = cutwin->inputdot.begline;
      cutwin->dot.begchar = cutwin->inputdot.begchar + cutwin->inputlen;
      collapse_dot_back(cutwin);
    }
    else {
      if (DOT_EXISTS(&cutwin->dot)) {
	cutwin->dot.begchar = cutwin->width;
	collapse_dot_back(cutwin);
      }
    }
    break;
  }
  
  win_textgrid_layout(cutwin, FALSE);
}

void xgc_grid_insert(window_textgrid_t *cutwin, int ch)
{
  char realch;
  int ix, pos;
  
  /* ### not perfect -- should be all typable chars */
  if (ch < 32 || ch >= 127)
    ch = ' ';

  realch = ch;
  
  if (!DOT_EXISTS(&cutwin->dot)) {
    cutwin->dot.begline = 0;
    cutwin->dot.begchar = 0;
    collapse_dot_back(cutwin);
  }
  
  if (!DOT_LENGTH_ZERO(&cutwin->dot)) {
    kill_input(cutwin, cutwin->dot.begchar, cutwin->dot.endchar);
    collapse_dot_back(cutwin);
  }
  
  ix = compare_pos_to_dot(cutwin, &cutwin->inputdot, cutwin->dot.begchar, cutwin->dot.begline);
  if (ix == 0) {
    pos = cutwin->inputdot.begchar;
  }
  else if (ix == 3) {
    pos = cutwin->inputdot.begchar + cutwin->inputlen;
  }
  else {
    pos = cutwin->dot.begchar;
    if (pos > cutwin->inputdot.begchar + cutwin->inputlen)
      pos = cutwin->inputdot.begchar + cutwin->inputlen;
  }
  cutwin->cursory = cutwin->inputdot.begline;
  
  insert_input(cutwin, pos, &realch, 1);
  
  win_textgrid_layout(cutwin, FALSE);
}

void xgc_grid_delete(window_textgrid_t *cutwin, int op)
{
  if (!DOT_EXISTS(&cutwin->dot))
    return;
  
  if (!dot_contains_dot(cutwin, &cutwin->inputdot, &cutwin->dot))
    return;
  
  if (cutwin->dot.endline > cutwin->inputdot.begline 
    || cutwin->dot.endchar > cutwin->inputdot.begchar + cutwin->inputmaxlen) {
    cutwin->dot.endline = cutwin->inputdot.begline;
    cutwin->dot.endchar = cutwin->inputdot.begchar + cutwin->inputmaxlen;
  }
  
  if (!DOT_LENGTH_ZERO(&cutwin->dot) 
    && (op == op_BackChar || op == op_ForeChar)) {
    kill_input(cutwin, cutwin->dot.begchar, cutwin->dot.endchar);
    win_textgrid_layout(cutwin, FALSE);
    return;
  }
  
  collapse_dot(cutwin);

  switch (op) {
  case op_BackChar:
    if (cutwin->dot.begchar > cutwin->inputdot.begchar)
      kill_input(cutwin, cutwin->dot.begchar-1, cutwin->dot.begchar);
    break;
  case op_ForeChar:
    kill_input(cutwin, cutwin->dot.begchar, cutwin->dot.begchar+1);
    break;
  }

  win_textgrid_layout(cutwin, FALSE);
}

static char *xgrid_alloc_selection(window_textgrid_t *cutwin, 
  sdot_t *dot, long *lenv)
{
  long len, cx;
  int jx, sx;
  char *res;
  
  if (!DOT_EXISTS(dot) || DOT_LENGTH_ZERO(dot))
    return NULL;
  
  len = (dot->endline - dot->begline + 1) * (cutwin->width + 1) + 1;
  res = malloc(len);
  if (!res)
    return NULL;
  
  cx = 0;
  
  for (jx = dot->begline; jx <= dot->endline; jx++) {
    int begchar, endchar;
    sline_t *ln = &(cutwin->linelist[jx]);
    
    if (jx == dot->begline)
      begchar = dot->begchar;
    else
      begchar = 0;
    if (jx == dot->endline)
      endchar = dot->endchar;
    else
      endchar = cutwin->width;
    
    if (endchar >= begchar) {
      int ix, ix2;
      sattr_t *attr;
      ix = begchar;
      sx = find_style_at(ln, ix, 0);
      while (ix < endchar) {
	if (sx >= 0 && sx < ln->numattr) {
	  attr = &(ln->attr[sx]);
	  ix2 = attr->end;
	}
	else { 
	  attr = NULL;
	  ix2 = cutwin->width;
	}
	if (ix2 > endchar)
	  ix2 = endchar;
	
	if (!attr || attr->blank) {
	  int ix3;
	  if (!attr || sx == ln->numattr-1) {
	    /* skip trailing blanks */
	  }
	  else {
	    for (ix3 = ix; ix3 < ix2; ix3++) {
	      res[cx] = ' ';
	      cx++;
	    }
	  }
	}
	else {
	  memcpy(res+cx, ln->text + ix, ix2 - ix);
	  cx += (ix2 - ix);
	}
	
	ix = ix2;
	sx++;
      }
      
      if (jx < dot->endline) {
	res[cx] = '\n';
	cx++;
      }
    }
  }
  
  if (cx > len) {
    gli_strict_warning("xgrid_alloc_selection: overran allocation");
  }
  
  *lenv = cx;
  return res;
}

void xgc_grid_cutbuf(window_textgrid_t *cutwin, int op)
{
  long len, num;
  char *buf, *cx;
  
  if (op != op_Copy) {
    if (!cutwin->buffer) {
      xmsg_set_message("You are not editing input in this window.", FALSE);
      return;
    }
  }
  
  switch (op) {
  case op_Copy:
    buf = xgrid_alloc_selection(cutwin, &cutwin->dot, &len);
    if (buf) {
      xglk_store_scrap(buf, len);
      free(buf);
    }
    break;
  case op_Wipe:
    buf = xgrid_alloc_selection(cutwin, &cutwin->dot, &len);
    if (buf) {
      xglk_store_scrap(buf, len);
      free(buf);
    }
    if (dot_contains_dot(cutwin, &cutwin->inputdot, &cutwin->dot)) {
      if (!DOT_LENGTH_ZERO(&cutwin->dot)) {
	kill_input(cutwin, cutwin->dot.begchar, cutwin->dot.endchar);
      }
    }
    break;
  case op_Erase:
    if (dot_contains_dot(cutwin, &cutwin->inputdot, &cutwin->dot)) {
      if (!DOT_LENGTH_ZERO(&cutwin->dot)) {
	kill_input(cutwin, cutwin->dot.begchar, cutwin->dot.endchar);
      }
    }
    break;
  case op_YankReplace:
    if (!dot_contains_dot(cutwin, &cutwin->inputdot, &cutwin->dot))
      break;
    xglk_fetch_scrap(&cx, &num);
    xglk_strip_garbage(cx, num);
    if (cx && num) {
      if (!DOT_LENGTH_ZERO(&cutwin->dot)) {
	kill_input(cutwin, cutwin->dot.begchar, cutwin->dot.endchar);
      }
      insert_input(cutwin, cutwin->dot.begchar, cx, num);
      free(cx);
    }
    break;
  case op_Untype:
    if (cutwin->inputlen > 0) {
      kill_input(cutwin, cutwin->inputdot.begchar, 
	cutwin->inputdot.begchar + cutwin->inputlen);
    }
    break;
  }

  win_textgrid_layout(cutwin, FALSE);
}

void xgc_grid_enter(window_textgrid_t *cutwin, int op)
{
  long ix, len2, inputlen;
  sline_t *ln;
  long buflen;
  char *buffer;
  gidispatch_rock_t inarrayrock;

  if (op != op_Enter)
    return;

  if (!cutwin->buffer) 
    return;

  buffer = cutwin->buffer;
  buflen = cutwin->buflen;
  inarrayrock = cutwin->inarrayrock;
  
  ln = &cutwin->linelist[cutwin->inputdot.begline];
  
  cutwin->owner->style = cutwin->originalattr;

  inputlen = cutwin->inputlen;
  /*if (inputlen > buflen)
    inputlen = buflen;
    memmove(buffer, ln->text+cutwin->inputdot.begchar, inputlen*sizeof(char));*/

  len2 = 0;
  for (ix=0; ix < inputlen && len2 < buflen; ix++) {
    unsigned char ch = ln->text[cutwin->inputdot.begchar+ix];
    buffer[len2] = ch;
    len2++;
  }

  /*if (len) {
    if (cutwin->historynum==cutwin->historylength) {
    free(cutwin->history[0].str);
    memmove(&cutwin->history[0], &cutwin->history[1], (cutwin->historylength-1) * (sizeof(histunit)));
    }
    else
    cutwin->historynum++;
    cutwin->history[cutwin->historynum-1].str = malloc(len*sizeof(char));
    memmove(cutwin->history[cutwin->historynum-1].str, cutwin->charbuf+cutwin->inputfence, len*sizeof(char));
    cutwin->history[cutwin->historynum-1].len = len;
    }*/

  if (cutwin->owner->echostr) {
    window_t *oldwin = cutwin->owner;
    gli_stream_echo_line(cutwin->owner->echostr, 
      buffer, len2*sizeof(char));
  }
  
  cutwin->dot.begline = cutwin->inputdot.begline;
  cutwin->dot.begchar = cutwin->inputdot.begchar + inputlen;
  collapse_dot_back(cutwin);
  cutwin->cursorx = 0;
  cutwin->cursory = cutwin->inputdot.begline + 1;
  win_textgrid_layout(cutwin, FALSE);

  eventloop_setevent(evtype_LineInput, cutwin->owner, len2, 0);
  cutwin->owner->line_request = FALSE;
  cutwin->buffer = NULL;
  cutwin->buflen = 0;

  if (gli_unregister_arr) {
    (*gli_unregister_arr)(buffer, buflen, "&+#!Cn", inarrayrock);
  }

}

