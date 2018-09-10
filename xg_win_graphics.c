#include <stdlib.h>
#include <string.h>
#include "xglk.h"
#include "xg_internal.h"
#include "xg_win_graphics.h"

struct window_graphics_struct {
  window_t *owner;
  XRectangle bbox;

  XRectangle winbox;
  XPoint dirtyul, dirtylr;
  
  XColor background;
  GC gcfill;

  Pixmap world;

  int drag_mouseevent;
  XPoint drag_pt;
};

window_graphics_t *win_graphics_create(window_t *win)
{
  XRectangle box;
  XGCValues gcvalues;
  int jx;
  window_graphics_t *res = 
    (window_graphics_t *)malloc(sizeof(window_graphics_t));
  if (!res)
    return NULL;
  
  res->owner = win;

  res->winbox.x = 0;
  res->winbox.y = 0;
  res->winbox.width = 0;
  res->winbox.height = 0;

  res->dirtyul.x = 0;
  res->dirtyul.y = 0;
  res->dirtylr.x = 0;
  res->dirtylr.y = 0;
  
  res->world = 0;

  UnpackRGBColor(&res->background, 0xFFFFFF);
  XAllocColor(xiodpy, xiomap, &res->background);

  gcvalues.function = GXcopy;
  gcvalues.foreground = res->background.pixel;
  gcvalues.background = res->background.pixel;
  res->gcfill = XCreateGC(xiodpy, xiowin, 
    GCForeground|GCBackground, &gcvalues);
  
  return res;
}

void win_graphics_destroy(window_graphics_t *dwin)
{
  dwin->owner = NULL;
  
  if (dwin->world) {
    XFreePixmap(xiodpy, dwin->world);
    dwin->world = 0;
  }
  if (dwin->gcfill) {
    XFreeGC(xiodpy, dwin->gcfill);
    dwin->gcfill = 0;
  }

  free(dwin);
}

void win_graphics_rearrange(window_t *win, XRectangle *box)
{
  window_graphics_t *dwin = win->data;
  Pixmap newpixmap = 0;
  int bothwid, bothhgt;
  
  if (box->width <= 0 || box->height <= 0) {
    if (dwin->world) {
      XFreePixmap(xiodpy, dwin->world);
      dwin->world = 0;
    }
    dwin->winbox = (*box);
    return;
  }

  bothwid = dwin->winbox.width;
  if (box->width < bothwid)
    bothwid = box->width;
  bothhgt = dwin->winbox.height;
  if (box->height < bothhgt)
    bothhgt = box->height;

  newpixmap = XCreatePixmap(xiodpy, xiowin, box->width, box->height,
    xiodepth);
  
  if (dwin->world && bothwid && bothhgt) {
    XCopyArea(xiodpy, dwin->world, newpixmap, gcfore, 
      0, 0, bothwid, bothhgt, 0, 0);
  }

  if (dwin->world) {
    XFreePixmap(xiodpy, dwin->world);
    dwin->world = 0;
  }

  dwin->world = newpixmap;

  if (box->width > dwin->winbox.width) {
    win_graphics_erase_rect(dwin, FALSE, dwin->winbox.width, 0, 
      box->width-dwin->winbox.width, box->height);
  }
  if (box->height > dwin->winbox.height) {
    win_graphics_erase_rect(dwin, FALSE, 0, dwin->winbox.height, 
      box->width, box->height-dwin->winbox.height);
  }

  dwin->winbox = (*box);
  dwin->winbox.x = 0;
  dwin->winbox.y = 0;
  dwin->bbox = (*box);
  dwin->dirtyul.x = 0;
  dwin->dirtyul.y = 0;
  dwin->dirtylr.x = box->width;
  dwin->dirtylr.y = box->height;
}

XRectangle *win_graphics_get_rect(window_t *win)
{
  window_graphics_t *dwin = win->data;
  return &dwin->bbox;
}

long win_graphics_figure_size(window_t *win, long size, int vertical)
{
  window_graphics_t *dwin = win->data;
  
  if (vertical) {
    return size;
  }
  else {
    return size;
  }
}

void win_graphics_get_size(window_t *win, glui32 *width, glui32 *height)
{
  window_graphics_t *dwin = win->data;
  *width = dwin->winbox.width;
  *height = dwin->winbox.height;
}

void win_graphics_redraw(window_t *win)
{
  window_graphics_t *dwin = win->data;
  
  dwin->dirtyul.x = dwin->winbox.width;
  dwin->dirtyul.y = dwin->winbox.height;
  dwin->dirtylr.x = 0;
  dwin->dirtylr.y = 0;

  gli_draw_window_outline(&dwin->bbox);

  if (!dwin->world) {
    win_graphics_erase_rect(dwin, TRUE, 0, 0, 0, 0);
    if (!eventloop_isevent())
      eventloop_setevent(evtype_Redraw, NULL, 0, 0);
    return;
  }

  XCopyArea(xiodpy, dwin->world, xiowin, gcfore,
    0, 0, dwin->winbox.width, dwin->winbox.height, 
    dwin->bbox.x, dwin->bbox.y);
}

void win_graphics_flush(window_t *win)
{
  window_graphics_t *dwin = win->data;
  XPoint boxul, boxlr;
  int width, height;

  boxul = dwin->dirtyul;
  boxlr = dwin->dirtylr;

  dwin->dirtyul.x = dwin->winbox.width;
  dwin->dirtyul.y = dwin->winbox.height;
  dwin->dirtylr.x = 0;
  dwin->dirtylr.y = 0;

  if (boxul.x < 0)
    boxul.x = 0;
  if (boxul.y < 0)
    boxul.y = 0;
  if (boxlr.x >= dwin->winbox.width)
    boxlr.x = dwin->winbox.width;
  if (boxlr.y >= dwin->winbox.height)
    boxlr.y = dwin->winbox.height;

  if (boxul.x >= boxlr.x || boxul.y >= boxlr.y) {
    return;
  }

  width = boxlr.x - boxul.x;
  height = boxlr.y - boxul.y;
  
  if (!dwin->world) {
    return;
  }
  
  XCopyArea(xiodpy, dwin->world, xiowin, gcfore,
    boxul.x, boxul.y, width, height,
    dwin->bbox.x+boxul.x, dwin->bbox.y+boxul.y);
}

void win_graphics_perform_click(window_t *win, int dir, XPoint *pt, 
  int butnum, int clicknum, unsigned int state)
{
  window_graphics_t *dwin = win->data; 
  long pos;
  long px, px2;

  if (dir == mouse_Down) {
    dwin->drag_mouseevent = FALSE;
    if (dwin->owner->mouse_request) {
      dwin->drag_mouseevent = TRUE;
      dwin->drag_pt.x = pt->x - dwin->bbox.x;
      dwin->drag_pt.y = pt->y - dwin->bbox.y;
    }
  }
  else if (dir == mouse_Up) {
    if (dwin->drag_mouseevent) {
      dwin->owner->mouse_request = FALSE;
      eventloop_setevent(evtype_MouseInput, dwin->owner, 
	dwin->drag_pt.x, dwin->drag_pt.y);
    }
  }
}

static void dirty_rect(XPoint *ul, XPoint *lr, 
  int x, int y, int width, int height)
{
  if (ul->x > x)
    ul->x = x;
  if (ul->y > y)
    ul->y = y;
  if (lr->x < x+width)
    lr->x = x+width;
  if (lr->y < y+height)
    lr->y = y+height;
}

glui32 win_graphics_draw_picture(window_graphics_t *dwin, glui32 image, 
  glsi32 xpos, glsi32 ypos,
  int scale, glui32 imagewidth, glui32 imageheight)
{
  picture_t *pic = picture_find(image);
  
  if (!pic) {
    return FALSE;
  }
  
  if (!scale) {
    imagewidth = pic->width;
    imageheight = pic->height;
  }
  
  if (dwin->world) {
    picture_draw(pic, dwin->world, xpos, ypos, imagewidth, imageheight, NULL);
    dirty_rect(&dwin->dirtyul, &dwin->dirtylr, 
      xpos, ypos, imagewidth, imageheight);
  }
  else {
    picture_draw(pic, xiowin, dwin->bbox.x+xpos, dwin->bbox.y+ypos, 
      imagewidth, imageheight, NULL);
  }
  
  picture_release(pic);
  return TRUE;
}

void win_graphics_erase_rect(window_graphics_t *dwin, int whole,
  glsi32 left, glsi32 top, glui32 width, glui32 height)
{
  XGCValues gcvalues;
  
  if (whole) {
    left = 0;
    top = 0;
    width = dwin->winbox.width;
    height = dwin->winbox.height;
  }
  
  if (width == 0 || height == 0)
    return;

  gcvalues.foreground = dwin->background.pixel;
  XChangeGC(xiodpy, dwin->gcfill, GCForeground, &gcvalues);
  
  if (!dwin->world) {
    XFillRectangle(xiodpy, xiowin, dwin->gcfill,
      dwin->bbox.x+left, dwin->bbox.y+top, width, height);
    return;
  }
  
  dirty_rect(&dwin->dirtyul, &dwin->dirtylr, 
    left, top, (glsi32)width, (glsi32)height);

  XFillRectangle(xiodpy, dwin->world, dwin->gcfill,
    left, top, width, height);
}

void win_graphics_fill_rect(window_graphics_t *dwin, glui32 color,
  glsi32 left, glsi32 top, glui32 width, glui32 height)
{
  XGCValues gcvalues;
  XColor col;

  if (width == 0 || height == 0)
    return;

  UnpackRGBColor(&col, color);
  XAllocColor(xiodpy, xiomap, &col);

  gcvalues.foreground = col.pixel;
  XChangeGC(xiodpy, dwin->gcfill, GCForeground, &gcvalues);
  
  if (!dwin->world) {
    XFillRectangle(xiodpy, xiowin, dwin->gcfill,
      dwin->bbox.x+left, dwin->bbox.y+top, width, height);
    return;
  }
  
  dirty_rect(&dwin->dirtyul, &dwin->dirtylr, 
    left, top, (glsi32)width, (glsi32)height);

  XFillRectangle(xiodpy, dwin->world, dwin->gcfill,
    left, top, width, height);
}

void win_graphics_set_background_color(window_graphics_t *dwin, 
  glui32 color)
{
  UnpackRGBColor(&dwin->background, color);
  XAllocColor(xiodpy, xiomap, &dwin->background);
}


