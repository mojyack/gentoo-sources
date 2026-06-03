/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Apple Touch Bar shared definitions
 *
 * Definitions and helpers common to the Apple Touch Bar drivers for the
 * T1 (iBridge, apple-ib-tb) and T2 (hid-appletb-kbd) chips. The touch bar
 * "mode" output report and the Fn-row key layout are identical on both
 * generations; only the way the device is enumerated and driven differs.
 *
 * Copyright (c) 2017-2018 Ronald Tschalär
 */

#ifndef _APPLE_TOUCHBAR_H
#define _APPLE_TOUCHBAR_H

#include <linux/input.h>
#include <linux/input/sparse-keymap.h>

/* Touch bar mode, i.e. the value of the mode output report */
#define APPLETB_MODE_ESC	0
#define APPLETB_MODE_FN		1
#define APPLETB_MODE_SPCL	2
#define APPLETB_MODE_OFF	3
#define APPLETB_MODE_MAX	APPLETB_MODE_OFF

/*
 * Fn-row layout: ESC plus F1-F12, where F1-F12 carry the usual media /
 * brightness functions when the touch bar is in the "special keys" mode.
 */
static const struct key_entry appletb_fn_keymap[] = {
	{ KE_KEY, KEY_ESC, { KEY_ESC } },
	{ KE_KEY, KEY_F1,  { KEY_BRIGHTNESSDOWN } },
	{ KE_KEY, KEY_F2,  { KEY_BRIGHTNESSUP } },
	{ KE_KEY, KEY_F3,  { KEY_RESERVED } },
	{ KE_KEY, KEY_F4,  { KEY_RESERVED } },
	{ KE_KEY, KEY_F5,  { KEY_KBDILLUMDOWN } },
	{ KE_KEY, KEY_F6,  { KEY_KBDILLUMUP } },
	{ KE_KEY, KEY_F7,  { KEY_PREVIOUSSONG } },
	{ KE_KEY, KEY_F8,  { KEY_PLAYPAUSE } },
	{ KE_KEY, KEY_F9,  { KEY_NEXTSONG } },
	{ KE_KEY, KEY_F10, { KEY_MUTE } },
	{ KE_KEY, KEY_F11, { KEY_VOLUMEDOWN } },
	{ KE_KEY, KEY_F12, { KEY_VOLUMEUP } },
	{ KE_END, 0 }
};

/*
 * Map a touch bar Fn-row key code to its slot (0 = ESC, 1-12 = F1-F12).
 * Returns -EINVAL for keys that are not part of the touch bar.
 */
static inline int appletb_tb_key_to_slot(unsigned int code)
{
	switch (code) {
	case KEY_ESC:
		return 0;
	case KEY_F1 ... KEY_F10:
		return code - KEY_F1 + 1;
	case KEY_F11 ... KEY_F12:
		return code - KEY_F11 + 11;
	default:
		return -EINVAL;
	}
}

#endif /* _APPLE_TOUCHBAR_H */
