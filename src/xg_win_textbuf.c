#include <stdlib.h>
#include <string.h>
#include "xglk.h"
#include "xg_internal.h"
#include "xg_win_textbuf.h"

#define stype_Text (0)
#define stype_Image (1)
#define stype_Break (2)

/* ### find and kill: cutwin->font.line{height,off} */

typedef struct style_struct {
  int stype; 
  int imagealign; /* from the Glk constants */
  glui32 linkid; /* for hyperlinks */
  glui32 image; /* resource ID */
  glui32 imagewidth, imageheight; /* scaled bounds */
  glui32 attr; /* a style number */
  long pos; /* position this style starts at */
} style_t;

typedef struct imageword_struct {
  picture_t *pic;
  long width, height; /* scaled bounds */
  long pos; /* inline: distance up from the baseline to put top edge;
	       margin: distance in from margin. */
  int imagealign; /* from the Glk constants */
} imageword_t;

typedef struct word_struct {
  int stype;
  long pos, len;
  long width; /* in pixels */
  glui32 attr;
  glui32 linkid;
  union {
    long *letterpos; /* if not NULL, an array[0..len] of pixel offsets 
			from wordpos; */
    imageword_t *image;
  } u;
} word_t;

#define lineflag_Wrapped (1) /* line is a wrap or split from previous line */
#define lineflag_Extra (2) /* the magic extra line on the end */

typedef struct lline_struct {
  long pos; /* line starts here */
  long posend; /* number of chars. May not be exactly to start of next line, 
		  because it won't include the newline or space that ends 
		  the line. */
  long indent; /* in pixels, from textwin_w (0 or greater) */
  word_t *wordlist;
  long numwords;
  long height; /* in pixels */
  long off; /* baseline offset from top, in pixels */
  long leftindent; /* how much flow-object there is on the left margin */
  long leftbelow; /* how much of that object hangs below this line */
  long rightindent; /* how much flow-object there is on the right margin */
  long rightbelow; /* how much of that object hangs below this line */
  int flags;
} lline_t;

typedef struct histunit {
  char *str;
  int len;
} histunit;

struct window_textbuffer_struct {
  window_t *owner;
  XRectangle bbox;

  int textwin_x, textwin_y, textwin_w, textwin_h; /* bbox minus margins */
  /*XRectangle textwin_cursor_box;*/ /* This is the area in which the cursor 
				    is an ibeam. Assuming we do cursors. */
  wegscroll_t scrollbar; 

  stylehints_t hints;
  fontset_t font;

  char *charbuf;
  long numchars;
  long char_size;

  style_t *stylelist;
  long numstyles;
  long styles_size;

  lline_t *linelist;
  long numlines;
  long lines_size;

  lline_t *tmplinelist;
  long tmplines_size;

  long scrollpos; /* character position at top of screen */
  long scrollline; /* number of line at top of screen, after 
		      win_textbuffer_layout() */
  long lastseenline; /* last line read before more stuff was output. If 
			everything has been read, this is numlines. */
  long dotpos, dotlen; /* dotpos is in [0..numchars] */
  long lastdotpos, lastdotlen; /* cached values -- fiddled inside 
				  win_textbuffer_layout() */

  long linesonpage; /* this stuff is set by xtext_layout() too. */
  long lineoffset_size;
  int *lineoffsetlist; /* In [0..linesonpage], and the first element is 
			    zero. */

  long drag_firstbeg, drag_firstend;
  int drag_inscroll; 
  glui32 drag_firstlink;

  int isactive; /* is window active? */

  long dirtybeg, dirtyend; /* mark the limits of what needs to be laid 
			      out, [) format */
  long dirtydelta; /* how much the dirty area has grown (or shrunk) */

  int isclear;

  int historylength; /* cached in case the pref is changed */
  int historynum, historypos;
  histunit *history;

  /* these are for line input */
  long buflen;
  char *buffer;
  long inputfence;
  glui32 originalattr;
  gidispatch_rock_t inarrayrock;
};

#define collapse_dot(cwin)  (cwin->dotpos += cwin->dotlen, cwin->dotlen = 0)
#define SIDEMARGIN (0)
#define BARWIDTH (18)
#define MACSTYLESCROLL

static void redrawtext(window_textbuffer_t *cutwin, long beg, 
  long num, int clearnum);
static void win_textbuffer_layout(window_textbuffer_t *cutwin);
static void flip_selection(window_textbuffer_t *cutwin, long dpos, long dlen);
static long find_loc_by_pos(window_textbuffer_t *cutwin, long pos, 
  int *xposret, int *yposret);
static long find_pos_by_loc(window_textbuffer_t *cutwin, int xpos, int ypos,
  long *truepos);
static long find_line_by_pos(window_textbuffer_t *cutwin, long pos, 
  long guessline);
static void measure_word(window_textbuffer_t *cutwin, lline_t *curline, 
  word_t *curword);
static void win_textbuffer_line_cancel(window_textbuffer_t *cutwin, 
  event_t *ev);
static void win_textbuffer_setstyle(window_textbuffer_t *cutwin,
  long pos, int stype, 
  glui32 attr, glui32 linkid, glui32 image, int imagealign, 
  glui32 imagewidth, glui32 imageheight);
static void readd_lineheights(window_textbuffer_t *cutwin, long lx);

static long back_to_white(window_textbuffer_t *cutwin, long pos);
static long fore_to_white(window_textbuffer_t *cutwin, long pos);
static long back_to_nonwhite(window_textbuffer_t *cutwin, long pos);
static long fore_to_nonwhite(window_textbuffer_t *cutwin, long pos);
static void delete_imageword(imageword_t *iwd);

window_textbuffer_t *win_textbuffer_create(window_t *win)
{
  window_textbuffer_t *res = 
    (window_textbuffer_t *)malloc(sizeof(window_textbuffer_t));
  if (!res)
    return NULL;
  
  res->owner = win;
  
  gli_stylehints_for_window(wintype_TextBuffer, &(res->hints));
  gli_styles_compute(&(res->font), &(res->hints));
  
  res->char_size = 256;
  res->charbuf = (char *)malloc(sizeof(char) * res->char_size);
  res->numchars = 0;

  res->styles_size = 8;
  res->stylelist = (style_t *)malloc(sizeof(style_t) * res->styles_size);
  res->numstyles = 1;
  res->stylelist[0].pos = 0;
  res->stylelist[0].stype = stype_Text;
  res->stylelist[0].attr = style_Normal; 
  res->stylelist[0].linkid = 0;
  res->stylelist[0].image = 0;
  res->stylelist[0].imagealign = 0;

  res->lines_size = 8;
  res->linelist = (lline_t *)malloc(sizeof(lline_t) * res->lines_size);
  res->numlines = 0;

  res->tmplines_size = 8;
  res->tmplinelist = (lline_t *)malloc(sizeof(lline_t) * res->tmplines_size);

  res->historynum = 0;
  res->historylength = prefs.historylength;
  res->history = (histunit *)malloc(res->historylength * sizeof(histunit));

  res->scrollpos = 0;
  res->scrollline = 0;
  xweg_init_scrollbar(&res->scrollbar, win, xgc_scroll, xgc_scrollto);

  res->dirtybeg = 0;
  res->dirtyend = 0;
  res->dirtydelta = 0;

  res->dotpos = 0;
  res->dotlen = 0;
  res->lastdotpos = -1;
  res->lastdotlen = 0;

  res->drag_firstbeg = 0;
  res->drag_firstend = 0;
  res->drag_firstlink = 0;
  res->drag_inscroll = FALSE;

  res->isactive = FALSE;

  res->lastseenline = 0;
  res->isclear = TRUE;
  
  res->linesonpage = 0;
  res->lineoffset_size = 80;
  res->lineoffsetlist = (int *)malloc(res->lineoffset_size * sizeof(int));
  res->lineoffsetlist[0] = 0;
  /*### res->charsperline = 0; */

  res->buffer = NULL;
  res->buflen = 0;
  res->inputfence = 0;
  
  return res;
}

void win_textbuffer_destroy(struct window_textbuffer_struct *dwin)
{
  int ix;

  if (dwin->buffer) {
    if (gli_unregister_arr) {
      (*gli_unregister_arr)(dwin->buffer, dwin->buflen, "&+#!Cn", 
	dwin->inarrayrock);
    }
    dwin->buffer = NULL;
  }

  if (dwin->history) {
    for (ix=0; ix<dwin->historynum; ix++) {
      free(dwin->history[ix].str);
    }
    free(dwin->history);
    dwin->history = NULL;
  }
  
  if (dwin->stylelist) {
    free(dwin->stylelist);
    dwin->stylelist = NULL;
  }
  
  if (dwin->linelist) {
    long lx, wx;
    
    for (lx=0; lx<dwin->numlines; lx++) {
      word_t *thisword;
      for (wx=0, thisword=dwin->linelist[lx].wordlist;
	    wx<dwin->linelist[lx].numwords;
	   wx++, thisword++) {
	if (thisword->stype == stype_Text) {
	  if (thisword->u.letterpos) {
	    free(thisword->u.letterpos);
	    thisword->u.letterpos = NULL;
	  }
	}
	else if (thisword->stype == stype_Image) {
	  if (thisword->u.image) {
	    delete_imageword(thisword->u.image);
	    thisword->u.image = NULL;
	  }
	}
      }
      free(dwin->linelist[lx].wordlist);
      dwin->linelist[lx].wordlist = NULL;
    }

    free(dwin->linelist);
    dwin->linelist = NULL;
  }
  
  if (dwin->tmplinelist) {
    /* don't free contents; they're owned by linelist. */
    free(dwin->tmplinelist);
    dwin->tmplinelist = NULL;
  }
  
  if (dwin->charbuf) {
    free(dwin->charbuf);
    dwin->charbuf = NULL;
  }
  
  free(dwin);
}

void win_textbuffer_rearrange(window_t *win, XRectangle *box)
{
  window_textbuffer_t *cutwin = win->data;
  
  int xpos = box->x;
  int ypos = box->y; 
  int width = box->width; 
  int height = box->height;
  
  cutwin->bbox = *box;
  
  cutwin->textwin_x = xpos+SIDEMARGIN+prefs.textbuffer.marginx;
  cutwin->textwin_y = ypos+prefs.textbuffer.marginy;
  cutwin->textwin_w = width-2*SIDEMARGIN-BARWIDTH-2*prefs.textbuffer.marginx;
  cutwin->textwin_h = height-2*prefs.textbuffer.marginy;

  cutwin->scrollbar.box.y = ypos;
  cutwin->scrollbar.box.height = height;
  cutwin->scrollbar.box.width = BARWIDTH;
  cutwin->scrollbar.box.x = xpos + width - BARWIDTH;

  /*
  cutwin->textwin_cursor_box.x = xpos;
  cutwin->textwin_cursor_box.y = ypos;
  cutwin->textwin_cursor_box.width = width - BARWIDTH;
  cutwin->textwin_cursor_box.height = height;
  */

  cutwin->dirtybeg = 0;
  cutwin->dirtyend = cutwin->numchars;
  cutwin->dirtydelta = 0;

  cutwin->linesonpage = 0; 
  cutwin->lineoffsetlist[0] = 0;
  /* This will be set to something real later. */
}

XRectangle *win_textbuffer_get_rect(window_t *win)
{
  window_textbuffer_t *dwin = win->data;
  return &dwin->bbox;
}

void win_textbuffer_get_size(window_t *win, glui32 *width, glui32 *height)
{
  window_textbuffer_t *dwin = win->data;
  *width = 0; /*###dwin->linesperpage; */
  *height = 0; /*###dwin->charsperline; */
}

long win_textbuffer_figure_size(window_t *win, long size, int vertical)
{
  window_textbuffer_t *dwin = win->data;

  if (vertical) {
    /* size * charwidth */
    long textwin_w = size * dwin->font.gc[style_Normal].spacewidth;
    return textwin_w + 2*SIDEMARGIN + BARWIDTH + 2*prefs.textbuffer.marginx;
  }
  else {
    /* size * lineheight */
    long textwin_h = size * dwin->font.lineheight;
    return textwin_h + 2*prefs.textbuffer.marginy;
  }
}

fontset_t *win_textbuffer_get_fontset(window_t *win)
{
  window_textbuffer_t *dwin = win->data;
  return &(dwin->font);
}

stylehints_t *win_textbuffer_get_stylehints(window_t *win)
{
  window_textbuffer_t *dwin = win->data;
  return &(dwin->hints);
}

void win_textbuffer_redraw(window_t *win)
{
  window_textbuffer_t *cutwin = win->data;

  gli_draw_window_outline(&cutwin->bbox);

  gli_draw_window_margin(&(cutwin->font.backcolor),
    cutwin->bbox.x, cutwin->bbox.y,
    cutwin->bbox.width-BARWIDTH, cutwin->bbox.height,
    cutwin->textwin_x-SIDEMARGIN, cutwin->textwin_y, 
    cutwin->textwin_w+2*SIDEMARGIN, cutwin->textwin_h);

  xweg_draw_scrollbar(&cutwin->scrollbar);

  if (cutwin->dirtybeg >= 0 || cutwin->dirtyend >= 0) {
    /* something is dirty. Go through the whole shebang. */
    win_textbuffer_layout(cutwin);
    return;
  }
  
  /* this assumes that an exposure event will not come in
     between a data update and an win_textbuffer_layout call. */
  /*flip_selection(cutwin->dotpos, cutwin->dotlen);*/
  redrawtext(cutwin, 0, -1, -1);
  flip_selection(cutwin, cutwin->dotpos, cutwin->dotlen);

  xweg_adjust_scrollbar(&cutwin->scrollbar, cutwin->numlines, 
    cutwin->scrollline, cutwin->linesonpage);
}

void win_textbuffer_flush(window_t *win)
{
  window_textbuffer_t *cutwin = win->data;
  win_textbuffer_layout(cutwin);
}

void win_textbuffer_setfocus(window_t *win, int turnon)
{
  window_textbuffer_t *cutwin = win->data;

  if (turnon) {
    if (!cutwin->isactive) {
      cutwin->isactive = TRUE;
      flip_selection(cutwin, cutwin->dotpos, cutwin->dotlen);
    }
  }
  else {
    if (cutwin->isactive) {
      flip_selection(cutwin, cutwin->dotpos, cutwin->dotlen);
      cutwin->isactive = FALSE;
    }
  }
}
void win_textbuffer_init_line(window_t *win, char *buffer, int buflen, 
  int readpos)
{
  window_textbuffer_t *cutwin = win->data;
  
  cutwin->buflen = buflen;
  cutwin->buffer = buffer;

  if (readpos) {
    /* The terp has to enter the text. */
    cutwin->inputfence = cutwin->numchars;
    cutwin->originalattr = cutwin->stylelist[cutwin->numstyles-1].attr;
    win_textbuffer_set_style_text(cutwin, style_Input);
    win_textbuffer_replace(cutwin, cutwin->inputfence, 0, 
      cutwin->buffer, readpos);
    win_textbuffer_layout(cutwin);
  }
  else {
    cutwin->inputfence = cutwin->numchars;
    cutwin->originalattr = cutwin->stylelist[cutwin->numstyles-1].attr;
    win_textbuffer_set_style_text(cutwin, style_Input);
  }

  cutwin->historypos = cutwin->historynum;

  if (gli_register_arr) {
    cutwin->inarrayrock = (*gli_register_arr)(buffer, buflen, "&+#!Cn");
  }
}

void win_textbuffer_cancel_line(window_t *win, event_t *ev)
{
  window_textbuffer_t *cutwin = win->data;
  win_textbuffer_line_cancel(cutwin, ev);
}

void win_textbuffer_perform_click(window_t *win, int dir, XPoint *pt, 
  int butnum, int clicknum, unsigned int state)
{
  window_textbuffer_t *cutwin = win->data; 
  long pos, hitpos;
  long px, px2;

  if (dir == mouse_Down) {
    cutwin->drag_firstlink = 0;
    if (pt->x >= cutwin->scrollbar.box.x)
      cutwin->drag_inscroll = TRUE;
    else
      cutwin->drag_inscroll = FALSE;

    if (cutwin->drag_inscroll) {
      xweg_click_scrollbar(&cutwin->scrollbar, dir, pt, butnum, 
	clicknum, state, 
	cutwin->numlines, cutwin->scrollline, cutwin->linesonpage);
    }
    else {
      pt->x -= cutwin->textwin_x;
      pt->y -= cutwin->textwin_y;

      pos = find_pos_by_loc(cutwin, pt->x, pt->y, &hitpos);
      if (butnum==1) {
	if (!(clicknum & 1)) {
	  px = back_to_white(cutwin, pos);
	  px2 = fore_to_white(cutwin, pos);
	}
	else {
	  if (cutwin->owner->hyperlink_request) {
	    long sx;
	    /* find the last style at or before hitpos. */
	    for (sx=cutwin->numstyles-1; sx>=0; sx--) {
	      if (cutwin->stylelist[sx].pos <= hitpos) {
		cutwin->drag_firstlink = cutwin->stylelist[sx].linkid;
		break;
	      }
	    }
	  }
	  px = pos;
	  px2 = pos;
	}
	cutwin->dotpos = px;
	cutwin->dotlen = px2-px;
	cutwin->drag_firstbeg = px;
	cutwin->drag_firstend = px2;
      }
      else {
	if (pos < cutwin->dotpos+cutwin->dotlen/2) {
	  cutwin->drag_firstbeg = cutwin->dotpos+cutwin->dotlen;
	}
	else {
	  cutwin->drag_firstbeg = cutwin->dotpos;
	}
	cutwin->drag_firstend = cutwin->drag_firstbeg;
	if (pos < cutwin->drag_firstbeg) {
	  if (!(clicknum & 1))
	    cutwin->dotpos = back_to_white(cutwin, pos);
	  else
	    cutwin->dotpos = pos;
	  cutwin->dotlen = cutwin->drag_firstend-cutwin->dotpos;
	}
	else if (pos > cutwin->drag_firstend) {
	  cutwin->dotpos = cutwin->drag_firstbeg;
	  if (!(clicknum & 1))
	    cutwin->dotlen = fore_to_white(cutwin, pos)-cutwin->drag_firstbeg;
	  else
	    cutwin->dotlen = pos-cutwin->drag_firstbeg;
	}
	else {
	  cutwin->dotpos = cutwin->drag_firstbeg;
	  cutwin->dotlen = cutwin->drag_firstend-cutwin->drag_firstbeg;
	}
      }
      win_textbuffer_layout(cutwin);
    }
  }
  else if (dir == mouse_Move) {
    if (cutwin->drag_inscroll) {
      xweg_click_scrollbar(&cutwin->scrollbar, dir, pt, butnum, 
	clicknum, state, 
	cutwin->numlines, cutwin->scrollline, cutwin->linesonpage);
    }
    else {
      pt->x -= cutwin->textwin_x;
      pt->y -= cutwin->textwin_y;

      pos = find_pos_by_loc(cutwin, pt->x, pt->y, &hitpos);

      if (pos < cutwin->drag_firstbeg) {
	if (!(clicknum & 1))
	  cutwin->dotpos = back_to_white(cutwin, pos);
	else
	  cutwin->dotpos = pos;
	cutwin->dotlen = cutwin->drag_firstend-cutwin->dotpos;
      }
      else if (pos > cutwin->drag_firstend) {
	cutwin->dotpos = cutwin->drag_firstbeg;
	if (!(clicknum & 1))
	  cutwin->dotlen = fore_to_white(cutwin, pos)-cutwin->drag_firstbeg;
	else
	  cutwin->dotlen = pos-cutwin->drag_firstbeg;
      }
      else {
	cutwin->dotpos = cutwin->drag_firstbeg;
	cutwin->dotlen = cutwin->drag_firstend-cutwin->drag_firstbeg;
      }
      win_textbuffer_layout(cutwin);
    }
  }
  else if (dir == mouse_Up) {
    if (cutwin->drag_inscroll) {
    }
    else {
      pt->x -= cutwin->textwin_x;
      pt->y -= cutwin->textwin_y;
      if (cutwin->drag_firstlink && cutwin->owner->hyperlink_request) {
	pos = find_pos_by_loc(cutwin, pt->x, pt->y, &hitpos);
	if (pos == cutwin->drag_firstbeg) {
	  cutwin->owner->hyperlink_request = FALSE;
	  eventloop_setevent(evtype_Hyperlink, cutwin->owner, 
	    cutwin->drag_firstlink, 0);
	  /* suppressdblclick = TRUE; */
	}
      }
    }
  }
}

static void delete_imageword(imageword_t *iwd)
{
  if (iwd->pic) {
    picture_release(iwd->pic);
    iwd->pic = NULL;
  }
  free(iwd);
}

static long back_to_white(window_textbuffer_t *cutwin, long pos)
{
  char *charbuf = cutwin->charbuf;
  long inputfence = cutwin->inputfence;
  while (pos > 0 && charbuf[pos-1] != ' ' && charbuf[pos-1] != '\n' && pos-1 != inputfence-1)
    pos--;
  return pos;
}

static long fore_to_white(window_textbuffer_t *cutwin, long pos)
{
  char *charbuf = cutwin->charbuf;
  long inputfence = cutwin->inputfence;
  long numchars = cutwin->numchars;
  while (pos < numchars && charbuf[pos] != ' ' && charbuf[pos] != '\n' && pos != inputfence-1)
    pos++;
  return pos;
}

static long back_to_nonwhite(window_textbuffer_t *cutwin, long pos)
{
  char *charbuf = cutwin->charbuf;
  long inputfence = cutwin->inputfence;
  while (pos > 0 && (charbuf[pos-1] == ' ' || charbuf[pos-1] == '\n' || pos-1 == inputfence-1))
    pos--;
  return pos;
}

static long fore_to_nonwhite(window_textbuffer_t *cutwin, long pos)
{
  char *charbuf = cutwin->charbuf;
  long inputfence = cutwin->inputfence;
  long numchars = cutwin->numchars;
  while (pos < numchars && (charbuf[pos] == ' ' || charbuf[pos] == '\n' || pos == inputfence-1))
    pos++;
  return pos;
}

/* Coordinates are in screen lines. If num < 0, go to the end. clearnum is the 
   number of lines to clear (may be to a notional line); if 0, don't clear at 
   all; if -1, clear whole window. */
static void redrawtext(window_textbuffer_t *cutwin, long beg, long num, 
  int clearnum)
{
  long lx, wx, end;
  int ypos, ypos2, xpos;
  lline_t *thisline;
  fontref_t *gclist;
  word_t *thisword;
  int textwin_x, textwin_y, textwin_w, textwin_h;
  XRectangle textwinbox;

  /* cache some much-used values. */
  gclist = cutwin->font.gc;
  textwinbox.x = textwin_x = cutwin->textwin_x;
  textwinbox.y = textwin_y = cutwin->textwin_y;
  textwinbox.width = textwin_w = cutwin->textwin_w;
  textwinbox.height = textwin_h = cutwin->textwin_h;
  
  if (num<0)
    end = cutwin->numlines;
  else {
    end = beg+num;
    if (end > cutwin->numlines)
      end = cutwin->numlines;
  }

  if (beg < cutwin->scrollline)
    beg = cutwin->scrollline;

  if (clearnum != 0) {
    if (beg-cutwin->scrollline < 0) {
      ypos = textwin_y;
    }
    else if (beg-cutwin->scrollline >= cutwin->linesonpage) {
      ypos = textwin_y + cutwin->lineoffsetlist[cutwin->linesonpage];
    }
    else {
      ypos = textwin_y + cutwin->lineoffsetlist[beg-cutwin->scrollline];
    }
    if (clearnum > 0) {
      long clearend = beg+clearnum;
      if (clearend-cutwin->scrollline < 0) {
	ypos2 = textwin_y;
      }
      else if (clearend-cutwin->scrollline >= cutwin->linesonpage) {
	ypos2 = textwin_y + textwin_h;
      }
      else {
	ypos2 = textwin_y + cutwin->lineoffsetlist[clearend-cutwin->scrollline];
      }
    }
    else {
      ypos2 = textwin_y+textwin_h;
    }
    if (ypos < ypos2) {
      xglk_clearfor_string(&(cutwin->font.backcolor),
	textwin_x-SIDEMARGIN, ypos, 
	textwin_w+2*SIDEMARGIN, ypos2-ypos);
    }
  }

  /* Back up and draw any hanging images above. */
  if (beg < end 
    && (cutwin->linelist[beg].leftindent || cutwin->linelist[beg].rightindent)) {
    int stillleft = (cutwin->linelist[beg].leftindent != 0);
    int stillright = (cutwin->linelist[beg].rightindent != 0);
    ypos = textwin_y + cutwin->lineoffsetlist[beg-cutwin->scrollline];
    for (lx = beg-1; lx >= 0; lx--) {
      thisline = (&cutwin->linelist[lx]);
      if (thisline->leftbelow == 0)
	stillleft = FALSE;
      if (thisline->rightbelow == 0)
	stillright = FALSE;
      if (!stillleft && !stillright)
	break;
      ypos -= thisline->height;
      for (wx=0; wx<thisline->numwords; wx++) {
	thisword = thisline->wordlist+wx;
	if (thisword->stype == stype_Image) {
	  imageword_t *iwd = thisword->u.image;
	  if (iwd) {
	    if (iwd->imagealign == imagealign_MarginLeft) {
	      if (stillleft) 
		picture_draw(iwd->pic, xiowin, textwin_x+iwd->pos, ypos, 
		  iwd->width, iwd->height, &textwinbox);
	    }
	    else if (iwd->imagealign == imagealign_MarginRight) {
	      if (stillright)
		picture_draw(iwd->pic, xiowin, 
		  (textwin_x+textwin_w-iwd->width)-iwd->pos, 
		  ypos, iwd->width, iwd->height, &textwinbox);
	    }
	  }
	}
	else if (thisword->stype == stype_Text) {
	  break;
	}
      }
    }
  }

  /* The main line-drawing loop. */
  for (lx=beg; lx<end; lx++) {
    thisline = (&cutwin->linelist[lx]);
    if (lx-cutwin->scrollline >= cutwin->linesonpage) {
      break;
    }
    ypos = textwin_y + cutwin->lineoffsetlist[lx-cutwin->scrollline];
    xpos = textwin_x + thisline->indent;
    for (wx=0; wx<thisline->numwords; wx++) {
      thisword = thisline->wordlist+wx;
      if (thisword->stype == stype_Image) {
	imageword_t *iwd = thisword->u.image;
	if (iwd) {
	  if (iwd->imagealign == imagealign_MarginLeft) {
	    picture_draw(iwd->pic, xiowin, textwin_x+iwd->pos, ypos, 
	      iwd->width, iwd->height, &textwinbox);
	  }
	  else if (iwd->imagealign == imagealign_MarginRight) {
	    picture_draw(iwd->pic, xiowin, 
	      (textwin_x+textwin_w-iwd->width)-iwd->pos, 
	      ypos, iwd->width, iwd->height, &textwinbox);
	  }
	  else {
	    picture_draw(iwd->pic, xiowin, 
	      xpos, ypos+thisline->off-iwd->pos, 
	      iwd->width, iwd->height, &textwinbox);
	  }
	}
      }
      else if (thisword->stype == stype_Text) {
	if (gclist[thisword->attr].backcolor.pixel 
	  != cutwin->font.backcolor.pixel) {
	  xglk_clearfor_string(&(gclist[thisword->attr].backcolor), 
	    xpos, ypos, thisword->width, thisline->height);
	}
	xglk_draw_string(&(gclist[thisword->attr]), 
	  (thisword->linkid != 0), thisword->width,
	  xpos, ypos+thisline->off, 
	  cutwin->charbuf+thisline->pos+thisword->pos, thisword->len);
      }
      xpos += thisword->width;
    }
  }
}

static void scroll_to(window_textbuffer_t *cutwin, long newscrollline)
{
  long ix, val, total;
  long oldscrollline;

#ifdef MACSTYLESCROLL
  total = 0;
  for (val = cutwin->numlines; val > 0; val--) {
    total += cutwin->linelist[val-1].height;
    if (total > cutwin->textwin_h)
      break;
  }
  if (newscrollline > val)
    newscrollline = val;
  if (newscrollline < 0)
    newscrollline = 0;
#else
  if (newscrollline > cutwin->numlines-cutwin->linesonpage)
    newscrollline = cutwin->numlines-cutwin->linesonpage;
  if (newscrollline < 0)
    newscrollline = 0;
#endif

  if (cutwin->numlines == 0)
    cutwin->scrollpos = 0;
  else
    cutwin->scrollpos = cutwin->linelist[newscrollline].pos;
  
  if (cutwin->scrollline != newscrollline) {
    oldscrollline = cutwin->scrollline;
    if (!xiobackstore
      || oldscrollline + cutwin->linesonpage <= newscrollline
      || newscrollline + cutwin->linesonpage <= oldscrollline) {
      /* scroll to an entirely new place. */
      cutwin->scrollline = newscrollline;
      readd_lineheights(cutwin, cutwin->scrollline);
      redrawtext(cutwin, cutwin->scrollline, -1, -1);
      flip_selection(cutwin, cutwin->dotpos, cutwin->dotlen);
    }
    else {
      int ypos1, ypos2, yhgt;
      long oldlop, diff;
      flip_selection(cutwin, cutwin->dotpos, cutwin->dotlen);
      cutwin->scrollline = newscrollline;
      if (oldscrollline < newscrollline) {
	/* scroll down -- things move up */
	ypos1 = cutwin->textwin_y + cutwin->lineoffsetlist[newscrollline-oldscrollline];
	ypos2 = cutwin->textwin_y;
	yhgt = cutwin->textwin_y + cutwin->textwin_h - ypos1;
	XCopyArea(xiodpy, xiowin, xiowin, gcfore,
	  cutwin->textwin_x-SIDEMARGIN, ypos1, 
	  cutwin->textwin_w+2*SIDEMARGIN, yhgt, 
	  cutwin->textwin_x-SIDEMARGIN, ypos2);
	oldlop = cutwin->linesonpage;
	readd_lineheights(cutwin, cutwin->scrollline);
	diff = (cutwin->scrollline + cutwin->linesonpage) - (oldscrollline + oldlop);
	redrawtext(cutwin, oldscrollline + oldlop, diff, diff);
      }
      else {
	/* scroll up -- things move down */
	ypos2 = cutwin->textwin_y;
	for (ix = newscrollline; ix < oldscrollline; ix++)
	  ypos2 += cutwin->linelist[ix].height;
	ypos1 = cutwin->textwin_y;
	yhgt = cutwin->textwin_y + cutwin->textwin_h - ypos2;
	XCopyArea(xiodpy, xiowin, xiowin, gcfore,
	  cutwin->textwin_x-SIDEMARGIN, ypos1, 
	  cutwin->textwin_w+2*SIDEMARGIN, yhgt, 
	  cutwin->textwin_x-SIDEMARGIN, ypos2);
	readd_lineheights(cutwin, cutwin->scrollline);
	redrawtext(cutwin, newscrollline, (oldscrollline-newscrollline), 
	  (oldscrollline-newscrollline));
      }
      flip_selection(cutwin, cutwin->dotpos, cutwin->dotlen);
    }
    xweg_adjust_scrollbar(&cutwin->scrollbar, cutwin->numlines,
      cutwin->scrollline, cutwin->linesonpage);
  }
  
  if (cutwin->lastseenline < cutwin->scrollline) {
    cutwin->lastseenline = cutwin->scrollline;
  }
  if (cutwin->lastseenline >= cutwin->numlines - cutwin->linesonpage) {
    /* this is wrong -- linesonpage doesn't correctly measure lines at 
       the end. */
    cutwin->lastseenline = cutwin->numlines;
  }
}

static void refiddle_selection(window_textbuffer_t *cutwin, 
  long oldpos, long oldlen, long newpos, long newlen)
{
  if (oldlen==0 || newlen==0 || oldpos<0 || newpos<0) {
    flip_selection(cutwin, oldpos, oldlen);
    flip_selection(cutwin, newpos, newlen);
    return;
  }

  if (oldpos == newpos) {
    /* start at same place */
    if (oldlen < newlen) {
      flip_selection(cutwin, oldpos+oldlen, newlen-oldlen);
    }
    else if (newlen < oldlen) {
      flip_selection(cutwin, oldpos+newlen, oldlen-newlen);
    }
    return;
  }
  if (oldpos+oldlen == newpos+newlen) {
    /* end at same place */
    if (oldpos < newpos) {
      flip_selection(cutwin, oldpos, newpos-oldpos);
    }
    else if (newpos < oldpos) {
      flip_selection(cutwin, newpos, oldpos-newpos);
    }
    return;
  }

  flip_selection(cutwin, oldpos, oldlen);
  flip_selection(cutwin, newpos, newlen);
}

static void flip_selection(window_textbuffer_t *cutwin, long dpos, long dlen)
{
  int xpos, ypos;
  int xpos2, ypos2;
  int off, height;
  long ybody, ybody2;
  long lx, lx2;

  if (!cutwin->isactive) {
    return; /* not the front window */
  }

  if (dpos < 0) {
    return; /* dot hidden */
  }

  if (dlen==0) {
    lx = find_loc_by_pos(cutwin, dpos, &xpos, &ypos);
    if (lx-cutwin->scrollline < 0
      || lx-cutwin->scrollline >= cutwin->linesonpage)
      return;
    height = cutwin->linelist[lx].height;
    xglk_draw_dot(cutwin->textwin_x + xpos, 
      cutwin->textwin_y + ypos + height, height);
  }
  else {
    lx = find_loc_by_pos(cutwin, dpos, &xpos, &ypos);
    lx2 = find_loc_by_pos(cutwin, dpos+dlen, &xpos2, &ypos2);
    if (ypos==ypos2) {
      /* within one line */
      if (lx-cutwin->scrollline < 0
	|| lx-cutwin->scrollline >= cutwin->linesonpage)
	return;
      height = cutwin->linelist[lx].height;
      if (xpos < xpos2) {
	XFillRectangle(xiodpy, xiowin, gcflip, 
	  xpos+cutwin->textwin_x, ypos+cutwin->textwin_y, 
	  xpos2-xpos, height);
      }
    }
    else {
      if (lx-cutwin->scrollline >= cutwin->linesonpage)
	return;
      if (lx2-cutwin->scrollline < 0)
	return;
      if (lx-cutwin->scrollline >= 0) {
	height = cutwin->linelist[lx].height;
	if (xpos < cutwin->textwin_w) {
	  /* first partial line */
	  XFillRectangle(xiodpy, xiowin, gcflip, 
	    xpos+cutwin->textwin_x, ypos+cutwin->textwin_y, 
	    cutwin->textwin_w-xpos, height);
	}
	ybody = ypos + cutwin->linelist[lx].height;
	if (ybody < 0)
	  ybody = 0;
      }
      else {
	ybody = 0;
      }
      if (lx2-cutwin->scrollline < cutwin->linesonpage) {
	ybody2 = ypos2;
	height = cutwin->linelist[lx2].height;
      }
      else {
	ybody2 = cutwin->textwin_h;
	height = 0;
      }
      if (ybody < ybody2) {
	/* main body */
	XFillRectangle(xiodpy, xiowin, gcflip, 
	  cutwin->textwin_x, ybody+cutwin->textwin_y, 
	  cutwin->textwin_w, ybody2-ybody);
      }
      if (xpos2 > 0 && height) {
	/* last partial line */
	XFillRectangle(xiodpy, xiowin, gcflip, 
	  cutwin->textwin_x, ypos2+cutwin->textwin_y, 
	  xpos2, height);
      }
    }
  }
}

/* push lines from tmplinelist[0..newnum) in place of 
   linelist[oldbeg..oldend) */
static void slapover(window_textbuffer_t *cutwin, long newnum, 
  long oldbeg, long oldend)
{
  long wx, lx;
  long newnumlines;

  newnumlines = cutwin->numlines-(oldend-oldbeg)+newnum;
  if (newnumlines >= cutwin->lines_size) {
    while (newnumlines >= cutwin->lines_size)
      cutwin->lines_size *= 2;
    cutwin->linelist = (lline_t *)realloc(cutwin->linelist, 
      sizeof(lline_t) * cutwin->lines_size);
  }

  /* clobber old */
  for (lx=oldbeg; lx<oldend; lx++) {
    word_t *thisword;
    /* --- finalize word structure --- */
    for (wx=0, thisword=cutwin->linelist[lx].wordlist;
	 wx<cutwin->linelist[lx].numwords;
	 wx++, thisword++) {
      if (thisword->stype == stype_Text) {
	if (thisword->u.letterpos) {
	  free(thisword->u.letterpos);
	  thisword->u.letterpos = NULL;
	}
      }
      else if (thisword->stype == stype_Image) {
	if (thisword->u.image) {
	  delete_imageword(thisword->u.image);
	  thisword->u.image = NULL;
	}
      }
    }
    free(cutwin->linelist[lx].wordlist);
    cutwin->linelist[lx].wordlist = NULL;
  }

  if (oldend < cutwin->numlines && newnumlines != cutwin->numlines) {
    memmove(&cutwin->linelist[oldend+(newnumlines-cutwin->numlines)],
      &cutwin->linelist[oldend],
      sizeof(lline_t) * (cutwin->numlines-oldend));
  }
  cutwin->numlines = newnumlines;

  if (newnum) {
    memcpy(&cutwin->linelist[oldbeg],
         &cutwin->tmplinelist[0],
         sizeof(lline_t) * (newnum));
  }
}

/* xpos, ypos are relative to textwin origin */
static long find_pos_by_loc(window_textbuffer_t *cutwin, int xpos, int ypos,
  long *truepos)
{
  int ix, jx;
  long linenum;
  long wx, atpos, newpos;
  lline_t *curline;
  word_t *curword;

  *truepos = -1;

  if (ypos < 0) 
    linenum = (-1) - ((-1)-ypos / cutwin->font.lineheight);
  else if (ypos >= cutwin->lineoffsetlist[cutwin->linesonpage]) 
    linenum = cutwin->linesonpage 
      + (ypos - cutwin->lineoffsetlist[cutwin->linesonpage]) 
      / cutwin->font.lineheight;
  else {
    long min = 0;
    long max = cutwin->linesonpage;
    while (min+1 < max) {
      linenum = (min+max) / 2;
      if (ypos < cutwin->lineoffsetlist[linenum])
	max = linenum;
      else if (ypos >= cutwin->lineoffsetlist[linenum+1])
	min = linenum+1;
      else {
	min = linenum;
	break;
      }
    }
    linenum = min;
  }

  linenum += cutwin->scrollline;

  if (linenum < 0)
    return 0;
  if (linenum >= cutwin->numlines)
    return cutwin->numchars;

  curline = (&cutwin->linelist[linenum]);

  if (curline->leftindent > 0 && xpos < curline->leftindent) {
    long lx;
    int ylinepos = cutwin->lineoffsetlist[linenum-cutwin->scrollline];
    for (lx = linenum; lx >= 0; lx--) {
      lline_t *thisline = (&cutwin->linelist[lx]);
      if (thisline->leftbelow == 0 && lx < linenum)
	break;
      for (wx=0; wx<thisline->numwords; wx++) {
	curword = thisline->wordlist+wx;
	if (curword->stype == stype_Image) {
	  imageword_t *iwd = curword->u.image;
	  if (iwd) {
	    if (iwd->imagealign == imagealign_MarginLeft
	      && xpos >= iwd->pos && xpos < iwd->pos+iwd->width
	      && ypos >= ylinepos && ypos < ylinepos+iwd->height) {
	      *truepos = thisline->pos + curword->pos;
	      break;
	    }
	  }
	}
	else if (curword->stype == stype_Text) {
	  break;
	}
      }
      ylinepos -= thisline->height;
    }
  }

  if (curline->rightindent > 0 && xpos >= cutwin->textwin_w - curline->rightindent) {
    long lx;
    int ylinepos = cutwin->lineoffsetlist[linenum-cutwin->scrollline];
    for (lx = linenum; lx >= 0; lx--) {
      lline_t *thisline = (&cutwin->linelist[lx]);
      if (thisline->rightbelow == 0 && lx < linenum)
	break;
      for (wx=0; wx<thisline->numwords; wx++) {
	curword = thisline->wordlist+wx;
	if (curword->stype == stype_Image) {
	  imageword_t *iwd = curword->u.image;
	  if (iwd) {
	    if (iwd->imagealign == imagealign_MarginRight
	      && xpos >= (cutwin->textwin_w-iwd->pos)-iwd->width 
	      && xpos < cutwin->textwin_w-iwd->pos
	      && ypos >= ylinepos && ypos < ylinepos+iwd->height) {
	      *truepos = thisline->pos + curword->pos;
	      break;
	    }
	  }
	}
	else if (curword->stype == stype_Text) {
	  break;
	}
      }
      ylinepos -= thisline->height;
    }
  }

  xpos -= curline->indent; /* now xpos is relative to line beginning */
  if (xpos < 0) {
    return curline->pos; /* beginning of line */
  }
  atpos = 0;
  for (wx=0; wx<curline->numwords; wx++) {
    newpos = atpos + curline->wordlist[wx].width;
    if (xpos < newpos)
      break;
    atpos = newpos;
  }
  if (wx==curline->numwords) {
    return curline->posend; /* end of line */
  }

  xpos -= atpos; /* now xpos is relative to word beginning */
  curword = (&curline->wordlist[wx]);

  if (curword->stype == stype_Text) {
    if (!curword->u.letterpos)
      measure_word(cutwin, curline, curword);

    for (ix=0; ix<curword->len; ix++) {
      if (xpos <= (curword->u.letterpos[ix]+curword->u.letterpos[ix+1])/2) {
	jx = ix;
	/* if (curword->pos+ix && xpos <= curword->u.letterpos[ix])
	   jx--;*/ /* doesn't work right */
	break;
      }
    }
    *truepos = curline->pos + curword->pos + ix; /* jx */
    return curline->pos + curword->pos + ix;
  }
  else if (curword->stype == stype_Image) {
    imageword_t *iwd = curword->u.image;
    if (iwd) {
      if (iwd->imagealign != imagealign_MarginLeft 
	&& iwd->imagealign != imagealign_MarginRight) {
	/* Just hit the beginning or end of the word. */
	*truepos = curline->pos + curword->pos;
	ix = 0;
	if (xpos > iwd->width/2)
	  ix++;
	return curline->pos + curword->pos + ix;
      }
    }
  }
  
  /* Just the beginning. */
  *truepos = curline->pos + curword->pos;
  return curline->pos + curword->pos;
}

/* returns the last line such that pos >= line.pos. guessline is a guess 
   to start searching at; -1 means end of file. Can return -1 if pos is 
   before the start of the layout. */
static long find_line_by_pos(window_textbuffer_t *cutwin, long pos, 
  long guessline)
{
  long lx;

  if (guessline < 0 || guessline >= cutwin->numlines)
    guessline = cutwin->numlines-1;

  if (guessline < cutwin->numlines-1 
    && cutwin->linelist[guessline].pos <= pos) {
    for (lx=guessline; lx<cutwin->numlines; lx++) {
      if (cutwin->linelist[lx].pos > pos)
	break;
    }
    lx--;
  }
  else {
    for (lx=guessline; lx>=0; lx--) {
      if (cutwin->linelist[lx].pos <= pos)
	break;
    }
  }

  return lx;
}

/* returns values relative to textwin origin, at top of line. */
static long find_loc_by_pos(window_textbuffer_t *cutwin, long pos, 
  int *xposret, int *yposret)
{
  long lx, lx2;
  long wx, atpos;
  lline_t *curline;
  word_t *curword;

  lx = find_line_by_pos(cutwin, pos, -1);
  lx2 = lx - cutwin->scrollline;
  if (lx < 0 || lx2 < 0) {
    /* somehow before first line laid out */
    *xposret = 0;
    *yposret = (-cutwin->scrollline) * cutwin->font.lineheight;
    return lx;
  }
  if (lx2 >= cutwin->linesonpage) {
    /* or after */
    *xposret = 0;
    *yposret = cutwin->lineoffsetlist[cutwin->linesonpage] 
      + (lx2 - cutwin->linesonpage) * cutwin->font.lineheight;
    return lx;
  }
  curline = (&cutwin->linelist[lx]);
  *yposret = cutwin->lineoffsetlist[lx2];

  atpos = curline->indent;
  for (wx=0; wx<curline->numwords; wx++) {
    if (curline->pos+curline->wordlist[wx].pos+curline->wordlist[wx].len >= pos)
      break;
    atpos += curline->wordlist[wx].width;
  }
  if (wx==curline->numwords) {
    *xposret = atpos;
    return lx;
  }

  curword = (&curline->wordlist[wx]);
  if (curword->stype == stype_Text) {
    if (!curword->u.letterpos)
      measure_word(cutwin, curline, curword);
    atpos += curword->u.letterpos[pos - (curline->pos+curword->pos)];
  }
  else if (curword->stype == stype_Image) {
    if (curword->u.image && pos > (curline->pos+curword->pos))
      atpos += curword->u.image->width;
  }
  else if (curword->stype == stype_Break) {
    /* leave it */
  }
  
  *xposret = atpos;
  return lx;
}

static void readd_lineheights(window_textbuffer_t *cutwin, long lx)
{
  long jx, lx2;
  
  lx2 = lx-cutwin->scrollline;
  if (lx2 > cutwin->linesonpage)
    return;
  
  for (; lx < cutwin->numlines; lx++, lx2++) {
    jx = cutwin->lineoffsetlist[lx2] + cutwin->linelist[lx].height;
    if (jx > cutwin->textwin_h)
      break;
    if (lx2+1 >= cutwin->lineoffset_size) {
      cutwin->lineoffset_size = cutwin->lineoffset_size * 2 + lx2 + 1;
      cutwin->lineoffsetlist = (int *)realloc(cutwin->lineoffsetlist,
	cutwin->lineoffset_size * sizeof(int));
    }
    cutwin->lineoffsetlist[lx2+1] = jx;
  }
  cutwin->linesonpage = lx2;
}

/* Text words only, please. */
static void measure_word(window_textbuffer_t *cutwin, lline_t *curline, 
  word_t *curword)
{
  int cx;
  char *buf;
  int direction;
  int letterwid;
  long *arr;

  if (curword->u.letterpos) {
    free(curword->u.letterpos);
    curword->u.letterpos = NULL;
  }

  arr = (long *)malloc(sizeof(long) * (curword->len+1));

  buf = cutwin->charbuf+curline->pos+curword->pos;
  arr[0] = 0;
  for (cx=0; cx<curword->len-1; cx++) {
    letterwid = XTextWidth(cutwin->font.gc[curword->attr].fontstr, buf+cx, 1);
    /* attend to curword->linkid? */
    arr[cx+1] = arr[cx] + letterwid;
  }
  arr[cx+1] = curword->width;

  curword->u.letterpos = arr;
}

void win_textbuffer_clear_window(window_textbuffer_t *cutwin)
{
  int ix;
  
  if (1) {
    /* normal clear */
    win_textbuffer_delete_start(cutwin, cutwin->numlines);
  }
  else {
    /* bonzo scrolling clear. This doesn't actually work, since lines can
       be of different heights. */
    if (!cutwin->isclear) {
      for (ix=0; ix<cutwin->linesonpage; ix++)
	win_textbuffer_add(cutwin, '\n', -1);
    }
  }
  
  cutwin->isclear = TRUE;
}

/* pos < 0 means add at end.
   all this is grotesquely inefficient if adding anywhere but the end. */
void win_textbuffer_add(window_textbuffer_t *cutwin, char ch, long pos)
{
  if (ch != '\n')
    cutwin->isclear = FALSE;
  if (pos<0)
    pos = cutwin->numchars;
  win_textbuffer_replace(cutwin, pos, 0, &ch, 1);
}

/* update data, adjusting dot and styles as necessary. */
void win_textbuffer_replace(window_textbuffer_t *cutwin, long pos, long oldlen, 
  char *buf, long newlen)
{
  long newnumchars;

  newnumchars = cutwin->numchars-oldlen+newlen;
  if (newnumchars >= cutwin->char_size) {
    while (newnumchars >= cutwin->char_size) 
      cutwin->char_size *= 2;
    cutwin->charbuf = (char *)realloc(cutwin->charbuf, sizeof(char) * cutwin->char_size);
  }

  if (pos < cutwin->dirtybeg || cutwin->dirtybeg < 0)
    cutwin->dirtybeg = pos;

  if (newlen != oldlen) {
    if (pos+oldlen != cutwin->numchars) {
      memmove(cutwin->charbuf+pos+newlen, cutwin->charbuf+pos+oldlen, sizeof(char) * (cutwin->numchars-(pos+oldlen)));
    }
    if (cutwin->numchars >= cutwin->dirtyend)
      cutwin->dirtyend = cutwin->numchars+1;
    cutwin->dirtydelta += (newlen-oldlen);
  }
  else {
    if (pos+newlen >= cutwin->dirtyend)
      cutwin->dirtyend = pos+newlen+1;
    cutwin->dirtydelta += (newlen-oldlen);
  }

  /* copy in the new stuff */
  if (newlen)
    memmove(cutwin->charbuf+pos, buf, sizeof(char) * newlen);

  /* diddle the dot */
  if (cutwin->dotpos >= pos+oldlen) {
    /* starts after changed region */
    cutwin->dotpos += (newlen-oldlen);
  }
  else if (cutwin->dotpos >= pos) {
    /* starts inside changed region */
    if (cutwin->dotpos+cutwin->dotlen >= pos+oldlen) {
      /* ...but ends after it */
      cutwin->dotlen = (cutwin->dotpos+cutwin->dotlen)-(pos+oldlen);
      cutwin->dotpos = pos+newlen;
    }
    else {
      /* ...and ends inside it */
      cutwin->dotpos = pos+newlen;
      cutwin->dotlen = 0;
    }
  }
  else {
    /* starts before changed region */
    if (cutwin->dotpos+cutwin->dotlen >= pos+oldlen) {
      /* ...but ends after it */
      cutwin->dotlen += (newlen-oldlen);
    }
    else if (cutwin->dotpos+cutwin->dotlen >= pos) {
      /* ...but ends inside it */
      cutwin->dotlen = (pos+newlen) - cutwin->dotpos;
    }
  }

  cutwin->numchars = newnumchars;
}

void win_textbuffer_set_style_text(window_textbuffer_t *cutwin, glui32 attr)
{
  style_t *sty = &(cutwin->stylelist[cutwin->numstyles-1]);
  if (attr == 0xFFFFFFFF)
    attr = sty->attr;
  win_textbuffer_setstyle(cutwin, -1, stype_Text, attr, sty->linkid, 
    0, 0, 0, 0);
}

void win_textbuffer_set_style_image(window_textbuffer_t *cutwin, 
  glui32 image, int imagealign, 
  glui32 imagewidth, glui32 imageheight)
{
  style_t *sty = &(cutwin->stylelist[cutwin->numstyles-1]);
  win_textbuffer_setstyle(cutwin, -1, stype_Image, sty->attr, sty->linkid, 
    image, imagealign, imagewidth, imageheight);
}

void win_textbuffer_set_style_break(window_textbuffer_t *cutwin)
{
  style_t *sty = &(cutwin->stylelist[cutwin->numstyles-1]);
  win_textbuffer_setstyle(cutwin, -1, stype_Break, sty->attr, sty->linkid, 
    0, 0, 0, 0);
}

void win_textbuffer_set_style_link(window_textbuffer_t *cutwin, 
  glui32 linkid)
{
  style_t *sty = &(cutwin->stylelist[cutwin->numstyles-1]);
  win_textbuffer_setstyle(cutwin, -1, sty->stype, sty->attr, linkid, 
    sty->image, sty->imagealign, sty->imagewidth, sty->imageheight);
}

static void win_textbuffer_setstyle(window_textbuffer_t *cutwin, 
  long pos, int stype, 
  glui32 attr, glui32 linkid, glui32 image, int imagealign,
  glui32 imagewidth, glui32 imageheight)
{
  long sx;

  if (pos < 0)
    pos = cutwin->numchars;

  /* find the last style at or before pos. */
  for (sx=cutwin->numstyles-1; sx>=0; sx--) {
    if (cutwin->stylelist[sx].pos <= pos) {
      break;
    }
  }
  if (sx < 0) {
    /*printf("oops, went back behind style 0\n");*/
    return;
  }

  if (cutwin->stylelist[sx].pos == pos) {
    /* if sx is *at* pos, just change it. */
  }
  else {
    /* if before, insert a new style after it. */
    sx++;
    if (cutwin->numstyles+1 >= cutwin->styles_size) {
      cutwin->styles_size *= 2;
      cutwin->stylelist = (style_t *)realloc(cutwin->stylelist, 
	sizeof(style_t) * cutwin->styles_size);
    }
    cutwin->numstyles++;
    if (sx+1 < cutwin->numstyles) {
      memmove(&cutwin->stylelist[sx+1], &cutwin->stylelist[sx], 
	sizeof(style_t) * (cutwin->numstyles-sx));
    }
    cutwin->stylelist[sx].pos = pos;
  }

  cutwin->stylelist[sx].stype = stype;
  cutwin->stylelist[sx].linkid = linkid;
  cutwin->stylelist[sx].attr = attr;
  cutwin->stylelist[sx].image = image;
  cutwin->stylelist[sx].imagealign = imagealign;
  cutwin->stylelist[sx].imagewidth = imagewidth;
  cutwin->stylelist[sx].imageheight = imageheight;

  if (pos != cutwin->numchars) {
    /* really, should only go to next style_t */
    cutwin->dirtybeg = pos;
    cutwin->dirtyend = cutwin->numchars;
    cutwin->dirtydelta = 0;
    win_textbuffer_layout(cutwin);
  }
}

void win_textbuffer_end_visible(window_textbuffer_t *cutwin)
{
  if (cutwin->scrollline < cutwin->numlines-cutwin->linesonpage) {
    /* this is wrong -- linesonpage doesn't correctly measure lines 
       at the end. */
    scroll_to(cutwin, cutwin->numlines-cutwin->linesonpage);
  }
}

void win_textbuffer_set_paging(window_textbuffer_t *cutwin, int forcetoend)
{
  long val;
  long total;

  if (cutwin->lastseenline == cutwin->numlines)
    return;
  
  if (!forcetoend 
    && cutwin->lastseenline - 0 < cutwin->numlines - cutwin->linesonpage) {
    /* this is wrong -- linesonpage doesn't correctly measure lines 
       at the end. */
    /* scroll lastseenline to top, stick there */
    val = cutwin->lastseenline;
    if (val > cutwin->scrollline)
      val--;
  }
  else {
    /* scroll to bottom, set lastseenline to end. */
    total = 0;
    for (val = cutwin->numlines; val > 0; val--) {
      total += cutwin->linelist[val-1].height;
      if (total > cutwin->textwin_h)
	break;
    }

    cutwin->lastseenline = cutwin->numlines;
  }

  scroll_to(cutwin, val);
}

int win_textbuffer_is_paging(window_t *win)
{
  window_textbuffer_t *dwin = win->data;

  if (dwin->lastseenline < dwin->numlines - dwin->linesonpage) {
    /* this is wrong -- linesonpage doesn't correctly measure lines 
       at the end. */
    return TRUE;
  }
  return FALSE;
}

/* delete num lines from the top */
void win_textbuffer_delete_start(window_textbuffer_t *cutwin, long num)
{
  long delchars;
  long lx, sx, sx2;
  glui32 origattr;

  if (num > cutwin->numlines)
    num = cutwin->numlines;
  if (num < 0)
    num = 0;

  if (cutwin->numlines==0)
    return;
  
  if (num < cutwin->numlines)
    delchars = cutwin->linelist[num].pos;
  else
    delchars = cutwin->numchars;
  
  if (!delchars)
    return;

  /* lines */
  slapover(cutwin, 0, 0, num);
  for (lx=0; lx<cutwin->numlines; lx++) {
    cutwin->linelist[lx].pos -= delchars;
    cutwin->linelist[lx].posend -= delchars;
  }

  /* styles */
  for (sx=0; sx<cutwin->numstyles; sx++) {
    if (cutwin->stylelist[sx].pos > delchars)
      break;
  }
  if (sx>0) {
    origattr = cutwin->stylelist[sx-1].attr;
    cutwin->stylelist[0].pos = 0;
    cutwin->stylelist[0].attr = origattr;
    for (sx2=1; sx<cutwin->numstyles; sx++, sx2++) {
      cutwin->stylelist[sx2].pos = cutwin->stylelist[sx].pos - delchars;
      cutwin->stylelist[sx2].attr = cutwin->stylelist[sx].attr;
    }
    cutwin->numstyles = sx2;
  }

  /* chars */
  if (cutwin->numchars > delchars) 
    memmove(&cutwin->charbuf[0], &cutwin->charbuf[delchars], sizeof(char) * (cutwin->numchars-delchars));
  cutwin->numchars -= delchars;

  /* adjust, I mean, everything */
  if (cutwin->dirtybeg != (-1)) {
    cutwin->dirtybeg -= delchars;
    cutwin->dirtyend -= delchars;
    if (cutwin->dirtyend < 0) {
      cutwin->dirtybeg = (-1);
      cutwin->dirtyend = (-1);
    }
    else if (cutwin->dirtybeg < 0) {
      cutwin->dirtybeg = 0;
    }
  }

  cutwin->dotpos -= delchars;
  if (cutwin->dotpos < 0) {
    if (cutwin->dotpos+cutwin->dotlen < 0) {
      cutwin->dotpos = 0;
      cutwin->dotlen = 0;
    }
    else {
      cutwin->dotlen += cutwin->dotpos;
      cutwin->dotpos = 0;
    }
  }
  cutwin->lastdotpos -= delchars;
  if (cutwin->lastdotpos < 0) {
    if (cutwin->lastdotpos+cutwin->lastdotlen < 0) {
      cutwin->lastdotpos = 0;
      cutwin->lastdotlen = 0;
    }
    else {
      cutwin->lastdotlen += cutwin->lastdotpos;
      cutwin->lastdotpos = 0;
    }
  }
  cutwin->inputfence -= delchars;
  if (cutwin->inputfence < 0)
    cutwin->inputfence = 0;

  cutwin->lastseenline -= num;
  if (cutwin->lastseenline < 0)
    cutwin->lastseenline = 0;

  cutwin->scrollline -= num;
  cutwin->scrollpos -= delchars;
  if (cutwin->scrollline < 0 || cutwin->scrollpos < 0) {
    cutwin->scrollline = 0;
    cutwin->scrollpos = 0;
    readd_lineheights(cutwin, cutwin->scrollline);
    redrawtext(cutwin, 0, -1, -1);
    flip_selection(cutwin, cutwin->dotpos, cutwin->dotlen);
    xweg_adjust_scrollbar(&cutwin->scrollbar, cutwin->numlines,
      cutwin->scrollline, cutwin->linesonpage);
  }
  else {
    xweg_adjust_scrollbar(&cutwin->scrollbar, cutwin->numlines,
      cutwin->scrollline, cutwin->linesonpage);
  }
}

static void win_textbuffer_layout(window_textbuffer_t *cutwin)
{
  long ix, jx, ejx, lx;
  long styx, nextstylepos;
  style_t *curattr;
  long overline, overlineend;
  long tmpl, startpos;
  int prevflags;
  int needwholeredraw;
  int textwin_w = cutwin->textwin_w; /* cache */
  int textwin_h = cutwin->textwin_h; /* cache */
  long leftindent, leftbelow, rightindent, rightbelow;

  int direction;
  int wordwidth;
  
  static long lastline = 0; /* last line dirtied */

  /* Shut up compiler warnings */
  ejx = 0;
  jx = 0;

  if (cutwin->dirtybeg < 0 || cutwin->dirtyend < 0) {
    if (cutwin->lastdotpos != cutwin->dotpos 
      || cutwin->lastdotlen != cutwin->dotlen) {
      refiddle_selection(cutwin, cutwin->lastdotpos, 
	cutwin->lastdotlen, cutwin->dotpos, cutwin->dotlen);
      /*flip_selection(cutwin, cutwin->lastdotpos, cutwin->lastdotlen);*/
      cutwin->lastdotpos = cutwin->dotpos;
      cutwin->lastdotlen = cutwin->dotlen;
      /*flip_selection(cutwin, cutwin->lastdotpos, cutwin->lastdotlen);*/
    }
    return;
  }

  /* we start by turning off the selection. */
  flip_selection(cutwin, cutwin->lastdotpos, cutwin->lastdotlen);
  cutwin->lastdotpos = cutwin->dotpos;
  cutwin->lastdotlen = cutwin->dotlen;

  if (cutwin->numlines==0) {
    overline = 0;
    startpos = 0;
  }
  else {
    lx = find_line_by_pos(cutwin, cutwin->dirtybeg, lastline);
    /* now lx is the line containing dirtybeg */

    if (lx>0 && lx<cutwin->numlines && (cutwin->linelist[lx].flags & lineflag_Wrapped)) {
      /* do layout from previous line, in case a word from the changed area pops back there. */
      lx--;
    }
    overline = lx;
    startpos = cutwin->linelist[overline].pos;
  }

  /* get any margin info left from previous lines */
  if (overline > 0 && cutwin->linelist[overline-1].leftbelow) {
    leftbelow = cutwin->linelist[overline-1].leftbelow;
    leftindent = cutwin->linelist[overline-1].leftindent;
  }
  else {
    leftbelow = 0;
    leftindent = 0;
  }
  if (overline > 0 && cutwin->linelist[overline-1].rightbelow) {
    rightbelow = cutwin->linelist[overline-1].rightbelow;
    rightindent = cutwin->linelist[overline-1].rightindent;
  }
  else {
    rightbelow = 0;
    rightindent = 0;
  }

  /* get the first relevant style_t */
  for (styx=cutwin->numstyles-1; styx>0; styx--)
    if (cutwin->stylelist[styx].pos <= startpos)
      break;
  if (styx==cutwin->numstyles-1)
    nextstylepos = cutwin->numchars+10;
  else
    nextstylepos = cutwin->stylelist[styx+1].pos;
  curattr = &(cutwin->stylelist[styx]);

  /* start a-layin' */
  tmpl = 0;
  prevflags = 0;

  while (startpos<cutwin->numchars 
    && !(startpos >= cutwin->dirtyend && cutwin->charbuf[startpos]=='\n')) {
    lline_t *thisline;
    long tmpw, tmpwords_size;
    long widthsofar, spaceswidth=0;
    int lineattrknown, anyimages;
    glui32 lineattr;
    long ascent, descent, superscent;

    if (tmpl+1 >= cutwin->tmplines_size) {
      /* the +1 allows the extra blank line at the end */
      cutwin->tmplines_size *= 2;
      cutwin->tmplinelist = (lline_t *)realloc(cutwin->tmplinelist, 
	sizeof(lline_t) * cutwin->tmplines_size);
    }
    thisline = (&cutwin->tmplinelist[tmpl]);
    thisline->flags = prevflags;
    thisline->height = 64; /* initially silly values */
    thisline->off = 48;
    thisline->indent = cutwin->font.baseindent + leftindent; 
    /* indent will be added to when lineattr is known. */
    lineattrknown = FALSE;
    lineattr = style_Normal;
    tmpwords_size = 8;
    thisline->wordlist = (word_t *)malloc(tmpwords_size * sizeof(word_t));
    tmpw = 0;

    /*printf("laying tmpline %d, from charpos %d\n", tmpl, startpos);*/
    tmpl++;

    ix = startpos;
    widthsofar = thisline->indent; /* will be set when lineattr is known. */
    prevflags = 0;

    while (ix<cutwin->numchars && cutwin->charbuf[ix]!='\n') {
      word_t *thisword;
      int sidebar;

      while (ix >= nextstylepos) {
	/* ahead one style_t */
	styx++;
	if (styx==cutwin->numstyles-1)
	  nextstylepos = cutwin->numchars+10;
	else
	  nextstylepos = cutwin->stylelist[styx+1].pos;
	curattr = &(cutwin->stylelist[styx]);
      }
      
      if (!lineattrknown) {
	lineattr = curattr->attr;
	thisline->indent += cutwin->font.gc[lineattr].indent;
	if (!(thisline->flags & lineflag_Wrapped)) {
	  thisline->indent += cutwin->font.gc[lineattr].parindent;
	}
	widthsofar = thisline->indent;
	lineattrknown = TRUE;
      }

      if (tmpw >= tmpwords_size) {
	tmpwords_size *= 2;
	thisline->wordlist = (word_t *)realloc(thisline->wordlist, 
	  tmpwords_size * sizeof(word_t));
      }
      thisword = (&thisline->wordlist[tmpw]);
      /* --- initialize word structure --- */

      sidebar = FALSE;
      wordwidth = 0;

      if (curattr->stype == stype_Text) {
	thisword->stype = stype_Text;
	thisword->u.letterpos = NULL;
	for (jx=ix; 
	     jx<cutwin->numchars && jx<nextstylepos 
	       && cutwin->charbuf[jx]!=' ' 
	       && cutwin->charbuf[jx]!='\n'; 
	     jx++);
	wordwidth = XTextWidth(cutwin->font.gc[curattr->attr].fontstr, 
	  cutwin->charbuf+ix, jx-ix);
	/* attend to curattr->linkid? */
      }
      else if (curattr->stype == stype_Break) {
	thisword->stype = stype_Break;
	thisword->u.letterpos = NULL;
	jx = ix+1;
	if (leftindent || rightindent)
	  wordwidth = textwin_w + 10;
	else
	  wordwidth = 0;
      }
      else if (curattr->stype == stype_Image) {
	thisword->stype = stype_Image;
	jx = ix+1;
	thisword->u.image = (imageword_t *)malloc(sizeof(imageword_t));
	thisword->u.image->pic = picture_find(curattr->image);
	if (!thisword->u.image->pic) {
	  delete_imageword(thisword->u.image);
	  thisword->u.image = NULL;
	  wordwidth = 0;
	}
	else {
	  thisword->u.image->width = curattr->imagewidth;
	  thisword->u.image->height = curattr->imageheight;
	  thisword->u.image->imagealign = curattr->imagealign;
	  thisword->u.image->pos = 0;
	  wordwidth = thisword->u.image->width;
	  
	  if (curattr->imagealign == imagealign_MarginLeft 
	    || curattr->imagealign == imagealign_MarginRight) {
	    int px, pcount;
	    wordwidth = 0;
	    sidebar = TRUE;
	    pcount = 0;
	    for (px=0; !pcount && px<tmpw; px++) {
	      if (thisline->wordlist[px].stype == stype_Text)
		pcount++;
	    }
	    if (pcount) {
	      delete_imageword(thisword->u.image);
	      thisword->u.image = NULL;
	    }
	    else {
	      if (curattr->imagealign == imagealign_MarginLeft) {
		long oldleftindent = leftindent;
		leftindent += thisword->u.image->width;
		if (!oldleftindent) {
		  leftindent += prefs.textbuffer.marginx;
		  thisword->u.image->pos = 0;
		}
		else {
		  thisword->u.image->pos = oldleftindent 
		    - prefs.textbuffer.marginx;
		}
		if (leftbelow < thisword->u.image->height)
		  leftbelow = thisword->u.image->height;
		thisline->indent += (leftindent - oldleftindent);
		widthsofar += (leftindent - oldleftindent);
	      }
	      else {
		long oldrightindent = rightindent;
		rightindent += thisword->u.image->width;
		if (!oldrightindent) {
		  rightindent += prefs.textbuffer.marginx;
		  thisword->u.image->pos = 0;
		}
		else {
		  thisword->u.image->pos = oldrightindent 
		    - prefs.textbuffer.marginx;
		}
		if (rightbelow < thisword->u.image->height)
		  rightbelow = thisword->u.image->height;
	      }
	    }
	  }
	}
      }

      if (widthsofar + wordwidth > textwin_w - rightindent && !sidebar) {
	prevflags = lineflag_Wrapped;
	/* the word overflows, so throw it away -- unless it's the first
	   word on a line. (That would lead to an infinite loop.) 
	   Of course break-words are always thrown away. */
	if (tmpw == 0 && !(curattr->stype == stype_Break && wordwidth)
	  /*###&& (leftindent == 0 && rightindent == 0)*/) {
	  if (curattr->stype == stype_Text) {
	    /* do something clever -- split the word, put first part in tmplist. 
	       but be sure to take at least one letter. */
	    long letx;
	    long wordwidthsofar = 0;
	    for (letx=ix; letx<jx; letx++) {
	      wordwidth = XTextWidth(cutwin->font.gc[curattr->attr].fontstr, 
		cutwin->charbuf+letx, 1);
	      /* attend to curattr->linkid? */
	      if (letx > ix 
		&& widthsofar + wordwidthsofar+wordwidth 
		> (textwin_w - rightindent)) {
		break;
	      }
	      wordwidthsofar += wordwidth;
	    }
	    jx = letx;
	    wordwidth = wordwidthsofar;
	    /* spaceswidth and ejx will be 0 */
	  }
	  /* don't break */
	}
	else {
	  /* pitch this word. */
	  if (thisword->stype == stype_Image) {
	    if (thisword->u.image) {
	      delete_imageword(thisword->u.image);
	      thisword->u.image = NULL;
	    }
	  }
	  /* ejx and spaceswidth are properly set from last word, 
	     trim them off. */
	  thisword--;
	  thisword->len -= ejx;
	  thisword->width -= spaceswidth;
	  break; /* line over. */
	}
      }

      /* figure out trailing whitespace ### for images too? */
      if (!sidebar) {
	ejx = 0;
	while (jx+ejx<cutwin->numchars 
	  && jx+ejx<nextstylepos && cutwin->charbuf[jx+ejx]==' ') {
	  ejx++;
	}
	spaceswidth = ejx * cutwin->font.gc[curattr->attr].spacewidth;
      }
      else {
	ejx = 0;
	spaceswidth = 0;
      }

      /* put the word in tmplist */
      thisword->pos = ix-startpos;
      thisword->len = jx+ejx-ix;
      thisword->attr = curattr->attr;
      thisword->linkid = curattr->linkid;
      thisword->width = wordwidth+spaceswidth;
      widthsofar += thisword->width;
      tmpw++; 

      ix = jx+ejx;
    }
    thisline->pos = startpos;
    if (tmpw) {
      word_t *thisword = (&thisline->wordlist[tmpw-1]);
      thisline->posend = startpos + thisword->pos + thisword->len;
    }
    else {
      thisline->posend = startpos;
    }

    if (ix<cutwin->numchars && cutwin->charbuf[ix]=='\n')
      ix++;

    thisline->numwords = tmpw;

    superscent = 0;
    ascent = 0;
    descent = 0;
    anyimages = FALSE;
    for (jx=0; jx<tmpw; jx++) {
      word_t *thisword = (&thisline->wordlist[jx]);
      if (thisword->stype == stype_Text) {
	fontref_t *font = &cutwin->font.gc[thisword->attr];
	if (font->ascent > ascent)
	  ascent = font->ascent;
	if (font->descent > descent)
	  descent = font->descent;
      }
      else {
	anyimages = TRUE;
      }
    }
    if (anyimages) {
      int diff;
      for (jx=0; jx<tmpw; jx++) {
	word_t *thisword = (&thisline->wordlist[jx]);
	if (thisword->stype == stype_Image) {
	  imageword_t *iwd = thisword->u.image;
	  if (!iwd)
	    continue;
	  switch (iwd->imagealign) {
	  case imagealign_InlineUp:
	    if (superscent < iwd->height - ascent)
	      superscent = iwd->height - ascent;
	    iwd->pos = iwd->height;
	    break;
	  case imagealign_InlineCenter:
	    diff = iwd->height - ascent;
	    diff = (diff+1) / 2;
	    if (superscent < diff)
	      superscent = diff;
	    if (descent < diff)
	      descent = diff;
	    iwd->pos = ascent + diff;
	    break;
	  case imagealign_InlineDown:
	    if (descent < iwd->height - ascent)
	      descent = iwd->height - ascent;
	    iwd->pos = ascent;
	    break;
	  case imagealign_MarginLeft:
	  case imagealign_MarginRight:
	    /* iwd->pos already set */
	    break;
	  }
	}
      }
    }
    if (superscent+ascent+descent == 0) {
      fontref_t *font = &cutwin->font.gc[lineattr];
      ascent = font->ascent;
      descent = font->descent;
    }
    thisline->height = superscent+ascent+descent;
    thisline->off = superscent+ascent;
    
    switch (cutwin->font.gc[lineattr].justify) {
    case stylehint_just_LeftRight:
      if (prevflags==lineflag_Wrapped && tmpw>1) {
	/* full-justify (but only wrapped lines) */
	long extraspace, each;
	extraspace = (textwin_w - rightindent) - widthsofar;
	each = extraspace / (tmpw-1);
	extraspace -= (each*(tmpw-1));
	for (jx=0; jx<extraspace; jx++) {
	  thisline->wordlist[jx].width += (each+1);
	}
	for (; jx<tmpw-1; jx++) {
	  thisline->wordlist[jx].width += each;
	}
      }
      break;
    case stylehint_just_Centered:
      {
	long extraspace;
	extraspace = (textwin_w - rightindent) - widthsofar;
	thisline->indent += extraspace / 2;
      }
      break;
    case stylehint_just_RightFlush:
      {
	long extraspace;
	extraspace = (textwin_w - rightindent) - widthsofar;
	thisline->indent += extraspace;
      }
      break;
    case stylehint_just_LeftFlush:
    default:
      break;
    }

    thisline->leftindent = leftindent;
    if (leftindent && thisline->height < leftbelow) {
      leftbelow -= thisline->height;
      thisline->leftbelow = leftbelow;
    }
    else {
      leftbelow = 0;
      leftindent = 0;
      thisline->leftbelow = 0;
    }
    thisline->rightindent = rightindent;
    if (rightindent && thisline->height < rightbelow) {
      rightbelow -= thisline->height;
      thisline->rightbelow = rightbelow;
    }
    else {
      rightbelow = 0;
      rightindent = 0;
      thisline->rightbelow = 0;
    }
    
    startpos = ix;
  } /* done laying tmp lines */

  if (startpos == cutwin->numchars 
    && (cutwin->numchars==0 || cutwin->charbuf[cutwin->numchars-1]=='\n')) {
    /* lay one more line! */
    lline_t *thisline;
    fontref_t *font;

    curattr = &(cutwin->stylelist[cutwin->numstyles-1]);
    font = &(cutwin->font.gc[curattr->attr]);

    thisline = (&cutwin->tmplinelist[tmpl]);
    thisline->flags = lineflag_Extra;
    thisline->height = font->ascent + font->descent;
    thisline->off = font->ascent;
    thisline->indent = cutwin->font.baseindent;
    tmpl++;
    
    thisline->leftindent = leftindent;
    if (leftindent && thisline->height < leftbelow) {
      leftbelow -= thisline->height;
      thisline->leftbelow = leftbelow;
    }
    else {
      thisline->leftbelow = 0;
    }
    thisline->rightindent = rightindent;
    if (rightindent && thisline->height < rightbelow) {
      rightbelow -= thisline->height;
      thisline->rightbelow = rightbelow;
    }
    else {
      thisline->rightbelow = 0;
    }

    thisline->wordlist = (word_t *)malloc(sizeof(word_t));
    thisline->numwords = 0;
    thisline->pos = startpos;
    thisline->posend = startpos;
  }

  /*printf("laid %d tmplines, and startpos now %d (delta %d)\n", tmpl, startpos, dirtydelta);*/

  for (lx=overline; lx<cutwin->numlines && cutwin->linelist[lx].pos < startpos-cutwin->dirtydelta; lx++);
  if (lx==cutwin->numlines-1 && (cutwin->linelist[lx].flags & lineflag_Extra)) {
    /* account for the extra line */
    lx++;
  }
  overlineend = lx;

  /*printf("overwrite area is lines [%d..%d) (of %d); replacing with %d lines\n", overline, overlineend, numlines, tmpl);*/

  slapover(cutwin, tmpl, overline, overlineend);

  lastline = overline+tmpl; /* re-cache value */
  needwholeredraw = FALSE;

  /* diddle scroll stuff */
  if (cutwin->scrollpos <= cutwin->dirtybeg) {
    /* disturbance is at or below the screen-top -- do nothing */
  }
  else if (cutwin->scrollpos >= startpos-cutwin->dirtydelta) {
    /* disturbance is off top of screen -- adjust so that no difference 
       is visible. */
    cutwin->scrollpos += cutwin->dirtydelta;
    cutwin->scrollline += (overline-overlineend) - tmpl;
    /* the lineoffsetlist therefore doesn't need to be changed, 
       theoretically. */
  }
  else {
    /* The disturbance spans the screen-top, which is annoying. */
    cutwin->scrollpos += cutwin->dirtydelta; /* kind of strange, but 
						shouldn't cause trouble */
    if (cutwin->scrollpos >= cutwin->numchars)
      cutwin->scrollpos = cutwin->numchars-1;
    if (cutwin->scrollpos < 0)
      cutwin->scrollpos = 0;
    cutwin->scrollline = find_line_by_pos(cutwin, cutwin->scrollpos, 
      cutwin->scrollline);
    needwholeredraw = TRUE;
  }

  /* rebuild lineoffsetlist */
  if (needwholeredraw) {
    lx = cutwin->scrollline;
  }
  else {
    lx = overline;
    if (lx < cutwin->scrollline)
      lx = cutwin->scrollline;
  }
  readd_lineheights(cutwin, lx);

  /* clean up afterwards. */
  cutwin->dirtybeg = -1;
  cutwin->dirtyend = -1;
  cutwin->dirtydelta = 0;

  if (needwholeredraw) {
    redrawtext(cutwin, cutwin->scrollline, -1, -1);
  }
  else if (tmpl == overlineend-overline) {
    redrawtext(cutwin, overline, tmpl, tmpl);
  }
  else {
    if (overlineend > cutwin->numlines)
      redrawtext(cutwin, overline, -1, overlineend-overline);
    else
      redrawtext(cutwin, overline, -1, cutwin->numlines-overline);
  }

  flip_selection(cutwin, cutwin->lastdotpos, cutwin->lastdotlen);

  xweg_adjust_scrollbar(&cutwin->scrollbar, cutwin->numlines,
    cutwin->scrollline, cutwin->linesonpage);
}

void xgc_buf_scrollto(window_textbuffer_t *cutwin, int op)
{
  scroll_to(cutwin, op);
}

void xgc_buf_scroll(window_textbuffer_t *cutwin, int op)
{
  long total, val;

  switch (op) {
  case op_UpLine:
    scroll_to(cutwin, cutwin->scrollline-1);
    break;
  case op_DownLine:
    scroll_to(cutwin, cutwin->scrollline+1);
    break;
  case op_UpPage:
    total = 0;
    for (val = cutwin->scrollline; val > 0; val--) {
      total += cutwin->linelist[val-1].height;
      if (total > cutwin->textwin_h) {
	val++;
	break;
      }
    }
    scroll_to(cutwin, val);
    break;
  case op_DownPage:
    scroll_to(cutwin, cutwin->scrollline+(cutwin->linesonpage-1));
    break;
  case op_ToTop:
    scroll_to(cutwin, 0);
    break;
  case op_ToBottom:
    scroll_to(cutwin, cutwin->numlines);
    break;
  }
}

void xgc_buf_movecursor(window_textbuffer_t *cutwin, int op)
{
  long pos;

  switch (op) {
  case op_BackChar:
    if (cutwin->dotlen) {
      cutwin->dotlen = 0;
    }
    else {
      collapse_dot(cutwin);
      if (cutwin->dotpos > 0)
	cutwin->dotpos--;
    }
    break;
  case op_ForeChar:
    if (cutwin->dotlen) {
      collapse_dot(cutwin);
    }
    else {
      collapse_dot(cutwin);
      if (cutwin->dotpos < cutwin->numchars)
	cutwin->dotpos++;
    }
    break;
  case op_BackWord:
    collapse_dot(cutwin);
    cutwin->dotpos = back_to_nonwhite(cutwin, cutwin->dotpos);
    cutwin->dotpos = back_to_white(cutwin, cutwin->dotpos);
    break;
  case op_ForeWord:
    collapse_dot(cutwin);
    cutwin->dotpos = fore_to_nonwhite(cutwin, cutwin->dotpos);
    cutwin->dotpos = fore_to_white(cutwin, cutwin->dotpos);
    break;
  case op_BeginLine:
    if (cutwin->dotlen) {
      cutwin->dotlen = 0;
    }
    else {
      if (cutwin->buffer && cutwin->dotpos >= cutwin->inputfence)
	cutwin->dotpos = cutwin->inputfence;
      else {
	pos = cutwin->dotpos;
	while (pos > 0 && cutwin->charbuf[pos-1] != '\n')
	  pos--;
	cutwin->dotpos = pos;
      }
    }
    break;
  case op_EndLine:
    if (cutwin->dotlen) {
      collapse_dot(cutwin);
    }
    else {
      if (cutwin->buffer && cutwin->dotpos >= cutwin->inputfence)
	cutwin->dotpos = cutwin->numchars;
      else {
	pos = cutwin->dotpos;
	while (pos < cutwin->numchars && cutwin->charbuf[pos] != '\n')
	  pos++;
	cutwin->dotpos = pos;
      }
    }
    break;
  }
  win_textbuffer_layout(cutwin);
}

void xgc_buf_getchar(window_textbuffer_t *cutwin, int ch)
{
  if (cutwin->owner->char_request) {
    glui32 key = ch;
    eventloop_setevent(evtype_CharInput, cutwin->owner, key, 0);
    cutwin->owner->char_request = FALSE;
  }
}

void xgc_buf_insert(window_textbuffer_t *cutwin, int ch)
{
  char realch;
  
  /* ###### not perfect -- should be all typable chars */
  if (ch < 32 || ch >= 127)
    ch = ' ';
  
  realch = ch;
  
  if (cutwin->dotpos < cutwin->inputfence) {
    cutwin->dotpos = cutwin->numchars;
    cutwin->dotlen = 0;
    win_textbuffer_add(cutwin, ch, cutwin->dotpos);
  }
  else {
    win_textbuffer_replace(cutwin, cutwin->dotpos, cutwin->dotlen, &realch, 1);
  }

  win_textbuffer_layout(cutwin);
  win_textbuffer_end_visible(cutwin);
}

void xgc_buf_delete(window_textbuffer_t *cutwin, int op)
{
  long pos;

  if (cutwin->dotpos < cutwin->inputfence)
    return;
  
  if (cutwin->dotlen != 0 && (op == op_BackChar || op == op_ForeChar)) {
    win_textbuffer_replace(cutwin, cutwin->dotpos, cutwin->dotlen, "", 0);
    win_textbuffer_layout(cutwin);
    return;
  }
  
  collapse_dot(cutwin);

  switch (op) {
  case op_BackChar:
    if (cutwin->dotpos <= cutwin->inputfence)
      return;
    win_textbuffer_replace(cutwin, cutwin->dotpos-1, 1, "", 0);
    break;
  case op_ForeChar:
    if (cutwin->dotpos < cutwin->inputfence || cutwin->dotpos >= cutwin->numchars)
      return;
    win_textbuffer_replace(cutwin, cutwin->dotpos, 1, "", 0);
    break;
  case op_BackWord:
    pos = back_to_nonwhite(cutwin, cutwin->dotpos);
    pos = back_to_white(cutwin, pos);
    if (pos < cutwin->inputfence)
      pos = cutwin->inputfence;
    if (pos >= cutwin->dotpos)
      return;
    win_textbuffer_replace(cutwin, pos, cutwin->dotpos-pos, "", 0);
    break;
  case op_ForeWord:
    pos = fore_to_nonwhite(cutwin, cutwin->dotpos);
    pos = fore_to_white(cutwin, pos);
    if (pos < cutwin->inputfence)
      pos = cutwin->inputfence;
    if (pos <= cutwin->dotpos)
      return;
    win_textbuffer_replace(cutwin, cutwin->dotpos, pos-cutwin->dotpos, "", 0);
    break;
  }
  win_textbuffer_layout(cutwin);
}

void xgc_buf_cutbuf(window_textbuffer_t *cutwin, int op)
{
  char *cx;
  long num;
  long tmppos;

  if (op != op_Copy) {
    if (!cutwin->buffer) {
      xmsg_set_message("You are not editing input in this window.", FALSE);
      return;
    }
  }

  switch (op) {
  case op_Copy:
    if (cutwin->dotlen) {
      xglk_store_scrap(cutwin->charbuf+cutwin->dotpos, cutwin->dotlen);
    }
    break;
  case op_Wipe:
    if (cutwin->dotlen) {
      xglk_store_scrap(cutwin->charbuf+cutwin->dotpos, cutwin->dotlen);
      if (cutwin->dotpos >= cutwin->inputfence) {
	win_textbuffer_replace(cutwin, cutwin->dotpos, cutwin->dotlen, "", 0);
	win_textbuffer_layout(cutwin);
      }
    }
    break;
  case op_Erase:
    if (cutwin->dotlen) {
      if (cutwin->dotpos >= cutwin->inputfence) {
	win_textbuffer_replace(cutwin, cutwin->dotpos, cutwin->dotlen, "", 0);
	win_textbuffer_layout(cutwin);
      }
    }
    break;
  case op_Kill:
    if (cutwin->dotpos < cutwin->inputfence) {
      /* maybe extend to end-of-line and copy? */
      break;
    }
    cutwin->dotlen = cutwin->numchars-cutwin->dotpos;
    xglk_store_scrap(cutwin->charbuf+cutwin->dotpos, cutwin->dotlen);
    win_textbuffer_replace(cutwin, cutwin->dotpos, cutwin->dotlen, "", 0);
    win_textbuffer_layout(cutwin);
    break;
  case op_Yank:
    collapse_dot(cutwin);
    if (cutwin->dotpos < cutwin->inputfence)
      cutwin->dotpos = cutwin->numchars;
    xglk_fetch_scrap(&cx, &num);
    xglk_strip_garbage(cx, num);
    if (cx && num) {
      tmppos = cutwin->dotpos;
      win_textbuffer_replace(cutwin, tmppos, 0, cx, num);
      cutwin->dotpos = tmppos+num;
      cutwin->dotlen = 0;
      free(cx);
    }
    win_textbuffer_layout(cutwin);
    break;
  case op_YankReplace:
    xglk_fetch_scrap(&cx, &num);
    xglk_strip_garbage(cx, num);
    if (cx && num) {
      if (cutwin->dotpos < cutwin->inputfence) {
	cutwin->dotpos = cutwin->numchars;
	cutwin->dotlen = 0;
      }
      tmppos = cutwin->dotpos;
      win_textbuffer_replace(cutwin, tmppos, cutwin->dotlen, cx, num);
      cutwin->dotpos = tmppos+num;
      cutwin->dotlen = 0;
      free(cx);
    }
    win_textbuffer_layout(cutwin);
    break;
  case op_Untype:
    if (cutwin->numchars == cutwin->inputfence)
      break;
    cutwin->dotpos = cutwin->inputfence;
    cutwin->dotlen = cutwin->numchars-cutwin->inputfence;
    xglk_store_scrap(cutwin->charbuf+cutwin->dotpos, cutwin->dotlen);
    win_textbuffer_replace(cutwin, cutwin->dotpos, cutwin->dotlen, "", 0);
    win_textbuffer_layout(cutwin);
    break;
    
  }
}

void xgc_buf_history(window_textbuffer_t *cutwin, int op)
{
  long pos, len;

  switch (op) {
  case op_BackLine:
    if (cutwin->historypos > 0) {
#ifdef OLDSTYLEHISTORY
      if (cutwin->dotpos < cutwin->inputfence) {
	cutwin->dotpos = cutwin->numchars;
	cutwin->dotlen = 0;
      }
      cutwin->historypos--;
      pos = cutwin->dotpos;
      win_textbuffer_replace(cutwin, pos, cutwin->dotlen, cutwin->history[cutwin->historypos].str, cutwin->history[cutwin->historypos].len);
      cutwin->dotpos = pos;
      cutwin->dotlen = cutwin->history[cutwin->historypos].len;
#else
      cutwin->historypos--;
      win_textbuffer_replace(cutwin, cutwin->inputfence, cutwin->numchars-cutwin->inputfence, 
	cutwin->history[cutwin->historypos].str, cutwin->history[cutwin->historypos].len);
      cutwin->dotpos = cutwin->inputfence + cutwin->history[cutwin->historypos].len;
      cutwin->dotlen = 0;
#endif
      win_textbuffer_layout(cutwin);
    }
    break;
  case op_ForeLine:
    if (cutwin->historypos < cutwin->historynum) {
#ifdef OLDSTYLEHISTORY
      if (cutwin->dotpos < cutwin->inputfence) {
	cutwin->dotpos = cutwin->numchars;
	cutwin->dotlen = 0;
      }
      cutwin->historypos++;
      if (cutwin->historypos < cutwin->historynum) {
	pos = cutwin->dotpos;
	win_textbuffer_replace(cutwin, cutwin->dotpos, cutwin->dotlen, cutwin->history[cutwin->historypos].str, cutwin->history[cutwin->historypos].len);
	cutwin->dotpos = pos;
	cutwin->dotlen = cutwin->history[cutwin->historypos].len;
      }
      else {
	pos = cutwin->dotpos;
	win_textbuffer_replace(cutwin, cutwin->dotpos, cutwin->dotlen, "", 0);
	cutwin->dotpos = pos;
	cutwin->dotlen = 0;
      }
#else
      cutwin->historypos++;
      if (cutwin->historypos < cutwin->historynum) {
	win_textbuffer_replace(cutwin, cutwin->inputfence, cutwin->numchars-cutwin->inputfence, 
	  cutwin->history[cutwin->historypos].str, cutwin->history[cutwin->historypos].len);
	cutwin->dotpos = cutwin->inputfence + cutwin->history[cutwin->historypos].len;
	cutwin->dotlen = 0;
      }
      else {
	win_textbuffer_replace(cutwin, cutwin->inputfence, cutwin->numchars-cutwin->inputfence, "", 0);
	cutwin->dotpos = cutwin->inputfence;
	cutwin->dotlen = 0;
      }
#endif
      win_textbuffer_layout(cutwin);
    }
  }
}

void xgc_buf_enter(window_textbuffer_t *cutwin, int op)
{
  long ix, len, len2;
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

  win_textbuffer_set_style_text(cutwin, cutwin->originalattr);

  len = cutwin->numchars - cutwin->inputfence;
  len2 = 0;
  for (ix=0; ix < len && len2 < buflen; ix++) {
    unsigned char ch = cutwin->charbuf[cutwin->inputfence+ix];
    buffer[len2] = ch;
    len2++;
  }
  
  len = cutwin->numchars - cutwin->inputfence;
  if (len) {
    /* add to history */
    if (cutwin->historynum==cutwin->historylength) {
      free(cutwin->history[0].str);
      memmove(&cutwin->history[0], &cutwin->history[1], (cutwin->historylength-1) * (sizeof(histunit)));
    }
    else
      cutwin->historynum++;
    cutwin->history[cutwin->historynum-1].str = malloc(len*sizeof(char));
    memmove(cutwin->history[cutwin->historynum-1].str, cutwin->charbuf+cutwin->inputfence, len*sizeof(char));
    cutwin->history[cutwin->historynum-1].len = len;
  }

  if (cutwin->owner->echostr) {
    window_t *oldwin = cutwin->owner;
    /*gli_stream_echo_line(cutwin->owner->echostr, 
      cutwin->charbuf+cutwin->inputfence, len*sizeof(char));*/
    gli_stream_echo_line(cutwin->owner->echostr, 
      buffer, len2*sizeof(char));
  }
  
  win_textbuffer_add(cutwin, '\n', -1);
  cutwin->dotpos = cutwin->numchars;
  cutwin->dotlen = 0;
  cutwin->inputfence = 0;
  win_textbuffer_layout(cutwin);

  eventloop_setevent(evtype_LineInput, cutwin->owner, len2, 0);
  cutwin->owner->line_request = FALSE;
  cutwin->buffer = NULL;
  cutwin->buflen = 0;

  if (gli_unregister_arr) {
    (*gli_unregister_arr)(buffer, buflen, "&+#!Cn", inarrayrock);
  }
}

static void win_textbuffer_line_cancel(window_textbuffer_t *cutwin, 
  event_t *ev)
{
  long ix, len, len2;
  long buflen;
  char *buffer;
  gidispatch_rock_t inarrayrock;
  
  /* same as xgc_buf_enter(), but skip the unnecessary stuff.
      We don't need to add to history, collapse the dot, win_textbuffer_layout,
      trim the buffer, or shrink the status window. */
  
  if (!cutwin->buffer) 
    return;

  buffer = cutwin->buffer;
  buflen = cutwin->buflen;
  inarrayrock = cutwin->inarrayrock;

  len = cutwin->numchars - cutwin->inputfence;
  len2 = 0;
  for (ix=0; ix < len && len2 < buflen; ix++) {
    unsigned char ch = cutwin->charbuf[cutwin->inputfence+ix];
    buffer[len2] = ch;
    len2++;
  }

  len = cutwin->numchars - cutwin->inputfence;
  /*if (len) {
    win_textbuffer_replace(cutwin->inputfence, len, "", 0);
    cutwin->dotpos = cutwin->numchars;
    cutwin->dotlen = 0;
    win_textbuffer_layout(cutwin);
    }*/

  win_textbuffer_set_style_text(cutwin, cutwin->originalattr);
  
  if (cutwin->owner->echostr) {
    window_t *oldwin = cutwin->owner;
    /*gli_stream_echo_line(cutwin->owner->echostr, 
      cutwin->charbuf+cutwin->inputfence, len*sizeof(char));*/
    gli_stream_echo_line(cutwin->owner->echostr, 
      buffer, len2*sizeof(char));
  }
  
  win_textbuffer_add(cutwin, '\n', -1);
  cutwin->dotpos = cutwin->numchars;
  cutwin->dotlen = 0;
  cutwin->inputfence = 0;
  win_textbuffer_layout(cutwin);
  
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

void win_textbuffer_trim_buffer(window_textbuffer_t *cutwin)
{
  if (cutwin->numchars > prefs.buffersize + prefs.bufferslack) {
    long lx;
    for (lx=0; lx<cutwin->numlines; lx++)
      if (cutwin->linelist[lx].pos > (cutwin->numchars-prefs.buffersize))
	break;
    if (lx) {
      win_textbuffer_delete_start(cutwin, lx);
    }
  }
}

