# Unix Makefile for XGlk library

# This generates two files. One, of course, is libxglk.a -- the library
# itself. The other is Make.xglk; this is a snippet of Makefile code
# which locates the xglk library and associated libraries (such as
# curses.)
#
# When you install xglk, you must put libxglk.a in the lib directory,
# and glk.h, glkstart.h, and Make.xglk in the include directory.

# If you get errors in xio.c about fd_set or FD_SET being 
#   undefined, put "-DNEEDS_SELECT_H" in the SYSTEMFLAGS line,
#   as has been done for the RS6000.

# --------------------

# definitions for RS6000 / AIX
#SYSTEMFLAGS = -DNEEDS_SELECT_H

# definitions for HP / HPUX
#SYSTEMFLAGS = -Ae

# definitions for HP / HPUX 9.0 
#    (Dunno; this was contributed to me)
#SYSTEMFLAGS = -Aa -D_HPUX_SOURCE

# definitions for SparcStation / SunOS
#SYSTEMFLAGS =

# definitions for SparcStation / Solaris 
#    (Solaris 2.5, don't know about other versions)
#SYSTEMLIBS = -R$(XLIB)  -lsocket

# definitions for DECstation / Ultrix
#SYSTEMFLAGS =

# definitions for SGI / Irix
#SYSTEMFLAGS =

# definitions for Linux
#SYSTEMFLAGS =

# --------------------

# definitions for where the X lib and include directories are.
# The following are defaults that might work.

# If your compiler can't find these things, try commenting out the
# above, and uncommenting various versions below. Also look around
# your hard drive for the appropriate files. (The XINCLUDE directory
# should contain the file "Xlib.h", and the XLIB dir should contain 
# "libX11.so" or "libX11.a".)
# The problem is, depending on how things are installed, the
# directories could be just about anywhere. Sigh.

# for Debian Linux
#XINCLUDE = -I/usr/X11R6/include/X11
#XLIB = -L/usr/X11R6/lib -lX11

# for Red Hat Linux
XINCLUDE = -I/usr/X11R6/include/X11
XLIB = -L/usr/X11R6/lib -lX11

# for SparcStation / Solaris 
#XINCLUDE = -I/usr/openwin/include
#XLIB = -R/usr/openwin/lib -L/usr/openwin/lib/ -lX11 

# --------------------

# definitions for where the PNG and JPEG libs are. 
PNGINCLUDE = 
PNGLIB = -lpng
JPEGINCLUDE = 
JPEGLIB = -ljpeg

# If there is no PNG lib available, uncomment this line.
PNGFLAG = -DNO_PNG_AVAILABLE
# If there is no JPEG lib available, uncomment this line.
JPEGFLAG = -DNO_JPEG_AVAILABLE

# --------------------

# Pick a C compiler.
#CC = cc
CC = gcc

CFLAGS = -O -ansi $(PNGFLAG) $(JPEGFLAG) $(PNGINCLUDE) $(JPEGINCLUDE) -Wall -Wmissing-prototypes -Wstrict-prototypes -Wno-unused -Wbad-function-cast $(SYSTEMFLAGS) $(XINCLUDE)
LDFLAGS =
LIBS = $(XLIB) $(PNGLIB) $(JPEGLIB) $(SYSTEMLIBS)

OBJS = main.o xglk.o xglk_vars.o xglk_prefs.o xglk_loop.o xglk_init.o \
  xglk_scrap.o xglk_msg.o xglk_key.o xglk_weggie.o xglk_pict.o \
  xglk_res.o \
  xg_event.o xg_window.o xg_stream.o xg_fileref.o xg_style.o xg_misc.o \
  xg_gestalt.o xg_win_textbuf.o xg_win_textgrid.o xg_win_graphics.o \
  xg_schan.o \
  gi_dispa.o gi_blorb.o

all: libxglk.a Make.xglk

libxglk.a: $(OBJS)
	ar r libxglk.a $(OBJS)
	ranlib libxglk.a

Make.xglk:
	echo LINKLIBS = $(LIBDIRS) $(LIBS) > Make.xglk
	echo GLKLIB = -lxglk >> Make.xglk

$(OBJS): xglk.h xg_internal.h xglk_option.h glk.h gi_dispa.h gi_blorb.h

xg_win_textgrid.o xg_window.o xglk_key.o: xg_win_textgrid.h

xg_win_textbuf.o xg_window.o xglk_key.o: xg_win_textbuf.h

clean:
	-rm -f *~ *.o libxglk.a Make.xglk
