#include "xglk.h"
#include <X11/Xresource.h>
#include <stdlib.h>
#include <string.h>

static char *parse_fontnamespec(char *str, fontnamespec_t **result);
static int string_to_bool(char *str);

static XrmOptionDescRec argtable[] = {
  {"-background", ".background", XrmoptionSepArg, NULL},
  {"-linkcolor", ".linkColor", XrmoptionSepArg, NULL},
  {"-foreground", ".foreground", XrmoptionSepArg, NULL},
  {"-selectcolor", ".selectColor", XrmoptionSepArg, NULL},
  {"-framecolor", ".frameColor", XrmoptionSepArg, NULL},
  {"-frameupcolor", ".frameUpColor", XrmoptionSepArg, NULL},
  {"-framedowncolor", ".frameDownColor", XrmoptionSepArg, NULL},
  {"-ditherimages", ".ditherImages", XrmoptionSepArg, NULL},
  {"-geometry", ".geometry", XrmoptionSepArg, NULL},
  {"-historylength", ".historyLength", XrmoptionSepArg, NULL},
  {"-defprompt", ".defaultPrompts", XrmoptionSepArg, NULL},
  {"-savelength", ".textBuffer.saveLength", XrmoptionSepArg, NULL},
  {"-saveslack", ".textBuffer.saveSlack", XrmoptionSepArg, NULL},
  {"-colorlinks", ".colorLinks", XrmoptionSepArg, NULL},
  {"-underlinelinks", ".underlineLinks", XrmoptionSepArg, NULL},
  {"-marginx", ".Window.margin.x", XrmoptionSepArg, NULL},
  {"-marginy", ".Window.margin.y", XrmoptionSepArg, NULL},
  {"-bufmarginx", ".textBuffer.margin.x", XrmoptionSepArg, NULL},
  {"-bufmarginy", ".textBuffer.margin.y", XrmoptionSepArg, NULL},
  {"-gridmarginx", ".textGrid.margin.x", XrmoptionSepArg, NULL},
  {"-gridmarginy", ".textGrid.margin.y", XrmoptionSepArg, NULL},
  {"-bufbackground", ".textBuffer.background", XrmoptionSepArg, NULL},
  {"-buflinkcolor", ".textBuffer.linkColor", XrmoptionSepArg, NULL},
  {"-bufforeground", ".textBuffer.foreground", XrmoptionSepArg, NULL},
  {"-gridbackground", ".textGrid.background", XrmoptionSepArg, NULL},
  {"-gridlinkcolor", ".textGrid.linkColor", XrmoptionSepArg, NULL},
  {"-gridforeground", ".textGrid.foreground", XrmoptionSepArg, NULL},
};

int xglk_init_preferences(int argc, char *argv[],
  glkunix_startup_t *startdata)
{
  int ix, jx, val;
  char *cx;
  XrmValue xval;
  XColor xcol;
  winprefs_t *wprefs;
  char *resourcestring = XResourceManagerString(xiodpy);
  XrmDatabase db = NULL;
  int errflag = FALSE;

  if (resourcestring) {
    db = XrmGetStringDatabase(resourcestring);
  }

  XrmParseCommand(&db, argtable, 
    sizeof(argtable) / sizeof(XrmOptionDescRec),
    "glk", &argc, argv);

  /* Now the program-specific argument parsing. */
  startdata->argc = 0;
  startdata->argv = (char **)malloc(argc * sizeof(char *));
  
  /* Copy in the program name. */
  startdata->argv[startdata->argc] = argv[0];
  startdata->argc++;
    
  for (ix=1; ix<argc && !errflag; ix++) {
    glkunix_argumentlist_t *argform;
    int inarglist = FALSE;
    char *cx;
        
    for (argform = argform; 
	 argform->argtype != glkunix_arg_End && !errflag; 
	 argform++) {
            
      if (argform->name[0] == '\0') {
	if (argv[ix][0] != '-') {
	  startdata->argv[startdata->argc] = argv[ix];
	  startdata->argc++;
	  inarglist = TRUE;
	}
      }
      else if ((argform->argtype == glkunix_arg_NumberValue)
	&& !strncmp(argv[ix], argform->name, strlen(argform->name))
	&& (cx = argv[ix] + strlen(argform->name))
	&& (atoi(cx) != 0 || cx[0] == '0')) {
	startdata->argv[startdata->argc] = argv[ix];
	startdata->argc++;
	inarglist = TRUE;
      }
      else if (!strcmp(argv[ix], argform->name)) {
	int numeat = 0;
                
	if (argform->argtype == glkunix_arg_ValueFollows) {
	  if (ix+1 >= argc) {
	    printf("%s: %s must be followed by a value\n", 
	      argv[0], argform->name);
	    errflag = TRUE;
	    break;
	  }
	  numeat = 2;
	}
	else if (argform->argtype == glkunix_arg_NoValue) {
	  numeat = 1;
	}
	else if (argform->argtype == glkunix_arg_ValueCanFollow) {
	  if (ix+1 < argc && argv[ix+1][0] != '-') {
	    numeat = 2;
	  }
	  else {
	    numeat = 1;
	  }
	}
	else if (argform->argtype == glkunix_arg_NumberValue) {
	  if (ix+1 >= argc
	    || (atoi(argv[ix+1]) == 0 && argv[ix+1][0] != '0')) {
	    printf("%s: %s must be followed by a number\n", 
	      argv[0], argform->name);
	    errflag = TRUE;
	    break;
	  }
	  numeat = 2;
	}
	else {
	  errflag = TRUE;
	  break;
	}
                
	for (jx=0; jx<numeat; jx++) {
	  startdata->argv[startdata->argc] = argv[ix];
	  startdata->argc++;
	  if (jx+1 < numeat)
	    ix++;
	}
	inarglist = TRUE;
	break;
      }
    }

    if (errflag)
      break;
    if (!inarglist) {
      printf("%s: unknown argument: %s\n", argv[0], argv[ix]);
      errflag = TRUE;
      break;
    }
  }

  if (errflag) {
    printf("usage: %s [ options ... ]\n", argv[0]);
    /*if (glkunix_arguments[0].argtype != glkunix_arg_End) { */
    if (1) {
      glkunix_argumentlist_t *argform;
      printf("game options:\n");
      /*
      for (argform = glkunix_arguments;
	   argform->argtype != glkunix_arg_End;
	   argform++) {
        printf("  %s\n", argform->desc);
      }
      */
    }
    return FALSE;
  }

  /* We've survived argument parsing. Go ahead and use the values
     we've got. */
  prefs.win_w = 500;
  prefs.win_h = 600;
  prefs.win_x = 64;
  prefs.win_y = 10;
  if (db && XrmGetResource(db, 
    "glk.geometry", "Glk.Geometry",
    &cx, &xval)) {
    XParseGeometry(xval.addr, &prefs.win_x, &prefs.win_y,
      &prefs.win_w, &prefs.win_h);
  }

  if (db && XrmGetResource(db,
    "glk.ditherImages", "Glk.DitherImages",
    &cx, &xval)) {
    prefs.ditherimages = string_to_bool(xval.addr);
  }
  else {
    prefs.ditherimages = TRUE;
  }

  if (db && XrmGetResource(db,
    "glk.foreground", "Glk.Foreground",
    &cx, &xval)
    && XParseColor(xiodpy, xiomap, xval.addr, &xcol)) {
    XAllocColor(xiodpy, xiomap, &xcol);
    prefs.forecolor = xcol;
  }
  else {
    xcol.red = xcol.green = xcol.blue = 0;
    XAllocColor(xiodpy, xiomap, &xcol);
    prefs.forecolor = xcol;
    /*prefs.forecolor.pixel = BlackPixel(xiodpy, xioscn);
      XQueryColor(xiodpy, xiomap, &prefs.forecolor);*/
  }

  if (db && XrmGetResource(db,
    "glk.linkColor", "Glk.LinkColor",
    &cx, &xval)
    && XParseColor(xiodpy, xiomap, xval.addr, &xcol)) {
    XAllocColor(xiodpy, xiomap, &xcol);
    prefs.linkcolor = xcol;
  }
  else {
    xcol.red = xcol.green = 0;
    xcol.blue = 65535;
    XAllocColor(xiodpy, xiomap, &xcol);
    prefs.linkcolor = xcol;
  }

  if (db && XrmGetResource(db,
    "glk.background", "Glk.Background",
    &cx, &xval)
    && XParseColor(xiodpy, xiomap, xval.addr, &xcol)) {
    XAllocColor(xiodpy, xiomap, &xcol);
    prefs.backcolor = xcol;
  }
  else {
    xcol.red = xcol.green = xcol.blue = 65535;
    XAllocColor(xiodpy, xiomap, &xcol);
    prefs.backcolor = xcol;
    /*prefs.backcolor.pixel = WhitePixel(xiodpy, xioscn);
      XQueryColor(xiodpy, xiomap, &prefs.backcolor);*/
  }

  if (db && XrmGetResource(db,
    "glk.selectColor", "Glk.SelectColor",
    &cx, &xval))
    XParseColor(xiodpy, xiomap, xval.addr, &xcol);
  else
    XParseColor(xiodpy, xiomap, "blue", &xcol);
  XAllocColor(xiodpy, xiomap, &xcol);
  prefs.selectcolor = xcol;

  if (db && XrmGetResource(db,
    "glk.frameColor", "Glk.FrameColor",
    &cx, &xval))
    XParseColor(xiodpy, xiomap, xval.addr, &xcol);
  else
    XParseColor(xiodpy, xiomap, "grey50", &xcol);
  XAllocColor(xiodpy, xiomap, &xcol);
  prefs.techcolor = xcol;

  if (db && XrmGetResource(db,
    "glk.frameUpColor", "Glk.FrameUpColor",
    &cx, &xval))
    XParseColor(xiodpy, xiomap, xval.addr, &xcol);
  else
    XParseColor(xiodpy, xiomap, "grey75", &xcol);
  XAllocColor(xiodpy, xiomap, &xcol);
  prefs.techucolor = xcol;

  if (db && XrmGetResource(db,
    "glk.frameDownColor", "Glk.FrameDownColor",
    &cx, &xval))
    XParseColor(xiodpy, xiomap, xval.addr, &xcol);
  else
    XParseColor(xiodpy, xiomap, "grey25", &xcol);
  XAllocColor(xiodpy, xiomap, &xcol);
  prefs.techdcolor = xcol;

  if (db && XrmGetResource(db,
    "glk.textBuffer.saveLength", "Glk.TextBuffer.SaveLength",
    &cx, &xval)) 
    prefs.buffersize = atoi(xval.addr);
  else
    prefs.buffersize = 8000;

  if (db && XrmGetResource(db,
    "glk.textBuffer.saveSlack", "Glk.TextBuffer.SaveSlack",
    &cx, &xval)) 
    prefs.bufferslack = atoi(xval.addr);
  else
    prefs.bufferslack = 1000;

  if (db && XrmGetResource(db,
    "glk.historyLength", "Glk.HistoryLength",
    &cx, &xval)) 
    prefs.historylength = atoi(xval.addr);
  else
    prefs.historylength = 20;

  if (db && XrmGetResource(db,
    "glk.defaultPrompts", "Glk.DefaultPrompts",
    &cx, &xval)) {
    prefs.prompt_defaults = string_to_bool(xval.addr);
  }
  else {
    prefs.prompt_defaults = TRUE;
  }

  if (db && XrmGetResource(db,
    "glk.colorLinks", "Glk.ColorLinks",
    &cx, &xval)) {
    prefs.colorlinks = string_to_bool(xval.addr);
  }
  else {
    prefs.colorlinks = TRUE;
  }

  if (db && XrmGetResource(db,
    "glk.underlineLinks", "Glk.UnderlineLinks",
    &cx, &xval)) {
    prefs.underlinelinks = string_to_bool(xval.addr);
  }
  else {
    prefs.underlinelinks = TRUE;
  }

  for (wprefs = &(prefs.textbuffer); 
       wprefs;
       wprefs 
	 = (((wprefs == &(prefs.textbuffer)) ? &(prefs.textgrid) : NULL))) {

    char classbuf[256];
    char *instance;
    if (wprefs == &(prefs.textgrid))
      instance = "textGrid";
    else
      instance = "textBuffer";

    sprintf(classbuf, "%s.%s.margin.x", "glk", instance);
    if (db && XrmGetResource(db,
      classbuf, "Glk.Window.Margin.Width",
      &cx, &xval))
      val = atoi(xval.addr);
    else
      val = 4;
    wprefs->marginx = val;

    sprintf(classbuf, "%s.%s.margin.y", "glk", instance);
    if (db && XrmGetResource(db,
      classbuf, "Glk.Window.Margin.Width",
      &cx, &xval))
      val = atoi(xval.addr);
    else
      val = 2;
    wprefs->marginy = val;

    sprintf(classbuf, "%s.%s.foreground", "glk", instance);
    if (db && XrmGetResource(db,
      classbuf, "Glk.Window.Foreground",
      &cx, &xval)
      && XParseColor(xiodpy, xiomap, xval.addr, &xcol)) {
      XAllocColor(xiodpy, xiomap, &xcol);
    }
    else {
      xcol = prefs.forecolor;
    }
    wprefs->forecolor = xcol;

    sprintf(classbuf, "%s.%s.linkColor", "glk", instance);
    if (db && XrmGetResource(db,
      classbuf, "Glk.Window.LinkColor",
      &cx, &xval)
      && XParseColor(xiodpy, xiomap, xval.addr, &xcol)) {
      XAllocColor(xiodpy, xiomap, &xcol);
    }
    else {
      xcol = prefs.linkcolor;
    }
    wprefs->linkcolor = xcol;

    sprintf(classbuf, "%s.%s.background", "glk", instance);
    if (db && XrmGetResource(db,
      classbuf, "Glk.Window.Background",
      &cx, &xval)
      && XParseColor(xiodpy, xiomap, xval.addr, &xcol)) {
      XAllocColor(xiodpy, xiomap, &xcol);
    }
    else {
      xcol = prefs.backcolor;
    }
    wprefs->backcolor = xcol;

    wprefs->sizehint = FALSE;
    wprefs->fixedhint = FALSE;
    wprefs->attribhint = FALSE;
    wprefs->justhint = FALSE;
    wprefs->indenthint = FALSE;
    wprefs->colorhint = FALSE;

    for (ix=0; ix<style_NUMSTYLES; ix++) {
      static char *stylenames[style_NUMSTYLES] = {
	"normal", "emphasized", "preformatted", "header", "subheader",
	"alert", "note", "blockQuote", "input", "user1", "user2"
      };
      fontprefs_t *fprefs = &(wprefs->style[ix]);

      sprintf(classbuf, "%s.%s.%s.spec", "glk", instance, stylenames[ix]);
      if (db && XrmGetResource(db,
	classbuf, "Glk.Window.Style.Spec",
	&cx, &xval)) {
	cx = xval.addr;
      }
      else {
	if (wprefs == &(prefs.textbuffer))
	  cx = "%p{-adobe-courier-%w{medium,bold}-%o{r,o}-normal-"
	    "-%s{8,10,12,14,18,24}-*-*-*-*-*-iso8859-1,"
	    "-adobe-times-%w{medium,bold}-%o{r,i}-normal-"
	    "-%s{8,10,12,14,18,24,34}-*-*-*-*-*-iso8859-1}";
	else
	  cx = "-adobe-courier-%w{medium,bold}-%o{r,o}-normal-"
	    "-%s{8,10,12,14,18,24}-*-*-*-*-*-iso8859-1";
      }
      fprefs->specname = cx;
      parse_fontnamespec(fprefs->specname, &fprefs->spec);

      sprintf(classbuf, "%s.%s.%s.size", "glk", instance, stylenames[ix]);
      if (db && XrmGetResource(db,
        classbuf, "Glk.Window.Style.Size",
        &cx, &xval)) 
        val = atoi(xval.addr);
      else
	val = 0;
      fprefs->size = val;

      sprintf(classbuf, "%s.%s.%s.weight", "glk", instance, stylenames[ix]);
      if (db && XrmGetResource(db,
        classbuf, "Glk.Window.Style.Weight",
        &cx, &xval)) 
        val = atoi(xval.addr);
      else
	val = (ix == style_Input 
	  || ix == style_Header 
	  || ix == style_Subheader)
	  ? 1 : 0;
      fprefs->weight = val;

      sprintf(classbuf, "%s.%s.%s.oblique", "glk", instance, stylenames[ix]);
      if (db && XrmGetResource(db,
        classbuf, "Glk.Window.Style.Oblique",
        &cx, &xval)) 
        val = atoi(xval.addr);
      else
	val = (ix == style_Emphasized) ? 1 : 0;
      fprefs->oblique = val;

      sprintf(classbuf, "%s.%s.%s.proportional", "glk", instance, stylenames[ix]);
      if (db && XrmGetResource(db,
        classbuf, "Glk.Window.Style.Proportional",
        &cx, &xval)) 
        val = atoi(xval.addr);
      else
	val = (ix == style_Preformatted || wprefs == &(prefs.textgrid)) 
	  ? 0 : 1;
      fprefs->proportional = val;

      sprintf(classbuf, "%s.%s.%s.justify", "glk", instance, stylenames[ix]);
      if (db && XrmGetResource(db,
        classbuf, "Glk.Window.Style.Justify",
        &cx, &xval)) 
        val = atoi(xval.addr);
      else
	val = 1;
      fprefs->justify = val;

      sprintf(classbuf, "%s.%s.%s.indent", "glk", instance, stylenames[ix]);
      if (db && XrmGetResource(db,
        classbuf, "Glk.Window.Style.Indent",
        &cx, &xval)) 
        val = atoi(xval.addr);
      else
	val = 0;
      fprefs->baseindent = val;

      sprintf(classbuf, "%s.%s.%s.parIndent", "glk", instance, stylenames[ix]);
      if (db && XrmGetResource(db,
        classbuf, "Glk.Window.Style.ParIndent",
        &cx, &xval)) 
        val = atoi(xval.addr);
      else
	val = 0;
      fprefs->parindent = val;

      sprintf(classbuf, "%s.%s.%s.foreground", "glk", instance, stylenames[ix]);
      if (db && XrmGetResource(db,
	classbuf, "Glk.Window.Style.Foreground",
	&cx, &xval)
	&& XParseColor(xiodpy, xiomap, xval.addr, &xcol)) {
	XAllocColor(xiodpy, xiomap, &xcol);
      }
      else {
	xcol = wprefs->forecolor;
      }
      fprefs->forecolor = xcol;

      sprintf(classbuf, "%s.%s.%s.linkColor", "glk", instance, stylenames[ix]);
      if (db && XrmGetResource(db,
	classbuf, "Glk.Window.Style.LinkColor",
	&cx, &xval)
	&& XParseColor(xiodpy, xiomap, xval.addr, &xcol)) {
	XAllocColor(xiodpy, xiomap, &xcol);
      }
      else {
	xcol = wprefs->linkcolor;
      }
      fprefs->linkcolor = xcol;

      sprintf(classbuf, "%s.%s.%s.background", "glk", instance, stylenames[ix]);
      if (db && XrmGetResource(db,
	classbuf, "Glk.Window.Style.Background",
	&cx, &xval)
	&& XParseColor(xiodpy, xiomap, xval.addr, &xcol)) {
	XAllocColor(xiodpy, xiomap, &xcol);
      }
      else {
	xcol = wprefs->backcolor;
      }
      fprefs->backcolor = xcol;
    }
  }

  if (db)
    XrmDestroyDatabase(db);

  return TRUE;
}

static int string_to_bool(char *str)
{
  if (*str == 'y' || *str == 'Y')
    return TRUE;
  if (*str == 't' || *str == 'T')
    return TRUE;
  if (*str == '1')
    return TRUE;
  if (*str == 'o' || *str == 'O') {
    if (*str == 'f' || *str == 'F')
      return TRUE;
  }
  return FALSE;
}

typedef struct fontnamespecopt_struct {
  char key; /* 'w', 'o', 's', 'p' */
  int numopts;
  fontnamespec_t **opts;
} fontnamespecopt_t;

#define fnstype_String (0)
#define fnstype_Option (1)
struct fontnamespec_struct {
  int type;
  union {
    char *str;
    fontnamespecopt_t *opt;
  } u;
  fontnamespec_t *next;
};

static char *parse_fontnamespec(char *str, fontnamespec_t **result)
{
  fontnamespec_t **spec;
  char *bx, *tx;

  *result = NULL;
  spec = result;

  while (*str && *str != ',' && *str != '}') {
    *spec = (fontnamespec_t *)malloc(sizeof(fontnamespec_t));
    (*spec)->next = NULL;

    if (*str == '%') {
      fontnamespecopt_t *opt = 
	(fontnamespecopt_t *)malloc(sizeof(fontnamespecopt_t));
      fontnamespec_t *subspec;
      (*spec)->type = fnstype_Option;
      opt->numopts = 0;
      opt->opts = NULL;
      str++;
      opt->key = *str;
      if (*str)
	str++;
      while (*str == '{' || *str == ',') {
	str++;
	str = parse_fontnamespec(str, &subspec);
	if (opt->numopts == 0) {
	  opt->numopts++;
	  opt->opts = 
	    (fontnamespec_t **)malloc(opt->numopts * sizeof(fontnamespec_t *));
	}
	else {
	  opt->numopts++;
	  opt->opts = 
	    (fontnamespec_t **)realloc(opt->opts, 
	      opt->numopts * sizeof(fontnamespec_t *));
	}
	opt->opts[opt->numopts-1] = subspec;
      }
      if (*str == '}')
	str++;
      (*spec)->u.opt = opt;
    }
    else {
      (*spec)->type = fnstype_String;
      bx = str;
      while (*str && *str != ',' && *str != '}' && *str != '%')
	str++;
      tx = (char *)malloc(str+1-bx);
      memcpy(tx, bx, (str-bx));
      tx[str-bx] = '\0';
      (*spec)->u.str = tx;
    }

    spec = &((*spec)->next);
  }

  return str;
}

void xglk_build_fontname(fontnamespec_t *spec, char *buf, 
  int size, int weight, int oblique, int proportional)
{
  char *str = buf;
  int val;

  while (spec) {
    switch (spec->type) {
    case fnstype_String:
      if (spec->u.str == NULL)
	break;
      strcpy(str, spec->u.str);
      str += strlen(spec->u.str);
      break;
    case fnstype_Option:
      if (spec->u.opt->numopts == 0 || spec->u.opt->opts == NULL)
	break;
      switch (spec->u.opt->key) {
      case 'w':
	val = weight;
	break;
      case 'p':
	val = proportional;
	break;
      case 's':
	val = size;
	break;
      case 'o':
	val = oblique;
	break;
      default:
	val = 0;
	break;
      }
      val += ((spec->u.opt->numopts-1) / 2); /* rounding down */
      if (val >= spec->u.opt->numopts)
	val = spec->u.opt->numopts-1;
      if (val < 0)
	val = 0;
      xglk_build_fontname(spec->u.opt->opts[val], str,
	size, weight, oblique, proportional);
      str += strlen(str);
      break;
    }
    spec = spec->next;
  }

  *str = '\0';
}
