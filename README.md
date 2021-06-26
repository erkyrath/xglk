# XGlk: X Windows Implementation of the Glk API.

XGlk Library: version 0.4.11
Glk API which this implements: version 0.6.1.
Designed by Andrew Plotkin <erkyrath@eblong.com>
http://www.eblong.com/zarf/glk/index.html

This is source code for an implementation of the Glk library which runs
under X windows.

## Command-line arguments and X resources:

XGlk can accept command-line arguments both for itself and on behalf
of the underlying program. The ones it accepts for itself are taken
both from X resources and from command-line options. See the PREFS
file for complete information.

## Bugs:

Image display is experimental (read: "probably not broken, but I'm not
guaranteeing anything".)

Images only display on displays with 32 bits per pixel, 24, 16, 8, or
1 (monochrome.) On other displays, images just won't display. Nothing
will appear at all, and glk_image_draw() will return false.

(Note: "16" actually includes 15 bits per pixels (5 per color
component), as well as the extremely weird NeXT color display, which
has 12 bpp.)

I've tested images on displays with all the above depths. However, I
haven't tried a very wide range, nor have I tested each depth with
both endiannesses. I would not be surprised to learn that it fails on
some systems. If you see images displaying badly or not at all,
please do "setenv XGLK_DEBUG", run the program again, and email me
with (1) the command-line output, (2) your machine type and OS, (3)
what you saw. Thanks.

On 8-bit paletted displays, XGlk tries to allocate a 6x6x6 color cube
(the Palette of the Beast), plus assorted text and border colors. If it
can't get all of these, the display goes badly wrong.

## Notes on building this mess:

This is set up for a fairly generic ANSI C compiler. If you get it to
compile under some particular system, please send me mail, and I'll
stick in the relevant #ifdefs and whatnot.

If your compiler can't find X11 headers or libraries, change the
definitions of XLIB and XINCLUDE in the Makefile. There are some
sample values there. If you find other values that work on particular
operating systems, send me email.

If your compiler can't find PNG headers or libraries, change the
definitions of PNGLIB and PNGINCLUDE. If you don't have pnglib
installed on your system at all, uncomment the definition of
-DNO_PNG_AVAILABLE. I am told that pnglib version 0.96 is too old to
work with XGlk. I've tested it with pnglib 1.0.1 and 1.0.3, both of
which are fine.

If your compiler can't find JPEG headers or libraries, change the
definitions of JPEGLIB and JPEGINCLUDE. If you don't have libjpeg
installed on your system at all, uncomment the definition of
-DNO_JPEG_AVAILABLE.

If you get errors in xio.c about fd_set or FD_SET being undefined, put
"-DNEEDS_SELECT_H" in the SYSTEMFLAGS line, as has been done for the
RS6000.

There are a few compile-time options. These are defined in xglk_option.h.
Before you compile, you should go into xglk_option.h and make any changes
necessary. You may also need to edit some include and library paths in
the Makefile.

See the top of the Makefile for comments on installation.

When you compile a Glk program and link it with GlkTerm, you must supply
one more file: you must define a function called glkunix_startup_code(),
and an array glkunix_arguments[]. These set up various Unix-specific
options used by the Glk library. There is a sample "glkstart.c" file
included in this package; you should modify it to your needs.

The glkunix_arguments[] array is a list of command-line arguments that
your program can accept. The library will sort these out of the command
line and pass them on to your code. The array structure looks like this:

```c
typedef struct glkunix_argumentlist_struct {
    char *name;
    int argtype;
    char *desc;
} glkunix_argumentlist_t;

extern glkunix_argumentlist_t glkunix_arguments[];
```

In each entry, name is the option as it would appear on the command line
(including the leading dash, if any.) The desc is a description of the
argument; this is used when the library is printing a list of options.
And argtype is one of the following constants:

    glkunix_arg_NoValue: The argument appears by itself.
    glkunix_arg_ValueFollows: The argument must be followed by another
argument (the value).
    glkunix_arg_ValueCanFollow: The argument may be followed by a value,
optionally. (If the next argument starts with a dash, it is taken to be
a new argument, not the value of this one.)
    glkunix_arg_NumberValue: The argument must be followed by a number,
which may be the next argument or part of this one. (That is, either
"-width 20" or "-width20" will be accepted.)
    glkunix_arg_End: The glkunix_arguments[] array must be terminated
with an entry containing this value.

To accept arbitrary arguments which lack dashes, specify a name of ""
and an argtype of glkunix_arg_ValueFollows.

If you don't care about command-line arguments, you must still define an
empty arguments list, as follows:

```c
glkunix_argumentlist_t glkunix_arguments[] = {
    { NULL, glkunix_arg_End, NULL }
};
```

Here is a more complete sample list:

```c
glkunix_argumentlist_t glkunix_arguments[] = {
    { "", glkunix_arg_ValueFollows, "filename: The game file to load."
},
    { "-hum", glkunix_arg_ValueFollows, "-hum NUM: Hum some NUM." },
    { "-bom", glkunix_arg_ValueCanFollow, "-bom [ NUM ]: Do a bom (on
the NUM, if given)." },
    { "-goo", glkunix_arg_NoValue, "-goo: Find goo." },
    { "-wob", glkunix_arg_NumberValue, "-wob NUM: Wob NUM times." },
    { NULL, glkunix_arg_End, NULL }
};
```

This would match the arguments "thingfile -goo -wob8 -bom -hum song".

After the library parses the command line, it does various occult
rituals of initialization, and then calls glkunix_startup_code().

```c
int glkunix_startup_code(glkunix_startup_t *data);
```

This should return TRUE if everything initializes properly. If it
returns FALSE, the library will shut down without ever calling your
glk_main() function.

The data structure looks like this:

```c
typedef struct glkunix_startup_struct {
    int argc;
    char **argv;
} glkunix_startup_t;
```

The fields are a standard Unix (argc, argv) list, which contain the
arguments you requested from the command line. In deference to custom,
argv[0] is always the program name.

You can put other startup code in glkunix_startup_code(). This should
generally be limited to finding and opening data files. There are a few
Unix Glk library functions which are convenient for this purpose:

```c
strid_t glkunix_stream_open_pathname(char *pathname, glui32 textmode, 
    glui32 rock);
```

This opens an arbitrary file, in read-only mode. Note that this function
is *only* available during glkunix_startup_code(). It is inherent
non-portable; it should not and cannot be called from inside glk_main().

```c
void glkunix_set_base_file(char *filename);
```

This sets the library's idea of the "current directory" for the executing
program. The argument should be the name of a file (not a directory).
When this is set, fileref_create_by_name() will create files in the same
directory as that file, and create_by_prompt() will base default filenames
off of the file. If this is not called, the library works in the Unix
current working directory, and picks reasonable default defaults.

## Notes on the source code:

Functions which begin with glk_ are, of course, Glk API functions. These
are declared in glk.h.

Functions which begin with gli_, xglk_, and other prefixes are
internal to the XGlk library implementation. They don't exist in every
Glk library, because different libraries implement things in different
ways. (In fact, they may be declared differently, or have different
meanings, in different Glk libraries.) These gli_ functions (and other
internal constants and structures) are declared in `xg_internal.h` and
`xglk.h`.

As you can see from the code, I've kept a policy of catching every error
that I can possibly catch, and printing visible warnings.

Thanks to Torbjorn Andersson for monochrome display patches.

## Permissions

The source code in this package is copyright 1998-9 by Andrew Plotkin. You
may copy and distribute it freely, by any means and under any conditions,
as long as the code and documentation is not changed. You may also
incorporate this code into your own program and distribute that, or modify
this code and use and distribute the modified version, as long as you retain
a notice in your program or documentation which mentions my name and the
URL shown above.

## Version history:

0.4.11:
    Upgraded to Glk API version 0.6.1; i.e., a couple of new gestalt
    selectors.
    Fixed dispatch bug for glk_get_char_stream.

0.4.10:
    Fixed a couple of display bugs (one that could cause freezes)

0.4.9:
    Added hyperlink code, and other changes for Glk 0.6.0.
    Improved mouse-clicking code for textgrids.

0.4.8:
    Changed the default save game name to "game.sav".
    Added "-defprompt" switch, to suppress default file names.
    Added glkunix_set_base_file().
    Added support for function keys.

0.4.7:
    Fixed a small bug in image code, sometimes prevented JPEGs from loading.

0.4.6:
    Fixed various problems with Blorb support.

0.4.5:
    Added JPEG image support.

0.4.4:
    Updated for Glk API 0.5.2; that is, added sound channel stubs.
    Made the license a bit friendlier.

0.4.3, 0.4.2:
    Image display slowly nears acceptable functionality.

0.4.1:
    Fixed display of images on 8-bit displays. Also added a -ditherimage
switch and resource (default is "true")
    Fixed text flowing around margin images.

0.4:
    Updated for Glk API 0.5.1.

0.3:
    Updated for Glk API 0.5.

0.2:
    The one true Unix Glk Makefile system.
    Startup code and command-line argument system.

0.1 alpha: initial release.
