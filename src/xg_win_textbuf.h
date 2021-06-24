
typedef struct window_textbuffer_struct window_textbuffer_t;

extern struct window_textbuffer_struct *win_textbuffer_create(window_t *win);
extern void win_textbuffer_destroy(struct window_textbuffer_struct *dwin);
extern void win_textbuffer_rearrange(window_t *win, XRectangle *box);
extern void win_textbuffer_perform_click(window_t *win, int dir, XPoint *pt, 
  int butnum, int clicknum, unsigned int state);
extern void win_textbuffer_get_size(window_t *win, glui32 *width, glui32 *height);
extern long win_textbuffer_figure_size(window_t *win, long size, int vertical);
extern fontset_t *win_textbuffer_get_fontset(window_t *win);
extern stylehints_t *win_textbuffer_get_stylehints(window_t *win);
extern XRectangle *win_textbuffer_get_rect(window_t *win);
extern void win_textbuffer_redraw(window_t *win);
extern void win_textbuffer_activate(window_t *win, int turnon);
extern void win_textbuffer_setfocus(window_t *win, int turnon);
extern void win_textbuffer_caret_changed(window_t *win, int turnon);
extern void win_textbuffer_flush(window_t *win);
extern void win_textbuffer_init_line(window_t *win, char *buffer, int buflen, 
  int readpos);
extern void win_textbuffer_cancel_line(window_t *win, event_t *ev);


extern void win_textbuffer_add(window_textbuffer_t *cutwin, char ch, long pos);
extern void win_textbuffer_replace(window_textbuffer_t *cutwin, long pos, long oldlen, 
  char *buf, long newlen);
extern void win_textbuffer_set_style_text(window_textbuffer_t *cutwin, glui32 attr);
extern void win_textbuffer_set_style_image(window_textbuffer_t *cutwin,
  glui32 image, int imagealign,
  glui32 imagewidth, glui32 imageheight);
extern void win_textbuffer_set_style_break(window_textbuffer_t *cutwin);
extern void win_textbuffer_set_style_link(window_textbuffer_t *cutwin,
  glui32 linkid);
extern void win_textbuffer_trim_buffer(window_textbuffer_t *cutwin);
extern void win_textbuffer_end_visible(window_textbuffer_t *cutwin);
extern void win_textbuffer_set_paging(window_textbuffer_t *cutwin, int forcetoend);
extern int win_textbuffer_is_paging(window_t *win);
extern void win_textbuffer_clear_window(window_textbuffer_t *cutwin);
extern void win_textbuffer_delete_start(window_textbuffer_t *cutwin, long num);

extern void xgc_buf_getchar(window_textbuffer_t *cutwin, int ch);
extern void xgc_buf_insert(window_textbuffer_t *cutwin, int ch);
extern void xgc_buf_delete(window_textbuffer_t *cutwin, int op);
extern void xgc_buf_enter(window_textbuffer_t *cutwin, int op);
extern void xgc_buf_scroll(window_textbuffer_t *cutwin, int op);
extern void xgc_buf_scrollto(window_textbuffer_t *cutwin, int op);
extern void xgc_buf_movecursor(window_textbuffer_t *cutwin, int op);
extern void xgc_buf_cutbuf(window_textbuffer_t *cutwin, int op);
extern void xgc_buf_history(window_textbuffer_t *cutwin, int op);
extern void xgc_buf_macro(window_textbuffer_t *cutwin, int op);
extern void xgc_buf_macro_enter(window_textbuffer_t *cutwin, int op);
extern void xgc_buf_macro_fail(window_textbuffer_t *cutwin, int op);
extern void xgc_buf_define_macro(window_textbuffer_t *cutwin, int keynum);

