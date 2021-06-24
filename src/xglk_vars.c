#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "xglk.h"

Display *xiodpy;
Colormap xiomap;
int xioscn;
int xiodepth;
int xiobackstore;
Window xiowin;
GC gcfore, gcback, gctech, gctechu, gctechd, gcselect, gcflip;
GC gctextfore, gctextback;
Font textforefont;
unsigned long textforepixel, textbackpixel;
unsigned char *pixelcube;
int imageslegal;

int xio_wid, xio_hgt;
XRectangle matte_box;
int xio_any_invalid;

preferences_t prefs;
