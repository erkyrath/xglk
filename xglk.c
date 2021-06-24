#include "xglk.h"
#include "xg_internal.h"

#include "greypm.bm"

fontset_t plainfonts;

#define EVENTMASK (ButtonPressMask | ButtonReleaseMask | ButtonMotionMask \
                   | KeyPressMask | StructureNotifyMask | ExposureMask)

static XPoint polydot[3]; /* For the cursor-dot. */

static void arrange_window(void);

int xglk_init(int argc, char *argv[], glkunix_startup_t *startdata)
{
  int err;
  XSetWindowAttributes winattrs;

  /* Test for compile-time errors. If one of these spouts off, you
     must edit glk.h and recompile. */
  if (sizeof(glui32) != 4) {
      size_t size = sizeof(glui32);
      printf("Compile-time error: glui32 is not a 32-bit value. Please fix glk.h.\n");
      printf("glui32 is a %li", size);
      return 1;
  }
  if ((glui32)(-1) < (glui32)(0)) {
    printf("Compile-time error: glui32 is not unsigned. Please fix glk.h.\n");
    return 1;
  }

  /* Get the X connection going. */
  err = xglk_open_connection(argv[0]);
  if (!err)
    return FALSE;

  polydot[0].x = 0;
  polydot[0].y = 0;
  polydot[1].x = 4;
  polydot[1].y = 5;
  polydot[2].x = -8;
  polydot[2].y = 0;

  if (!init_xkey())
    return FALSE;

  if (!init_xmsg())
    return FALSE;

  if (!init_pictures())
    return FALSE;

  /* Set up all the library internal stuff. */
  if (!init_gli_misc())
    return FALSE;
  if (!init_gli_styles())
    return FALSE;
  if (!init_gli_streams())
    return FALSE;
  if (!init_gli_filerefs())
    return FALSE;
  if (!init_gli_windows())
    return FALSE;

  /* Preferences. */
  err = xglk_init_preferences(argc, argv, startdata);
  if (!err)
    return FALSE;

  xio_any_invalid = TRUE;

  xio_wid = prefs.win_w;
  xio_hgt = prefs.win_h;

  xiowin = XCreateSimpleWindow(xiodpy, DefaultRootWindow(xiodpy), 
    prefs.win_x, prefs.win_y, xio_wid, xio_hgt, 1, 
    prefs.forecolor.pixel, prefs.backcolor.pixel);

  /*
  winattrs.background_pixel = prefs.backcolor.pixel;
  winattrs.border_pixel = prefs.forecolor.pixel;
  xiowin = XCreateWindow(xiodpy, DefaultRootWindow(xiodpy),
    prefs.win_x, prefs.win_y, xio_wid, xio_hgt, 1,
    xiodepth, InputOutput, CopyFromParent, 
    CWBackPixel | CWBorderPixel, &winattrs);
  */

  if (xiomap != DefaultColormap(xiodpy, xioscn))
    XSetWindowColormap(xiodpy, xiowin, xiomap);

  {
    XSizeHints szhints;
    szhints.flags = PMinSize|USPosition|USSize;
    szhints.min_width = 250;
    szhints.min_height = 200;
    szhints.x = prefs.win_x;
    szhints.y = prefs.win_y;
    szhints.width = xio_wid;
    szhints.height = xio_hgt;
    XSetWMNormalHints(xiodpy, xiowin, &szhints); 
  }
  { /* make some window managers happy */
    XWMHints wmhints;
    wmhints.flags = InputHint;
    wmhints.input = True;
    XSetWMHints(xiodpy, xiowin, &wmhints);
  }
  {
    XSetWindowAttributes attr;
    attr.event_mask = EVENTMASK;
    attr.backing_store = WhenMapped;
    XChangeWindowAttributes(xiodpy, xiowin, CWEventMask|CWBackingStore, &attr);
  }

  XStoreName(xiodpy, xiowin, "XGlk"); /*### library issue*/
  {
    XGCValues gcvalues;

    gcvalues.function = GXcopy;
    gcvalues.foreground = prefs.forecolor.pixel;
    gcvalues.background = prefs.backcolor.pixel;
    gcfore = XCreateGC(xiodpy, xiowin, GCForeground|GCBackground, &gcvalues);
    XSetGraphicsExposures(xiodpy, gcfore, FALSE);
    
    gcvalues.foreground = prefs.backcolor.pixel;
    gcvalues.background = prefs.forecolor.pixel;
    gcback = XCreateGC(xiodpy, xiowin, GCForeground|GCBackground, &gcvalues);

    gcvalues.function = GXxor;
    /*### does this hork for colored text? */
    gcvalues.foreground = (prefs.forecolor.pixel)^(prefs.backcolor.pixel);
    gcvalues.background = (prefs.forecolor.pixel)^(prefs.backcolor.pixel);
    gcflip = XCreateGC(xiodpy, xiowin, GCFunction|GCForeground|GCBackground, &gcvalues);

    textforepixel = prefs.forecolor.pixel;
    textbackpixel = prefs.backcolor.pixel;
    textforefont = 0;
    gcvalues.foreground = textforepixel;
    gctextfore = XCreateGC(xiodpy, xiowin, GCForeground, &gcvalues);
    gcvalues.foreground = textbackpixel;
    gctextback = XCreateGC(xiodpy, xiowin, GCForeground, &gcvalues);

    if (xiodepth==1) {
      Pixmap greypm;
      gcvalues.fill_style = FillOpaqueStippled;
      greypm = XCreateBitmapFromData(xiodpy, xiowin, greypm_bits, 
	greypm_width, greypm_height);
      gcvalues.foreground = prefs.forecolor.pixel;
      gcvalues.background = prefs.backcolor.pixel;
      gcvalues.stipple = greypm;
      gctech = XCreateGC(xiodpy, xiowin, 
	GCForeground|GCBackground|GCFillStyle|GCStipple, &gcvalues);
      gcselect = gcfore;
      gctechu = gctech;
      gctechd = gctech;
    }
    else {

      gcvalues.foreground = prefs.selectcolor.pixel;
      gcvalues.background = prefs.backcolor.pixel;
      gcselect = XCreateGC(xiodpy, xiowin, GCForeground|GCBackground, &gcvalues);

      gcvalues.foreground = prefs.techcolor.pixel;
      gcvalues.background = prefs.backcolor.pixel;
      gctech = XCreateGC(xiodpy, xiowin, GCForeground|GCBackground, &gcvalues);

      gcvalues.foreground = prefs.techucolor.pixel;
      gctechu = XCreateGC(xiodpy, xiowin, GCForeground|GCBackground, &gcvalues);

      gcvalues.foreground = prefs.techdcolor.pixel;
      gctechd = XCreateGC(xiodpy, xiowin, GCForeground|GCBackground, &gcvalues);
    }
  }

  XMapWindow(xiodpy, xiowin);

  gli_styles_compute(&plainfonts, NULL);
  xglk_arrange_window();
  
  return TRUE;
}

/* Arrange all the subwindows and the message line, based on the 
   xio_wid and xio_hgt. Doesn't do any drawing. */
void xglk_arrange_window()
{
  XRectangle box;
  int botheight;
  
  botheight = plainfonts.lineheight+4;
  
  xmsg_resize(0, xio_hgt-botheight, xio_wid, botheight);

  box.x = 0;
  box.y = 0;
  box.width = xio_wid;
  box.height = xio_hgt-botheight;
  matte_box = box;

  box.x += (MATTE_WIDTH-1);
  box.y += (MATTE_WIDTH-1);
  box.width -= 2*(MATTE_WIDTH-1);
  box.height -= 2*(MATTE_WIDTH-1);
  
  if (gli_rootwin)
    gli_window_rearrange(gli_rootwin, &box);

  eventloop_setevent(evtype_Arrange, NULL, 0, 0);
}

void xglk_invalidate(XRectangle *box)
{
  XRectangle dummybox;

  if (!box) {
    dummybox.x = 0;
    dummybox.y = 0;
    dummybox.width = xio_wid;
    dummybox.height = xio_hgt;
    box = &dummybox;
  }

  xio_any_invalid = TRUE;
}

void xglk_redraw()
{
  if (gli_rootwin) {
    int linewid = MATTE_WIDTH-3;
    XGCValues gcvalues;
    gcvalues.line_width = linewid;
    XChangeGC(xiodpy, gctech, GCLineWidth, &gcvalues);
    XDrawRectangle(xiodpy, xiowin, gctech, 
      matte_box.x+linewid/2, matte_box.y+linewid/2, 
      matte_box.width-linewid, matte_box.height-linewid);
    gcvalues.line_width = 1;
    XChangeGC(xiodpy, gctech, GCLineWidth, &gcvalues);
  }
  else {
    XFillRectangle(xiodpy, xiowin, gctech, 
      matte_box.x, matte_box.y, matte_box.width, matte_box.height);
  }

  xmsg_redraw();

  if (gli_rootwin)
    gli_window_redraw(gli_rootwin);
  
  if (gli_focuswin) {
    gli_draw_window_highlight(gli_focuswin, TRUE);
  }

  xio_any_invalid = FALSE;
}

void xglk_perform_click(int dir, XPoint *pt, int butnum, 
  unsigned int state)
{
  window_t *win;
  /* For click continuity */
  static window_t *hitwin = NULL;
  static unsigned int buttonhit;
  static unsigned int buttonmods;
  static int clicknum;
  static int lastxpos, lastypos;

  switch (dir) {

  case mouse_Reset:
    hitwin = NULL;
    clicknum = 0;
    lastxpos = -32767;
    lastypos = -32767;
    break;

  case mouse_Down:
    if (pt->x >= lastxpos-1 && pt->x <= lastxpos+1
      && pt->y >= lastypos-1 && pt->y <= lastypos+1) {
      clicknum++;
    }
    else {
      lastxpos = pt->x;
      lastypos = pt->y;
      clicknum = 1;
    }
    win = gli_find_window_by_point(gli_rootwin, pt);
    if (win) {
      if (win != gli_focuswin && win->type != wintype_Pair) {
	gli_set_focus(win);
      }
      if (win == gli_focuswin) {
	hitwin = win;
	gli_window_perform_click(hitwin, dir, pt, butnum, clicknum, state);
      }
    }
    break;

  case mouse_Move:
    if (hitwin) {
      gli_window_perform_click(hitwin, dir, pt, butnum, clicknum, state);
    }
    break;

  case mouse_Up:
    if (hitwin) {
      gli_window_perform_click(hitwin, dir, pt, butnum, clicknum, state);
      hitwin = NULL;
    }
    break;
  }
}

void xglk_clearfor_string(XColor *colref, int xpos, int ypos,
  int width, int height)
{
  if (colref->pixel != textbackpixel) {
    XGCValues gcvalues;
    textbackpixel = colref->pixel;
    gcvalues.foreground = textbackpixel;
    XChangeGC(xiodpy, gctextback, GCForeground, &gcvalues);
  }

  XFillRectangle(xiodpy, xiowin, gctextback,
    xpos, ypos, width, height);
}

void xglk_draw_string(fontref_t *fontref, int islink, int width, 
  int xpos, int ypos, char *str, int len)
{
  XGCValues gcvalues;
  unsigned long mask = 0;
  unsigned long forepix;

  if (fontref->fontstr->fid != textforefont) {
    mask |= GCFont;
    textforefont = fontref->fontstr->fid;
    gcvalues.font = textforefont;
  }
  forepix = ((islink && prefs.colorlinks) 
    ? (fontref->linkcolor.pixel) 
    : (fontref->forecolor.pixel));
  if (forepix != textforepixel) {
    mask |= GCForeground;
    textforepixel = forepix;
    gcvalues.foreground = textforepixel;
  }

  if (mask) {
    XChangeGC(xiodpy, gctextfore, mask, &gcvalues);
  }

  XDrawString(xiodpy, xiowin, gctextfore, xpos, ypos, str, len);
  if (islink && prefs.underlinelinks) {
    int liney = ypos + fontref->underliney;
    XDrawLine(xiodpy, xiowin, gctextfore, xpos, liney, xpos+width, liney);
  }
}

void gli_draw_window_outline(XRectangle *winbox)
{
  XRectangle box;
  
  box.x = winbox->x-1;
  box.y = winbox->y-1;
  box.width = winbox->width+2;
  box.height = winbox->height+2;
  
  if (xiodepth > 1) {
    XDrawLine(xiodpy, xiowin, gctechd,
      box.x-1, box.y+box.height, box.x-1, box.y-1);
    XDrawLine(xiodpy, xiowin, gctechd,
      box.x-1, box.y-1, box.x+box.width, box.y-1);

    XDrawLine(xiodpy, xiowin, gctechu,
      box.x, box.y+box.height, box.x+box.width, box.y+box.height);
    XDrawLine(xiodpy, xiowin, gctechu,
      box.x+box.width, box.y+box.height, box.x+box.width, box.y);
  }
  XDrawRectangle(xiodpy, xiowin, gcfore, 
    box.x, box.y, box.width-1, box.height-1); 
}

void gli_draw_window_margin(XColor *colref, 
  int outleft, int outtop, int outwidth, int outheight,
  int inleft, int intop, int inwidth, int inheight)
{
  int outright = outleft + outwidth;
  int inright = inleft + inwidth;
  int outbottom = outtop + outheight;
  int inbottom = intop + inheight;
  GC gc;

  if (colref) {
    gc = gctextback;
    if (colref->pixel != textbackpixel) {
      XGCValues gcvalues;
      textbackpixel = colref->pixel;
      gcvalues.foreground = textbackpixel;
      XChangeGC(xiodpy, gctextback, GCForeground, &gcvalues);
    }
  }
  else {
    gc = gcback;
  }

  if (outleft < inleft)
    XFillRectangle(xiodpy, xiowin, gc, 
      outleft, outtop, inleft-outleft, outheight);
  if (outtop < intop)
    XFillRectangle(xiodpy, xiowin, gc, 
      outleft, outtop, outwidth, intop-outtop);
  if (outright > inright)
    XFillRectangle(xiodpy, xiowin, gc, 
      inright, outtop, outright-inright, outheight);
  if (outbottom > inbottom)
    XFillRectangle(xiodpy, xiowin, gc, 
      outleft, inbottom, outwidth, outbottom-inbottom);
}

void gli_draw_window_highlight(window_t *win, int turnon)
{
  XRectangle *boxptr = gli_window_get_rect(win);
  int boxleft, boxtop, boxwidth, boxheight;
  GC gc;
  
  boxleft = boxptr->x - (MATTE_WIDTH - 2);
  boxwidth = boxptr->width + 2 * (MATTE_WIDTH - 2);
  boxtop = boxptr->y - (MATTE_WIDTH - 2);
  boxheight = boxptr->height + 2 * (MATTE_WIDTH - 2);

  if (turnon)
    gc = gcselect;
  else
    gc = gctech;
  
  if (MATTE_WIDTH < 6) {
    XDrawRectangle(xiodpy, xiowin, gc,
      boxleft, boxtop, boxwidth-1, boxheight-1);
  }
  else {
    int linewid = MATTE_WIDTH-4;
    XGCValues gcvalues;
    gcvalues.line_width = linewid;
    XChangeGC(xiodpy, gc, GCLineWidth, &gcvalues);
    XDrawRectangle(xiodpy, xiowin, gc,
      boxleft+1, boxtop+1, boxwidth-linewid, boxheight-linewid);
    gcvalues.line_width = 1;
    XChangeGC(xiodpy, gc, GCLineWidth, &gcvalues);
  }
}

void xglk_draw_dot(int xpos, int ypos, int linehgt)
{
  polydot[0].x = xpos;
  polydot[0].y = ypos;
  XFillPolygon(xiodpy, xiowin, gcflip, polydot, 3, Convex, CoordModePrevious);
}

void xglk_relax_memory()
{
  picture_relax_memory();
}

void xgc_focus(window_t *dummy, int op)
{
  window_t *win;
  
  if (!gli_rootwin) {
    gli_set_focus(NULL);
    return;
  }
  
  win = gli_focuswin;
  do {
    win = gli_window_fixiterate(win);
  } while (!(win && win->type != wintype_Pair));
  gli_set_focus(win);
}

void xgc_noop(window_t *dummy, int op)
{

}

void xgc_redraw(window_t *win, int op)
{
  /* Clear the window -- debugging only. (If the program
     works properly, it will all be redrawn.) */
  /*
  XFillRectangle(xiodpy, xiowin, gcselect,
    0, 0, xio_wid, xio_hgt); 
  */

  xglk_invalidate(NULL);
}


