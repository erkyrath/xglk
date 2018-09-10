#include "xglk.h"
#include "xg_internal.h"

#define BUTTONSIZE (10)

void xweg_init_scrollbar(wegscroll_t *weg, void *rock, 
  cmdfunc_ptr scrollfunc, cmdfunc_ptr scrolltofunc)
{
  weg->rock = rock;
  weg->scrollfunc = scrollfunc;
  weg->scrolltofunc = scrolltofunc;
}

void xweg_draw_scrollbar(wegscroll_t *weg)
{
  XRectangle *box = &weg->box;
  XPoint poly[3];

  XFillRectangle(xiodpy, xiowin, gcback, box->x, box->y,
    box->width, box->height);

  poly[0].x = box->x + box->width/2;
  poly[0].y = box->y + 1;
  poly[1].x = box->x + 1;
  poly[1].y = poly[0].y + BUTTONSIZE-1;
  poly[2].x = box->x + box->width - 1;
  poly[2].y = poly[0].y + BUTTONSIZE-1;
  XFillPolygon(xiodpy, xiowin, gcfore, poly, 3, Convex, CoordModeOrigin);

  poly[0].x = box->x + box->width/2;
  poly[0].y = box->y + box->height - 1;
  poly[1].x = box->x + 1;
  poly[1].y = poly[0].y - (BUTTONSIZE-1);
  poly[2].x = box->x + box->width - 1;
  poly[2].y = poly[0].y - (BUTTONSIZE-1);
  XFillPolygon(xiodpy, xiowin, gcfore, poly, 3, Convex, CoordModeOrigin);

  XFillRectangle(xiodpy, xiowin, gctech, box->x+1, box->y+BUTTONSIZE,
    box->width-1, box->height-2*BUTTONSIZE);

  XDrawLine(xiodpy, xiowin, gcfore, box->x, box->y, 
    box->x, box->y+box->height);

  weg->vistop = -1;
  weg->visbot = -1;
}

void xweg_adjust_scrollbar(wegscroll_t *weg, int numlines, int scrollline,
  int linesperpage) 
{
  int newtop, newbot;
  int barheight = (weg->box.height-2*BUTTONSIZE);

  if (numlines) {
    newtop = ((barheight*scrollline) / numlines) + BUTTONSIZE;
    newbot = ((barheight*(scrollline+linesperpage)) / numlines) + BUTTONSIZE;
    if (newtop < BUTTONSIZE)
      newtop = BUTTONSIZE;
    if (newbot >= weg->box.height-BUTTONSIZE)
      newbot = weg->box.height-BUTTONSIZE;
  }
  else {
    newtop = BUTTONSIZE;
    newbot = weg->box.height-BUTTONSIZE;
  }

  if (newtop == weg->vistop && newbot==weg->visbot)
    return;

  if (weg->vistop != (-1)
    && (weg->vistop >= newbot || newtop >= weg->visbot)) {
    /* erase old completely */
    XFillRectangle(xiodpy, xiowin, gctech, 
      weg->box.x+1, weg->box.y+weg->vistop, 
      weg->box.width-1, weg->visbot-weg->vistop);
    weg->vistop = (-1);
  }

  if (weg->vistop == (-1)) {
    /* redraw new completely */
    XDrawRectangle(xiodpy, xiowin, gcfore, 
      weg->box.x+1, weg->box.y+newtop, 
      weg->box.width-2, (newbot-newtop)-1);
    XFillRectangle(xiodpy, xiowin, gcback, 
      weg->box.x+2, weg->box.y+newtop+1, 
      weg->box.width-3, (newbot-newtop)-2);
    weg->vistop = newtop;
    weg->visbot = newbot;
    return;
  }

  /* ok, the old and new overlap */
  if (newtop < weg->vistop) {
    XFillRectangle(xiodpy, xiowin, gcback, 
      weg->box.x+2, weg->box.y+newtop+1, 
      weg->box.width-3, weg->vistop-newtop);
  }
  else if (newtop > weg->vistop) {
    XFillRectangle(xiodpy, xiowin, gctech, 
      weg->box.x+1, weg->box.y+weg->vistop, 
      weg->box.width-1, newtop-weg->vistop);
  }

  if (newbot > weg->visbot) {
    XFillRectangle(xiodpy, xiowin, gcback, 
      weg->box.x+2, weg->box.y+weg->visbot-1, 
      weg->box.width-3, newbot-weg->visbot);
  }
  else if (newbot < weg->visbot) {
    XFillRectangle(xiodpy, xiowin, gctech, 
      weg->box.x+1, weg->box.y+newbot, 
      weg->box.width-1, weg->visbot-newbot);
  }

  XDrawRectangle(xiodpy, xiowin, gcfore, 
    weg->box.x+1, weg->box.y+newtop, 
    weg->box.width-2, (newbot-newtop)-1);
  weg->vistop = newtop;
  weg->visbot = newbot;
}

void xweg_click_scrollbar(wegscroll_t *weg, int dir, XPoint *pt, 
  int butnum, int clicknum, unsigned int state, 
  int numlines, int scrollline, int linesperpage)
{
  int ypos = pt->y - weg->box.y;
  int px;

  if (dir == mouse_Down) {
    switch (butnum) {
    case 1:
      if (ypos < BUTTONSIZE) {
	weg->drag_scrollmode = 2;
        (*weg->scrollfunc)(weg->rock, op_ToTop);
      }
      else if (ypos >= weg->box.height-BUTTONSIZE) {
	weg->drag_scrollmode = 2;
        (*weg->scrollfunc)(weg->rock, op_ToBottom);
      }
      else {
	weg->drag_hitypos = ypos;
	weg->drag_origline = scrollline;
	if (ypos >= weg->vistop
          && ypos < weg->visbot) {
          weg->drag_scrollmode = 0;
	}
        else {
          weg->drag_scrollmode = 3;
	  if (ypos < weg->vistop)
	    (*weg->scrollfunc)(weg->rock, op_UpPage);
	  else
	    (*weg->scrollfunc)(weg->rock, op_DownPage);	    
	}
      }
      break;
    case 3:
      if (ypos < BUTTONSIZE) {
	weg->drag_scrollmode = 2;
        (*weg->scrollfunc)(weg->rock, op_UpLine);
      }
      else if (ypos >= weg->box.height-BUTTONSIZE) {
	weg->drag_scrollmode = 2;
        (*weg->scrollfunc)(weg->rock, op_DownLine);
      }
      break;
    }
  }
  else if (dir == mouse_Move) {
    if (weg->drag_scrollmode == 0) {
      px = ((long)(ypos - weg->drag_hitypos) * (long)numlines) 
	/ (long)(weg->box.height-2*BUTTONSIZE);
      (*weg->scrolltofunc)(weg->rock, weg->drag_origline+px);
    }
  }
}

