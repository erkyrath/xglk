#include <X11/keysym.h>
#include <stdlib.h>
#include <string.h>
#include "xglk.h"
#include "xg_internal.h"
#include "xg_win_textgrid.h"
#include "xg_win_textbuf.h"

/* The first 256 commands are the standard character set. 
   The second 256 are special keyboard keys. 
   The third 256 are option-key combinations. */
#define NUMCOMMANDS (768)
#define NUMMACROS (256)
#define MACRO_MASK (0xff)

typedef struct keymap_struct {
  cmdentry_t *keycmds[NUMCOMMANDS];
} keymap_t;

typedef struct binding_struct {
  unsigned int key; 
  char *name;
} binding_t;

#define range_Printable (1)
#define range_Typable (2)

static char *macrolist[NUMMACROS];
static int modify_mode;

static keymap_t *global_map = NULL;
static keymap_t *msg_char_map = NULL;
static keymap_t *msg_line_map = NULL;
static keymap_t *win_textgrid_map = NULL;
static keymap_t *win_textgrid_char_map = NULL;
static keymap_t *win_textgrid_line_map = NULL;
static keymap_t *win_textbuffer_map = NULL;
static keymap_t *win_textbuffer_paging_map = NULL;
static keymap_t *win_textbuffer_char_map = NULL;
static keymap_t *win_textbuffer_line_map = NULL;

static char *xkey_get_key_name(int key);
static cmdentry_t *xkey_find_cmd_by_name(char *str);
static keymap_t *new_keymap(binding_t *bindlist);
static void keymap_add_multiple(keymap_t *map, int range, char *func);

static cmdentry_t mastertable[] = {
  {xgc_insert, -1, 0, "insert-self"},
  {xgc_getchar, -1, 0, "getchar-self"},
  {xgc_enter, op_Enter, 0, "enter-line"},

  {xgc_movecursor, op_ForeChar, 0, "forward-char"},
  {xgc_movecursor, op_BackChar, 0, "backward-char"},
  {xgc_movecursor, op_ForeWord, 0, "forward-word"},
  {xgc_movecursor, op_BackWord, 0, "backward-word"},
  {xgc_movecursor, op_ForeLine, 0, "forward-line"},
  {xgc_movecursor, op_BackLine, 0, "backward-line"},
  {xgc_movecursor, op_BeginLine, 0, "beginning-of-line"},
  {xgc_movecursor, op_EndLine, 0, "end-of-line"},

  {xgc_scroll, op_DownPage, 0, "scroll-down"},
  {xgc_scroll, op_UpPage, 0, "scroll-up"},
  {xgc_scroll, op_DownLine, 0, "scroll-down-line"},
  {xgc_scroll, op_UpLine, 0, "scroll-up-line"},
  {xgc_scroll, op_ToBottom, 0, "scroll-to-bottom"},
  {xgc_scroll, op_ToTop, 0, "scroll-to-top"},

  {xgc_delete, op_ForeChar, 0, "delete-next-char"},
  {xgc_delete, op_BackChar, 0, "delete-char"},
  {xgc_delete, op_ForeWord, 0, "delete-next-word"},
  {xgc_delete, op_BackWord, 0, "delete-word"},

  {xgc_cutbuf, op_Yank, 0, "yank-scrap"},
  {xgc_cutbuf, op_YankReplace, 0, "yank-scrap-replace"},
  {xgc_cutbuf, op_Copy, 0, "copy-region"},
  {xgc_cutbuf, op_Kill, 0, "kill-line"},
  {xgc_cutbuf, op_Wipe, 0, "kill-region"},
  {xgc_cutbuf, op_Erase, 0, "erase-region"},
  {xgc_cutbuf, op_Untype, 0, "kill-input"},

  {xgc_history, op_BackLine, 0, "backward-history"},
  {xgc_history, op_ForeLine, 0, "forward-history"},

  {xgc_focus, op_ForeWin, 0, "focus-forward"},
  {xgc_redraw, op_AllWindows, 0, "redraw-all-windows"},

  {xgc_work_meta, op_Cancel, 1, "cancel"},
  {xgc_work_meta, op_Meta, 0, "meta"},
  {xgc_work_meta, op_ExplainKey, 0, "explain-key"},
  
  {xgc_noop, -1, 0, "no-op"},

  {NULL, 0, 0, NULL}
};

#define KEYSYM(ksym) (0x100 | ((ksym) & 0xff))
#define META(ksym)   (0x200 | ((ksym) & 0xff))

static binding_t global_bindings[] = {

  {'\007' /* ctrl-G */, "cancel"},
  {META('\007') /* ctrl-G */, "cancel"},
  {KEYSYM(XK_Escape), "meta"},
  {KEYSYM(XK_Help), "explain-key"},
  {META('x'), "explain-key"},

  {KEYSYM(XK_Tab), "focus-forward"},
  {'\014' /* ctrl-L */, "redraw-all-windows"},

  {0, NULL}
};

static binding_t win_textbuffer_bindings[] = {

  {'\001' /* ctrl-A */, "beginning-of-line"},
  {'\005' /* ctrl-E */, "end-of-line"},
  {'\002' /* ctrl-B */, "backward-char"},
  {KEYSYM(XK_Left), "backward-char"},
  {KEYSYM(XK_KP_Left), "backward-char"},
  {'\006' /* ctrl-F */, "forward-char"},
  {KEYSYM(XK_Right), "forward-char"},
  {KEYSYM(XK_KP_Right), "forward-char"},
  {META('f'), "forward-word"},
  {META('b'), "backward-word"},

  {'\026' /* ctrl-V */, "scroll-down"},
  {KEYSYM(XK_Page_Down), "scroll-down"},
  {KEYSYM(XK_KP_Page_Down), "scroll-down"},
  {META('v'), "scroll-up"},
  {KEYSYM(XK_Page_Up), "scroll-up"},
  {KEYSYM(XK_KP_Page_Up), "scroll-up"},

  {META('<'), "scroll-to-top"},
  {META('>'), "scroll-to-bottom"},

  {META('w'), "copy-region"},
  {'\027' /* ctrl-W */, "copy-region"},

  {0, NULL}
};

static binding_t win_textgrid_bindings[] = {

  {'\001' /* ctrl-A */, "beginning-of-line"},
  {'\005' /* ctrl-E */, "end-of-line"},
  {'\002' /* ctrl-B */, "backward-char"},
  {KEYSYM(XK_Left), "backward-char"},
  {KEYSYM(XK_KP_Left), "backward-char"},
  {'\006' /* ctrl-F */, "forward-char"},
  {KEYSYM(XK_Right), "forward-char"},
  {KEYSYM(XK_KP_Right), "forward-char"},
  {META('f'), "forward-word"},
  {META('b'), "backward-word"},

  {META('w'), "copy-region"},
  {'\027' /* ctrl-W */, "copy-region"},

  {0, NULL}
};

static binding_t win_textbuffer_char_bindings[] = {
  {0, NULL}
};

static binding_t win_textgrid_char_bindings[] = {
  {0, NULL}
};

static binding_t msg_char_bindings[] = {
  {0, NULL}
};

static binding_t win_textbuffer_line_bindings[] = {
  {KEYSYM(XK_Down), "forward-history"},
  {KEYSYM(XK_KP_Down), "forward-history"},
  {'\016' /* ctrl-N */, "forward-history"},
  {KEYSYM(XK_Up), "backward-history"},
  {KEYSYM(XK_KP_Up), "backward-history"},
  {'\020' /* ctrl-P */, "backward-history"},
  {'\004' /* ctrl-D */, "delete-next-char"},
  {KEYSYM(XK_Delete), "delete-char"},
  {KEYSYM(XK_KP_Delete), "delete-char"},
  {KEYSYM(XK_BackSpace), "delete-char"},
  {'\177' /* delete */, "delete-char"},
  {'\010' /* ctrl-H */, "delete-char"},
  {'\n' /* newline */, "enter-line"},
  {'\r' /* return */, "enter-line"},
  {KEYSYM(XK_Return), "enter-line"},
  {KEYSYM(XK_KP_Enter), "enter-line"},

  {'\013' /* ctrl-K */, "kill-line"},
  {'\025' /* ctrl-U */, "kill-input"},
  {'\027' /* ctrl-W */, "kill-region"},
  {'\031' /* ctrl-Y */, "yank-scrap"},

  /*### macros */

  {0, NULL}
};

static binding_t win_textgrid_line_bindings[] = {
  {KEYSYM(XK_Down), "forward-history"},
  {KEYSYM(XK_KP_Down), "forward-history"},
  {'\016' /* ctrl-N */, "forward-history"},
  {KEYSYM(XK_Up), "backward-history"},
  {KEYSYM(XK_KP_Up), "backward-history"},
  {'\020' /* ctrl-P */, "backward-history"},
  {'\004' /* ctrl-D */, "delete-next-char"},
  {KEYSYM(XK_Delete), "delete-char"},
  {KEYSYM(XK_KP_Delete), "delete-char"},
  {KEYSYM(XK_BackSpace), "delete-char"},
  {'\177' /* delete */, "delete-char"},
  {'\010' /* ctrl-H */, "delete-char"},
  {'\n' /* newline */, "enter-line"},
  {'\r' /* return */, "enter-line"},
  {KEYSYM(XK_Return), "enter-line"},
  {KEYSYM(XK_KP_Enter), "enter-line"},

  {'\013' /* ctrl-K */, "kill-line"},
  {'\025' /* ctrl-U */, "kill-input"},
  {'\027' /* ctrl-W */, "kill-region"},
  {'\031' /* ctrl-Y */, "yank-scrap"},

  {0, NULL}
};

static binding_t msg_line_bindings[] = {
  {'\001' /* ctrl-A */, "beginning-of-line"},
  {'\005' /* ctrl-E */, "end-of-line"},
  {'\002' /* ctrl-B */, "backward-char"},
  {KEYSYM(XK_Left), "backward-char"},
  {KEYSYM(XK_KP_Left), "backward-char"},
  {'\006' /* ctrl-F */, "forward-char"},
  {KEYSYM(XK_Right), "forward-char"},
  {KEYSYM(XK_KP_Right), "forward-char"},
  {META('f'), "forward-word"},
  {META('b'), "backward-word"},

  {'\004' /* ctrl-D */, "delete-next-char"},
  {KEYSYM(XK_Delete), "delete-char"},
  {KEYSYM(XK_BackSpace), "delete-char"},
  {'\177' /* delete */, "delete-char"},
  {'\010' /* ctrl-H */, "delete-char"},
  {'\n' /* newline */, "enter-line"},
  {'\r' /* return */, "enter-line"},
  {KEYSYM(XK_Return), "enter-line"},
  {KEYSYM(XK_KP_Enter), "enter-line"},

  {0, NULL}
};

static binding_t win_textbuffer_paging_bindings[] = {
  {'\n' /* newline */, "scroll-down"},
  {'\r' /* return */, "scroll-down"},
  {KEYSYM(XK_Return), "scroll-down"},
  {KEYSYM(XK_KP_Enter), "scroll-down"},

  {META('<'), "scroll-to-top"},
  {META('>'), "scroll-to-bottom"},

  {0, NULL}
};

int init_xkey()
{
  int ix;

  modify_mode = op_Cancel;

  for (ix=0; ix<NUMMACROS; ix++) {
    macrolist[ix] = NULL;
  }
  
  global_map = new_keymap(global_bindings);
  if (!global_map)
    return FALSE;

  win_textbuffer_map = new_keymap(win_textbuffer_bindings);
  win_textbuffer_paging_map = new_keymap(win_textbuffer_paging_bindings);
  win_textbuffer_char_map = new_keymap(win_textbuffer_char_bindings);
  win_textbuffer_line_map = new_keymap(win_textbuffer_line_bindings);
  if (!win_textbuffer_map 
    || !win_textbuffer_paging_map 
    || !win_textbuffer_char_map 
    || !win_textbuffer_line_map)
    return FALSE;
  keymap_add_multiple(win_textbuffer_char_map, range_Typable, "getchar-self");
  keymap_add_multiple(win_textbuffer_paging_map, range_Printable, "scroll-down");
  keymap_add_multiple(win_textbuffer_line_map, range_Printable, "insert-self");

  win_textgrid_map = new_keymap(win_textgrid_bindings);
  win_textgrid_char_map = new_keymap(win_textgrid_char_bindings);
  win_textgrid_line_map = new_keymap(win_textgrid_line_bindings);
  if (!win_textgrid_map 
    || !win_textgrid_char_map 
    || !win_textgrid_line_map)
    return FALSE;
  keymap_add_multiple(win_textgrid_char_map, range_Typable, "getchar-self");
  keymap_add_multiple(win_textgrid_line_map, range_Printable, "insert-self");

  msg_char_map = new_keymap(msg_char_bindings);
  msg_line_map = new_keymap(msg_line_bindings);
  if (!msg_char_map || !msg_line_map)
    return FALSE;
  keymap_add_multiple(msg_char_map, range_Typable, "getchar-self");
  keymap_add_multiple(msg_line_map, range_Printable, "insert-self");

  return TRUE;
}

char *xkey_get_macro(int key)
{
  key &= MACRO_MASK;
  return macrolist[key];
}

static keymap_t *new_keymap(binding_t *bindlist)
{
  keymap_t *res;
  int ix;
  int keynum;
  cmdentry_t *cmd;
  binding_t *bx;
  char *cx;

  res = (keymap_t *)malloc(sizeof(keymap_t));
  if (!res)
    return NULL;
  
  for (ix=0; ix<NUMCOMMANDS; ix++) {
    res->keycmds[ix] = NULL;
  }
  
  for (bx=bindlist; bx->name; bx++) {
    ix = (bx->key);
    cmd = xkey_find_cmd_by_name(bx->name);
    if (cmd) {
      keynum = ix;
      res->keycmds[keynum] = cmd;
    }
  }

  return res;
}

static void keymap_add_multiple(keymap_t *map, int range, char *func)
{
  cmdentry_t *cmd;
  int ix;
  
  cmd = xkey_find_cmd_by_name(func);
  
  if (!cmd) {
    return;
  }
  
  /* ### both of these are non-ideal */

  switch (range) {
  case range_Printable:
    for (ix = 32; ix <= 255; ix++) {
      if (ix >= 127 && ix < 160)
	continue;
      if (!map->keycmds[ix])
	map->keycmds[ix] = cmd;
    }
    break;
  case range_Typable:
    for (ix = 0; ix <= 511; ix++) {
      if (ix == KEYSYM(XK_Tab))
	continue;
      if (!map->keycmds[ix])
	map->keycmds[ix] = cmd;
    }
    break;
  }
}

void xkey_set_macro(int key, char *str, int chown)
{
  char *cx;
  
  key &= MACRO_MASK;

  if (macrolist[key]) {
    free(macrolist[key]);
    macrolist[key] = NULL;
  }
  
  if (str) {
    if (chown) {
      macrolist[key] = str;
    }
    else {
      cx = (char *)malloc(strlen(str)+1);
      strcpy(cx, str);
      macrolist[key] = cx;
    }
  }
}

#define TEST_KEY_MAP(mp, ky, rs)  \
  if ((mp) && ((rs) = (mp)->keycmds[ky]))  \
    return (rs); 

/* check keymap for a single window -- or nonwindow. */
static cmdentry_t *xkey_parse_key(int key, window_t *win)
{
  cmdentry_t *res;
  
  if (!win) {
    TEST_KEY_MAP(global_map, key, res);
    if (xmsg_msgmode) {
      if (xmsg_msgmode == xmsg_mode_Char) {
	TEST_KEY_MAP(msg_char_map, key, res);
      }
      else if (xmsg_msgmode == xmsg_mode_Line) {
	TEST_KEY_MAP(msg_line_map, key, res);
      }
    }
  }
  else {
    switch (win->type) {
    case wintype_TextGrid:
      if (!xmsg_msgmode) {
	if (win->char_request) {
	  TEST_KEY_MAP(win_textgrid_char_map, key, res);
	}
	else if (win->line_request) {
	  TEST_KEY_MAP(win_textgrid_line_map, key, res);
	}
      }
      TEST_KEY_MAP(win_textgrid_map, key, res);
      break;
    case wintype_TextBuffer:
      if (win_textbuffer_is_paging(win)) {
	TEST_KEY_MAP(win_textbuffer_paging_map, key, res);
      }
      if (!xmsg_msgmode) {
	if (win->char_request) {
	  TEST_KEY_MAP(win_textbuffer_char_map, key, res);
	}
	else if (win->line_request) {
	  TEST_KEY_MAP(win_textbuffer_line_map, key, res);
	}
      }
      TEST_KEY_MAP(win_textbuffer_map, key, res);
      break;
    default:
      break;
    }
  }

  return NULL;
}

static char *xkey_get_key_name(int key)
{
  static char buf[32];
  KeySym ksym;
  char *prefix, *name;

  if ((key & 0xff00) == 0x100) {
    key &= 0xff;
    ksym = (KeySym)((XK_Home & 0xff00) | key); 
    name = XKeysymToString(ksym);
    if (!name)
      name = "Unknown key";
    strcpy(buf, name);
    return buf;
  }
  
  if (key & 0xff00) {
    key &= 0xff;
    prefix = "meta-";
  }
  else {
    prefix = "";
  }

  if (key < 32) {
    sprintf(buf, "%sctrl-%c", prefix, key+'A'-1);	
  }
  else {
    sprintf(buf, "%s%c", prefix, key);
  }

  return buf;
}

static cmdentry_t *xkey_find_cmd_by_name(char *str)
{
  cmdentry_t *retval;

  for (retval = mastertable; retval->func; retval++) {
    if (!strcmp(str, retval->name))
      return retval;
  }
  return NULL;
}

void xkey_perform_key(int key, unsigned int state)
{
  cmdentry_t *command = NULL;
  window_t *cmdwin = NULL;
  int op;

  if (modify_mode == op_Meta) {
    modify_mode = op_Cancel;
    if ((key & 0xFF00) == 0) {
      key |= 0x200;
    }
  }

  if (gli_focuswin) {
    command = xkey_parse_key(key, gli_focuswin);
    cmdwin = gli_focuswin;
  }
  if (!command) {
    command = xkey_parse_key(key, NULL);
    cmdwin = NULL;
  }

  if (modify_mode == op_ExplainKey) {
    char buf[128];
    char *cx, *cxmac;
    modify_mode = op_Cancel;
    cx = xkey_get_key_name(key);
    cxmac = xkey_get_macro(key);
    if (!command)
      sprintf(buf, "Key <%s> is not bound", cx);
    else if (!cxmac)
      sprintf(buf, "Key <%s>: %s", cx, command->name);
    else {
      if (strlen(cxmac) < sizeof(buf) - 64)
	sprintf(buf, "Key <%s>: %s \"%s\"", cx, command->name, cxmac);
      else {
	sprintf(buf, "Key <%s>: %s \"", cx, command->name);
	strncat(buf, cxmac, sizeof(buf) - 64);
	strcat(buf, "...\"");
      }
    }
    xmsg_set_message(buf, FALSE);
    return;
  }

  if (!command && gli_rootwin) {
    /* look for a focuswin which knows this key */
    window_t *win = gli_focuswin;
    cmdentry_t *altcommand = NULL;
    do {
      win = gli_window_fixiterate(win);
      if (win && win->type != wintype_Pair) {
	altcommand = xkey_parse_key(key, win);
	if (altcommand)
	  break;
      }
    } while (win != gli_focuswin);
    if (win != gli_focuswin && altcommand) {
      command = altcommand;
      cmdwin = win;
      gli_set_focus(win);
    }
  }

  if (command) {
    if (command->operand == (-1)) {
      /* Translate a key or keysym to a Glk code. */
      if (key & 0xff00) {
	KeySym ksym;
	key &= 0xff;
	ksym = (KeySym)((XK_Home & 0xff00) | key);
	switch (ksym) {
	case XK_Left:
	case XK_KP_Left:
	  op = keycode_Left; 
	  break;
	case XK_Right:
	case XK_KP_Right:
	  op = keycode_Right; 
	  break;
	case XK_Up:
	case XK_KP_Up:
	  op = keycode_Up; 
	  break;
	case XK_Down:
	case XK_KP_Down:
	  op = keycode_Down; 
	  break;
	case XK_Page_Up:
	case XK_KP_Page_Up:
	  op = keycode_PageUp; 
	  break;
	case XK_Page_Down:
	case XK_KP_Page_Down:
	  op = keycode_PageDown; 
	  break;
	case XK_Home:
	case XK_KP_Home:
	case XK_Begin:
	case XK_KP_Begin:
	  op = keycode_Home; 
	  break;
	case XK_End:
	case XK_KP_End:
	  op = keycode_End; 
	  break;
	case XK_Return:
	case XK_KP_Enter:
	case XK_Linefeed:
	  op = keycode_Return;
	  break;
	case XK_BackSpace:
	case XK_Delete:
	case XK_KP_Delete:
	  op = keycode_Delete;
	  break;
	case XK_Escape:
	  op = keycode_Escape;
	  break;
	case XK_F1:
	case XK_KP_F1:
	  op = keycode_Func1;
	  break;
	case XK_F2:
	case XK_KP_F2:
	  op = keycode_Func2;
	  break;
	case XK_F3:
	case XK_KP_F3:
	  op = keycode_Func3;
	  break;
	case XK_F4:
	case XK_KP_F4:
	  op = keycode_Func4;
	  break;
	case XK_F5:
	  op = keycode_Func5;
	  break;
	case XK_F6:
	  op = keycode_Func6;
	  break;
	case XK_F7:
	  op = keycode_Func7;
	  break;
	case XK_F8:
	  op = keycode_Func8;
	  break;
	case XK_F9:
	  op = keycode_Func9;
	  break;
	case XK_F10:
	  op = keycode_Func10;
	  break;
	case XK_F11:
	  op = keycode_Func11;
	  break;
	case XK_F12:
	  op = keycode_Func12;
	  break;
	default:
	  op = keycode_Unknown;
	  break;
	}
      }
      else {
	switch (key) {
	case '\177':
	  op = keycode_Delete;
	  break;
	default:
	  op = key;
	  break;
	}
      }
    }
    else {
      op = command->operand;
    }
    (*(command->func))(cmdwin, op);
    
  }
  else {
    char buf[128];
    char *cx;
    cx = xkey_get_key_name(key);
    sprintf(buf, "Key <%s> not bound", cx);
    xmsg_set_message(buf, FALSE);
  }
}

void xkey_guess_focus()
{
  window_t *altwin;
  
  if (xmsg_msgmode) {
    gli_set_focus(NULL);
    return;
  }

  if (gli_focuswin 
    && (gli_focuswin->line_request || gli_focuswin->char_request)) {
    return;
  }
  
  altwin = gli_focuswin;
  do {
    altwin = gli_window_fixiterate(altwin);
    if (altwin 
      && (altwin->line_request || altwin->char_request)) {
      break;
    }
  } while (altwin != gli_focuswin);
  
  gli_set_focus(altwin);
}

void xgc_work_meta(struct glk_window_struct *dummy, int op)
{
  switch (op) {
  case op_Cancel:
    xmsg_set_message("Cancelled.", FALSE);
    modify_mode = op_Cancel;
    break;
  case op_Meta:
    modify_mode = op_Meta;
    break;
  case op_ExplainKey:
    xmsg_set_message("Type a key to explain.", FALSE);
    modify_mode = op_ExplainKey;
    break;
  /*###case op_DefineMacro:
    xmsg_set_message("Select some text, and type a macro command key to define.", 
      FALSE);
    modify_mode = op_DefineMacro;
    break; ###*/
  }
}

