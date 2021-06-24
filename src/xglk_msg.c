#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include "xglk.h"
#include "xg_internal.h"

static XRectangle bbox;

#define TIMEOUT (5)

static char *message = NULL;
static int messagelen, message_size;
static int messagesticky;
static struct timeval messagetime;

int xmsg_msgmode = xmsg_mode_None;
static unsigned char keygot;
static char *linebuf;
static int linemaxlen;
static int linepos, linelen;
static int editx, dotx;

static void redrawbuf(void);
static void adjustdot(int visible);

int init_xmsg()
{
  message_size = 80;
  message = (char *)malloc(message_size * sizeof(char));
  if (!message)
    return FALSE;

  sprintf(message, "Welcome to %s library, version %s.", 
    LIBRARYNAME, LIBRARYVERSION);
  messagelen = strlen(message);
  messagesticky = FALSE;
  gettimeofday(&messagetime, NULL);
  messagetime.tv_sec += TIMEOUT;

  return TRUE;
}

void xmsg_resize(int x, int y, int wid, int hgt)
{
  /* No drawing here. */
  bbox.x = x;
  bbox.y = y;
  bbox.width = wid;
  bbox.height = hgt;
}

void xmsg_redraw()
{
  XFillRectangle(xiodpy, xiowin, gcback, 
    bbox.x, bbox.y, bbox.width, bbox.height);
  if (message && messagelen) {
    xglk_draw_string(&(plainfonts.gc[0]), FALSE, 0,
      bbox.x+4, bbox.y+plainfonts.lineoff+2, 
      message, messagelen);
  }

  if (xmsg_msgmode == xmsg_mode_Line) {
    xglk_draw_string(&(plainfonts.gc[0]), FALSE, 0,
      editx, bbox.y+plainfonts.lineoff+2,
      linebuf, linelen);
    dotx = -1;
    adjustdot(TRUE);
  }
}

static void redrawbuf()
{
  xglk_clearfor_string(&(plainfonts.gc[0].backcolor),
    editx-4, bbox.y, bbox.x+bbox.width-editx+4, bbox.height);

  xglk_draw_string(&(plainfonts.gc[0]), FALSE, 0,
    editx, bbox.y+plainfonts.lineoff+2,
    linebuf, linelen);

  dotx = -1;
  adjustdot(TRUE);
}

static void adjustdot(int visible)
{
  if (dotx >= 0)
    xglk_draw_dot(dotx, bbox.y+plainfonts.lineoff+2, 
      bbox.y+plainfonts.lineoff);

  if (visible) {
    dotx = editx
      + XTextWidth(plainfonts.gc[0].fontstr, linebuf, linepos);
  }
  else {
    dotx = -1;
  }

  if (dotx >= 0)
    xglk_draw_dot(dotx, bbox.y+plainfonts.lineoff+2, 
      bbox.y+plainfonts.lineoff);
}

void xmsg_set_message(char *str, int sticky)
{
  if (!message)
    return;
  if (!str)
    str = "";
  messagelen = strlen(str);
  if (messagelen >= message_size) {
    while (messagelen >= message_size)
      message_size *= 2;
    message = (char *)realloc(message, message_size * sizeof(char));
  }
  strcpy(message, str);

  xmsg_redraw();

  messagesticky = sticky;
  gettimeofday(&messagetime, NULL);
  messagetime.tv_sec += TIMEOUT;
}

void xmsg_check_timeout()
{
  struct timeval tv;

  if (messagesticky)
    return;

  gettimeofday(&tv, NULL);
  if (tv.tv_sec > messagetime.tv_sec
    || (tv.tv_sec == messagetime.tv_sec && tv.tv_usec > messagetime.tv_usec)) {
    xmsg_set_message(NULL, TRUE);
  }
}

int xmsg_getline(char *prompt, char *buf, int maxlen, int *length)
{
  event_t ev;

  xmsg_set_message(prompt, TRUE);

  editx = bbox.x + 10 
    + XTextWidth(plainfonts.gc[0].fontstr, message, messagelen);

  xmsg_msgmode = xmsg_mode_Line;
  keygot = FALSE;
  linebuf = buf;
  linemaxlen = maxlen;
  linelen = *length;
  linepos = *length;

  dotx = -1;
  redrawbuf();

  do {
    glk_select(&ev);
  } while (ev.type != -1);

  adjustdot(FALSE);

  xmsg_msgmode = xmsg_mode_None;

  if (keygot) {
    *length = linelen;
    return TRUE;
  }
  else {
    return FALSE;
  }
}

int xmsg_getchar(char *prompt)
{
  event_t ev;

  xmsg_set_message(prompt, TRUE);

  xmsg_msgmode = xmsg_mode_Char;
  keygot = '\0';

  do {
    glk_select(&ev);
  } while (ev.type != -1);

  xmsg_msgmode = xmsg_mode_None;
  return keygot;
}

void xgc_msg_getchar(int op)
{
  keygot = op;
  eventloop_setevent(-1, 0, 0, 0);
}

void xgc_msg_enter(int op)
{
  keygot = TRUE;
  eventloop_setevent(-1, 0, 0, 0);
}

void xgc_msg_insert(int op)
{
  if (linelen < linemaxlen) {
    if (linepos < linelen)
      memmove(linebuf+linepos+1, linebuf+linepos, linelen-linepos);
    linelen++;
    linebuf[linepos] = op;
    linepos++;
    redrawbuf();
  }
}

void xgc_msg_delete(int op)
{
  switch (op) {
  case op_BackChar:
    if (linepos < 1)
      break;
    if (linepos < linelen)
      memmove(linebuf+linepos-1, linebuf+linepos, linelen-linepos);
    linelen--;
    linepos--;
    redrawbuf();
    break;
  case op_ForeChar:
    if (linepos >= linelen)
      break;
    if (linepos+1 < linelen)
      memmove(linebuf+linepos, linebuf+linepos+1, linelen-(linepos+1));
    linelen--;
    redrawbuf();
    break;
  }
}

void xgc_msg_movecursor(int op)
{
  switch (op) {
  case op_BackChar:
    if (linepos < 1)
      break;
    linepos--;
    adjustdot(TRUE);
    break;
  case op_ForeChar:
    if (linepos >= linelen)
      break;
    linepos++;
    adjustdot(TRUE);
    break;
  case op_BeginLine:
    if (linepos < 1)
      break;
    linepos = 0;
    adjustdot(TRUE);
    break;
  case op_EndLine:
    if (linepos >= linelen)
      break;
    linepos = linelen;
    adjustdot(TRUE);
    break;
  }
}

