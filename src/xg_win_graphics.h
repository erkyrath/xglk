typedef struct window_graphics_struct window_graphics_t;

extern window_graphics_t *win_graphics_create(window_t *win);
extern void win_graphics_destroy(window_graphics_t *cutwin);
extern void win_graphics_rearrange(window_t *win, XRectangle *box);
extern void win_graphics_perform_click(window_t *win, int dir, XPoint *pt, 
  int butnum, int clicknum, unsigned int state);
extern void win_graphics_get_size(window_t *win, glui32 *width, glui32 *height);
extern void win_graphics_redraw(window_t *win);
extern XRectangle *win_graphics_get_rect(window_t *win);
extern long win_graphics_figure_size(window_t *win, long size, int vertical);
extern void win_graphics_screen_state(window_t *win);
extern void win_graphics_flush(window_t *win);

extern glui32 win_graphics_draw_picture(window_graphics_t *cutwin, 
  glui32 image, glsi32 xpos, glsi32 ypos, 
  int scale, glui32 imagewidth, glui32 imageheight);
extern void win_graphics_erase_rect(window_graphics_t *cutwin, int whole,
  glsi32 xpos, glsi32 ypos, glui32 width, glui32 height);
extern void win_graphics_fill_rect(window_graphics_t *cutwin, glui32 color, 
  glsi32 xpos, glsi32 ypos, glui32 width, glui32 height);
extern void win_graphics_set_background_color(window_graphics_t *cutwin, 
  glui32 color);
