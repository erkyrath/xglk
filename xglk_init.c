#include <stdlib.h>
#include "xglk.h"

int xglk_open_connection(char *progname)
{
  int ix;
  int direction;
  int ascent, descent;
  XCharStruct overall;
  Visual *vis;
  char *cx;

  xiodpy = XOpenDisplay(NULL);
  if (!xiodpy) {
    fprintf(stderr, "%s: could not open display.\n", progname);
    return FALSE;
  }
  
  /* init all the X stuff */
  xioscn = DefaultScreen(xiodpy);
  vis = DefaultVisual(xiodpy, xioscn);
  xiodepth = DefaultDepth(xiodpy, xioscn);
  if (DoesBackingStore(ScreenOfDisplay(xiodpy, xioscn)) == NotUseful)
    xiobackstore = FALSE;
  else
    xiobackstore = TRUE;

  imageslegal = FALSE;
  if (xiodepth == 1 || xiodepth == 8 || xiodepth == 16 
    || xiodepth == 24 || xiodepth == 32) {
    imageslegal = TRUE;
  }

  /*xiomap = XCreateColormap(xiodpy, DefaultRootWindow(xiodpy),
    vis, AllocNone);*/
  xiomap = DefaultColormap(xiodpy, xioscn);
  pixelcube = NULL;

#if 0
  if (getenv("XGLK_DEBUG")) { /* ### */
    printf("DefaultVisual id=%ld: ", vis->visualid);
    switch (vis->class) {
    case StaticGray:
      printf("StaticGray"); break;
    case GrayScale: 
      printf("GrayScale"); break;
    case StaticColor:
      printf("StaticColor"); break;
    case PseudoColor:
      printf("PseudoColor"); break;
    case TrueColor:  
      printf("TrueColor"); break;
    case DirectColor:
      printf("DirectColor"); break;
    default:
      printf("???"); break;
    }
    printf(" (masks = 0x%lx, 0x%lx, 0x%lx)\n",
      vis->red_mask, vis->green_mask, vis->blue_mask);
  } /* ### */
#endif

  if (vis->class == PseudoColor && xiodepth >= 8) {
    /* Better shove in a standard 6x6x6 color cube. */
    XColor colorlist[6*6*6];
    XColor *colptr = colorlist;
    int ix, jx, kx, count;
    pixelcube = (unsigned char *)calloc(6*6*6, sizeof(unsigned char));
    if (!pixelcube)
      return FALSE;
    count = 0;
    for (ix=0; ix<6; ix++) {
      for (jx=0; jx<6; jx++) {
	for (kx=0; kx<6; kx++) {
	  colptr->red = 13107 * ix;
	  colptr->green = 13107 * jx;
	  colptr->blue = 13107 * kx;
	  XAllocColor(xiodpy, xiomap, colptr);
	  pixelcube[count] = colptr->pixel;
	  colptr++;
	  count++;
	}
      }
    }
  }

  XrmInitialize();

  return TRUE;
}
