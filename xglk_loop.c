#include <sys/time.h>
#include <stdlib.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

#include "glk.h"
#include "xglk.h"
#include "xg_internal.h"

/* This is the granularity at which we check for timer events; measured
   in microseconds. 1/20 second sounds good. */
#define TICKLENGTH (50000)

static event_t *eventloop_event = NULL;
static struct timeval lasttime = {0, 0};

void xglk_event_poll(event_t *ev, glui32 millisec)
{
  struct timeval tv, curtime, outtime;
  struct timezone tz;
  /* just check for a timer event, nothing else. */
  
  eventloop_event = ev;

  if (millisec) {
    if (lasttime.tv_sec == 0)
      gettimeofday(&lasttime, &tz);
    outtime.tv_sec = lasttime.tv_sec + (millisec/1000);
    outtime.tv_usec = lasttime.tv_usec + ((millisec%1000)*1000);
    if (outtime.tv_usec >= 1000000) {
      outtime.tv_sec++;
      outtime.tv_usec -= 1000000;
    }
  }

  if (millisec) {
    gettimeofday(&curtime, &tz);
    if (curtime.tv_sec > outtime.tv_sec 
      || (curtime.tv_sec == outtime.tv_sec 
	&& curtime.tv_usec > outtime.tv_usec)) {
      lasttime = curtime;
      eventloop_setevent(evtype_Timer, NULL, 0, 0);
    }
  }

  eventloop_event = NULL;
}

static Bool alleventsplot(Display *dpy, XEvent *ev, char *rock)
{
  return TRUE;
}

void xglk_event_loop(event_t *ev, glui32 millisec)
{
  XEvent xev;
  KeySym ksym;
  int ix, val;
  XPoint pt;
  char ch;
  int eventp;
  int firsttime = TRUE;
  struct timeval tv, curtime, outtime;
  struct timezone tz;

  eventloop_event = ev;
  xglk_perform_click(mouse_Reset, NULL, 0, 0);

  if (millisec) {
    if (lasttime.tv_sec == 0)
      gettimeofday(&lasttime, &tz);
    outtime.tv_sec = lasttime.tv_sec + (millisec/1000);
    outtime.tv_usec = lasttime.tv_usec + ((millisec%1000)*1000);
    if (outtime.tv_usec >= 1000000) {
      outtime.tv_sec++;
      outtime.tv_usec -= 1000000;
    }
  }

  while (ev->type == evtype_None) {

    if (xio_any_invalid) {
      xglk_redraw();
    }

    if (millisec && !firsttime) {
      gettimeofday(&curtime, &tz);
      if (curtime.tv_sec > outtime.tv_sec 
	|| (curtime.tv_sec == outtime.tv_sec 
	  && curtime.tv_usec > outtime.tv_usec)) {
	lasttime = curtime;
	eventloop_setevent(evtype_Timer, NULL, 0, 0);
	continue;
      }
    }
    firsttime = FALSE;

    /*
    eventp = XCheckMaskEvent(xiodpy, ~(NoEventMask), &xev); 
    if (!eventp)
    eventp = XCheckTypedEvent(xiodpy, SelectionClear, &xev);*/

    eventp = XCheckIfEvent(xiodpy, &xev, &alleventsplot, NULL);
    
    if (gli_just_killed) {
      gli_fast_exit();
      continue;
    }

    if (!eventp) {
      struct timeval tv;
      fd_set readbits;

      /* Wait for some activity on the X connection. */      
      tv.tv_sec = 0;
      tv.tv_usec = TICKLENGTH;
      FD_ZERO(&readbits);
      FD_SET(ConnectionNumber(xiodpy), &readbits);
      XFlush(xiodpy);
      select(1+ConnectionNumber(xiodpy), &readbits, 0, 0, &tv);

      if (gli_just_killed) {
	gli_fast_exit();
      }
      continue;
    }

    if (xev.xany.window != xiowin) {
      continue;
    }

    switch (xev.type) {

    case KeyPress:
      xmsg_check_timeout();
      ix = XLookupString(&xev.xkey, &ch, 1, &ksym, NULL);
      if (IsModifierKey(ksym) || ksym==XK_Multi_key) {
	break;
      }
      if (ksym >= 0x0000 && ksym < 0x00ff) {
	if (xev.xkey.state & ControlMask) {
	  if (ksym >= 'A' && ksym <= '_')
	    val = ksym - 'A' + 1;
	  else if (ksym >= 'a' && ksym <= '~')
	    val = ksym - 'a' + 1;
	  else if (ksym == '@' || ksym == ' ')
	    val = '\0';
	  else
	    val = ksym;
	}
	else {
	  val = ksym;
	}
	if (xev.xkey.state & Mod1Mask) {
	  val |= 0x200;
	}
      }
      else if (ksym >= 0xff00 && ksym <= 0xffff) {
	val = 0x100 | (ksym & 0xff);
      }
      else {
	break;
      }
      xkey_perform_key(val, xev.xkey.state);
      break;

    case ButtonPress:
      xmsg_check_timeout();
      pt.x = xev.xbutton.x;
      pt.y = xev.xbutton.y;
      xglk_perform_click(mouse_Down, &pt,
	xev.xbutton.button, xev.xbutton.state);
      break;

    case MotionNotify:
      do {
	ix = XCheckWindowEvent(xiodpy, xiowin, ButtonMotionMask, &xev);
      } while (ix); 
      pt.x = xev.xbutton.x;
      pt.y = xev.xbutton.y;
      xglk_perform_click(mouse_Move, &pt,
	xev.xbutton.button, xev.xbutton.state);
      break;

    case ButtonRelease:
      pt.x = xev.xbutton.x;
      pt.y = xev.xbutton.y;
      xglk_perform_click(mouse_Up, &pt,
	xev.xbutton.button, xev.xbutton.state);
      break;
      
    case ConfigureNotify:
      if (xev.xconfigure.width != xio_wid 
	|| xev.xconfigure.height != xio_hgt) {
	xio_wid = xev.xconfigure.width;
	xio_hgt = xev.xconfigure.height;
	xglk_arrange_window();
      }
      break;

    case SelectionRequest: {
      XSelectionRequestEvent *req = &(xev.xselectionrequest);
      XEvent xevnew;
      XSelectionEvent *not = &(xevnew.xselection);
      char *cx;
      long len;

      /*printf("### SelectionRequest %s %s %s\n",
	XGetAtomName(xiodpy, req->selection),
	XGetAtomName(xiodpy, req->target),
	XGetAtomName(xiodpy, req->property));*/

      xglk_fetch_scrap(&cx, &len);
      if (cx && req->target == XA_STRING) {
	not->property = req->property;
	/*printf("### XChangeProperty win %lx; %s %s %d %d %c... %ld\n",
	  req->requestor, 
	  XGetAtomName(xiodpy, req->property),
	  XGetAtomName(xiodpy, req->target),
	  8, PropModeReplace, *cx, len);*/
	val = XChangeProperty(xiodpy, req->requestor, req->property,
	  req->target, 8, PropModeReplace, cx, len);
	/*printf("### XChangeProperty got %d\n", val);*/
      }
      else {
	not->property = None;
	/*printf("### No XChangeProperty\n");*/
      }
      xevnew.type = SelectionNotify;
      not->display = xiodpy;
      not->time = CurrentTime;
      not->send_event = True;
      not->requestor = req->requestor;
      not->selection = req->selection;
      not->target = req->target;
      XSendEvent(xiodpy, req->requestor, TRUE, 0, &xevnew);
      if (cx) {
	free(cx);
      }
      break;
    }

    case SelectionClear:
      /*printf("### SelectionClear event\n");*/
      xglk_clear_scrap();
      break;

    case Expose:
      do {
	ix = XCheckWindowEvent(xiodpy, xiowin, ExposureMask, &xev);
      } while (ix);
      xglk_invalidate(NULL);
      break;

    default:
      break;
    }

  }

  /* If any mouse-up routine did real work, we'd have to call it here. 
     Since none does, we'll hold off for now. */

  eventloop_event = NULL;
}

void eventloop_setevent(glui32 evtype, window_t *win, glui32 val1, glui32 val2)
{
  if (eventloop_event) {
    eventloop_event->type = evtype;
    eventloop_event->win = win;
    eventloop_event->val1 = val1;
    eventloop_event->val2 = val2;
  }
}

glui32 eventloop_isevent()
{
  if (eventloop_event) 
    return eventloop_event->type;
  else
    return 0;
}

