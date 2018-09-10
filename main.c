#include <stdio.h>

#include "glk.h"
#include "xglk.h"
#include "xg_internal.h"

static int inittime = FALSE;

int main(int argc, char *argv[]) 
{
  int err;
  glkunix_startup_t startdata;

  err = xglk_init(argc, argv, &startdata);
  if (!err) {
    fprintf(stderr, "%s: exiting.\n", argv[0]);
    return 1;
  }

  inittime = TRUE;
  if (!glkunix_startup_code(&startdata)) {
    glk_exit();
  }
  inittime = FALSE;

  glk_main();
  glk_exit();

  return 0; /* We never reach here, really */
}

strid_t glkunix_stream_open_pathname(char *pathname, glui32 textmode, 
  glui32 rock)
{
  if (!inittime)
    return 0;
  return gli_stream_open_pathname(pathname, (textmode != 0), rock);
}
