#include <stdlib.h>
#include "xglk.h"
#include "xg_internal.h"
#include "xg_win_textbuf.h"
#include "xg_win_textgrid.h"
#include "xg_win_graphics.h"

window_t *gli_rootwin = NULL;
window_t *gli_focuswin = NULL;

static window_t *gli_windowlist = NULL;

/* Window type data structures */

typedef struct window_blank_struct {
  window_t *owner;
  XRectangle bbox;
} window_blank_t;

typedef struct window_pair_struct {
  window_t *child1, *child2; 
  /* split info... */
  glui32 dir; 
  int vertical, backward;
  glui32 division;
  window_t *key; /* NULL or a leaf-descendant (not a Pair) */
  int keydamage;
  glui32 size;
  
  window_t *owner;
  XRectangle bbox; 
  int flat; /* true if there's nothing to draw */
  int splithgt; /* The split center. Will be >= N, < width-N. 
		   Unless bbox is too small. */
  
} window_pair_t;

static window_t *gli_new_window(glui32 type, glui32 rock);
static void gli_delete_window(window_t *win);

static window_blank_t *win_blank_create(window_t *win);
static void win_blank_destroy(window_blank_t *dwin);
static void win_blank_rearrange(window_t *win, XRectangle *box);
static void win_blank_redraw(window_t *win);

static window_pair_t *win_pair_create(window_t *win, glui32 method, 
  window_t *key, glui32 size);
static void win_pair_destroy(window_pair_t *dwin);
static void win_pair_rearrange(window_t *win, XRectangle *box);
static void win_pair_redraw(window_t *win);

int init_gli_windows()
{
  gli_windowlist = NULL;
  
  gli_focuswin = NULL;
  gli_rootwin = NULL;

  return TRUE;
}

static void gli_delete_window(window_t *win)
{
  window_t *prev, *next;
  
  if (gli_unregister_obj)
    (*gli_unregister_obj)(win, gidisp_Class_Window, win->disprock);
  
  if (win->str) {
    gli_stream_close(win->str);
    win->str = NULL;
  }
  
  win->type = -1;
  win->parent = NULL;
  win->data = NULL;

  prev = win->chain_prev;
  next = win->chain_next;
  win->chain_prev = NULL;
  win->chain_next = NULL;

  if (prev)
    prev->chain_next = next;
  else
    gli_windowlist = next;
  if (next)
    next->chain_prev = prev;

  free(win);
}

static window_t *gli_new_window(glui32 type, glui32 rock)
{
  window_t *win = (window_t *)malloc(sizeof(window_t));
  
  if (!win)
    return NULL;
  
  win->type = type;
  
  win->rock = rock;
  win->parent = NULL; /* for now */
  win->data = NULL; /* for now */
  win->mouse_request = FALSE;
  win->char_request = FALSE;
  win->line_request = FALSE;
  win->hyperlink_request = FALSE;
  win->style = 0;
  win->linkid = 0;
  
  win->str = gli_stream_open_window(win);
  win->echostr = NULL;
  
  win->chain_prev = NULL;
  win->chain_next = gli_windowlist;
  gli_windowlist = win;
  if (win->chain_next) {
    win->chain_next->chain_prev = win;
  }
  
  if (gli_register_obj)
    win->disprock = (*gli_register_obj)(win, gidisp_Class_Window);
  else
    win->disprock.ptr = NULL;

  return win;
}

window_t *gli_window_fixiterate(window_t *win)
{
  if (!win)
    return gli_rootwin;
  
  if (win->type == wintype_Pair) {
    window_pair_t *dwin = win->data;
    if (!dwin->backward)
      return dwin->child1;
    else
      return dwin->child2;
  }
  else {
    window_t *parwin;
    window_pair_t *dwin;
    
    while (win->parent) {
      parwin = win->parent;
      dwin = parwin->data;
      if (!dwin->backward) {
	if (win == dwin->child1)
	  return dwin->child2;
      }
      else {
	if (win == dwin->child2)
	  return dwin->child1;
      }
      win = parwin;
    }
    
    return NULL;
  }
}

window_t *glk_window_iterate(window_t *win, glui32 *rock)
{
  if (!win) {
    win = gli_windowlist;
  }
  else {
    win = win->chain_next;
  }
  
  if (win) {
    if (rock)
      *rock = win->rock;
    return win;
  }
  
  if (rock)
    *rock = 0;
  return NULL;
}

winid_t glk_window_get_root()
{
  if (!gli_rootwin)
    return 0;
  return gli_rootwin;
}

glui32 glk_window_get_rock(window_t *win)
{
  if (!win) {
    gli_strict_warning("window_get_rock: invalid ref");
    return 0;
  }
  return win->rock;
}

glui32 glk_window_get_type(window_t *win)
{
  if (!win) {
    gli_strict_warning("window_get_type: invalid ref");
    return 0;
  }
  return win->type;
}

winid_t glk_window_get_parent(window_t *win)
{
  if (!win) {
    gli_strict_warning("window_get_parent: invalid ref");
    return 0;
  }
  if (win->parent)
    return win->parent;
  else
    return 0;
}

winid_t glk_window_get_sibling(window_t *win)
{
  window_pair_t *dpairwin;
  if (!win) {
    gli_strict_warning("window_get_sibling: invalid ref");
    return NULL;
  }
  if (!win->parent)
    return NULL;
  dpairwin = win->parent->data;
  if (dpairwin->child1 == win)
    return dpairwin->child2;
  else if (dpairwin->child2 == win)
    return dpairwin->child1;
  else {
    gli_strict_warning("window_get_sibling: damaged window tree");
    return NULL;
  }
}

void glk_window_get_size(window_t *win, glui32 *width, glui32 *height)
{
  glui32 wid = 0;
  glui32 hgt = 0;
  
  if (!win) {
    gli_strict_warning("window_get_size: invalid ref");
    return;
  }
  
  switch (win->type) {
  case wintype_Blank:
  case wintype_Pair:
    /* always zero */
    break;
  case wintype_TextGrid:
    win_textgrid_get_size(win, &wid, &hgt);
    break;
  case wintype_TextBuffer:
    win_textbuffer_get_size(win, &wid, &hgt);
    break;
  case wintype_Graphics:
    win_graphics_get_size(win, &wid, &hgt);
    break;
  }

  if (width)
    *width = wid;
  if (height)
    *height = hgt;
}

winid_t glk_window_open(winid_t splitwin, glui32 method, glui32 size, 
  glui32 wintype, glui32 rock)
{
  window_t *newwin, *pairwin, *oldparent;
  window_pair_t *dpairwin;
  XRectangle box, *boxptr;
  glui32 val;
  
  if (!gli_rootwin) {
    if (splitwin) {
      gli_strict_warning("window_open: id must be 0");
      return 0;
    }
    /* ignore method and size now */
    oldparent = NULL;
    
    box = matte_box;
    
    box.x += (MATTE_WIDTH-1);
    box.y += (MATTE_WIDTH-1);
    box.width -= 2*(MATTE_WIDTH-1);
    box.height -= 2*(MATTE_WIDTH-1);
  }
  else {
    
    if (!splitwin) {
      gli_strict_warning("window_open: invalid ref");
      return 0;
    }
    
    val = (method & winmethod_DivisionMask);
    if (val != winmethod_Fixed && val != winmethod_Proportional) {
      gli_strict_warning(
	"window_open: invalid method (not fixed or proportional)");
      return 0;
    }
    
    val = (method & winmethod_DirMask);
    if (val != winmethod_Above && val != winmethod_Below 
      && val != winmethod_Left && val != winmethod_Right) {
      gli_strict_warning("window_open: invalid method (bad direction)");
      return 0;
    }

    boxptr = gli_window_get_rect(splitwin);
    if (!boxptr) {
      gli_strict_warning("window_open: can't get window rect");
      return 0;
    }
    box = *boxptr;
    
    oldparent = splitwin->parent;
    if (oldparent && oldparent->type != wintype_Pair) {
      gli_strict_warning("window_open: parent window is not Pair");
      return 0;
    }
    
  }
  
  newwin = gli_new_window(wintype, rock);
  if (!newwin) {
    gli_strict_warning("window_open: unable to create window");
    return 0;
  }
  switch (wintype) {
  case wintype_Blank:
    newwin->data = win_blank_create(newwin);
    break;
  case wintype_TextGrid:
    newwin->data = win_textgrid_create(newwin);
    break;
  case wintype_TextBuffer:
    newwin->data = win_textbuffer_create(newwin);
    break;
  case wintype_Graphics:
    newwin->data = win_graphics_create(newwin);
    break;
  case wintype_Pair:
    gli_strict_warning("window_open: cannot open pair window directly");
    gli_delete_window(newwin);
    return 0;
  default:
    /* Unknown window type -- do not print a warning, just return 0
       to indicate that it's not possible. */
    gli_delete_window(newwin);
    return 0;
  }
  
  if (!newwin->data) {
    gli_strict_warning("window_open: unable to create window");
    return 0;
  }
  
  if (!splitwin) {
    gli_rootwin = newwin;
    gli_window_rearrange(newwin, &box);
  }
  else {
    /* create pairwin, with newwin as the key */
    pairwin = gli_new_window(wintype_Pair, 0);
    dpairwin = win_pair_create(pairwin, method, newwin, size);
    pairwin->data = dpairwin;
    
    dpairwin->child1 = splitwin;
    dpairwin->child2 = newwin;
    
    splitwin->parent = pairwin;
    newwin->parent = pairwin;
    pairwin->parent = oldparent;

    if (oldparent) {
      window_pair_t *dparentwin = oldparent->data;
      if (dparentwin->child1 == splitwin)
	dparentwin->child1 = pairwin;
      else
	dparentwin->child2 = pairwin;
    }
    else {
      gli_rootwin = pairwin;
    }
    
    gli_window_rearrange(pairwin, &box);
  }
  
  /* We don't send an event, because we're not in a select(). The user has 
     to know what to redraw. */

  /* Redraw the box area. */
  box.x -= (MATTE_WIDTH-1);
  box.y -= (MATTE_WIDTH-1);
  box.width += 2*(MATTE_WIDTH-1);
  box.height += 2*(MATTE_WIDTH-1);
  xglk_invalidate(&box);

  return newwin;
}

void glk_window_get_arrangement(window_t *win, glui32 *method, glui32 *size, 
  winid_t *keywin_id)
{
  window_pair_t *dwin;
  glui32 val;
  
  if (!win) {
    gli_strict_warning("window_get_arrangement: invalid ref");
    return;
  }
  
  if (win->type != wintype_Pair) {
    gli_strict_warning("window_get_arrangement: not a Pair window");
    return;
  }
  
  dwin = win->data;
  
  val = dwin->dir | dwin->division;
  
  if (size)
    *size = dwin->size;
  if (keywin_id) {
    if (dwin->key)
      *keywin_id = dwin->key;
    else
      *keywin_id = 0;
  }
  if (method)
    *method = val;
}

void glk_window_set_arrangement(window_t *win, glui32 method, glui32 size, winid_t key)
{
  window_pair_t *dwin;
  glui32 newdir;
  XRectangle box;
  int newvertical, newbackward;
  
  if (!win) {
    gli_strict_warning("window_set_arrangement: invalid ref");
    return;
  }
  
  if (win->type != wintype_Pair) {
    gli_strict_warning("window_set_arrangement: not a Pair window");
    return;
  }
  
  if (key) {
    window_t *wx;
    if (key->type == wintype_Pair) {
      gli_strict_warning("window_set_arrangement: keywin cannot be a Pair");
      return;
    }
    for (wx=key; wx; wx=wx->parent) {
      if (wx == win)
	break;
    }
    if (wx == NULL) {
      gli_strict_warning("window_set_arrangement: keywin must be a descendant");
      return;
    }
  }
  
  dwin = win->data;
  box = dwin->bbox;
  
  newdir = method & winmethod_DirMask;
  newvertical = (newdir == winmethod_Left || newdir == winmethod_Right);
  newbackward = (newdir == winmethod_Left || newdir == winmethod_Above);
  if (!key)
    key = dwin->key;

  if ((newvertical && !dwin->vertical) || (!newvertical && dwin->vertical)) {
    if (!dwin->vertical)
      gli_strict_warning("window_set_arrangement: split must stay horizontal");
    else
      gli_strict_warning("window_set_arrangement: split must stay vertical");
    return;
  }
  
  if (key && key->type == wintype_Blank 
    && (method & winmethod_DivisionMask) == winmethod_Fixed) {
    gli_strict_warning("window_set_arrangement: a Blank window cannot have a fixed size");
    return;
  }

  if ((newbackward && !dwin->backward) || (!newbackward && dwin->backward)) {
    /* switch the children */
    window_t *tmpwin = dwin->child1;
    dwin->child1 = dwin->child2;
    dwin->child2 = tmpwin;
  }
  
  /* set up everything else */
  dwin->dir = newdir;
  dwin->division = method & winmethod_DivisionMask;
  dwin->key = key;
  dwin->size = size;
  
  dwin->vertical = (dwin->dir == winmethod_Left || dwin->dir == winmethod_Right);
  dwin->backward = (dwin->dir == winmethod_Left || dwin->dir == winmethod_Above);
  
  gli_window_rearrange(win, &box);
  
  /* We don't send an event, because we're not in a select(). 
     The user has to know what to redraw. */
  box.x -= (MATTE_WIDTH-1);
  box.y -= (MATTE_WIDTH-1);
  box.width += 2*(MATTE_WIDTH-1);
  box.height += 2*(MATTE_WIDTH-1);
  xglk_invalidate(&box);
}

static void gli_window_close(window_t *win, int recurse)
{
  window_t *wx;
  
  if (gli_focuswin == win) {
    /* gli_set_focus(NULL); */ /* We skip this, because it only does drawing,
       and the whole area will be redrawn anyway. */
    gli_focuswin = NULL;
  }
  
  for (wx=win->parent; wx; wx=wx->parent) {
    if (wx->type == wintype_Pair) {
      window_pair_t *dwx = wx->data;
      if (dwx->key == win) {
	dwx->key = NULL;
	dwx->keydamage = TRUE;
      }
    }
  }
  
  switch (win->type) {
  case wintype_Blank: {
    window_blank_t *dwin = win->data;
    win_blank_destroy(dwin);
  }
  break;
  case wintype_Pair: {
    window_pair_t *dwin = win->data;
    if (recurse) {
      if (dwin->child1)
	gli_window_close(dwin->child1, TRUE);
      if (dwin->child2)
	gli_window_close(dwin->child2, TRUE);
    }
    win_pair_destroy(dwin);
  }
  break;
  case wintype_TextBuffer: {
    window_textbuffer_t *dwin = win->data;
    win_textbuffer_destroy(dwin);
  }
  break;
  case wintype_TextGrid: {
    window_textgrid_t *dwin = win->data;
    win_textgrid_destroy(dwin);
  }
  break;
  case wintype_Graphics: {
    window_graphics_t *dwin = win->data;
    win_graphics_destroy(dwin);
  }
  break;
  }
  
  gli_delete_window(win);
}

void glk_window_close(window_t *win, stream_result_t *result)
{
  XRectangle box, *boxptr;
  
  if (!win) {
    gli_strict_warning("window_close: invalid ref");
    return;
  }
  
  if (win == gli_rootwin || win->parent == NULL) {
    /* close the root window, which means all windows. */
    
    gli_rootwin = 0;
    
    /* begin (simpler) closation */
    
    boxptr = gli_window_get_rect(win);
    if (!boxptr) {
      gli_strict_warning("window_close: can't get window rect");
      return;
    }
    box = *boxptr;

    gli_stream_fill_result(win->str, result);
    gli_window_close(win, TRUE); 
    
    box.x -= (MATTE_WIDTH-1);
    box.y -= (MATTE_WIDTH-1);
    box.width += 2*(MATTE_WIDTH-1);
    box.height += 2*(MATTE_WIDTH-1);
    xglk_invalidate(&box);
  }
  else {
    /* have to jigger parent */
    window_t *pairwin, *sibwin, *grandparwin, *wx;
    window_pair_t *dpairwin, *dgrandparwin, *dwx;
    int keydamage_flag;
    
    pairwin = win->parent;
    dpairwin = pairwin->data;
    if (win == dpairwin->child1) {
      sibwin = dpairwin->child2;
    }
    else if (win == dpairwin->child2) {
      sibwin = dpairwin->child1;
    }
    else {
      gli_strict_warning("window_close: window tree is corrupted");
      return;
    }
    
    boxptr = gli_window_get_rect(pairwin);
    if (!boxptr) {
      gli_strict_warning("window_close: can't get window rect");
      return;
    }
    box = *boxptr;

    grandparwin = pairwin->parent;
    if (!grandparwin) {
      gli_rootwin = sibwin;
      sibwin->parent = NULL;
    }
    else {
      dgrandparwin = grandparwin->data;
      if (dgrandparwin->child1 == pairwin)
	dgrandparwin->child1 = sibwin;
      else
	dgrandparwin->child2 = sibwin;
      sibwin->parent = grandparwin;
    }
    
    /* Begin closation */
    
    gli_stream_fill_result(win->str, result);

    /* Close the child window (and descendants), so that key-deletion can
       crawl up the tree to the root window. */
    gli_window_close(win, TRUE); 
    
    /* This probably isn't necessary, but the child *is* gone, so just in case. */
    if (win == dpairwin->child1) {
      dpairwin->child1 = NULL;
    }
    else if (win == dpairwin->child2) {
      dpairwin->child2 = NULL;
    }
    
    /* Now we can delete the parent pair. */
    gli_window_close(pairwin, FALSE);

    keydamage_flag = FALSE;
    for (wx=sibwin; wx; wx=wx->parent) {
      if (wx->type == wintype_Pair) {
	window_pair_t *dwx = wx->data;
	if (dwx->keydamage) {
	  keydamage_flag = TRUE;
	  dwx->keydamage = FALSE;
	}
      }
    }
    
    if (keydamage_flag) {
      box = matte_box;
      
      box.x += (MATTE_WIDTH-1);
      box.y += (MATTE_WIDTH-1);
      box.width -= 2*(MATTE_WIDTH-1);
      box.height -= 2*(MATTE_WIDTH-1);
      
      gli_window_rearrange(gli_rootwin, &box);
      
      xglk_invalidate(&matte_box);
    }
    else {
      gli_window_rearrange(sibwin, &box);
      
      /* We don't send an event, because we're not in a select(). The user
	 has to know what to redraw. */
      
      box.x -= (MATTE_WIDTH-1);
      box.y -= (MATTE_WIDTH-1);
      box.width += 2*(MATTE_WIDTH-1);
      box.height += 2*(MATTE_WIDTH-1);
      xglk_invalidate(&box);
    }
  }
}

/* The idea of the flush is that it happens before any entry to
   glk_select(), which takes care of dirty data in valid regions. 
   It would be really cool if this clipped to the valid region,
   and the exposure stuff clipped to invalid, but that's hard in X. */
void gli_windows_flush()
{
  int ix;
  window_t *win;
  
  for (win = gli_windowlist; win; win = win->chain_next) {
    switch (win->type) {
    case wintype_TextBuffer:
      win_textbuffer_flush(win);
      break;
    case wintype_TextGrid:
      win_textgrid_flush(win);
      break;
    case wintype_Graphics:
      win_graphics_flush(win);
      break;
    }
  }
}

void glk_window_clear(window_t *win)
{
  if (!win) {
    gli_strict_warning("window_clear: invalid ref");
    return;
  }
  
  if (win->line_request) {
    gli_strict_warning("window_clear: window has pending line request");
    return;
  }

  switch (win->type) {
  case wintype_TextBuffer:
    win_textbuffer_clear_window(win->data);
    break;
  case wintype_TextGrid:
    win_textgrid_clear_window(win->data);
    break;
  case wintype_Graphics:
    win_graphics_erase_rect(win->data, TRUE, 0, 0, 0, 0);
    break;
  }
}

void glk_window_move_cursor(window_t *win, glui32 xpos, glui32 ypos)
{
  if (!win) {
    gli_strict_warning("window_move_cursor: invalid ref");
    return;
  }
  
  switch (win->type) {
  case wintype_TextGrid:
    win_textgrid_set_pos(win->data, xpos, ypos);
    break;
  default:
    gli_strict_warning("window_move_cursor: not a TextGrid window");
    break;
  }
}

strid_t glk_window_get_stream(window_t *win)
{
  if (!win) {
    gli_strict_warning("window_get_stream: invalid ref");
    return 0;
  }
  
  return win->str;
}

strid_t glk_window_get_echo_stream(window_t *win)
{
  if (!win) {
    gli_strict_warning("window_get_echo_stream: invalid ref");
    return 0;
  }
  
  if (win->echostr)
    return win->echostr;
  else
    return 0;
}

void glk_window_set_echo_stream(window_t *win, stream_t *str)
{
  if (!win) {
    gli_strict_warning("window_set_echo_stream: invalid window id");
    return;
  }
  
  win->echostr = str;
}

void glk_set_window(window_t *win)
{
  if (!win) {
    glk_stream_set_current(NULL);
    return;
  }
  
  glk_stream_set_current(win->str);
}

fontset_t *gli_window_get_fontset(window_t *win)
{
  switch (win->type) {
  case wintype_TextBuffer:
    return win_textbuffer_get_fontset(win);
    break;
  case wintype_TextGrid:
    return win_textgrid_get_fontset(win);
    break;
  default:
    return NULL;
  }
}

static void gli_window_setfocus(window_t *win, int turnon)
{
  gli_draw_window_highlight(win, turnon);
  
  switch (win->type) {
  case wintype_TextBuffer:
    win_textbuffer_setfocus(win, turnon);
    break; 
  case wintype_TextGrid:
    win_textgrid_setfocus(win, turnon);
    break;
  }
}

void gli_set_focus(window_t *win)
{
  if (win == gli_focuswin)
    return;

  if (gli_focuswin) {
    gli_window_setfocus(gli_focuswin, FALSE);
  }
  
  gli_focuswin = win;

  if (gli_focuswin) {
    gli_window_setfocus(gli_focuswin, TRUE);
  }
}

window_t *gli_find_window_by_point(window_t *win, XPoint *pt)
{
  window_t *subwin;
  window_pair_t *dwin;
  int res;
  
  if (!win)
    return NULL;
  
  if (win == gli_rootwin) {
    if (pt->x < matte_box.x + (MATTE_WIDTH-1)
      || pt->x >= matte_box.x+matte_box.width - (MATTE_WIDTH-1)
      || pt->y < matte_box.y + (MATTE_WIDTH-1)
      || pt->y >= matte_box.y+matte_box.height - (MATTE_WIDTH-1)) {
      return NULL;
    }
  }
  
  if (win->type != wintype_Pair)
    return win;
  
  dwin = win->data;
  if (dwin->vertical) {
    int val = dwin->bbox.x + dwin->splithgt;
    if (pt->x >= val - (MATTE_WIDTH/2)
      && pt->x < val - (MATTE_WIDTH/2) + MATTE_WIDTH) {
      return win;
    }
    res = (pt->x >= val);
  }
  else {
    int val = dwin->bbox.y + dwin->splithgt;
    if (pt->y >= val - (MATTE_WIDTH/2)
      && pt->y < val - (MATTE_WIDTH/2) + MATTE_WIDTH) {
      return win;
    }
    res = (pt->y >= val);
  }
  
  if (dwin->backward)
    res = !res;
  if (res)
    subwin = dwin->child2;
  else
    subwin = dwin->child1;
  return gli_find_window_by_point(subwin, pt);
}

void gli_window_rearrange(window_t *win, XRectangle *box)
{
  switch (win->type) {
  case wintype_Blank:
    win_blank_rearrange(win, box);
    break;
  case wintype_Pair:
    win_pair_rearrange(win, box);
    break;
  case wintype_TextGrid:
    win_textgrid_rearrange(win, box);
    break;
  case wintype_TextBuffer:
    win_textbuffer_rearrange(win, box);
    break;
  case wintype_Graphics:
    win_graphics_rearrange(win, box);
    break;
  }
}

void gli_window_redraw(window_t *win)
{
  switch (win->type) {
  case wintype_Blank:
    win_blank_redraw(win);
    break;
  case wintype_Pair:
    win_pair_redraw(win);
    break;
  case wintype_TextGrid:
    win_textgrid_redraw(win);
    break;
  case wintype_TextBuffer:
    win_textbuffer_redraw(win);
    break;
  case wintype_Graphics:
    win_graphics_redraw(win);
    break;
  }
}

void gli_windows_set_paging(int forcetoend)
{
  window_t *win;
  
  for (win=gli_window_fixiterate(NULL); 
       win; 
       win=gli_window_fixiterate(win)) {
    switch (win->type) {
    case wintype_TextBuffer:
      win_textbuffer_set_paging(win->data, forcetoend);
      break;
    }
  }
}

void gli_windows_trim_buffers()
{
  window_t *win;
  
  for (win=gli_window_fixiterate(NULL); 
       win; 
       win=gli_window_fixiterate(win)) {
    switch (win->type) {
    case wintype_TextBuffer:
      win_textbuffer_trim_buffer(win->data);
      break;
    }
  }
}

void gli_window_perform_click(window_t *win, int dir, XPoint *pt, int butnum, 
  int clicknum, unsigned int state)
{
  switch (win->type) {
  case wintype_TextGrid: 
    win_textgrid_perform_click(win, dir, pt, butnum, clicknum, state);
    break;
  case wintype_TextBuffer: 
    win_textbuffer_perform_click(win, dir, pt, butnum, clicknum, state);
    break;
  case wintype_Graphics: 
    win_graphics_perform_click(win, dir, pt, butnum, clicknum, state);
    break;
  }
}

XRectangle *gli_window_get_rect(window_t *win)
{
  switch (win->type) {
  case wintype_Blank: {
    window_blank_t *dwin = win->data;
    return &dwin->bbox;
  }
  case wintype_Pair: {
    window_pair_t *dwin = win->data;
    return &dwin->bbox;
  }
  case wintype_TextGrid: 
    return win_textgrid_get_rect(win);
  case wintype_TextBuffer:
    return win_textbuffer_get_rect(win);
  case wintype_Graphics:
    return win_graphics_get_rect(win);
  default:
    return NULL;
  }
}

void glk_request_char_event(window_t *win)
{
  if (!win) {
    gli_strict_warning("request_char_event: invalid ref");
    return;
  }
  
  if (win->char_request || win->line_request) {
    gli_strict_warning("request_char_event: window already has keyboard request");
    return;
  }
  
  switch (win->type) {
  case wintype_TextBuffer:
  case wintype_TextGrid:
    win->char_request = TRUE;
    break;
  default:
    gli_strict_warning("request_char_event: window does not support keyboard input");
    break;
  }
  
}

void glk_cancel_char_event(window_t *win)
{
  if (!win) {
    gli_strict_warning("cancel_char_event: invalid ref");
    return;
  }
  
  switch (win->type) {
  case wintype_TextBuffer:
  case wintype_TextGrid:
    win->char_request = FALSE;
    break;
  default:
    /* do nothing */
    break;
  }
}

void glk_request_line_event(window_t *win, char *buf, glui32 maxlen, 
  glui32 initlen)
{
  if (!win) {
    gli_strict_warning("request_line_event: invalid ref");
    return;
  }
  
  if (win->char_request || win->line_request) {
    gli_strict_warning("request_line_event: window already has keyboard request");
    return;
  }
  
  switch (win->type) {
  case wintype_TextBuffer:
    win->line_request = TRUE;
    win_textbuffer_init_line(win, buf, maxlen, initlen);
    break;
  case wintype_TextGrid:
    win->line_request = TRUE;
    win_textgrid_init_line(win, buf, maxlen, initlen);
    break;
  default:
    gli_strict_warning("request_line_event: window does not support keyboard input");
    break;
  }
  
}

void glk_cancel_line_event(window_t *win, event_t *ev)
{
  event_t dummyev;
  
  if (!ev) {
    ev = &dummyev;
  }

  gli_event_clearevent(ev);
  
  if (!win) {
    gli_strict_warning("cancel_line_event: invalid ref");
    return;
  }
  
  switch (win->type) {
  case wintype_TextBuffer:
    if (win->line_request) {
      win_textbuffer_cancel_line(win, ev);
    }
    break;
  case wintype_TextGrid:
    if (win->line_request) {
      win_textgrid_cancel_line(win, ev);
    }
    break;
  default:
    /* do nothing */
    break;
  }
}

void glk_request_mouse_event(window_t *win)
{
  if (!win) {
    gli_strict_warning("request_mouse_event: invalid ref");
    return;
  }
    
  switch (win->type) {
  case wintype_Graphics:
  case wintype_TextGrid:
    win->mouse_request = TRUE;
    break;
  default:
    /* do nothing */
    break;
  }

  return;
}

void glk_cancel_mouse_event(window_t *win)
{
  if (!win) {
    gli_strict_warning("cancel_mouse_event: invalid ref");
    return;
  }
    
  switch (win->type) {
  case wintype_Graphics:
  case wintype_TextGrid:
    win->mouse_request = FALSE;
    break;
  default:
    /* do nothing */
    break;
  }
    
  return;
}

void glk_request_hyperlink_event(window_t *win)
{
  if (!win) {
    gli_strict_warning("request_hyperlink_event: invalid ref");
    return;
  }
    
  switch (win->type) {
  case wintype_TextBuffer:
  case wintype_TextGrid:
    win->hyperlink_request = TRUE;
    break;
  default:
    /* do nothing */
    break;
  }
    
  return;
}

void glk_cancel_hyperlink_event(window_t *win)
{
  if (!win) {
    gli_strict_warning("cancel_hyperlink_event: invalid ref");
    return;
  }
    
  switch (win->type) {
  case wintype_Graphics:
  case wintype_TextGrid:
    win->hyperlink_request = FALSE;
    break;
  default:
    /* do nothing */
    break;
  }
    
  return;
}

void gli_window_put_char(window_t *win, unsigned char ch)
{
  switch (win->type) {
  case wintype_TextBuffer:
    win_textbuffer_add(win->data, ch, -1);
    break;
  case wintype_TextGrid:
    win_textgrid_add(win->data, ch);
    break;
  }
}

void gli_window_set_style(window_t *win, glui32 val)
{
  win->style = val;

  switch (win->type) {
  case wintype_TextBuffer:
    win_textbuffer_set_style_text(win->data, win->style);
    break;
    /* Don't need a TextGrid case, since the code in macstat.c puts
       in style info as characters are added. */
  }
}

void gli_window_set_hyperlink(window_t *win, glui32 linkval)
{
  win->linkid = linkval;
	
  switch (win->type) {
  case wintype_TextBuffer:
    win_textbuffer_set_style_link(win->data, win->linkid);
    break;
    /* Don't need a TextGrid case, since the code in macstat.c puts
       in style info as characters are added. */
  }
}

glui32 glk_image_draw(winid_t win, glui32 image, glsi32 val1, glsi32 val2)
{
  picture_t *pic;
  
  switch (win->type) {
  case wintype_TextBuffer:
    pic = picture_find(image);
    if (!pic)
      return FALSE;
    win_textbuffer_set_style_image(win->data, image, val1, 
      pic->width, pic->height);
    win_textbuffer_add(win->data, '*', -1);
    win_textbuffer_set_style_text(win->data, 0xFFFFFFFF);
    picture_release(pic);
    return TRUE;
  case wintype_Graphics:
    return win_graphics_draw_picture(win->data, image, val1, val2,
      FALSE, 0, 0);
  }
  
  return FALSE;
}

glui32 glk_image_draw_scaled(winid_t win, glui32 image, glsi32 val1, glsi32 val2,
  glui32 width, glui32 height)
{
  picture_t *pic;
  
  switch (win->type) {
  case wintype_TextBuffer:
    pic = picture_find(image);
    if (!pic)
      return FALSE;
    win_textbuffer_set_style_image(win->data, image, val1, width, height);
    win_textbuffer_add(win->data, '*', -1);
    win_textbuffer_set_style_text(win->data, 0xFFFFFFFF);
    picture_release(pic);
    return TRUE;
  case wintype_Graphics:
    return win_graphics_draw_picture(win->data, image, val1, val2, 
      TRUE, width, height);
  }
  
  return FALSE;
}

glui32 glk_image_get_info(glui32 image, glui32 *width, glui32 *height)
{
  picture_t *pic = picture_find(image);
  if (!pic)
    return FALSE;
  
  if (width)
    *width = pic->width;
  if (height)
    *height = pic->height;
  
  picture_release(pic);
  return TRUE;
}

void glk_window_flow_break(winid_t win)
{
  switch (win->type) {
  case wintype_TextBuffer:
    win_textbuffer_set_style_break(win->data);
    win_textbuffer_add(win->data, '*', -1);
    win_textbuffer_set_style_text(win->data, 0xFFFFFFFF);
    break;
  }
}

void glk_window_erase_rect(winid_t win, 
  glsi32 left, glsi32 top, glui32 width, glui32 height)
{
  if (!win) {
    gli_strict_warning("window_erase_rect: invalid ref");
    return;
  }
  if (win->type != wintype_Graphics) {
    gli_strict_warning("window_erase_rect: not a graphics window");
    return;
  }
  win_graphics_erase_rect(win->data, FALSE, left, top, width, height);
}

void glk_window_fill_rect(winid_t win, glui32 color, 
  glsi32 left, glsi32 top, glui32 width, glui32 height)
{
  if (!win) {
    gli_strict_warning("window_fill_rect: invalid ref");
    return;
  }
  if (win->type != wintype_Graphics) {
    gli_strict_warning("window_fill_rect: not a graphics window");
    return;
  }
  win_graphics_fill_rect(win->data, color, left, top, width, height);
}

void glk_window_set_background_color(winid_t win, glui32 color)
{
  if (!win) {
    gli_strict_warning("window_set_background_color: invalid ref");
    return;
  }
  if (win->type != wintype_Graphics) {
    gli_strict_warning("window_set_background_color: not a graphics window");
    return;
  }
  win_graphics_set_background_color(win->data, color);
}

/* ----- subwindow functions ---------- */

window_blank_t *win_blank_create(window_t *win)
{
  window_blank_t *dwin = (window_blank_t *)malloc(sizeof(window_blank_t));
  dwin->owner = win;
  
  return dwin;
}

void win_blank_destroy(window_blank_t *dwin)
{
  dwin->owner = NULL;
  free(dwin);
}

void win_blank_rearrange(window_t *win, XRectangle *box)
{
  window_blank_t *dwin = win->data;
  dwin->bbox = *box;
}

void win_blank_redraw(window_t *win)
{
  window_blank_t *dwin = win->data;
  gli_draw_window_outline(&dwin->bbox);
  
  XFillRectangle(xiodpy, xiowin, gctech, dwin->bbox.x, dwin->bbox.y, 
    dwin->bbox.width, dwin->bbox.height);
}

window_pair_t *win_pair_create(window_t *win, glui32 method, window_t *key, 
  glui32 size)
{
  window_pair_t *dwin = (window_pair_t *)malloc(sizeof(window_pair_t));
  dwin->owner = win;
  
  dwin->dir = method & winmethod_DirMask; 
  dwin->division = method & winmethod_DivisionMask;
  dwin->key = key;
  dwin->keydamage = FALSE;
  dwin->size = size;
  
  dwin->vertical = 
    (dwin->dir == winmethod_Left || dwin->dir == winmethod_Right);
  dwin->backward = 
    (dwin->dir == winmethod_Left || dwin->dir == winmethod_Above);
  
  dwin->child1 = NULL;
  dwin->child2 = NULL;
  
  return dwin;
}

void win_pair_destroy(window_pair_t *dwin)
{
  dwin->owner = NULL;
  /* We leave the children untouched, because gli_window_close takes care 
     of that if it's desired. */
  dwin->child1 = NULL;
  dwin->child2 = NULL;
  dwin->key = NULL;
  free(dwin);
}

void win_pair_rearrange(window_t *win, XRectangle *box)
{
  window_pair_t *dwin = win->data;
  XRectangle box1, box2;
  long min, diff, split, max;
  window_t *key;
  window_t *ch1, *ch2;

  dwin->bbox = *box;
  dwin->flat = FALSE;

  if (dwin->vertical) {
    min = dwin->bbox.x;
    max = dwin->bbox.x+dwin->bbox.width;
  }
  else {
    min = dwin->bbox.y;
    max = dwin->bbox.y+dwin->bbox.height;
  }
  diff = max-min;
  
  switch (dwin->division) {
  case winmethod_Proportional:
    split = (diff * dwin->size) / 100;
    break;
  case winmethod_Fixed:
    key = dwin->key;
    if (!key) {
      split = 0;
    }
    else {
      switch (key->type) {
      case wintype_TextBuffer:
	split = win_textbuffer_figure_size(key, dwin->size, dwin->vertical);
	break;
      case wintype_TextGrid:
	split = win_textgrid_figure_size(key, dwin->size, dwin->vertical);
	break;
      case wintype_Graphics:
	split = win_graphics_figure_size(key, dwin->size, dwin->vertical);
	break;
      default:
	split = 0;
	break;
      }
    }
    split += HALF_MATTE_WIDTH; /* extra space for split bar */
    break;
  default:
    split = diff / 2;
    break;
  }
  
  if (!dwin->backward) {
    split = diff-split;
  }
  else {
    split = 0+split;
  }
  
  if (split < (MATTE_WIDTH/2))
    split = (MATTE_WIDTH/2);
  else if (split > diff - (MATTE_WIDTH - (MATTE_WIDTH/2)))
    split = diff - (MATTE_WIDTH - (MATTE_WIDTH/2));

  if (diff < MATTE_WIDTH) {
    /* blow off the whole routine */
    dwin->flat = TRUE;
    split = diff / 2;
  }
  
  if (dwin->vertical) {
    dwin->splithgt = split;
    if (dwin->flat) {
      box1.x = dwin->bbox.x+split;
      box1.width = 0;
      box2.x = dwin->bbox.x+split;
      box2.width = 0;
    }
    else {
      box1.x = dwin->bbox.x;
      box1.width = split - (MATTE_WIDTH/2);
      box2.x = box1.x + box1.width + MATTE_WIDTH;
      box2.width = dwin->bbox.x+dwin->bbox.width - box2.x;
    }
    box1.y = dwin->bbox.y;
    box1.height = dwin->bbox.height;
    box2.y = dwin->bbox.y;
    box2.height = dwin->bbox.height;
    if (!dwin->backward) {
      ch1 = dwin->child1;
      ch2 = dwin->child2;
    }
    else {
      ch1 = dwin->child2;
      ch2 = dwin->child1;
    }
  }
  else {
    dwin->splithgt = split;
    if (dwin->flat) {
      box1.y = dwin->bbox.y+split;
      box1.height = 0;
      box2.y = dwin->bbox.y+split;
      box2.height = 0;
    }
    else {
      box1.y = dwin->bbox.y;
      box1.height = split - (MATTE_WIDTH/2);
      box2.y = box1.y+box1.height + MATTE_WIDTH;
      box2.height = dwin->bbox.y+dwin->bbox.height - box2.y;
    }
    box1.x = dwin->bbox.x;
    box1.width = dwin->bbox.width;
    box2.x = dwin->bbox.x;
    box2.width = dwin->bbox.width;
    if (!dwin->backward) {
      ch1 = dwin->child1;
      ch2 = dwin->child2;
    }
    else {
      ch1 = dwin->child2;
      ch2 = dwin->child1;
    }
  }
  
  gli_window_rearrange(ch1, &box1);
  gli_window_rearrange(ch2, &box2);
}

void win_pair_redraw(window_t *win)
{
  XRectangle box;
  window_pair_t *dwin;
  
  if (!win)
    return;
  
  dwin = win->data;

  if (xiodepth > 1) {
    if (dwin->vertical) {
      box.y = dwin->bbox.y - MATTE_WIDTH/2;
      box.height = dwin->bbox.height + MATTE_WIDTH;
      box.x = dwin->bbox.x + dwin->splithgt - (MATTE_WIDTH/2);
      box.width = MATTE_WIDTH;
    }
    else {
      box.x = dwin->bbox.x - MATTE_WIDTH/2;
      box.width = dwin->bbox.width + MATTE_WIDTH;
      box.y = dwin->bbox.y + dwin->splithgt - (MATTE_WIDTH/2);
      box.height = MATTE_WIDTH;
    }
    XFillRectangle(xiodpy, xiowin, gctech, box.x, box.y, 
      box.width, box.height);
  }
  
  gli_window_redraw(dwin->child1);
  gli_window_redraw(dwin->child2);
}

void xgc_movecursor(window_t *win, int op)
{
  if (!win) {
    if (xmsg_msgmode == xmsg_mode_Line)
      xgc_msg_movecursor(op);
    else
      xmsg_set_message("invalid window for cursor movement", FALSE);
    return;
  }

  switch (win->type) {
  case wintype_TextBuffer:
    xgc_buf_movecursor(win->data, op);
    break;
  case wintype_TextGrid:
    xgc_grid_movecursor(win->data, op);
    break;
  default:
    xmsg_set_message("invalid window for cursor movement", FALSE);
    break;
  }
}

void xgc_scroll(window_t *win, int op)
{
  switch (win->type) {
  case wintype_TextBuffer:
    xgc_buf_scroll(win->data, op);
    break;
  default:
    xmsg_set_message("invalid window for scrolling", FALSE);
    break;
  }
}

void xgc_scrollto(window_t *win, int op)
{
  switch (win->type) {
  case wintype_TextBuffer:
    xgc_buf_scrollto(win->data, op);
    break;
  default:
    xmsg_set_message("invalid window for scrolling", FALSE);
    break;
  }
}

void xgc_insert(window_t *win, int op)
{
  if (!win) {
    if (xmsg_msgmode == xmsg_mode_Line)
      xgc_msg_insert(op);
    else
      xmsg_set_message("invalid window for inserting", FALSE);
    return;
  }

  switch (win->type) {
  case wintype_TextBuffer:
    xgc_buf_insert(win->data, op);
    break;
  case wintype_TextGrid:
    xgc_grid_insert(win->data, op);
    break;
  default:
    xmsg_set_message("invalid window for inserting", FALSE);
    break;
  }
}

void xgc_delete(window_t *win, int op)
{
  if (!win) {
    if (xmsg_msgmode == xmsg_mode_Line)
      xgc_msg_delete(op);
    else
      xmsg_set_message("invalid window for deleting", FALSE);
    return;
  }

  switch (win->type) {
  case wintype_TextBuffer:
    xgc_buf_delete(win->data, op);
    break;
  case wintype_TextGrid:
    xgc_grid_delete(win->data, op);
    break;
  default:
    xmsg_set_message("invalid window for deleting", FALSE);
    break;
  }
}

void xgc_getchar(window_t *win, int op)
{
  if (!win) {
    if (xmsg_msgmode == xmsg_mode_Char)
      xgc_msg_getchar(op);
    else
      xmsg_set_message("invalid window for key entry", FALSE);
    return;
  }

  switch (win->type) {
  case wintype_TextBuffer:
    xgc_buf_getchar(win->data, op);
    break;
  case wintype_TextGrid:
    xgc_grid_getchar(win->data, op);
    break;
  default:
    xmsg_set_message("invalid window for key entry", FALSE);
    break;
  }
}

void xgc_enter(window_t *win, int op)
{
  if (!win) {
    if (xmsg_msgmode == xmsg_mode_Line)
      xgc_msg_enter(op);
    else
      xmsg_set_message("invalid window for line entry", FALSE);
    return;
  }

  switch (win->type) {
  case wintype_TextBuffer:
    xgc_buf_enter(win->data, op);
    break;
  case wintype_TextGrid:
    xgc_grid_enter(win->data, op);
    break;
  default:
    xmsg_set_message("invalid window for line entry", FALSE);
    break;
  }
}

void xgc_cutbuf(window_t *win, int op)
{
  switch (win->type) {
  case wintype_TextBuffer:
    xgc_buf_cutbuf(win->data, op);
    break;
  case wintype_TextGrid:
    xgc_grid_cutbuf(win->data, op);
    break;
  default:
    xmsg_set_message("invalid window for cut/paste", FALSE);
    break;
  }
}

void xgc_history(window_t *win, int op)
{
  switch (win->type) {
  case wintype_TextBuffer:
    xgc_buf_history(win->data, op);
    break;
  default:
    xmsg_set_message("invalid window for command history", FALSE);
    break;
  }
}

