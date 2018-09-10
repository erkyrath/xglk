#include <stdlib.h>
#include <signal.h>
#include <X11/keysym.h>
#include "xglk.h"
#include "xg_internal.h"

static void (*gli_interrupt_handler)(void) = NULL;

static unsigned char char_tolower_table[256];
static unsigned char char_toupper_table[256];
int gli_special_typable_table[keycode_MAXVAL+1];

gidispatch_rock_t (*gli_register_obj)(void *obj, glui32 objclass) = NULL;
void (*gli_unregister_obj)(void *obj, glui32 objclass, 
  gidispatch_rock_t objrock) = NULL;
gidispatch_rock_t (*gli_register_arr)(void *array, glui32 len, 
  char *typecode) = NULL;
void (*gli_unregister_arr)(void *array, glui32 len, char *typecode, 
  gidispatch_rock_t objrock) = NULL;

static void gli_sig_interrupt(int val);
int gli_just_killed;

int init_gli_misc()
{
  int ix;
  glui32 res;

  gli_just_killed = FALSE;
  signal(SIGHUP, &gli_sig_interrupt);
  signal(SIGINT, &gli_sig_interrupt);

  for (ix=0; ix<256; ix++) {
    char_toupper_table[ix] = ix;
    char_tolower_table[ix] = ix;
  }
  for (ix=0; ix<256; ix++) {
    if (ix >= 'A' && ix <= 'Z') {
      res = ix + ('a' - 'A');
    }
    else if (ix >= 0xC0 && ix <= 0xDE && ix != 0xD7) {
      res = ix + 0x20;
    }
    else {
      res = 0;
    }
    if (res) {
      char_tolower_table[ix] = res;
      char_toupper_table[res] = ix;
    }
  }

  for (ix=0; ix<=keycode_MAXVAL; ix++) {
    res = FALSE;
    switch ((glui32)(0L-ix)) {
    case keycode_Left:
      res = (XKeysymToKeycode(xiodpy, XK_Left) != 0);
      break;
    case keycode_Right:
      res = (XKeysymToKeycode(xiodpy, XK_Right) != 0);
      break;
    case keycode_Up:
      res = (XKeysymToKeycode(xiodpy, XK_Up) != 0);
      break;
    case keycode_Down:
      res = (XKeysymToKeycode(xiodpy, XK_Down) != 0);
      break;
    case keycode_Return:
    case keycode_Delete:
    case keycode_Escape:
      res = TRUE;
      break;
    case keycode_Tab:
      res = FALSE;
      break;
    case keycode_PageUp:
      res = (XKeysymToKeycode(xiodpy, XK_Page_Up) != 0);
      break;
    case keycode_PageDown:
      res = (XKeysymToKeycode(xiodpy, XK_Page_Down) != 0);
      break;
    case keycode_Home:
      res = (XKeysymToKeycode(xiodpy, XK_Home) != 0);
      break;
    case keycode_End:
      res = (XKeysymToKeycode(xiodpy, XK_End) != 0);
      break;
    }
    gli_special_typable_table[ix] = res;
  }

  return TRUE;
}

void glk_set_interrupt_handler(void (*func)(void))
{
  gli_interrupt_handler = func;
}

/* Signal handler for SIGINT. */
static void gli_sig_interrupt(int val)
{
  gli_just_killed = TRUE;
}

void glk_exit()
{
  event_t ev;

  xmsg_getchar("Hit any key to exit.");

  exit(1);
}

void gli_fast_exit()
{
  void (*func)(void);

  func = gli_interrupt_handler;
  gli_interrupt_handler = NULL;

  if (func) {
    (*func)();
  }

  exit(1);
}

void gidispatch_set_object_registry(
  gidispatch_rock_t (*reg)(void *obj, glui32 objclass), 
  void (*unreg)(void *obj, glui32 objclass, gidispatch_rock_t objrock))
{
  window_t *win;
  stream_t *str;
  fileref_t *fref;
  
  gli_register_obj = reg;
  gli_unregister_obj = unreg;
  
  if (gli_register_obj) {
    /* It's now necessary to go through all existing objects, and register
       them. */
    for (win = glk_window_iterate(NULL, NULL); 
	 win;
	 win = glk_window_iterate(win, NULL)) {
      win->disprock = (*gli_register_obj)(win, gidisp_Class_Window);
    }
    for (str = glk_stream_iterate(NULL, NULL); 
	 str;
	 str = glk_stream_iterate(str, NULL)) {
      str->disprock = (*gli_register_obj)(str, gidisp_Class_Stream);
    }
    for (fref = glk_fileref_iterate(NULL, NULL); 
	 fref;
	 fref = glk_fileref_iterate(fref, NULL)) {
      fref->disprock = (*gli_register_obj)(fref, gidisp_Class_Fileref);
    }
  }
}

void gidispatch_set_retained_registry(
  gidispatch_rock_t (*reg)(void *array, glui32 len, char *typecode), 
  void (*unreg)(void *array, glui32 len, char *typecode, 
    gidispatch_rock_t objrock))
{
  gli_register_arr = reg;
  gli_unregister_arr = unreg;
}

gidispatch_rock_t gidispatch_get_objrock(void *obj, glui32 objclass)
{
  switch (objclass) {
  case gidisp_Class_Window:
    return ((window_t *)obj)->disprock;
  case gidisp_Class_Stream:
    return ((stream_t *)obj)->disprock;
  case gidisp_Class_Fileref:
    return ((fileref_t *)obj)->disprock;
  default: {
      gidispatch_rock_t dummy;
      dummy.num = 0;
      return dummy;
    }
  }
}

unsigned char glk_char_to_lower(unsigned char ch)
{
  return char_tolower_table[ch];
}

unsigned char glk_char_to_upper(unsigned char ch)
{
  return char_toupper_table[ch];
}


