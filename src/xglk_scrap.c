#include "xglk.h"
#include <stdlib.h>
#include <X11/Xatom.h>
#include "xg_internal.h"
#include <string.h>

static char *scrap = NULL;
static int scraplen = 0;

void xglk_store_scrap(char *str, long len)
{
  if (!str || !len)
    return;

  if (scrap) {
    free(scrap);
    scrap = NULL;
    scraplen = 0;
  }

  scrap = (char *)malloc(sizeof(char) * len);
  if (scrap) {
    memcpy(scrap, str, sizeof(char) * len);
    scraplen = len;
  }

  XSetSelectionOwner(xiodpy, XA_PRIMARY, xiowin, CurrentTime);
}

void xglk_clear_scrap()
{
  if (scrap) {
    free(scrap);
    scrap = NULL;
    scraplen = 0;
  }  
}

static Bool notifyeventsplot(Display *dpy, XEvent *ev, char *rock)
{
  return (ev->type == SelectionNotify);
}

void xglk_fetch_scrap(char **str, long *len)
{
  char *cx;

  *str = NULL;
  *len = 0;

  if (!scrap || !scraplen) {
    int eventp;
    XEvent xev;
    int res;
    Atom atype;
    int aformat;
    unsigned long nitems, after;
    unsigned char *ptr;

    XConvertSelection(xiodpy, XA_PRIMARY, XA_STRING, XA_STRING,
      xiowin, CurrentTime);
    XIfEvent(xiodpy, &xev, notifyeventsplot, NULL);
    if (xev.type == SelectionNotify
      && xev.xselection.property != None) {
      res = XGetWindowProperty(xiodpy, xiowin, xev.xselection.property,
	0, 1000, FALSE, XA_STRING,
	&atype, &aformat, &nitems, &after, &ptr);
      if (res == Success
	&& ptr && nitems > 0 && aformat == 8) {
	cx = (char *)malloc(sizeof(char) * nitems);
	memcpy(cx, ptr, sizeof(char) * nitems);
	*str = cx;
	*len = nitems;
	XFree(ptr);
      }
    }
    return;
  }

  cx = (char *)malloc(sizeof(char) * scraplen);
  if (cx) {
    memcpy(cx, scrap, sizeof(char) * scraplen);
    *str = cx;
    *len = scraplen;
  }
}

void xglk_strip_garbage(char *buf, long len)
{
  long ix;
  unsigned char ch;

  if (!buf)
    return;
  
  for (ix=0; ix<len; ix++, buf++) {
    ch = *buf;
    if (ch < 32 || (ch >= 127 && ch < 160))
      *buf = ' ';
  }
}

