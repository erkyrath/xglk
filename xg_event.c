#include "xglk.h"
#include "xg_internal.h"

static glui32 timer_millisecs = 0; /* 0 for no timer */

void glk_select(event_t *event)
{
  gli_event_clearevent(event);

  gli_windows_flush();
  gli_windows_set_paging(FALSE);
  xkey_guess_focus();
  xglk_relax_memory();

  xglk_event_loop(event, timer_millisecs);

  gli_windows_trim_buffers();
}

void glk_select_poll(event_t *event)
{
  gli_event_clearevent(event);
  
  gli_windows_flush();
  /* Don't set up paging, since we won't be getting any player input. */
  xglk_relax_memory();

  xglk_event_poll(event, timer_millisecs);
  
  /* But we don't reset_abend(), because the player hasn't had a chance to
     input anything. We could still be in an infinite loop. */
}

void glk_request_timer_events(glui32 millisecs)
{
  timer_millisecs = millisecs;
}

void glk_tick()
{
  /* Nothing for us to do. */
}
