#include "xglk.h"
#include <stdlib.h>
#include "xg_internal.h"

glui32 glk_gestalt(glui32 id, glui32 val)
{
  return glk_gestalt_ext(id, val, NULL, 0);
}

glui32 glk_gestalt_ext(glui32 id, glui32 val, glui32 *arr, glui32 arrlen)
{
  int ix;
  
  switch (id) {

  case gestalt_Version:
    return 0x00000601;
    
  case gestalt_LineInput:
    if (val >= 0 && val <= 31) 
      return FALSE;
    if (val >= 127 && val <= 159)
      return FALSE;
    if (val > 255)
      return FALSE;
#ifndef OPT_ALL_KEYS_TYPABLE
    if (val >= 128)
      return FALSE;
#endif
    return TRUE;

  case gestalt_CharInput: 
    if (val >= 0 && val <= 31) 
      return TRUE;
    if (val >= 127 && val <= 159)
      return FALSE;
    if (val <= 0xFFFFFFFF && val > (0xFFFFFFFF - keycode_MAXVAL)) {
      return gli_special_typable_table[(glui32)(0-val)];
    }
    if (val > 255)
      return FALSE;
    return TRUE;

  case gestalt_CharOutput: {
    if (val < 0 || val >= 256) {
      if (arr && arrlen>0)
	arr[0] = 0;
      return gestalt_CharOutput_CannotPrint;
    }
    if ((val >= 0 && val <= 31) || (val >= 127 && val <= 159)) {
      if (arr && arrlen>0)
	arr[0] = 0;
      return gestalt_CharOutput_CannotPrint;
    }
    if (arr && arrlen>0)
      arr[0] = 1;
    return gestalt_CharOutput_ExactPrint;
  }

  case gestalt_Timer: 
    return TRUE;
  
  case gestalt_MouseInput: {
    switch (val) {
    case wintype_Graphics:
    case wintype_TextGrid:
      return TRUE;
    default:
      return FALSE;
    }
  }

  case gestalt_Graphics: 
    return TRUE;
  case gestalt_GraphicsTransparency:
    return FALSE;
  
  case gestalt_DrawImage: {
#if defined(NO_PNG_AVAILABLE) || defined(NO_JPEG_AVAILABLE)
    return FALSE;
#else
    if (!imageslegal)
      return FALSE;
    if (val == wintype_Graphics || val == wintype_TextBuffer)
      return TRUE;
    return FALSE;
#endif /* NO_PNG_AVAILABLE */
  }

  case gestalt_Sound:
  case gestalt_SoundVolume:
  case gestalt_SoundNotify: 
  case gestalt_SoundMusic:
    return FALSE;

  case gestalt_Hyperlinks: {
    return TRUE;
  }
  
  case gestalt_HyperlinkInput: {
    switch (val) {
    case wintype_TextBuffer:
    case wintype_TextGrid:
      return TRUE;
    default:
      return FALSE;
    }
  }

  default:
    return 0;

  }
}

