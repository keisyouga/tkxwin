// send unicode string to specified window
// this program referenced xdotool's code

#include <X11/Xlib.h>
#include <stdio.h>              // fprintf()
#include <unistd.h>             // usleep()
#include <stdlib.h>             // strtol()
#include <string.h>

// convert first utf-8 character to unicode
// utf8string : utf-8 string
// unicode : unicode to return
// return value : bytes of utf-8 character
// only see first byte of utf8string to decide length
static int utf8_to_unicode(const char utf8string[], int *unicode)
{
	// 0x00000000 - 0x0000007F:
	//     0xxxxxxx
	if ((utf8string[0] & 0b10000000) == 0b00000000) {

		*unicode = utf8string[0];
		return 1;
	}
	// 0x00000080 - 0x000007FF:
	//     110xxxxx 10xxxxxx
	if ((utf8string[0] & 0b11100000) == 0b11000000) {
		*unicode = ((utf8string[0] & 0b00011111) << 6 |
		            (utf8string[1] & 0b00111111));
		return 2;
	}
	// 0x00000800 - 0x0000FFFF:
	//     1110xxxx 10xxxxxx 10xxxxxx
	if ((utf8string[0] & 0b11110000) == 0b11100000) {
		*unicode = ((utf8string[0] & 0b00001111) << 12 |
		            (utf8string[1] & 0b00111111) << 6 |
		            (utf8string[2] & 0b00111111));
		return 3;
	}
	// 0x00010000 - 0x001FFFFF:
	//     11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
	if ((utf8string[0] & 0b11111000) == 0b11110000) {
		*unicode = ((utf8string[0] & 0b00000111) << 18 |
		            (utf8string[1] & 0b00111111) << 12 |
		            (utf8string[2] & 0b00111111) << 6 |
		            (utf8string[3] & 0b00111111));
		return 4;
	}
	// 0x00200000 - 0x03FFFFFF:
	//     111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
	if ((utf8string[0] & 0b11111100) == 0b11111000) {
		*unicode = ((utf8string[0] & 0b00000011) << 24 |
		            (utf8string[1] & 0b00111111) << 18 |
		            (utf8string[2] & 0b00111111) << 12 |
		            (utf8string[3] & 0b00111111) << 6 |
		            (utf8string[4] & 0b00111111));
		return 5;
	}
	// 0x04000000 - 0x7FFFFFFF:
	//     1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
	if ((utf8string[0] & 0b11111110) == 0b11111100) {
		*unicode = ((utf8string[0] & 0b00000001) << 30 |
		            (utf8string[1] & 0b00111111) << 24 |
		            (utf8string[2] & 0b00111111) << 18 |
		            (utf8string[3] & 0b00111111) << 12 |
		            (utf8string[4] & 0b00111111) << 6 |
		            (utf8string[5] & 0b00111111));
		return 6;
	}
	// invalid utf-8
	return -1;
}

// send utf-8 string to window
void send_unicode(Display *dpy, Window target, const char *utf8string, int delay)
{
	// fprintf(stderr, "%s : %p 0x%lx %s\n", __func__, dpy, target, utf8string);

	XEvent event = {0};
	delay = delay / 2;      // delay 2 times after keypress and keyrelease

	event.xkey.display = dpy;
	event.xkey.window = target;

	event.xkey.root = XDefaultRootWindow(dpy);
	event.xkey.subwindow = None;
	event.xkey.time = CurrentTime;
	event.xkey.same_screen = True;
	event.xkey.x = 1;
	event.xkey.y = 1;
	event.xkey.x_root = 1;
	event.xkey.y_root = 1;

	// find unused keycode in keymap and bind it temporarily
	int min_keycode, max_keycode, keysyms_per_keycode;
	KeySym *keymap, *pkey;
	XDisplayKeycodes(dpy, &min_keycode, &max_keycode);
	keymap = XGetKeyboardMapping(dpy, min_keycode,
	                             max_keycode - min_keycode + 1,
	                             &keysyms_per_keycode);
	if (!keymap) {
		fprintf(stderr, "error : XGetKeyboardMapping()\n");
		return;
	}
	pkey = keymap;
	KeySym *all_zero = calloc(sizeof(KeySym), keysyms_per_keycode);
	int unused_keycode = 0;
	for (int i = min_keycode; i <= max_keycode; i++) {
		if (memcmp(pkey, all_zero, keysyms_per_keycode * sizeof(KeySym)) == 0) {
			unused_keycode = i;
			break;
		}
		pkey += keysyms_per_keycode;
	}
	free(all_zero);
	XFree(keymap);
	// fprintf(stderr, "unused_keycode = %d\n", unused_keycode);
	if (unused_keycode == 0) {
		fprintf(stderr, "unused keycode was not found\n");
		return;
	}

	const char *p = utf8string;
	// send utf8string until null
	while (*p) {
		int num_bytes;
		int unicode;

		num_bytes = utf8_to_unicode(p, &unicode);
		// add 0x1000000 for non-ascii, see /usr/include/X11/keysymdef.h
		//   U+0041 => 0x0000041
		//   U+1234 => 0x1001234
		if ((unicode >= 0x100) && (unicode <= 0x10ffff)) {
			unicode += 0x1000000;
		}
		// perhaps add 0x1000000 for all characters?
		// To send uppercase ascii, you need to set the shift bit modifier,
		// but you can omit it by adding a 0x1000000.

		// fprintf(stderr,"%s : %d, %x\n", p, num_bytes, unicode);
		p += num_bytes;

		// change keymap
		KeySym keysym_list[] = {unicode};
		XChangeKeyboardMapping(dpy, unused_keycode, 1, keysym_list, 1);
		XSync(dpy, False);
		event.xkey.keycode = unused_keycode;
		event.xkey.state = 0;

		int ret;
		(void)ret;

		// send KeyPress
		event.xkey.type = KeyPress;
		ret = XSendEvent(dpy, target, True, KeyPressMask, &event);
		// fprintf(stderr, "%d\n", ret);
		XFlush(dpy);
		usleep(delay);

		XSync(dpy, False);

		// send KeyRelease
		event.xkey.type = KeyRelease;
		ret = XSendEvent(dpy, target, True, KeyReleaseMask, &event);
		// fprintf(stderr, "%d\n", ret);
		XFlush(dpy);
		usleep(delay);
	}
	// restore keymap
	KeySym keysym_list[] = {0};
	XChangeKeyboardMapping(dpy, unused_keycode, 1, keysym_list, 1);
	XFlush(dpy);
}
