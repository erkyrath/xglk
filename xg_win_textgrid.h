
typedef struct window_textgrid_struct window_textgrid_t;

extern window_textgrid_t *win_textgrid_create(window_t *win);
extern void win_textgrid_destroy(window_textgrid_t *dwin);
extern void win_textgrid_rearrange(window_t *win, XRectangle *box);
extern void win_textgrid_perform_click(window_t *win, int dir, XPoint *pt, 
  int butnum, int clicknum, unsigned int state);
extern void win_textgrid_get_size(window_t *win, 
  glui32 *width, glui32 *height);
extern long win_textgrid_figure_size(window_t *win, long size, int vertical);
extern fontset_t *win_textgrid_get_fontset(window_t *win);
extern stylehints_t *win_textgrid_get_stylehints(window_t *win);
extern XRectangle *win_textgrid_get_rect(window_t *win);
extern void win_textgrid_redraw(window_t *win);
extern void win_textgrid_activate(window_t *win, int turnon);
extern void win_textgrid_setfocus(window_t *win, int turnon);
extern void win_textgrid_caret_changed(window_t *win, int turnon);
extern void win_textgrid_flush(window_t *win);
extern void win_textgrid_init_line(window_t *win, char *buffer, int buflen, 
  int readpos);
extern void win_textgrid_cancel_line(window_t *win, event_t *ev);

extern void win_textgrid_add(window_textgrid_t *cutwin, char ch);
extern void win_textgrid_set_pos(window_textgrid_t *cutwin, 
  glui32 xpos, glui32 ypos);
extern void win_textgrid_clear_window(window_textgrid_t *cutwin);

extern void xgc_grid_getchar(window_textgrid_t *cutwin, int ch);
extern void xgc_grid_insert(window_textgrid_t *cutwin, int ch);
extern void xgc_grid_delete(window_textgrid_t *cutwin, int op);
extern void xgc_grid_enter(window_textgrid_t *cutwin, int op);
extern void xgc_grid_movecursor(window_textgrid_t *cutwin, int op);
extern void xgc_grid_cutbuf(window_textgrid_t *cutwin, int op);
extern void xgc_grid_macro(window_textgrid_t *cutwin, int op);
extern void xgc_grid_macro_enter(window_textgrid_t *cutwin, int op);
