#ifndef _XG_INTERNAL_H
#define _XG_INTERNAL_H

#include <X11/X.h>
#include "gi_dispa.h"

/* --- General declarations --- */

#define gli_strict_warning(msg)    \
  (fprintf(stderr, "XGlk library error: %s\n", (msg)))

typedef struct glk_window_struct window_t;
typedef struct glk_stream_struct stream_t;
typedef struct glk_fileref_struct fileref_t;
typedef struct stylehints_struct stylehints_t;

extern int gli_special_typable_table[keycode_MAXVAL+1];
extern int gli_just_killed;

extern gidispatch_rock_t (*gli_register_obj)(void *obj, glui32 objclass);
extern void (*gli_unregister_obj)(void *obj, glui32 objclass, 
  gidispatch_rock_t objrock);
extern gidispatch_rock_t (*gli_register_arr)(void *array, glui32 len, 
  char *typecode);
extern void (*gli_unregister_arr)(void *array, glui32 len, char *typecode, 
  gidispatch_rock_t objrock);

extern int init_gli_misc(void);
extern void gli_fast_exit(void);

/* --- Windows --- */

#define MATTE_WIDTH (6) 
/* This *includes* the black outline, even though that's
   drawn by the window and not by the parent Pair. 
   Also means that the matte starts one pixel outside the window. */
#define HALF_MATTE_WIDTH (MATTE_WIDTH - (MATTE_WIDTH/2))
/* Rounded up, you notice */

struct glk_window_struct {
  glui32 type;
  
  window_t *parent; 
  int mouse_request, char_request, line_request, hyperlink_request;
  glui32 style, linkid;
  
  void *data; /* window_pair_t, etc */
  stream_t *str;
  stream_t *echostr;
  
  glui32 rock;
  
  gidispatch_rock_t disprock;
  window_t *chain_next, *chain_prev;
};

extern window_t *gli_rootwin;
extern window_t *gli_focuswin;

extern int init_gli_windows(void);
extern window_t *gli_window_fixiterate(window_t *win);
extern void gli_window_rearrange(window_t *win, XRectangle *box);
extern void gli_window_redraw(window_t *win);
extern void gli_window_perform_click(window_t *win, int dir, XPoint *pt, 
  int butnum, int clicknum, unsigned int state);
extern window_t *gli_find_window_by_point(window_t *win, XPoint *pt);
extern XRectangle *gli_window_get_rect(window_t *win);
extern fontset_t *gli_window_get_fontset(window_t *win);
extern void gli_set_focus(window_t *win);
extern void gli_windows_flush(void);
extern void gli_windows_set_paging(int forcetoend);
extern void gli_windows_trim_buffers(void);

extern void gli_window_put_char(window_t *win, unsigned char ch);
extern void gli_window_set_style(window_t *win, glui32 val);
extern void gli_window_set_hyperlink(window_t *win, glui32 linkval);

/* --- Streams --- */

#define strtype_Memory (1)
#define strtype_File (2)
#define strtype_Window (3)

struct glk_stream_struct {
  glui32 type;

  int readable, writable;
  glui32 readcount, writecount;
  
  /* for memory */
  unsigned char *buf;
  unsigned char *bufptr;
  unsigned char *bufend;
  unsigned char *bufeof;
  glui32 buflen;
  gidispatch_rock_t arrayrock;
  
  /* for a window */
  window_t *win;
  
  /* for a file */
  FILE *file;
  
  glui32 rock;
  
  gidispatch_rock_t disprock;
  stream_t *chain_next, *chain_prev;
};

extern int init_gli_streams(void);
extern stream_t *gli_stream_open_window(window_t *win);
extern strid_t gli_stream_open_pathname(char *pathname, int textmode, 
  glui32 rock);
extern void gli_stream_close(stream_t *str);
extern void gli_streams_close_all(void);
extern void gli_stream_fill_result(stream_t *str, stream_result_t *result);
extern void gli_stream_echo_line(stream_t *str, char *buf, glui32 len);

/* --- Filerefs --- */

struct glk_fileref_struct {
  char *filename;
  int filetype;
  int textmode;

  glui32 rock;
  
  gidispatch_rock_t disprock;
  fileref_t *chain_next, *chain_prev;
};

extern int init_gli_filerefs(void);

/* --- Styles --- */

typedef struct styleval_struct {
  int type;
  glsi32 val;
} styleval_t;

typedef struct stylehint_struct {
  glui32 setflags;
  styleval_t *vals;
  int length;
  int size;
} styleonehint_t;

struct stylehints_struct {
  glui32 type; /* which wintype prefs is this associated with? */
  styleonehint_t style[style_NUMSTYLES];
};

extern int init_gli_styles(void);
extern int gli_stylehints_for_window(glui32 wintype, stylehints_t *hints);
extern void gli_styles_compute(fontset_t *font, stylehints_t *hints);

/* --- Events --- */

extern void eventloop_setevent(glui32 type, window_t *win, 
  glui32 val1, glui32 val2); 
extern glui32 eventloop_isevent(void); 

#define gli_event_clearevent(evp)  \
((evp)->type = evtype_None,    \
 (evp)->win = 0,    \
 (evp)->val1 = 0,   \
 (evp)->val2 = 0)

#endif /* _XG_INTERNAL_H */
