#include <tcl.h>
#include <tk.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <X11/Xutil.h>          // XLookupString()

#include "sendunicode.h"

#define NS "::tkxwin"

// dict of hotkey/script pairs
//   key : hotkey
//   value : script
static Tcl_Obj *hotkeyInfo;

// dict of grabbed window
//   key : grabbed window
//   value : callback name
static Tcl_Obj *grabkeyInfo;

// x error handler
static int
IgnoreError(Display *dpy, XErrorEvent *ev)
{
	return 0;
}

static void MyEvalObjEx(Tcl_Interp *interp, Tcl_Obj *obj)
{
	Tcl_IncrRefCount(obj);
	if (Tcl_EvalObjEx(interp, obj, 0) != TCL_OK) {
		fprintf(stderr, "Tcl_EvalObjEx() returns without TCL_OK, script :\n%s\n", Tcl_GetString(obj));
		fprintf(stderr, "%s\n", Tcl_ErrnoMsg(Tcl_GetErrno()));
	}
	Tcl_DecrRefCount(obj);
}

// callback
// handle KeyPress events that caused by registerHotkey or grabKey
static int GenericProc(ClientData clientData, XEvent *eventPtr)
{
	Tcl_Interp *interp = clientData;
	if ((eventPtr->type == KeyPress)) {

		Tk_Window tkwin = Tk_MainWindow(interp);
		Display *dpy = Tk_Display(tkwin);
		Window root = DefaultRootWindow(dpy);

		Tcl_Obj *keyObj = Tcl_ObjPrintf("%d+%d",
		                                eventPtr->xkey.keycode,
		                                eventPtr->xkey.state);
		// fprintf(stderr, "GenericProc : KeyPress : %s\n", Tcl_GetString(key));
		Tcl_Obj *scriptObj;
		Tcl_DictObjGet(interp, hotkeyInfo, keyObj, &scriptObj);
		if (scriptObj && eventPtr->xkey.window == root) {
			// called by hotkey
			// run registered script
			MyEvalObjEx(interp, scriptObj);
			return 1;
		} else {
			Tcl_Obj *objWinid = Tcl_NewIntObj(eventPtr->xkey.window);
			Tcl_Obj *objProcname;
			Tcl_IncrRefCount(objWinid);
			Tcl_DictObjGet(interp, grabkeyInfo, objWinid, &objProcname);
			Tcl_DecrRefCount(objWinid);
			if (!objProcname) {
				// not a grabbed window
				return 0;
			}

			// called from grabbed window
#define STRSIZE 1000
			KeySym ks;
			char str[STRSIZE + 1];
			int nbytes;
			// return number of characters bytes
			nbytes = XLookupString(&eventPtr->xkey, str, STRSIZE, &ks, NULL);
			// get keysym name
			char *symstr = XKeysymToString(ks);
			// fprintf(stderr, "symstr=%s\n", symstr);

			// str is not null-terminated ?
			str[nbytes] = '\0';

			// fprintf(stderr,
			//         "GenericProc : state=0x%x, keycode=0x%x, keysym=0x%lx, str=%s, nbytes=%d\n",
			//         eventPtr->xkey.state, eventPtr->xkey.keycode, ks, str,
			//         nbytes);

			Tcl_Obj *callback_result;
			int callback_return = 1; // initially, set 1 (true)
			// exec callback proc
			if (ks != NoSymbol) {
				// create script string
				Tcl_Obj *script = Tcl_NewObj();
				Tcl_IncrRefCount(script);
				// use Tcl_ListObjAppendList() to escape characters like \ { } [ ].
				// symstr or str may contain those characters.
				Tcl_ListObjAppendList(interp, script, objProcname);
				Tcl_ListObjAppendElement(interp, script, Tcl_NewIntObj(eventPtr->xkey.window));
				Tcl_ListObjAppendElement(interp, script, Tcl_NewIntObj(eventPtr->xkey.state));
				Tcl_ListObjAppendElement(interp, script, Tcl_NewIntObj(eventPtr->xkey.keycode));
				Tcl_ListObjAppendElement(interp, script, Tcl_NewStringObj(symstr, -1));
				Tcl_ListObjAppendElement(interp, script, Tcl_NewStringObj(str, -1));
				// fprintf(stderr, "script=%s\n", Tcl_GetString(script));
				MyEvalObjEx(interp, script);
				Tcl_DecrRefCount(script);

				// if result is not true value, send original event to target
				callback_result = Tcl_GetObjResult(interp);
				Tcl_IncrRefCount(callback_result);
				// fprintf(stderr, "GenericProc : callback_result : %s\n", Tcl_GetString(callback_result));
				Tcl_GetBooleanFromObj(interp, callback_result, &callback_return);
				Tcl_DecrRefCount(callback_result);

			}
			if ((ks == NoSymbol) || !callback_return) {
				// send original event
				XSendEvent(eventPtr->xkey.display, eventPtr->xkey.window,
				           True, KeyPressMask, eventPtr);
			}
			return 1;
		}
	}

	return 0;
}

// register hotkey
// append to hotkeyInfo
static int AppendHotkey(Tcl_Interp *interp, int keycode, unsigned int modifiers,
                        Tcl_Obj *script)
{
	Tk_Window tkwin = Tk_MainWindow(interp);
	if (!tkwin) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Tk_MainWindow() return null", -1));
		return TCL_ERROR;
	}
	Display *dpy = Tk_Display(tkwin);
	if (!dpy) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Tk_Display() return null", -1));
		return TCL_ERROR;
	}
	Window root = DefaultRootWindow(dpy);

	XGrabKey(dpy, keycode, modifiers, root,
	         True, GrabModeAsync, GrabModeAsync);

	Tcl_Obj *key = Tcl_ObjPrintf("%d+%d", keycode, modifiers);
	int ret = Tcl_DictObjPut(interp, hotkeyInfo, key, script);
	if (ret == TCL_ERROR) {
		return TCL_ERROR;
	}

	return TCL_OK;
}

// unregister hotkey
// remove from hotkeyInfo
static int RemoveHotkey(Tcl_Interp *interp, int keycode, unsigned int modifiers)
{
	Tk_Window tkwin = Tk_MainWindow(interp);
	if (!tkwin) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Tk_MainWindow() return null", -1));
		return TCL_ERROR;
	}
	Display *dpy = Tk_Display(tkwin);
	if (!dpy) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Tk_Display() return null", -1));
		return TCL_ERROR;
	}
	Window root = DefaultRootWindow(dpy);
	XUngrabKey(dpy, keycode, modifiers, root);

	// no error even if key did not exist
	Tcl_Obj *key = Tcl_ObjPrintf("%d+%d", keycode, modifiers);
	int ret = Tcl_DictObjRemove(interp, hotkeyInfo, key);
	if (ret == TCL_ERROR) {
		return TCL_ERROR;
	}

	return TCL_OK;
}

// return current active window id
static Window GetActiveWindowId(Display *dpy)
{
	Window focus;           // Returns the focus window, PointerRoot, or None
	int revert_to;          // Returns  the  current  focus state (RevertToParent, RevertTo‐ PointerRoot, or RevertToNone)

	XGetInputFocus(dpy, &focus, &revert_to);

	// can not get active window
	if ((focus == PointerRoot) || (focus == None)) {
		// fprintf(stderr, "XGetInputFocus() returns %ld\n", focus);
		return None;
	}

	return focus;
}

// return current active window id, tcl command
static int GetActiveWindowIdCmd(ClientData clientdata,
                                Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
	// fprintf(stderr, "GetActiveWindowIdCmd : %d\n", objc);

	Tk_Window tkwin;
	tkwin = Tk_MainWindow(interp);
	if (!tkwin) {
		return TCL_ERROR;
	}
	Display *dpy;
	dpy = Tk_Display(tkwin);

	if (objc != 1) {
		Tcl_WrongNumArgs(interp, 1, objv, NULL);
		return TCL_ERROR;
	}

	Window focus;
	focus = GetActiveWindowId(dpy);
	if (focus != None) {
		Tcl_SetObjResult(interp, Tcl_NewIntObj(focus));
	} else {
		// make result empty
		Tcl_ResetResult(interp);
		// show error message ?
	}
	return TCL_OK;
}

// grab specified window
static int GrabKeyCmd(ClientData clientdata,
                      Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
	// fprintf(stderr, "GrabKeyCmd : %d\n", objc);

	Tk_Window tkwin;
	tkwin = Tk_MainWindow(interp);
	if (!tkwin) {
		return TCL_ERROR;
	}
	Display *dpy;
	dpy = Tk_Display(tkwin);

	if (objc != 3) {
		Tcl_WrongNumArgs(interp, 1, objv, "windowid procName");
		return TCL_ERROR;
	}

	Window win;
	Tcl_GetIntFromObj(interp, objv[1], (int*)&win);

	// update grabkeyInfo
	int ret = Tcl_DictObjPut(interp, grabkeyInfo, objv[1], objv[2]);
	if (ret == TCL_ERROR) {
		return TCL_ERROR;
	}

	// fprintf(stderr, "grabKeyCmd : win=%lx grabkeyInfo={%s}\n", win, Tcl_GetString(grabkeyInfo));

	XErrorHandler handler = XSetErrorHandler(IgnoreError); // ignore BadWindow error
	// grab any keyboard keys
	XGrabKey(dpy, AnyKey, AnyModifier, win, False, GrabModeAsync, GrabModeAsync);
	XSync(dpy, False);         // necessary
	XSetErrorHandler(handler); // restore error handler

	// ungrab modifier keys
	XModifierKeymap *map;
	map = XGetModifierMapping(dpy);
	// 8 : "Shift", "Lock", "Control", "Mod1", "Mod2", "Mod3", "Mod4", "Mod5"
	for (int i = 0; i < 8 * map->max_keypermod; i++) {
		// ignore keycode zero
		if (map->modifiermap[i] != 0) {
			XUngrabKey(dpy, map->modifiermap[i], 0, win);
		}
	}
	XFreeModifiermap(map);

	return TCL_OK;
}

// ungrab specified window
static int UngrabKeyCmd(ClientData clientdata,
                        Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
	// fprintf(stderr, "UngrabKeyCmd : %d\n", objc);

	Tk_Window tkwin;
	tkwin = Tk_MainWindow(interp);
	if (!tkwin) {
		return TCL_ERROR;
	}
	Display *dpy;
	dpy = Tk_Display(tkwin);

	if (objc != 2) {
		Tcl_WrongNumArgs(interp, 2, objv, "windowid");
		return TCL_ERROR;
	}

	Window win;
	Tcl_GetIntFromObj(interp, objv[1], (int*)&win);

	Tcl_DictObjRemove(interp, grabkeyInfo, objv[1]);

	// fprintf(stderr, "UngrabKeyCmd : win=%lx grabkeyInfo={%s}\n", win, Tcl_GetString(grabkeyInfo));

	// ungrab all key
	XErrorHandler handler = XSetErrorHandler(IgnoreError); // ignore BadWindow error
	XUngrabKey(dpy, AnyKey, AnyModifier, win);
	XSync(dpy, False);         // necessary
	XSetErrorHandler(handler); // restore error handler

	return TCL_OK;
}

// return value of modifier for XGrabkey()
//    Shift (1<<0)
//    Control (1<<2)
//    Alt (1<<3)
//    win (1<<6)
static int GetModValue(const char *modstr)
{
	if ((strcasecmp(modstr, "shift") == 0) || (strcmp(modstr, "S") == 0)) {
		return 1;
	} else if ((strcasecmp(modstr, "control") == 0) || (strcasecmp(modstr, "ctrl") == 0) || (strcmp(modstr, "C") == 0)) {
		return 1 << 2;
	} else if ((strcasecmp(modstr, "alt") == 0) || (strcmp(modstr, "A") == 0) ||
	           (strcasecmp(modstr, "meta") == 0) || (strcmp(modstr, "M") == 0) ||
	           (strcasecmp(modstr, "mod1") == 0)) {
		return 1 << 3;
	} else if ((strcasecmp(modstr, "win") == 0) || (strcmp(modstr, "W") == 0) ||
	           (strcasecmp(modstr, "super") == 0) || (strcmp(modstr, "s") == 0) ||
	           (strcasecmp(modstr, "mod4") == 0)) {
		return 1 << 6;
	}
	return 0;
}

// get keycode and modifier value from string
static int GetKeycodeFromKeystr(Tcl_Interp *interp, const char *keystr, int *keycode, unsigned int *modifiers)
{
	char *s = strdup(keystr);
	char *p;
	char *p2;

	// get modifiers
	p = s;
	*modifiers = 0;
	while ((p2 = (strchr(p, '-'))) != NULL) {
		*p2 = '\0';
		*modifiers |= GetModValue(p);
		p = p2 + 1;
	}

	// get keycode
	if (!*p) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("can not get keysym", -1));
		free(s);
		return TCL_ERROR;
	}
	Tk_Window tkwin = Tk_MainWindow(interp);
	if (!tkwin) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Tk_MainWindow() return null", -1));
		free(s);
		return TCL_ERROR;
	}
	Display *dpy = Tk_Display(tkwin);
	if (!dpy) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Tk_Display() return null", -1));
		free(s);
		return TCL_ERROR;
	}
	*keycode = XKeysymToKeycode(dpy, XStringToKeysym(p));

	free(s);
	return TCL_OK;
}

// register hotkey with keystr and script
static int RegisterHotkeyCmd(ClientData clientData,
                             Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
	if (objc != 3) {
		Tcl_WrongNumArgs(interp, 1, objv, "key script");
		return TCL_ERROR;
	}
	int keycode;
	unsigned int modifiers;
	if (GetKeycodeFromKeystr(interp, Tcl_GetString(objv[1]), &keycode, &modifiers) == TCL_ERROR) {
		return TCL_ERROR;
	}

	Tk_Window tkwin = Tk_MainWindow(interp);
	if (!tkwin) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Tk_MainWindow() return null", -1));
		return TCL_ERROR;
	}
	Display *dpy = Tk_Display(tkwin);
	if (!dpy) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Tk_Display() return null", -1));
		return TCL_ERROR;
	}

	int min_keycode;
	int max_keycode;
	XDisplayKeycodes(dpy, &min_keycode, &max_keycode);
	if ((keycode < min_keycode) || (keycode > max_keycode)) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("keycode out of range", -1));
		return TCL_ERROR;
	}
	if (modifiers > AnyModifier) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("modifiers out of range", -1));
		return TCL_ERROR;
	}

	if (AppendHotkey(interp, keycode, modifiers, objv[2]) == TCL_ERROR) {
		return TCL_ERROR;
	}

	return TCL_OK;
}

// unregister hotkey with keystr
static int UnregisterHotkeyCmd(ClientData clientData,
                               Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{

	if (objc != 2) {
		Tcl_WrongNumArgs(interp, 1, objv, "key");
		return TCL_ERROR;
	}

	int keycode;
	unsigned int modifiers;
	if (GetKeycodeFromKeystr(interp, Tcl_GetString(objv[1]), &keycode, &modifiers) == TCL_ERROR) {
		return TCL_ERROR;
	}

	if (RemoveHotkey(interp, keycode, modifiers) == TCL_ERROR) {
		return TCL_ERROR;
	}

	return TCL_OK;
}

static int SendUnicodeCmd(ClientData clientData,
                          Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
	// fprintf(stderr, "SendUnicodeCmd\n");

	const char *utf8string;
	int delay = 40000;

	if (objc == 2) {
		utf8string = Tcl_GetString(objv[1]);
	} else if (objc == 4) {
		const char *arg = Tcl_GetString(objv[1]);
		if (strcmp(arg, "-delay") == 0) {
			Tcl_GetIntFromObj(interp, objv[2], &delay);
			utf8string = Tcl_GetString(objv[3]);
		} else {
			Tcl_SetObjResult(interp, Tcl_ObjPrintf(
				                 "unknown option \"%s\"", arg));
			Tcl_SetErrorCode(interp, "TK", "LOOKUP", "OPTION", arg, NULL);
			return TCL_ERROR;
		}
	} else {
		Tcl_WrongNumArgs(interp, 1, objv, "?-delay microsec? string");
		return TCL_ERROR;
	}

	Tk_Window tkwin;
	tkwin = Tk_MainWindow(interp);
	if (!tkwin) {
		return TCL_ERROR;
	}
	Display *dpy;
	dpy = Tk_Display(tkwin);
	Window focus;
	focus = GetActiveWindowId(dpy);

	send_unicode(dpy, focus, utf8string, delay);
	return TCL_OK;
}

// unload procedure
int Tkxwin_Unload(Tcl_Interp *interp, int flags)
{
	fprintf(stderr, "Tkxwin_Unload : begin\n");

	// free hotkeyInfo
	Tcl_DecrRefCount(hotkeyInfo);
	// free grabkeyInfo
	Tcl_DecrRefCount(grabkeyInfo);

	// remove handler
	Tk_DeleteGenericHandler(GenericProc, interp);

	// remove commands
	Tcl_DeleteCommand(interp, NS "::grabKey");
	Tcl_DeleteCommand(interp, NS "::ungrabKey");
	Tcl_DeleteCommand(interp, NS "::registerHotkey");
	Tcl_DeleteCommand(interp, NS "::unregisterHotkey");
	Tcl_DeleteCommand(interp, NS "::sendUnicode");
	Tcl_DeleteCommand(interp, NS "::getActiveWindowId");

	fprintf(stderr, "Tkxwin_Unload : end\n");

	return TCL_OK;
}

// initialize extension
int Tkxwin_Init(Tcl_Interp *interp)
{
	if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL) {
		return TCL_ERROR;
	}
	if (Tk_InitStubs(interp, TK_VERSION, 0) == NULL) {
		return TCL_ERROR;
	}

	if (Tcl_PkgProvide(interp, "tkxwin", "1.0.0") == TCL_ERROR) {
		return TCL_ERROR;
	}
	Tcl_CreateObjCommand(interp, NS "::grabKey", GrabKeyCmd, NULL, NULL);
	Tcl_CreateObjCommand(interp, NS "::ungrabKey", UngrabKeyCmd, NULL, NULL);
	Tcl_CreateObjCommand(interp, NS "::registerHotkey", RegisterHotkeyCmd, NULL, NULL);
	Tcl_CreateObjCommand(interp, NS "::unregisterHotkey", UnregisterHotkeyCmd, NULL, NULL);
	Tcl_CreateObjCommand(interp, NS "::sendUnicode", SendUnicodeCmd, NULL, NULL);
	Tcl_CreateObjCommand(interp, NS "::getActiveWindowId", GetActiveWindowIdCmd, NULL, NULL);

	// initialize dictobj
	hotkeyInfo = Tcl_NewDictObj();
	grabkeyInfo = Tcl_NewDictObj();

	// create handler
	Tk_CreateGenericHandler(GenericProc, interp);

	return TCL_OK;
}
