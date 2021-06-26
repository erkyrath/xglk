#include <stdlib.h>
#include "xglk.h"
#include "xg_internal.h"
#include <string.h> 

static stylehints_t null_hints;
static stylehints_t textbuf_hints;
static stylehints_t textgrid_hints;

static int gli_stylehints_init(stylehints_t *hints);
static int gli_stylehints_clone(stylehints_t *dest, stylehints_t *src);
static int gli_stylehint_get(stylehints_t *hints, glui32 styl, glui32 hint, 
  glsi32 *val);
static void gli_stylehint_set(stylehints_t *hints, glui32 styl, glui32 hint, 
  int set, glsi32 val);

static unsigned short MaxRGBDifference(XColor *col1, XColor *col2);

int init_gli_styles()
{
  int ix, jx;
  
  ix = gli_stylehints_init(&null_hints);
  if (!ix)
    return FALSE;
  ix = gli_stylehints_init(&textbuf_hints);
  if (!ix)
    return FALSE;
  ix = gli_stylehints_init(&textgrid_hints);
  if (!ix)
    return FALSE;
  
  textbuf_hints.type = wintype_TextBuffer;
  textgrid_hints.type = wintype_TextGrid;
  
  return TRUE;
}

static int gli_stylehints_init(stylehints_t *hints)
{
  int ix;
  
  hints->type = wintype_AllTypes;
  
  for (ix=0; ix<style_NUMSTYLES; ix++) {
    hints->style[ix].setflags = 0;
    hints->style[ix].vals = NULL;
    hints->style[ix].length = 0;
    hints->style[ix].size = 0;
  }
  
  return TRUE;
}

static int gli_stylehints_clone(stylehints_t *dest, stylehints_t *src)
{
  int ix, jx, len;
  
  dest->type = src->type;
  
  for (ix=0; ix<style_NUMSTYLES; ix++) {
    dest->style[ix].setflags = src->style[ix].setflags;
    len = src->style[ix].length;
    dest->style[ix].length = len;
    dest->style[ix].size = len;
    if (len) {
      dest->style[ix].vals = (styleval_t *)malloc(len * sizeof(styleval_t));
      for (jx=0; jx<len; jx++) {
	dest->style[ix].vals[jx] = src->style[ix].vals[jx];
      }
    }
    else {
      dest->style[ix].vals = NULL;
    }
  }

  return TRUE;
}

void gli_styles_compute(fontset_t *font, stylehints_t *hints) 
{ 
  int ix;
  int negindent;
  int falied_fonts = 0;
  int loaded_fonts = 0;
  glsi32 val;
  winprefs_t *wprefs;

  /* Here is the routine: For every (damn) attribute, look at the
     prefs.fontprefs_t.  That will specify a value, and also a check-hints
     flag. If the latter is true, and there's a hint, let the hint
     override. Otherwise, use the value. */

  if (!hints)
    hints = &null_hints;
  
  if (hints->type == wintype_TextGrid)
    wprefs = &prefs.textgrid;
  else
    wprefs = &prefs.textbuffer;

  font->forecolor = wprefs->forecolor;
  font->linkcolor = wprefs->linkcolor;
  font->backcolor = wprefs->backcolor;


  for (ix=0; ix<style_NUMSTYLES; ix++) {
    fontref_t *fref = &(font->gc[ix]);
    loaded_fonts++;
    XGCValues gcvalues;
    int size, weight, oblique, proportional;
    char buf[256];

    size = wprefs->style[ix].size;
    if (wprefs->sizehint
      && gli_stylehint_get(hints, ix, stylehint_Size, &val))
      size += val;

    if (wprefs->attribhint
      && gli_stylehint_get(hints, ix, stylehint_Weight, &val))
      weight = val;
    else
      weight = wprefs->style[ix].weight;

    if (wprefs->attribhint
      && gli_stylehint_get(hints, ix, stylehint_Oblique, &val))
      oblique = val;
    else
      oblique = wprefs->style[ix].oblique;

    if (wprefs->fixedhint
      && gli_stylehint_get(hints, ix, stylehint_Proportional, &val))
      proportional = val;
    else
      proportional = wprefs->style[ix].proportional;

    xglk_build_fontname(wprefs->style[ix].spec, buf, 
      size, weight, oblique, proportional);
    fref->fontstr = XLoadQueryFont(xiodpy, buf);
    if (!fref->fontstr) {
      falied_fonts++;
      loaded_fonts--;
      fprintf(stderr, "### unable to snarf font %s.\n", buf);
    }

    fref->specname = (char *)malloc(sizeof(char) * (strlen(buf)+1));
    /* ### bit of a core leak here, I'm afraid. */
    strcpy(fref->specname, buf);

    fref->forecolor = wprefs->style[ix].forecolor;
    fref->linkcolor = wprefs->style[ix].linkcolor;
    fref->backcolor = wprefs->style[ix].backcolor;
    /*### or hint */

    if (wprefs->justhint 
      && gli_stylehint_get(hints, ix, stylehint_Justification, &val))
      font->gc[ix].justify = val;
    else
      font->gc[ix].justify = wprefs->style[ix].justify;

    if (wprefs->indenthint 
      && gli_stylehint_get(hints, ix, stylehint_Indentation, &val))
      font->gc[ix].indent = (font->gc[ix].fontstr->ascent * val) / 2; 
    else
      font->gc[ix].indent = wprefs->style[ix].baseindent;

    if (wprefs->indenthint 
      && gli_stylehint_get(hints, ix, stylehint_ParaIndentation, &val))
      font->gc[ix].parindent = (font->gc[ix].fontstr->ascent * val) / 2; 
    else
      font->gc[ix].parindent = wprefs->style[ix].parindent;
  }

  if (falied_fonts > 0) {
    fprintf(stderr, "### unable to load %i fonts.\n", falied_fonts);
    fprintf(stderr, "### loaded %i fonts in total.\n", loaded_fonts);
  }

  font->lineheight = 0;
  font->lineoff = 0;
  font->baseindent = 0;
  negindent = 0;
  
  for (ix=0; ix<style_NUMSTYLES; ix++) {
    fontref_t *fref = &(font->gc[ix]);
    if (fref->fontstr) {
      int ascent, descent, dir;
      int clineheight, clineheightoff, ival;
      XCharStruct strdat;

      XTextExtents(fref->fontstr, " ", 1, &dir, &ascent, &descent, &strdat);
      font->gc[ix].ascent = ascent;
      font->gc[ix].descent = descent;
      if (descent < 2)
        font->gc[ix].underliney = descent-1;
      else
        font->gc[ix].underliney = 1;
      
      clineheight = ascent + descent + wprefs->leading;
      clineheightoff = ascent;
      if (clineheight > font->lineheight)
        font->lineheight = clineheight;
      if (clineheightoff > font->lineoff)
        font->lineoff = clineheightoff;
      font->gc[ix].spacewidth = strdat.width;

      ival = font->gc[ix].indent + font->gc[ix].parindent;
      if (ival < negindent)
        negindent = ival;
      if (font->gc[ix].indent < negindent)
        negindent = font->gc[ix].indent;
    }
  }

  font->baseindent = -negindent;
}

static void gli_stylehint_set(stylehints_t *hints, glui32 styl, glui32 hint, 
  int set, glsi32 val)
{
  int ix, len;
  glui32 hintbit;
  styleonehint_t *hn;
  
  if (styl >= style_NUMSTYLES || hint >= stylehint_NUMHINTS)
    return;
  
  hn = &(hints->style[styl]);
  hintbit = (1 << hint);
  len = hn->length;
  
  if (set) {
    if (hn->setflags & hintbit) {
      for (ix=0; ix<len; ix++) {
	if (hn->vals[ix].type == hint) {
	  hn->vals[ix].val = val;
	  break;
	}
      }
    }
    else {
      if (!hn->size || !hn->vals) {
	hn->size = 3;
	hn->vals = (styleval_t *)malloc(hn->size * sizeof(styleval_t));
      }
      else if (len+1 > hn->size) {
	hn->size = len+4;
	hn->vals = (styleval_t *)realloc(hn->vals, hn->size * sizeof(styleval_t));
      }
      hn->vals[len].type = hint;
      hn->vals[len].val = val;
      hn->length++;
      hn->setflags |= (hintbit);
    }
  }
  else {
    if (!(hn->setflags & hintbit))
      return;
    for (ix=0; ix<len; ix++) {
      if (hn->vals[ix].type == hint)
	break;
    }
    for (; ix+1<len; ix++) {
      hn->vals[ix] = hn->vals[ix+1];
    }
    hn->length--;
    hn->setflags &= (~hintbit);
  }
}

static int gli_stylehint_get(stylehints_t *hints, glui32 styl, glui32 hint, 
  glsi32 *val)
{
  int ix, len;
  styleonehint_t *hn;
  
  if (styl >= style_NUMSTYLES || hint >= stylehint_NUMHINTS)
    return FALSE;
  
  hn = &(hints->style[styl]);
  
  if (!(hn->setflags & (1 << hint))) {
    return FALSE;
  }
  
  len = hn->length;
  for (ix=0; ix<len; ix++) {
    if (hn->vals[ix].type == hint) {
      *val = hn->vals[ix].val;
      return TRUE;
    }
  }
  
  return FALSE;
}

void glk_stylehint_set(glui32 wintype, glui32 styl, glui32 hint, glsi32 val)
{
  switch (wintype) {
  case wintype_TextBuffer:
    gli_stylehint_set(&textbuf_hints, styl, hint, TRUE, val);
    break;
  case wintype_TextGrid:
    gli_stylehint_set(&textgrid_hints, styl, hint, TRUE, val);
    break;
  case wintype_AllTypes:
    gli_stylehint_set(&textbuf_hints, styl, hint, TRUE, val);
    gli_stylehint_set(&textgrid_hints, styl, hint, TRUE, val);
    break;
  }
}

void glk_stylehint_clear(glui32 wintype, glui32 styl, glui32 hint)
{
  switch (wintype) {
  case wintype_TextBuffer:
    gli_stylehint_set(&textbuf_hints, styl, hint, FALSE, 0);
    break;
  case wintype_TextGrid:
    gli_stylehint_set(&textgrid_hints, styl, hint, FALSE, 0);
    break;
  case wintype_AllTypes:
    gli_stylehint_set(&textbuf_hints, styl, hint, FALSE, 0);
    gli_stylehint_set(&textgrid_hints, styl, hint, FALSE, 0);
    break;
  }
}

int gli_stylehints_for_window(glui32 wintype, stylehints_t *hints)
{
  switch (wintype) {
  case wintype_TextBuffer:
    return gli_stylehints_clone(hints, &textbuf_hints);
  case wintype_TextGrid:
    return gli_stylehints_clone(hints, &textgrid_hints);
  default:
    return gli_stylehints_init(hints);
  }
}

glui32 glk_style_distinguish(window_t *win, glui32 styl1, glui32 styl2)
{
  fontset_t *fonts;
  fontref_t *font1, *font2;
  
  if (!win) {
    gli_strict_warning("window_style_distinguish: invalid id");
    return FALSE;
  }
  
  fonts = gli_window_get_fontset(win);
  if (!fonts)
    return FALSE;
  
  if (styl1 >= style_NUMSTYLES || styl2 >= style_NUMSTYLES)
    return FALSE;
  
  if (styl1 == styl2)
    return FALSE;
  
  font1 = &(fonts->gc[styl1]);
  font2 = &(fonts->gc[styl2]);
  
  if (strcmp(font1->specname, font2->specname))
    return TRUE;

  if (font1->justify != font2->justify) {
    if (!((font1->justify == stylehint_just_LeftFlush 
      || font1->justify == stylehint_just_LeftRight)
      && (font2->justify == stylehint_just_LeftFlush 
	|| font2->justify == stylehint_just_LeftRight)))
      return TRUE;
  }
  
  if (font1->indent != font2->indent)
    return TRUE;
  
  if (MaxRGBDifference(&font1->forecolor, &font2->forecolor) >= 6500)
    return TRUE;
  if (MaxRGBDifference(&font1->backcolor, &font2->backcolor) >= 6500)
    return TRUE;
  
  return FALSE;
}

glui32 glk_style_measure(window_t *win, glui32 styl, glui32 hint, 
  glui32 *result)
{
  fontset_t *fonts;
  fontref_t *font;
  glui32 dummy, val;
  
  if (!win) {
    gli_strict_warning("window_style_measure: invalid id");
    return FALSE;
  }
  
  if (!result)
    result = &dummy;
  
  fonts = gli_window_get_fontset(win);
  if (!fonts)
    return FALSE;
  
  if (styl >= style_NUMSTYLES || hint >= stylehint_NUMHINTS)
    return FALSE;
  
  font = &(fonts->gc[styl]);
  
  switch (hint) {
  case stylehint_Weight:
    /* Can't figure this out in X */
    return FALSE;
  case stylehint_Oblique:
    /* Can't figure this out in X */
    return FALSE;
  case stylehint_TextColor:
    *result = PackRGBColor(&font->forecolor);
    return TRUE;
  case stylehint_BackColor:
    *result = PackRGBColor(&fonts->backcolor);
    return TRUE;
  case stylehint_Justification:
    *result = font->justify;
    return TRUE;
  case stylehint_Size:
    *result = font->fontstr->ascent + font->fontstr->descent;
    return TRUE;
  case stylehint_Indentation:
    *result = font->indent;
    return TRUE;
  case stylehint_ParaIndentation:
    *result = font->parindent;
    return TRUE;
  case stylehint_Proportional:
  default:
    return FALSE;
  }
}

static unsigned short MaxRGBDifference(XColor *col1, XColor *col2)
{
  unsigned short val, result;
  
  result = 0;
  
  if ((unsigned short)(col1->red) > (unsigned short)(col2->red))
    val = (unsigned short)(col1->red) - (unsigned short)(col2->red);
  else
    val = (unsigned short)(col2->red) - (unsigned short)(col1->red);
  if (val > result)
    result = val;
  
  if ((unsigned short)(col1->green) > (unsigned short)(col2->green))
    val = (unsigned short)(col1->green) - (unsigned short)(col2->green);
  else
    val = (unsigned short)(col2->green) - (unsigned short)(col1->green);
  if (val > result)
    result = val;
  
  if ((unsigned short)(col1->blue) > (unsigned short)(col2->blue))
    val = (unsigned short)(col1->blue) - (unsigned short)(col2->blue);
  else
    val = (unsigned short)(col2->blue) - (unsigned short)(col1->blue);
  if (val > result)
    result = val;
  
  return result;
}

