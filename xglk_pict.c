#include <stdlib.h>
#include <stdio.h>
#include <sys/param.h>

#ifndef NO_PNG_AVAILABLE
#include <png.h>
#endif /* NO_PNG_AVAILABLE */
#ifndef NO_JPEG_AVAILABLE
#include <jpeglib.h>
#endif /* NO_JPEG_AVAILABLE */

#if defined(NO_PNG_AVAILABLE) && defined(NO_JPEG_AVAILABLE)
#define NOTHING_AVAILABLE
#endif

#include "xglk.h"
#include "gi_blorb.h"

#define HASHSIZE (63)

#define giblorb_ID_JPEG      (giblorb_make_id('J', 'P', 'E', 'G'))
#define giblorb_ID_PNG       (giblorb_make_id('P', 'N', 'G', ' '))

static picture_t **table = NULL;

static XImage *load_image_png(FILE *fl);
static XImage *load_image_jpeg(FILE *fl);
static void fill_image(XImage *gimp, char *srcdata, char *destdata,
  int width, int height, int channels, int destdepth,
  long srcrowbytes, long destrowbytes);
static XImage *scale_image(XImage *src, int destwidth, int destheight);

int init_pictures()
{
  table = NULL;
  return TRUE;
}

picture_t *picture_find(unsigned long id)
{
  int ix;
  int buck;
  picture_t *pic;
  XImage *gimp = NULL;
  FILE *fl;
  int closeafter;
  glui32 chunktype;

#ifdef NOTHING_AVAILABLE

  return NULL;

#else /* NOTHING_AVAILABLE */

  if (!imageslegal) {
    return NULL;
  }
  
  if (!table) {
    table = (picture_t **)malloc(HASHSIZE * sizeof(picture_t *));
    if (!table)
      return NULL;
    for (ix=0; ix<HASHSIZE; ix++)
      table[ix] = NULL;
  }
  
  buck = id % HASHSIZE;
  
  for (pic = table[buck]; pic; pic = pic->hash_next) {
    if (pic->id == id)
      break;
  }
  
  if (pic && pic->gimp) {
    pic->refcount++;
    return pic;
  }
  
  /* First, make sure the data exists. */
  
  if (!xres_is_resource_map()) {
    char filename[PATH_MAX];
    unsigned char buf[8];

    sprintf(filename, "PIC%ld", id); 
    /* Could check an environment variable or preference for a directory,
       if we were clever. */

    closeafter = TRUE;
    fl = fopen(filename, "r");
    if (!fl) {
      return NULL;
    }

    if (fread(buf, 1, 8, fl) != 8) {
      /* Can't read the first few bytes. Forget it. */
      fclose(fl);
      return NULL;
    }

    if (!png_sig_cmp(buf, 0, 8)) {
      chunktype = giblorb_ID_PNG;
    }
    else if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
      chunktype = giblorb_ID_JPEG;
    }
    else {
      /* Not a readable file. Forget it. */
      fclose(fl);
      return NULL;
    }

    fseek(fl, 0, 0);
  }
  else {
    long pos;
    xres_get_resource(giblorb_ID_Pict, id, &fl, &pos, NULL, &chunktype);
    if (!fl)
      return NULL;
    fseek(fl, pos, 0);
    closeafter = FALSE;
  }

  gimp = NULL;
  
#ifndef NO_PNG_AVAILABLE
  if (chunktype == giblorb_ID_PNG)
    gimp = load_image_png(fl);
#endif /* NO_PNG_AVAILABLE */

#ifndef NO_JPEG_AVAILABLE
  if (chunktype == giblorb_ID_JPEG)
    gimp = load_image_jpeg(fl);
#endif /* NO_JPEG_AVAILABLE */

  if (closeafter)
    fclose(fl);

  if (!gimp)
    return NULL;

  if (pic) {
    pic->gimp = gimp;
    pic->refcount++;
    return pic;
  }
  
  pic = (picture_t *)malloc(sizeof(picture_t));
  pic->id = id;
  pic->refcount = 1;
  pic->gimp = gimp;
  pic->pix = 0;
  
  pic->width = gimp->width;
  pic->height = gimp->height;
  
  pic->hash_next = table[buck];
  table[buck] = pic;
  
  return pic;

#endif /* NOTHING_AVAILABLE */
}

void picture_release(picture_t *pic)
{
  pic->refcount--;
}

void picture_draw(picture_t *pic, Drawable dest, 
  int xpos, int ypos, int width, int height,
  XRectangle *clipbox)
{
  int destl, destt, destr, destb;

  if (!pic->gimp) {
    XFillRectangle(xiodpy, dest, gctechu, 
      xpos, ypos, width, height);
    XFillRectangle(xiodpy, dest, gctechd, 
      xpos+1, ypos+1, width-2, height-2);
    return;
  }

  if (pic->pix && (width != pic->pixwidth || height != pic->pixheight)) {
    XFreePixmap(xiodpy, pic->pix);
    pic->pix = 0;
  }

  if (!pic->pix) {
    pic->pix = XCreatePixmap(xiodpy, xiowin, width, height, xiodepth);
    if (!pic->pix)
      return;
    pic->pixwidth = width;
    pic->pixheight = height;

    if (width == pic->gimp->width && height == pic->gimp->height) {
      XPutImage(xiodpy, pic->pix, gcfore, pic->gimp,
	0, 0, 0, 0, width, height);
    }
    else {
      XImage *sgimp;
      sgimp = scale_image(pic->gimp, width, height);
      if (sgimp) {
	XPutImage(xiodpy, pic->pix, gcfore, sgimp,
	  0, 0, 0, 0, width, height);
	XDestroyImage(sgimp);
      }
    }
  }

  destl = xpos;
  destt = ypos;
  destr = xpos+width;
  destb = ypos+height;

  if (clipbox) {
    if (destl < clipbox->x)
      destl = clipbox->x;
    if (destt < clipbox->y)
      destt = clipbox->y;
    if (destr > clipbox->x+clipbox->width)
      destr = clipbox->x+clipbox->width;
    if (destb > clipbox->y+clipbox->height)
      destb = clipbox->y+clipbox->height;
  }

  XCopyArea(xiodpy, pic->pix, dest, gcfore, 
    destl-xpos, destt-ypos, destr-destl, destb-destt, destl, destt);
}

void picture_relax_memory()
{
  int ix, buck;
  picture_t *pic;
  
  if (!table)
    return;
  
  for (buck=0; buck<HASHSIZE; buck++) {
    for (pic = table[buck]; pic; pic = pic->hash_next) {
      if (pic->refcount == 0) {
	if (pic->gimp) {
	  XDestroyImage(pic->gimp);
	  pic->gimp = NULL;
	}
	if (pic->pix) {
	  XFreePixmap(xiodpy, pic->pix);
	  pic->pix = 0;
	}
      }
    }
  }
}

static void parse_mask(unsigned long mask, int *noffptr, int *nlenptr)
{
  int noff = 0;
  int nlen = 0;
  int ix;

  if (mask) {
    while ((mask & 1) == 0) {
      noff += 1;
      mask >>= 1;
    }
    while ((mask & 1) == 1) {
      nlen += 1;
      mask >>= 1;
    }
  }

  *noffptr = noff;
  *nlenptr = nlen;
}

#ifndef NO_JPEG_AVAILABLE

static XImage *load_image_jpeg(FILE *fl)
{
  int ix, jx;
  XImage *gimp = NULL;
  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;
  JDIMENSION width, height;
  int channels, destdepth;
  long srcrowbytes, destrowbytes;
  char *destdata = NULL;
  JSAMPARRAY rowarray = NULL; /* this is **JSAMPLE */
  JSAMPLE *srcdata = NULL;
  
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);

  jpeg_stdio_src(&cinfo, fl);

  jpeg_read_header(&cinfo, TRUE);

  /* Image info is now available. */
  
  jpeg_start_decompress(&cinfo);

  /* Output image info is now available. */
  width = cinfo.output_width;
  height = cinfo.output_height;
  channels = cinfo.output_components;

  destdepth = xiodepth;
  destrowbytes = (width * destdepth + 7) / 8;
  if (destrowbytes & 31)
    destrowbytes = (destrowbytes | 31) + 1;
  destdata = malloc(destrowbytes * height);

  srcrowbytes = channels * width;

  rowarray = (JSAMPARRAY)malloc(sizeof(JSAMPROW) * height);
  srcdata = malloc(srcrowbytes * (height+1) + 8);
  /* We leave a bit extra for the dithering algorithm */

  gimp = NULL;

  if (rowarray && srcdata && destdata) {
    for (ix=0; ix<height; ix++) {
      rowarray[ix] = srcdata + (ix*srcrowbytes);
    }
    
    gimp = XCreateImage(xiodpy, DefaultVisual(xiodpy, xioscn), 
      destdepth, ZPixmap, 0, destdata, 
      width, height, 32, destrowbytes);

    if (gimp) {
      while (cinfo.output_scanline < cinfo.output_height) {
	jpeg_read_scanlines(&cinfo, rowarray+cinfo.output_scanline, height);
      }
      jpeg_finish_decompress(&cinfo);

      fill_image(gimp, srcdata, destdata, width, height,
	channels, destdepth, srcrowbytes, destrowbytes);
    }    
  }

  jpeg_destroy_decompress(&cinfo);

  if (rowarray)
    free(rowarray);
  if (srcdata)
    free(srcdata);

  return gimp;
}

#endif /* NO_JPEG_AVAILABLE */

#ifndef NO_PNG_AVAILABLE

static XImage *load_image_png(FILE *fl)
{
  int ix, jx;
  XImage *gimp = NULL;
  png_uint_32 width, height;
  int srcdepth, destdepth;
  int color_type, channels;
  png_uint_32 srcrowbytes, destrowbytes;
  char *destdata = NULL;
  png_structp png_ptr = NULL;
  png_infop info_ptr = NULL;

  /* These are static so that the setjmp/longjmp error-handling of
     libpng doesn't mangle them. Horribly thread-unsafe, but we
     hope we don't run into that. */
  static png_bytep *rowarray;
  static png_bytep srcdata;

  rowarray = NULL;
  srcdata = NULL;

  png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
    NULL, NULL, NULL);
  if (!png_ptr) {
    return NULL;
  }

  info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    png_destroy_read_struct(&png_ptr, NULL, NULL);
    return NULL;
  }

  if (setjmp(png_ptr->jmpbuf)) {
    /* If we jump here, we had a problem reading the file */
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    if (rowarray)
      free(rowarray);
    if (srcdata)
      free(srcdata);
    return NULL;
  }

  png_init_io(png_ptr, fl);

  png_read_info(png_ptr, info_ptr);

  width = png_get_image_width(png_ptr, info_ptr);
  height = png_get_image_height(png_ptr, info_ptr);
  srcdepth = png_get_bit_depth(png_ptr, info_ptr);
  color_type = png_get_color_type(png_ptr, info_ptr);
  channels = png_get_channels(png_ptr, info_ptr);

  destdepth = xiodepth;
  destrowbytes = (width * destdepth + 7) / 8;
  if (destrowbytes & 31)
    destrowbytes = (destrowbytes | 31) + 1;
  destdata = malloc(destrowbytes * height);

  /* 
     printf("destdepth=%d; destrowbytes=%ld\n",
     destdepth, destrowbytes);
  */

  if (srcdepth == 16)
    png_set_strip_16(png_ptr);
  png_set_expand(png_ptr);

  png_read_update_info(png_ptr, info_ptr);
  channels = png_get_channels(png_ptr, info_ptr);

  srcrowbytes = png_get_rowbytes(png_ptr, info_ptr);

  rowarray = malloc(sizeof(png_bytep) * height);
  srcdata = malloc(srcrowbytes * (height+1) + 8);
  /* We leave a bit extra for the dithering algorithm */

  /*
  printf("width=%ld, height=%ld, coltype=%d, channels=%d, "
    "srcdepth=%d; srcrowbytes=%ld\n",
    width, height, color_type, channels, srcdepth, srcrowbytes);
  printf("new channels=%d, srcdepth=%d\n", 
    png_get_channels(png_ptr, info_ptr), 
    png_get_bit_depth(png_ptr, info_ptr));
  */

  gimp = NULL;

  if (rowarray && srcdata && destdata) {
    for (ix=0; ix<height; ix++) {
      rowarray[ix] = srcdata + (ix*srcrowbytes);
    }
    
    gimp = XCreateImage(xiodpy, DefaultVisual(xiodpy, xioscn), 
      destdepth, ZPixmap, 0, destdata, 
      width, height, 32, destrowbytes);

#if 0 /* ### */    
    if (getenv("XGLK_DEBUG")) {
      static int flag = 0;
      if (!flag) {
	flag = 1;
	printf("XImage depth = %d; byte_order = %s; bitmap_bit_order = %s\n",
	  destdepth,
	  ((gimp->byte_order == MSBFirst) ? "MSB" : "LSB"),
	  ((gimp->bitmap_bit_order == MSBFirst) ? "MSB" : "LSB"));
	printf("masks = 0x%lx, 0x%lx, 0x%lx\n",
	  gimp->red_mask, gimp->green_mask, gimp->blue_mask);
      }
    } 
#endif /* ### */

    /* Lab notes:

      XImage depth = 32; byte_order = MSB; bitmap_bit_order = MSB
      masks = 0xff0000, 0xff00, 0xff

      XImage depth = 16; byte_order = MSB; bitmap_bit_order = MSB
      masks = 0xf000, 0xf00, 0xf0

      XImage depth = 16; byte_order = LSB; bitmap_bit_order = LSB
      masks = 0xf800, 0x7e0, 0x1f

      XImage depth = 24; byte_order = LSB; bitmap_bit_order = LSB
      masks = 0xff0000, 0xff00, 0xff
     */

    if (gimp) {
      png_read_image(png_ptr, rowarray); 
      png_read_end(png_ptr, info_ptr);

      fill_image(gimp, srcdata, destdata, width, height,
	channels, destdepth, srcrowbytes, destrowbytes);
    }
  }

  png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
  if (rowarray)
    free(rowarray);
  if (srcdata)
    free(srcdata);

  return gimp;
}

#endif /* NO_PNG_AVAILABLE */

static void fill_image(XImage *gimp, char *srcdata, char *destdata,
  int width, int height, int channels, int destdepth,
  long srcrowbytes, long destrowbytes)
{
  int ix, jx;
  char *srcptr, *destptr;

  char *srcrowptr = srcdata;
  char *destrowptr = destdata;

  if (destdepth == 8 && pixelcube) {
    static struct ditherdata_struct {
      int fraction;
      int xoff, yoff;
    } ditherdata[4] = {
      { 7, 1, 0 },
      { 3, -1, 1 },
      { 5, 0, 1 },
      { 1, 1, 1 }
    };
    if (channels < 3) {
      for (jx=0; jx<height; jx++) {
	srcptr = srcrowptr;
	destptr = destrowptr;
	for (ix=0; ix<width; ix++) {
	  /* pull out the values, 0..255 */
	  int grey = (unsigned char)(srcptr[0]);
	  /* divide into the 0..5 ranges */
	  int greybase = (grey+25) / 51;
	  /* compute the error */
	  destptr[0] = pixelcube[greybase * 36 + greybase * 6 + greybase];
	  if (prefs.ditherimages) { 
	    int greyadd=0;
	    int iix, val, valadd;
	    grey -= greybase*51;
	    /* distribute it. This is annoying. */
	    for (iix = 0; iix < 4; iix++) {
	      struct ditherdata_struct *dith = &(ditherdata[iix]);
	      int byteoff = srcrowbytes * dith->yoff 
		+ channels * dith->xoff;
	      greyadd += (grey * dith->fraction) / 16;
	      val = (unsigned char)srcptr[byteoff];
	      valadd = val+greyadd;
	      if (valadd < 0) {
		srcptr[byteoff] = 0;
		greyadd = valadd;
	      }
	      else if (valadd > 255) {
		srcptr[byteoff] = 255;
		greyadd = valadd-255;
	      }
	      else {
		srcptr[byteoff] = valadd;
		greyadd = 0;
	      }
	    }
	  }
	  srcptr += channels;
	  destptr += 1;
	}
	srcrowptr += srcrowbytes;
	destrowptr += destrowbytes;
      }
    }
    else {
      for (jx=0; jx<height; jx++) {
	srcptr = srcrowptr;
	destptr = destrowptr;
	for (ix=0; ix<width; ix++) {
	  /* pull out the values, 0..255 */
	  int red = (unsigned char)(srcptr[0]);
	  int green = (unsigned char)(srcptr[1]);
	  int blue = (unsigned char)(srcptr[2]);
	  /* divide into the 0..5 ranges */
	  int redbase = (red+25) / 51;
	  int greenbase = (green+25) / 51;
	  int bluebase = (blue+25) / 51;
	  /* compute the error */
	  destptr[0] = pixelcube[redbase * 36 + greenbase * 6 + bluebase];
	  if (prefs.ditherimages) { 
	    int redadd=0, greenadd=0, blueadd=0;
	    int iix, val, valadd;
	    red -= redbase*51;
	    green -= greenbase*51;
	    blue -= bluebase*51;
	    /* distribute it. This is annoying. */
	    for (iix = 0; iix < 4; iix++) {
	      struct ditherdata_struct *dith = &(ditherdata[iix]);
	      int byteoff = srcrowbytes * dith->yoff 
		+ channels * dith->xoff;
	      redadd += (red * dith->fraction) / 16;
	      greenadd += (green * dith->fraction) / 16;
	      blueadd += (blue * dith->fraction) / 16;
	      val = (unsigned char)srcptr[byteoff+0];
	      valadd = val+redadd;
	      if (valadd < 0) {
		srcptr[byteoff+0] = 0;
		redadd = valadd;
	      }
	      else if (valadd > 255) {
		srcptr[byteoff+0] = 255;
		redadd = valadd-255;
	      }
	      else {
		srcptr[byteoff+0] = valadd;
		redadd = 0;
	      }
	      val = (unsigned char)srcptr[byteoff+1];
	      valadd = val+greenadd;
	      if (valadd < 0) {
		srcptr[byteoff+1] = 0;
		greenadd = valadd;
	      }
	      else if (valadd > 255) {
		srcptr[byteoff+1] = 255;
		greenadd = valadd-255;
	      }
	      else {
		srcptr[byteoff+1] = valadd;
		greenadd = 0;
	      }
	      val = (unsigned char)srcptr[byteoff+2];
	      valadd = val+blueadd;
	      if (valadd < 0) {
		srcptr[byteoff+2] = 0;
		blueadd = valadd;
	      }
	      else if (valadd > 255) {
		srcptr[byteoff+2] = 255;
		blueadd = valadd-255;
	      }
	      else {
		srcptr[byteoff+2] = valadd;
		blueadd = 0;
	      }
	    }
	  }
	  srcptr += channels;
	  destptr += 1;
	}
	srcrowptr += srcrowbytes;
	destrowptr += destrowbytes;
      }
    }
  }
  else if (destdepth == 32) {
    int nsb, msb, lsb, ksb;
    if (gimp->byte_order == MSBFirst) {
      nsb = 1;
      msb = 2;
      lsb = 3;
      ksb = 0;
    }
    else {
      nsb = 2;
      msb = 3;
      lsb = 0;
      ksb = 1;
    }
    if (channels < 3) {
      for (jx=0; jx<height; jx++) {
	srcptr = srcrowptr;
	destptr = destrowptr;
	for (ix=0; ix<width; ix++) {
	  destptr[nsb] = srcptr[0];
	  destptr[msb] = srcptr[0];
	  destptr[lsb] = srcptr[0];
	  destptr[ksb] = 0;
	  srcptr += channels;
	  destptr += 4;
	}
	srcrowptr += srcrowbytes;
	destrowptr += destrowbytes;
      }
    }
    else {
      for (jx=0; jx<height; jx++) {
	srcptr = srcrowptr;
	destptr = destrowptr;
	for (ix=0; ix<width; ix++) {
	  destptr[nsb] = srcptr[0]; /* R */
	  destptr[msb] = srcptr[1]; /* G */
	  destptr[lsb] = srcptr[2]; /* B */
	  destptr[ksb] = 0;
	  srcptr += channels;
	  destptr += 4;
	}
	srcrowptr += srcrowbytes;
	destrowptr += destrowbytes;
      }
    }
  }
  else if (destdepth == 24) {
    int rsb, gsb, bsb;
    if (gimp->byte_order == MSBFirst) {
      rsb = 0;
      gsb = 1;
      bsb = 2;
    }
    else {
      rsb = 2;
      gsb = 1;
      bsb = 0;
    }
    if (channels < 3) {
      for (jx=0; jx<height; jx++) {
	srcptr = srcrowptr;
	destptr = destrowptr;
	for (ix=0; ix<width; ix++) {
	  destptr[rsb] = srcptr[0];
	  destptr[gsb] = srcptr[0];
	  destptr[bsb] = srcptr[0];
	  srcptr += channels;
	  destptr += 3;
	}
	srcrowptr += srcrowbytes;
	destrowptr += destrowbytes;
      }
    }
    else {
      for (jx=0; jx<height; jx++) {
	srcptr = srcrowptr;
	destptr = destrowptr;
	for (ix=0; ix<width; ix++) {
	  destptr[rsb] = srcptr[0];
	  destptr[gsb] = srcptr[1];
	  destptr[bsb] = srcptr[2];
	  srcptr += channels;
	  destptr += 3;
	}
	srcrowptr += srcrowbytes;
	destrowptr += destrowbytes;
      }
    }
  }
  else if (destdepth == 16) {
    int msb, lsb;
    int noffr, nlenr, noffg, nleng, noffb, nlenb;
    int ntopr, ntopg, ntopb;
    parse_mask(gimp->red_mask, &noffr, &nlenr);
    parse_mask(gimp->green_mask, &noffg, &nleng);
    parse_mask(gimp->blue_mask, &noffb, &nlenb);
    ntopr = noffr + nlenr;
    ntopg = noffg + nleng;
    ntopb = noffb + nlenb;
    if (gimp->byte_order == MSBFirst) {
      msb = 0;
      lsb = 1;
    }
    else {
      msb = 1;
      lsb = 0;
    }
    if (channels < 3) {
      for (jx=0; jx<height; jx++) {
	srcptr = srcrowptr;
	destptr = destrowptr;
	for (ix=0; ix<width; ix++) {
	  int valr = srcptr[0];
	  int valg = srcptr[0];
	  int valb = srcptr[0];
	  unsigned long pixel;
	  if (ntopr < 8)
	    valr >>= (8-ntopr);
	  else
	    valr <<= (ntopr-8);
	  if (ntopg < 8)
	    valg >>= (8-ntopg);
	  else
	    valg <<= (ntopg-8);
	  if (ntopb < 8)
	    valb >>= (8-ntopb);
	  else
	    valb <<= (ntopb-8);
	  pixel = (valr & gimp->red_mask)
	    | (valg & gimp->green_mask)
	    | (valb & gimp->blue_mask);
	  destptr[msb] = (pixel >> 8) & 0xFF;
	  destptr[lsb] = (pixel) & 0xFF;
	  srcptr += channels;
	  destptr += 2;
	}
	srcrowptr += srcrowbytes;
	destrowptr += destrowbytes;
      }
    }
    else {
      for (jx=0; jx<height; jx++) {
	srcptr = srcrowptr;
	destptr = destrowptr;
	for (ix=0; ix<width; ix++) {
	  int valr = srcptr[0];
	  int valg = srcptr[1];
	  int valb = srcptr[2];
	  unsigned long pixel;
	  if (ntopr < 8)
	    valr >>= (8-ntopr);
	  else
	    valr <<= (ntopr-8);
	  if (ntopg < 8)
	    valg >>= (8-ntopg);
	  else
	    valg <<= (ntopg-8);
	  if (ntopb < 8)
	    valb >>= (8-ntopb);
	  else
	    valb <<= (ntopb-8);
	  pixel = (valr & gimp->red_mask)
	    | (valg & gimp->green_mask)
	    | (valb & gimp->blue_mask);
	  destptr[msb] = (pixel >> 8) & 0xFF;
	  destptr[lsb] = (pixel) & 0xFF;
	  srcptr += channels;
	  destptr += 2;
	}
	srcrowptr += srcrowbytes;
	destrowptr += destrowbytes;
      }
    }
  }
  else if (destdepth == 1) {
    int *acc_error;
    int error, next_error;
    int ix_stop, dx, tmp;

    acc_error = (int *) calloc(width + 2, sizeof (int));
    if (acc_error != NULL) {
      dx = 1;
      ix = 0;
      ix_stop = width - 1;

      for (jx = 0; jx < width + 2; jx++)
	acc_error[jx] = 0;

      for (jx = 0; jx < height; jx++) {
	if (dx == 1)
	  srcptr = srcrowptr;
	else 
	  srcptr = srcrowptr + (channels * (width-1));
	destptr = destrowptr;
	next_error = acc_error[1];
	for (; ix != ix_stop + dx; ix += dx) {
	  if (channels < 3)
	    error = (int)((unsigned char) srcptr[0]) + next_error;
	  else
	    error = ((int)((unsigned char) srcptr[0]) * 54
	      + (int)((unsigned char) srcptr[1]) * 183
	      + (int)((unsigned char) srcptr[2]) * 18) / 255 + next_error;

	  if (error >= 128) {
	    destrowptr[ix/8] &= ~((unsigned char)0x80 >> (ix % 8));
	    error -= 256;
	  }
	  else {
	    destrowptr[ix/8] |= ((unsigned char)0x80 >> (ix % 8));
	  }

	  next_error = acc_error[1 + ix + dx] +  (7 * error) / 16;
	  acc_error[1 + ix - dx]              += (3 * error) / 16;
	  acc_error[1 + ix]                   += (5 * error) / 16;
	  acc_error[1 + ix + dx]              =  (1 * error) / 16;

	  srcptr += (channels * dx);
	}

	ix      -= dx;
	ix_stop -= (dx * (width - 1));
	dx       = -dx;

	srcrowptr += srcrowbytes;
	destrowptr += destrowbytes;
      }

      free(acc_error);
    }
  }
  else { 
    /* All other bit depths, just use a constant pattern. */
    memset(destdata, 0x55, height*destrowbytes);
  }
}

static XImage *scale_image(XImage *src, int destwidth, int destheight)
{
  XImage *dest = NULL;
  int ix, jx;
  int ycounter, xcounter;
  int srcwidth = src->width;
  int srcheight = src->height;
  int depth = src->depth;
  unsigned long srcrowbytes = src->bytes_per_line;
  unsigned long destrowbytes;
  unsigned char *destdata;
  unsigned char *destrow, *srcrow;
  int destiptr, srciptr;
  int *rowmap;

  destrowbytes = (destwidth * depth + 7) / 8;
  if (destrowbytes & 31)
    destrowbytes = (destrowbytes | 31) + 1;
  destdata = (unsigned char *)malloc(destrowbytes * destheight);

  dest = XCreateImage(xiodpy, DefaultVisual(xiodpy, xioscn), 
    depth, ZPixmap, 0, destdata, 
    destwidth, destheight, 32, destrowbytes);
  if (!dest)
    return NULL;

  if (depth == 1)
    rowmap = (int *)calloc((destwidth+1), sizeof(int));
  else
    rowmap = (int *)calloc((destrowbytes+1), sizeof(int));
  destiptr = 0;
  srciptr = 0;
  xcounter = 0;

  if (depth == 1 || depth == 8) {
    for (ix=0; ix<destwidth; ix++) { 
      rowmap[destiptr] = srciptr;
      xcounter += srcwidth;
      while (xcounter >= destwidth) {
	xcounter -= destwidth;
	srciptr += 1;
      }
      destiptr += 1;
    }
  }
  else if (depth == 16) {
    for (ix=0; ix<destwidth; ix++) { 
      rowmap[destiptr] = srciptr;
      rowmap[destiptr+1] = srciptr+1;
      xcounter += srcwidth;
      while (xcounter >= destwidth) {
	xcounter -= destwidth;
	srciptr += 2;
      }
      destiptr += 2;
    }
  }
  else if (depth == 32) {
    for (ix=0; ix<destwidth; ix++) { 
      rowmap[destiptr] = srciptr;
      rowmap[destiptr+1] = srciptr+1;
      rowmap[destiptr+2] = srciptr+2;
      rowmap[destiptr+3] = srciptr+3;
      xcounter += srcwidth;
      while (xcounter >= destwidth) {
	xcounter -= destwidth;
	srciptr += 4;
      }
      destiptr += 4;
    }
  }

  if (depth == 1) {
    destrow = destdata;
    srcrow = src->data;
    ycounter = 0;

    for (jx=0; jx<destheight; jx++) {

      for (ix = 0; ix < destwidth; ix++) {
	int val = rowmap[ix];
	if (srcrow[val/8] & ((unsigned char)0x80 >> (val % 8)))
	  destrow[ix/8] |= ((unsigned char)0x80 >> (ix % 8));
	else
	  destrow[ix/8] &= ~((unsigned char)0x80 >> (ix % 8));
      }

      ycounter += srcheight;
      while (ycounter >= destheight) {
	ycounter -= destheight;
	srcrow += srcrowbytes;
      }
      destrow += destrowbytes;
    }
  }
  else {
    destrow = destdata;
    srcrow = src->data;
    ycounter = 0;

    for (jx=0; jx<destheight; jx++) {

      int val = destwidth * depth / 8;
      for (ix=0; ix<val; ix++) {
	destrow[ix] = srcrow[rowmap[ix]];
      }

      ycounter += srcheight;
      while (ycounter >= destheight) {
	ycounter -= destheight;
	srcrow += srcrowbytes;
      }
      destrow += destrowbytes;
    }
  }

  free(rowmap);

  return dest;
}
