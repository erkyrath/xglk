#ifndef _XGLK_H
#define _XGLK_H

#include "xglk_option.h"
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "glk.h"
#include "glkstart.h"

#define LIBRARYNAME "XGlk"
#define LIBRARYVERSION "0.4.11"

/* We define our own TRUE and FALSE and NULL, because ANSI
    is a strange world. */
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL 0
#endif

#define mouse_Reset (0)
#define mouse_Down (1)
#define mouse_Move (2)
#define mouse_Up (3)

#define xmsg_mode_None (0)
#define xmsg_mode_Char (1)
#define xmsg_mode_Line (2)

#define PackRGBColor(xcol)   \
   ((((glui32)(xcol)->red << 8) & 0x00FF0000)   \
  | (((glui32)(xcol)->green & 0x0000FF00))      \
  | (((glui32)(xcol)->blue >> 8) & 0x000000FF))

#define UnpackRGBColor(xcol, ui)  \
  (((xcol)->blue = ((ui) & 0x000000FF) * 0x101),   \
   ((xcol)->green = (((ui) >> 8) & 0x000000FF) * 0x101),   \
   ((xcol)->red = (((ui) >> 16) & 0x000000FF) * 0x101))

struct glk_window_struct;
typedef void (*cmdfunc_ptr)(struct glk_window_struct *win, int operand);

typedef struct cmdentry_struct {
  cmdfunc_ptr func;
  int operand;
  int ignoremods;
  char *name;
} cmdentry_t;
  
typedef struct fontnamespec_struct fontnamespec_t;

typedef struct fontprefs_struct {
  char *specname;
  fontnamespec_t *spec;
  int size, weight, oblique, proportional; /* all zero-based */
  int justify; /* 0:left, 1:full, 2:center, 3:right */
  int baseindent;
  int parindent;
  XColor forecolor;
  XColor linkcolor;
  XColor backcolor;
} fontprefs_t;

typedef struct winprefs_struct {
  int marginx, marginy; 
  int leading; 
  XColor forecolor;
  XColor linkcolor;
  XColor backcolor;
  int sizehint, fixedhint, attribhint, justhint, indenthint, colorhint;
  fontprefs_t style[style_NUMSTYLES];
} winprefs_t;

typedef struct preferences_struct {
  int win_x, win_y;
  int win_w, win_h;
  XColor forecolor;
  XColor linkcolor;
  XColor backcolor;
  XColor techcolor, techucolor, techdcolor, selectcolor;
  int ditherimages;
  int underlinelinks, colorlinks;

  winprefs_t textbuffer;
  winprefs_t textgrid;

  long buffersize;
  long bufferslack;

  int historylength;
  int prompt_defaults;
} preferences_t;

typedef struct fontref_struct {
  char *specname;
  int justify;
  int indent;
  int parindent;
  int ascent, descent;
  int underliney;
  int spacewidth;
  XColor forecolor;
  XColor linkcolor;
  XColor backcolor;
  XFontStruct *fontstr;
} fontref_t;

typedef struct fontset_struct {
  fontref_t gc[style_NUMSTYLES];
  int lineheight;
  int lineoff;
  int baseindent;
  XColor forecolor;
  XColor linkcolor;
  XColor backcolor;
} fontset_t;

typedef struct wegscroll_struct {
  void *rock;
  cmdfunc_ptr scrollfunc;
  cmdfunc_ptr scrolltofunc;

  XRectangle box;
  int vistop, visbot;

  int drag_scrollmode; /* 0 for click in elevator; 1 for dragged in elevator; 
			  2 for endzones; 3 for click in background */
  int drag_hitypos; 
  long drag_origline;
} wegscroll_t;

typedef struct picture_struct {
  unsigned long id;
  XImage *gimp;
  long width, height; /* natural bounds */
  Pixmap pix;
  long pixwidth, pixheight; /* pixmap's bounds */
  int refcount;
  struct picture_struct *hash_next;
} picture_t;

extern Display *xiodpy;
extern Colormap xiomap;
extern int xioscn;
extern int xiodepth;
extern int xiobackstore;
extern Window xiowin;
extern unsigned char *pixelcube;
extern int imageslegal;
extern GC gcfore, gcback, gctech, gctechu, gctechd, gcselect, gcflip;
extern GC gctextfore, gctextback;
extern Font textforefont;
extern unsigned long textforepixel, textbackpixel;
extern fontset_t plainfonts;
extern int xio_wid, xio_hgt;
extern XRectangle matte_box;
extern int xio_any_invalid;
extern int xmsg_msgmode;
extern preferences_t prefs;

extern int xglk_init(int argc, char *argv[], glkunix_startup_t *startdata);
extern int xglk_open_connection(char *progname);
extern int xglk_init_preferences(int argc, char *argv[], 
  glkunix_startup_t *startdata);
extern void xglk_build_fontname(fontnamespec_t *spec, char *buf, 
  int size, int weight, int oblique, int proportional);
extern void xglk_event_loop(event_t *ev, glui32 millisec);
extern void xglk_event_poll(event_t *ev, glui32 millisec);
extern void xglk_arrange_window(void);
extern void xglk_invalidate(XRectangle *box);
extern void xglk_redraw(void);
extern void xglk_perform_click(int dir, XPoint *pt, int butnum, 
  unsigned int state);
extern void xglk_relax_memory(void);
extern void gli_draw_window_highlight(struct glk_window_struct *win, 
  int turnon);
extern void gli_draw_window_outline(XRectangle *winbox);
extern void gli_draw_window_margin(XColor *colref, 
  int outleft, int outtop, int outwidth, int outheight,
  int inleft, int intop, int inwidth, int inheight);
extern void xglk_draw_dot(int xpos, int ypos, int linehgt);
extern void xglk_clearfor_string(XColor *colref, int xpos, int ypos,
  int width, int height);
extern void xglk_draw_string(fontref_t *fontref, int islink, 
  int width, int xpos, int ypos, char *str, int len);

extern void xglk_store_scrap(char *str, long len);
extern void xglk_clear_scrap(void);
extern void xglk_fetch_scrap(char **str, long *len);
extern void xglk_strip_garbage(char *str, long len);

extern int init_xmsg(void);
extern void xmsg_redraw(void);
extern void xmsg_resize(int x, int y, int wid, int hgt);
extern void xmsg_set_message(char *str, int sticky);
extern void xmsg_check_timeout(void);
extern int xmsg_getline(char *prompt, char *buf, int maxlen, int *length);
extern int xmsg_getchar(char *prompt);
extern void xgc_msg_getchar(int op);
extern void xgc_msg_insert(int op);
extern void xgc_msg_delete(int op);
extern void xgc_msg_movecursor(int op);
extern void xgc_msg_enter(int op);

extern void xweg_init_scrollbar(wegscroll_t *weg, void *rock, 
  cmdfunc_ptr scrollfunc, cmdfunc_ptr scrolltofunc);
extern void xweg_draw_scrollbar(wegscroll_t *weg);
extern void xweg_adjust_scrollbar(wegscroll_t *weg, int numlines, 
  int scrollline, int linesperpage);
extern void xweg_click_scrollbar(wegscroll_t *weg, int dir, XPoint *pt, 
  int butnum, int clicknum, unsigned int state, 
  int numlines, int scrollline, int linesperpage);

extern int init_xkey(void);
extern void xkey_perform_key(int key, unsigned int state);
extern char *xkey_get_macro(int key);
extern void xkey_set_macro(int key, char *str, int chown);
extern void xkey_guess_focus(void);

extern int xres_is_resource_map(void);
extern void xres_get_resource(glui32 usage, glui32 id, 
  FILE **file, long *pos, long *len, glui32 *type);

extern int init_pictures(void);
extern int init_picture_colortab(XColor *cols, int numcols);
extern void picture_relax_memory(void);
extern picture_t *picture_find(unsigned long id);
extern void picture_release(picture_t *pic);
extern void picture_draw(picture_t *pic, Drawable dest, int xpos, int ypos, 
  int width, int height, XRectangle *clipbox);

#define op_Cancel (0)
#define op_Meta (1)
#define op_ExplainKey (2)
#define op_DefineMacro (3)
#define op_ForeWin (4)
#define op_AllWindows (5)
#define op_Enter (6)
#define op_ForeChar (10)
#define op_BackChar (11)
#define op_ForeWord (12)
#define op_BackWord (13)
#define op_ForeLine (14)
#define op_BackLine (15)
#define op_BeginLine (16)
#define op_EndLine (17)
#define op_DownPage (20)
#define op_UpPage (21)
#define op_DownLine (22)
#define op_UpLine (23)
#define op_ToBottom (24)
#define op_ToTop (25)
#define op_Copy (30)
#define op_Wipe (31)
#define op_Yank (32)
#define op_Kill (33)
#define op_YankReplace (34)
#define op_Untype (35)
#define op_Erase (36)

extern void xgc_focus(struct glk_window_struct *dummy, int op);
extern void xgc_redraw(struct glk_window_struct *win, int op);
extern void xgc_noop(struct glk_window_struct *dummy, int op);
extern void xgc_work_meta(struct glk_window_struct *dummy, int op);
extern void xgc_enter(struct glk_window_struct *win, int op);
extern void xgc_insert(struct glk_window_struct *win, int op);
extern void xgc_getchar(struct glk_window_struct *win, int op);
extern void xgc_movecursor(struct glk_window_struct *win, int op);
extern void xgc_scroll(struct glk_window_struct *win, int op);
extern void xgc_scrollto(struct glk_window_struct *win, int op);
extern void xgc_delete(struct glk_window_struct *win, int op);
extern void xgc_cutbuf(struct glk_window_struct *win, int op);
extern void xgc_history(struct glk_window_struct *win, int op);

#endif /* _XGLK_H */
