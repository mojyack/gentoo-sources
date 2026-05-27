/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Apple-specific support for the Cirrus Logic CS8409 HDA bridge chip.
 * Public interface used by cs8409.c.
 */

#ifndef __CS8409_CIRRUS_APPLE_H
#define __CS8409_CIRRUS_APPLE_H

struct hda_codec;

int cs8409_apple(struct hda_codec *codec);

#endif
