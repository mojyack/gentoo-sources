// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Apple-specific support for the Cirrus Logic CS8409 HDA bridge chip.
 * Internal mike + line-in input handling split out from cirrus_apple.c.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <sound/core.h>

#include "cs8409.h"
#include "cirrus_apple_internal.h"

#define mycodec_info(...)
#define mycodec_i2c_info(...)
#define mydev_info(...)
#define mycodec_dbg(...)
#define myprintk_dbg(...)
#define myprintk(...)

void cs_8409_intmike_format_setup_enable(struct hda_codec *codec, int hda_format, int powered_down)
{
	int ret_coef9 = 0;
	int new_coef9 = 0;

	struct cs8409_apple_spec *spec = codec->spec;

	// 0x44 -> 0x22 is internal (I think) mike input (macbook pro)

	// now updated to not write the Apple format but use my format setting routines
	// (remember we have limited the allowed formats to acceptable ones)
	// note that apparently we can set the format with the nid powered down but for setting the
	// stream id the nid has to be powered up
	// this seems to be used a lot in plugin/unplug headset in a powered down state
	// - but when capturing no power changes done

	// for some very strange reason we setup a 4 channel format after unplug of headset with mike
	// - otherwise its 2 channel - pass the format to allow for this
	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_STREAM_FORMAT, 0x00004033); // 0x02224033
//      snd_hda:     stream format 34 [('CHAN', 4), ('RATE', 44100), ('BITS', 24), ('RATE_MUL', 1), ('RATE_DIV', 1)]

	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_STREAM_FORMAT, hda_format); // 0x02224033

	// now assuming have saved the stream info prior to calling this function

	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 10515); // 0x022f0500
	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_POWER_STATE, 0x00000000); // 0x02270500
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000030, 10518); // 0x022f0500
	if (powered_down) hda_set_node_power_state(codec, spec->intmike_adc_nid, AC_PWRST_D0);

	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000010); // 0x02270610
//      snd_hda:     conv stream channel map 34 [('CHAN', 0), ('STREAMID', 1)]

	// using the stored stream parameters update nid 0x22 stream parameters
	// we have limited the allowed formats so should only have working formats here
	cs_8409_really_update_stream_format(codec, spec->intmike_adc_nid, 1, 1, 0);

	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_POWER_STATE, 0x00000003); // 0x02270503
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 10521); // 0x022f0500
	if (powered_down) hda_set_node_power_state(codec, spec->intmike_adc_nid, AC_PWRST_D3);

//      snd_hda: # AppleHDAWidgetCS8409::setConnectionSelect:
	ret_coef9 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0009, 0x0000, 0x000000b3, 0); // AppleHDAWidgetCS8409::setConnectionSelect  coef read 10523
	//new_coef9 = (ret_coef9 | 0x20); // note most of the time it just seems to copy the value because bit 0x20 already set on input
	//                                // only on boot does this get set
	new_coef9 = (ret_coef9 | spec->reg9_intmike_dmic_mo); // note most of the time it just seems to copy the value because bit 0x20 already set on input
							      // only on boot does this get set
	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0009, 0x00b3, 0x00000000, 10527 ); // AppleHDAWidgetCS8409::setConnectionSelect  coef write 10527
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0009, new_coef9, 0x00000000, 0); // AppleHDAWidgetCS8409::setConnectionSelect  coef write 10527

	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_CONNECT_SEL, 0x00000000); // 0x02270100
	snd_hda_codec_write(codec, spec->intmike_adc_nid, 0, AC_VERB_SET_CONNECT_SEL, 0x00000000); // 0x02270100

}

void cs_8409_volume_set(struct hda_codec *codec, hda_nid_t nid, int volume)
{
	int retgain;
	int newgain;

	retgain = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000); // 0x022b2000

	newgain = (retgain & 0x80) | (volume & 0x7f) | 0x6000;

	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, newgain); // 0x02236027

	retgain = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000); // 0x022b0000

	newgain = (retgain & 0x80) | (volume & 0x7f) | 0x5000;

	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, newgain); // 0x02235027

}

static void cs_8409_volume_mute(struct hda_codec *codec, hda_nid_t nid)
{
	int retgain;
	int newgain;

	retgain = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000); // 0x022b2000

	newgain = (retgain & 0x7f) | 0x80 | 0x6000;

	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, newgain); // 0x02236000

	retgain = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000); // 0x022b0000

	newgain = (retgain & 0x7f) | 0x80 | 0x5000;

	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, newgain); // 0x02235027

}

static void cs_8409_volume_unmute(struct hda_codec *codec, hda_nid_t nid)
{
	int retgain;
	int newgain;

	retgain = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000); // 0x022b2000

	newgain = (retgain & 0x7f) | 0x6000;

	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, newgain); // 0x02236000

	retgain = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000); // 0x022b0000

	newgain = (retgain & 0x7f) | 0x5000;

	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, newgain); // 0x02235027

}

void cs_8409_intmike_volume_set(struct hda_codec *codec, int volume)
{
	struct cs8409_apple_spec *spec = codec->spec;
	cs_8409_volume_set(codec, spec->intmike_adc_nid, volume);
}

void cs_8409_linein_volume_set(struct hda_codec *codec, int volume)
{
	struct cs8409_apple_spec *spec = codec->spec;
	cs_8409_volume_set(codec, spec->linein_amp_nid, volume);
}

void cs_8409_intmike_volume_unmute(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;
	cs_8409_volume_unmute(codec, spec->intmike_adc_nid);
}

void cs_8409_linein_volume_unmute(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;
	cs_8409_volume_unmute(codec, spec->linein_amp_nid);
}

void cs_8409_intmike_volume_mute(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;
	cs_8409_volume_mute(codec, spec->intmike_adc_nid);
}

void cs_8409_linein_volume_mute(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;
	cs_8409_volume_mute(codec, spec->linein_amp_nid);
}

void cs_8409_intmike_volume_setup(struct hda_codec *codec, int volume)
{
	int retgain;
	int newgain;

	struct cs8409_apple_spec *spec = codec->spec;

	// plausibly AppleHDAWidget::setWidgetAmplifierGain

	//retgain = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x00000033, 0); // 0x022b2000
	retgain = snd_hda_codec_read_check(codec, spec->intmike_adc_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x00000033, 0); // 0x022b2000
//      snd_hda:     amp gain/mute 34 0x2000 index 0x00 left/right 1 left output/input 0 input
//      snd_hda:     amp gain/mute 34 0x0033 mute 0 gain 0x33 51

	newgain = (retgain & 0x80) | (volume & 0x7f) | 0x6000;

	snd_hda_codec_write(codec, spec->intmike_adc_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, newgain); // 0x02236027

	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x00006027); // 0x02236027
//      snd_hda:     amp gain/mute 34 0x6027 mute 0 gain 0x27 39 index 0x00 left 1 right 0 output 0 input 1 left   input

	//retgain = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x00000033, 0); // 0x022b0000
	retgain = snd_hda_codec_read_check(codec, spec->intmike_adc_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x00000033, 0); // 0x022b0000
//      snd_hda:     amp gain/mute 34 0x0000 index 0x00 left/right 0 right output/input 0 input
//      snd_hda:     amp gain/mute 34 0x0033 mute 0 gain 0x33 51

	newgain = (retgain & 0x80) | (volume & 0x7f) | 0x5000;

	snd_hda_codec_write(codec, spec->intmike_adc_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, newgain); // 0x02235027

	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x00005027); // 0x02235027
//      snd_hda:     amp gain/mute 34 0x5027 mute 0 gain 0x27 39 index 0x00 left 0 right 1 output 0 input 1  right  input

	// mute
	// plausibly AppleHDAWidget::setWidgetAmplifierMute

	//retgain = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x00000027, 0); // 0x022b2000
	retgain = snd_hda_codec_read_check(codec, spec->intmike_adc_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x00000027, 0); // 0x022b2000
//      snd_hda:     amp gain/mute 34 0x2000 index 0x00 left/right 1 left output/input 0 input
//      snd_hda:     amp gain/mute 34 0x0027 mute 0 gain 0x27 39

	newgain = (retgain & 0x7f) | 0x80 | 0x6000;

	snd_hda_codec_write(codec, spec->intmike_adc_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, newgain); // 0x022360a7

	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x000060a7); // 0x022360a7
//      snd_hda:     amp gain/mute 34 0x60a7 mute 1 gain 0x27 39 index 0x00 left 1 right 0 output 0 input 1 left   input

	//retgain = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x00000027, 0); // 0x022b0000
	retgain = snd_hda_codec_read_check(codec, spec->intmike_adc_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x00000027, 0); // 0x022b0000
//      snd_hda:     amp gain/mute 34 0x0000 index 0x00 left/right 0 right output/input 0 input
//      snd_hda:     amp gain/mute 34 0x0027 mute 0 gain 0x27 39

	newgain = (retgain & 0x7f) | 0x80 | 0x5000;

	snd_hda_codec_write(codec, spec->intmike_adc_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, newgain); // 0x022350a7

	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x000050a7); // 0x022350a7
//      snd_hda:     amp gain/mute 34 0x50a7 mute 1 gain 0x27 39 index 0x00 left 0 right 1 output 0 input 1  right  input

	// this is working on node 0x44 macbook pro
	// plausibly AppleHDAWidget::setWidgetAmplifierGain

	//retgain = snd_hda_codec_read_check(codec, 0x44, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x00000000, 0); // 0x044b2000
	retgain = snd_hda_codec_read_check(codec, spec->intmike_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x00000000, 0); // 0x044b2000
//      snd_hda:     amp gain/mute 68 0x2000 index 0x00 left/right 1 left output/input 0 input
//      snd_hda:     amp gain/mute 68 0x0000 mute 0 gain 0x0 0

	newgain = (retgain & 0x80) | (volume & 0x7f) | 0x6000;

	snd_hda_codec_write(codec, spec->intmike_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, newgain); // 0x04436000

	//snd_hda_codec_write(codec, 0x44, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x00006000); // 0x04436000
//      snd_hda:     amp gain/mute 68 0x6000 mute 0 gain 0x0 0 index 0x00 left 1 right 0 output 0 input 1 left   input

	//retgain = snd_hda_codec_read_check(codec, 0x44, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x00000000, 0); // 0x044b0000
	retgain = snd_hda_codec_read_check(codec, spec->intmike_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x00000000, 0); // 0x044b0000
//      snd_hda:     amp gain/mute 68 0x0000 index 0x00 left/right 0 right output/input 0 input
//      snd_hda:     amp gain/mute 68 0x0000 mute 0 gain 0x0 0

	newgain = (retgain & 0x80) | (volume & 0x7f) | 0x5000;

	snd_hda_codec_write(codec, spec->intmike_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, newgain); // 0x04435000

	//snd_hda_codec_write(codec, 0x44, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x00005000); // 0x04435000
//      snd_hda:     amp gain/mute 68 0x5000 mute 0 gain 0x0 0 index 0x00 left 0 right 1 output 0 input 1  right  input

}

void cs_8409_linein_volume_setup(struct hda_codec *codec, int volume)
{
	int retgain;
	int newgain;

	struct cs8409_apple_spec *spec = codec->spec;

	// so as far as I can see the 1st section sets the gain and the second section sets the mute
	// it appears we do masked updates

	// plausibly AppleHDAWidget::setWidgetAmplifierGain

	//retgain = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x000000b3, 0); // 0x023b2000
	retgain = snd_hda_codec_read_check(codec, spec->linein_amp_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x000000b3, 0); // 0x023b2000
//      snd_hda:     amp gain/mute 35 0x2000 index 0x00 left/right 1 left output/input 0 input
//      snd_hda:     amp gain/mute 35 0x00b3 mute 1 gain 0x33 51

	newgain = (retgain & 0x80) | (volume & 0x7f) | 0x6000;

	snd_hda_codec_write(codec, spec->linein_amp_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x000060a7); // 0x023360a7

	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x000060a7); // 0x023360a7
//      snd_hda:     amp gain/mute 35 0x60a7 mute 1 gain 0x27 39 index 0x00 left 1 right 0 output 0 input 1 left   input

	//retgain = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x000000b3, 0); // 0x023b0000
	retgain = snd_hda_codec_read_check(codec, spec->linein_amp_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x000000b3, 0); // 0x023b0000
//      snd_hda:     amp gain/mute 35 0x0000 index 0x00 left/right 0 right output/input 0 input
//      snd_hda:     amp gain/mute 35 0x00b3 mute 1 gain 0x33 51

	newgain = (retgain & 0x80) | (volume & 0x7f) | 0x5000;

	snd_hda_codec_write(codec, spec->linein_amp_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, newgain); // 0x023350a7

	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x000050a7); // 0x023350a7
//      snd_hda:     amp gain/mute 35 0x50a7 mute 1 gain 0x27 39 index 0x00 left 0 right 1 output 0 input 1  right  input

	// mute
	// plausibly AppleHDAWidget::setWidgetAmplifierMute

	//retgain = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x000000a7, 0); // 0x023b2000
	retgain = snd_hda_codec_read_check(codec, spec->linein_amp_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x000000a7, 0); // 0x023b2000
//      snd_hda:     amp gain/mute 35 0x2000 index 0x00 left/right 1 left output/input 0 input
//      snd_hda:     amp gain/mute 35 0x00a7 mute 1 gain 0x27 39

	newgain = (retgain & 0x7f) | 0x80 | 0x6000;

	snd_hda_codec_write(codec, spec->linein_amp_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, newgain); // 0x023360a7

	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x000060a7); // 0x023360a7
//      snd_hda:     amp gain/mute 35 0x60a7 mute 1 gain 0x27 39 index 0x00 left 1 right 0 output 0 input 1 left   input

	//retgain = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x000000a7, 0); // 0x023b0000
	retgain = snd_hda_codec_read_check(codec, spec->linein_amp_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x000000a7, 0); // 0x023b0000
//      snd_hda:     amp gain/mute 35 0x0000 index 0x00 left/right 0 right output/input 0 input
//      snd_hda:     amp gain/mute 35 0x00a7 mute 1 gain 0x27 39

	newgain = (retgain & 0x7f) | 0x80 | 0x5000;

	snd_hda_codec_write(codec, spec->linein_amp_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, newgain); // 0x023350a7

	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x000050a7); // 0x023350a7
//      snd_hda:     amp gain/mute 35 0x50a7 mute 1 gain 0x27 39 index 0x00 left 0 right 1 output 0 input 1  right  input

	// this is working on node 0x45 macbook pro

	// plausibly AppleHDAWidget::setWidgetAmplifierGain

	//retgain = snd_hda_codec_read_check(codec, 0x45, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x00000000, 0); // 0x045b2000
	retgain = snd_hda_codec_read_check(codec, spec->linein_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x00000000, 0); // 0x045b2000
//      snd_hda:     amp gain/mute 69 0x2000 index 0x00 left/right 1 left output/input 0 input
//      snd_hda:     amp gain/mute 69 0x0000 mute 0 gain 0x0 0

	snd_hda_codec_write(codec, spec->linein_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x00006000); // 0x04536000

	//snd_hda_codec_write(codec, 0x45, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x00006000); // 0x04536000
//      snd_hda:     amp gain/mute 69 0x6000 mute 0 gain 0x0 0 index 0x00 left 1 right 0 output 0 input 1 left   input

	//retgain = snd_hda_codec_read_check(codec, 0x45, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x00000000, 0); // 0x045b0000
	retgain = snd_hda_codec_read_check(codec, spec->linein_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x00000000, 0); // 0x045b0000
//      snd_hda:     amp gain/mute 69 0x0000 index 0x00 left/right 0 right output/input 0 input
//      snd_hda:     amp gain/mute 69 0x0000 mute 0 gain 0x0 0

	snd_hda_codec_write(codec, spec->linein_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x00005000); // 0x04535000

	//snd_hda_codec_write(codec, 0x45, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x00005000); // 0x04535000
//      snd_hda:     amp gain/mute 69 0x5000 mute 0 gain 0x0 0 index 0x00 left 0 right 1 output 0 input 1  right  input

}

void cs_8409_intmike_stream_on_nid(struct hda_codec *codec)
{
	int retval;
	int reg_coef82 = 0;
	int new_coef82 = 0;

	struct cs8409_apple_spec *spec = codec->spec;

	reg_coef82 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0082, 0x0000, 0x00005400, 0); //   coef read 10544

	new_coef82 = (reg_coef82 | spec->reg82_intmike_dmic_scl);

	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0082, 0x5401, 0x00000000, 10548 ); //   coef write 10548
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0082, new_coef82, 0x00000000, 0); //   coef write 10548

	//retval = snd_hda_codec_read_check(codec, 0x44, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000000, 0); // 0x044f0700
	retval = snd_hda_codec_read_check(codec, spec->intmike_nid, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000000, 0); // 0x044f0700

	snd_hda_codec_write(codec, spec->intmike_nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000020); // 0x04470720

	//snd_hda_codec_write(codec, 0x44, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000020); // 0x04470720
//      snd_hda:     68 ['AC_PINCTL_IN_EN']

	//snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_POWER_STATE, 0x00000003); // 0x00170503

}

void cs_8409_intmike_format_setup_disable(struct hda_codec *codec)
{
	int retval;
	int reg_coef82 = 0;
	int new_coef82 = 0;

	struct cs8409_apple_spec *spec = codec->spec;

	// set to defaults and disable input
	// note here we really reset to 0 format in addition to stream id 0/channel id 0

	// note this means the cached stream data in the hda_cvt_setup struct will now be inconsistent
	// we need to ensure any further stream format re-update MUST be a forced update
	// still not clear if should be calling eg __snd_hda_codec_cleanup_stream

	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 12217); // 0x022f0500
	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_POWER_STATE, 0x00000000); // 0x02270500
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000030, 12220); // 0x022f0500
	hda_set_node_power_state(codec, spec->intmike_adc_nid, AC_PWRST_D0);

	snd_hda_codec_write(codec, spec->intmike_adc_nid, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02270600

	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02270600
//      snd_hda:     conv stream channel map 34 [('CHAN', 0), ('STREAMID', 0)]

	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_POWER_STATE, 0x00000003); // 0x02270503
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 12223); // 0x022f0500
	hda_set_node_power_state(codec, spec->intmike_adc_nid, AC_PWRST_D3);

	snd_hda_codec_write(codec, spec->intmike_adc_nid, 0, AC_VERB_SET_STREAM_FORMAT, 0x00000000); // 0x02220000

	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_STREAM_FORMAT, 0x00000000); // 0x02220000
//      snd_hda:     stream format 34 [('CHAN', 1), ('RATE', 48000), ('BITS', 8), ('RATE_MUL', 1), ('RATE_DIV', 1)]

	// AppleHDAWidgetCS8409::configurePinForIO(bool)??
	reg_coef82 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0082, 0x0000, 0x0000a801, 0); //   coef read 12226

	new_coef82 = (reg_coef82 & ~spec->reg82_intmike_dmic_scl);

	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0082, 0xa800, 0x00000000, 12230 ); //   coef write 12230
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0082, new_coef82, 0x00000000, 0); //   coef write 12230

	//retval = snd_hda_codec_read_check(codec, 0x44, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000020, 0); // 0x044f0700
	retval = snd_hda_codec_read_check(codec, spec->intmike_nid, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000020, 0); // 0x044f0700

	snd_hda_codec_write(codec, spec->intmike_nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000000); // 0x04470700

	//snd_hda_codec_write(codec, 0x44, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000000); // 0x04470700
//      snd_hda:     68 []

}

void cs_8409_linein_format_setup_disable(struct hda_codec *codec)
{
	int retval;
	int reg_coef82 = 0;
	int new_coef82 = 0;

	struct cs8409_apple_spec *spec = codec->spec;

	// 0x45 -> 0x23 is line input (macbook pro)

	// set to defaults and disable input
	// note here we really reset to 0 format in addition to stream id 0/channel id 0

	// note this means the cached stream data in the hda_cvt_setup struct will now be inconsistent
	// we need to ensure any further stream format re-update MUST be a forced update
	// still not clear if should be calling eg __snd_hda_codec_cleanup_stream

	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 12248); // 0x023f0500
	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_POWER_STATE, 0x00000000); // 0x02370500
	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000030, 12251); // 0x023f0500
	hda_set_node_power_state(codec, spec->linein_amp_nid, AC_PWRST_D0);

	snd_hda_codec_write(codec, spec->linein_amp_nid, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02370600

	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02370600
//      snd_hda:     conv stream channel map 35 [('CHAN', 0), ('STREAMID', 0)]

	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_POWER_STATE, 0x00000003); // 0x02370503
	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 12254); // 0x023f0500
	hda_set_node_power_state(codec, spec->linein_amp_nid, AC_PWRST_D3);

	snd_hda_codec_write(codec, spec->linein_amp_nid, 0, AC_VERB_SET_STREAM_FORMAT, 0x00000000); // 0x02320000

	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_STREAM_FORMAT, 0x00000000); // 0x02320000
//      snd_hda:     stream format 35 [('CHAN', 1), ('RATE', 48000), ('BITS', 8), ('RATE_MUL', 1), ('RATE_DIV', 1)]

	reg_coef82 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0082, 0x0000, 0x0000a800, 0); //   coef read 12257

	new_coef82 = (reg_coef82 & ~spec->reg82_linein_dmic_scl);

	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0082, 0xa800, 0x00000000, 12261 ); //   coef write 12261
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0082, new_coef82, 0x00000000, 0); //   coef write 12261

	//retval = snd_hda_codec_read_check(codec, 0x45, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000000, 0); // 0x045f0700
	retval = snd_hda_codec_read_check(codec, spec->linein_nid, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000000, 0); // 0x045f0700

	snd_hda_codec_write(codec, spec->linein_nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000000); // 0x04570700

	//snd_hda_codec_write(codec, 0x45, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000000); // 0x04570700
//      snd_hda:     69 []

}

void cs_8409_intmike_stream_conn_off(struct hda_codec *codec)
{
	int retval;

	struct cs8409_apple_spec *spec = codec->spec;

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, AC_VERB_SET_PROC_STATE, 0x00000001); // 0x04770301

	// 0x44 -> 0x22 is internal (I think) mike input (macbook pro)

	retval = snd_hda_codec_read_check(codec, spec->intmike_adc_nid, 0, AC_VERB_GET_CONV, 0x00000000, 0x00000000, 0); // 0x022f0600

	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_CONV, 0x00000000, 0x00000000, 0); // 0x022f0600
//      snd_hda:     conv stream channel map 34 [('CHAN', 0), ('STREAMID', 0)]

	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 12160); // 0x022f0500
	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_POWER_STATE, 0x00000000); // 0x02270500
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000030, 12163); // 0x022f0500
	hda_set_node_power_state(codec, spec->intmike_adc_nid, AC_PWRST_D0);

	snd_hda_codec_write(codec, spec->intmike_adc_nid, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02270600

	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02270600
//      snd_hda:     conv stream channel map 34 [('CHAN', 0), ('STREAMID', 0)]

	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_POWER_STATE, 0x00000003); // 0x02270503
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 12166); // 0x022f0500
	hda_set_node_power_state(codec, spec->intmike_adc_nid, AC_PWRST_D3);

	// this seems to be updating the coef index associated with setConnectionSelect
	// unable to figure where this is coming from currently
//      snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0009, 0x0033, 0xffff, 0x00000033, 0, 12168 ); // coef write mask 12168
	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0009, 0x0000, 0x0000, 0x00000033, 0x0033, 0); // coef write mask 12168

}

void cs_8409_linein_stream_conn_off(struct hda_codec *codec)
{
	int retval;

	struct cs8409_apple_spec *spec = codec->spec;

	// Im thinking of a bugfix here to turn off bit 0x80 of index 0x0009

	// 0x45 -> 0x23 is line input (macbook pro)

	retval = snd_hda_codec_read_check(codec, spec->linein_amp_nid, 0, AC_VERB_GET_CONV, 0x00000000, 0x00000000, 0); // 0x023f0600

	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_CONV, 0x00000000, 0x00000000, 0); // 0x023f0600
//      snd_hda:     conv stream channel map 35 [('CHAN', 0), ('STREAMID', 0)]

	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 12175); // 0x023f0500
	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_POWER_STATE, 0x00000000); // 0x02370500
	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000030, 12178); // 0x023f0500
	hda_set_node_power_state(codec, spec->linein_amp_nid, AC_PWRST_D0);

	snd_hda_codec_write(codec, spec->linein_amp_nid, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02370600

	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02370600
//      snd_hda:     conv stream channel map 35 [('CHAN', 0), ('STREAMID', 0)]

	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_POWER_STATE, 0x00000003); // 0x02370503
	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 12181); // 0x023f0500
	hda_set_node_power_state(codec, spec->linein_amp_nid, AC_PWRST_D3);

	// this seems to be updating the coef index associated with setConnectionSelect
	// unable to figure where this is coming from currently
//      snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0009, 0x0033, 0xffff, 0x00000033, 0, 12183 ); // coef write mask 12183
	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0009, 0x0000, 0x0000, 0x00000033, 0x0033, 0); // coef write mask 12183
	// possible correct mask to use
	//snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0009, 0x0000, 0x0080, 0x00000033, 0, 0 ); // coef write mask 12183

}

void cs_8409_intmike_stream_off_nid(struct hda_codec *codec)
{

	struct cs8409_apple_spec *spec = codec->spec;

	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 12189); // 0x022f0500
	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_POWER_STATE, 0x00000000); // 0x02270500
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000030, 12192); // 0x022f0500
	hda_set_node_power_state(codec, spec->intmike_adc_nid, AC_PWRST_D0);

	snd_hda_codec_write(codec, spec->intmike_adc_nid, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02270600

	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02270600
//      snd_hda:     conv stream channel map 34 [('CHAN', 0), ('STREAMID', 0)]

	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_POWER_STATE, 0x00000003); // 0x02270503
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 12195); // 0x022f0500
	hda_set_node_power_state(codec, spec->intmike_adc_nid, AC_PWRST_D3);

}

void cs_8409_linein_stream_off_nid(struct hda_codec *codec)
{

	struct cs8409_apple_spec *spec = codec->spec;

	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 12197); // 0x023f0500
	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_POWER_STATE, 0x00000000); // 0x02370500
	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000030, 12200); // 0x023f0500
	hda_set_node_power_state(codec, spec->linein_amp_nid, AC_PWRST_D0);

	snd_hda_codec_write(codec, spec->linein_amp_nid, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02370600

	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02370600
//      snd_hda:     conv stream channel map 35 [('CHAN', 0), ('STREAMID', 0)]

	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_POWER_STATE, 0x00000003); // 0x02370503
	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 12203); // 0x023f0500
	hda_set_node_power_state(codec, spec->linein_amp_nid, AC_PWRST_D3);

}

void cs_8409_intmike_volume_mute_nouse(struct hda_codec *codec)
{
	int retval;

	struct cs8409_apple_spec *spec = codec->spec;

	// nodes 0x44 is connected to 0x22 which is labelled mic input (macbook pro)

	//snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_POWER_STATE, 0x00000000); // 0x00170500

	retval = snd_hda_codec_read_check(codec, spec->intmike_adc_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x000000a7, 0); // 0x022b2000
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x000000a7, 0); // 0x022b2000
//      snd_hda:     amp gain/mute 34 0x2000 index 0x00 left/right 1 left output/input 0 input
//      snd_hda:     amp gain/mute 34 0x00a7 mute 1 gain 0x27 39
	snd_hda_codec_write(codec, spec->intmike_adc_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x000060b3); // 0x022360b3
	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x000060b3); // 0x022360b3
//      snd_hda:     amp gain/mute 34 0x60b3 mute 1 gain 0x33 51 index 0x00 left 1 right 0 output 0 input 1 left   input
	retval = snd_hda_codec_read_check(codec, spec->intmike_adc_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x000000a7, 0); // 0x022b0000
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x000000a7, 0); // 0x022b0000
//      snd_hda:     amp gain/mute 34 0x0000 index 0x00 left/right 0 right output/input 0 input
//      snd_hda:     amp gain/mute 34 0x00a7 mute 1 gain 0x27 39
	snd_hda_codec_write(codec, spec->intmike_adc_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x000050b3); // 0x022350b3
	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x000050b3); // 0x022350b3
//      snd_hda:     amp gain/mute 34 0x50b3 mute 1 gain 0x33 51 index 0x00 left 0 right 1 output 0 input 1  right  input

}

void cs_8409_linein_volume_mute_nouse(struct hda_codec *codec)
{
	int retval;

	struct cs8409_apple_spec *spec = codec->spec;

	// nodes 0x45 which are connected to 0x23 is labelled as line input (macbook pro)

	retval = snd_hda_codec_read_check(codec, spec->linein_amp_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x000000a7, 0); // 0x023b2000
	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x000000a7, 0); // 0x023b2000
//      snd_hda:     amp gain/mute 35 0x2000 index 0x00 left/right 1 left output/input 0 input
//      snd_hda:     amp gain/mute 35 0x00a7 mute 1 gain 0x27 39
	snd_hda_codec_write(codec, spec->linein_amp_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x000060b3); // 0x023360b3
	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x000060b3); // 0x023360b3
//      snd_hda:     amp gain/mute 35 0x60b3 mute 1 gain 0x33 51 index 0x00 left 1 right 0 output 0 input 1 left   input
	retval = snd_hda_codec_read_check(codec, spec->linein_amp_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x000000a7, 0); // 0x023b0000
	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x000000a7, 0); // 0x023b0000
//      snd_hda:     amp gain/mute 35 0x0000 index 0x00 left/right 0 right output/input 0 input
//      snd_hda:     amp gain/mute 35 0x00a7 mute 1 gain 0x27 39
	snd_hda_codec_write(codec, spec->linein_amp_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x000050b3); // 0x023350b3
	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x000050b3); // 0x023350b3
//      snd_hda:     amp gain/mute 35 0x50b3 mute 1 gain 0x33 51 index 0x00 left 0 right 1 output 0 input 1  right  input

}

void cs_8409_intmike_volume_unmute_nouse(struct hda_codec *codec)
{
	int retval;

	struct cs8409_apple_spec *spec = codec->spec;

	// nodes 0x44 is connected to 0x22 which is labelled mic input (macbook pro)

	retval = snd_hda_codec_read_check(codec, spec->intmike_adc_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x000000b3, 0); // 0x022b2000
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x000000b3, 0); // 0x022b2000
//      snd_hda:     amp gain/mute 34 0x2000 index 0x00 left/right 1 left output/input 0 input
//      snd_hda:     amp gain/mute 34 0x00b3 mute 1 gain 0x33 51
	snd_hda_codec_write(codec, spec->intmike_adc_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x00006033); // 0x02236033
	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x00006033); // 0x02236033
//      snd_hda:     amp gain/mute 34 0x6033 mute 0 gain 0x33 51 index 0x00 left 1 right 0 output 0 input 1 left   input
	retval = snd_hda_codec_read_check(codec, spec->intmike_adc_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x000000b3, 0); // 0x022b0000
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x000000b3, 0); // 0x022b0000
//      snd_hda:     amp gain/mute 34 0x0000 index 0x00 left/right 0 right output/input 0 input
//      snd_hda:     amp gain/mute 34 0x00b3 mute 1 gain 0x33 51
	snd_hda_codec_write(codec, spec->intmike_adc_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x00005033); // 0x02235033
	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x00005033); // 0x02235033
//      snd_hda:     amp gain/mute 34 0x5033 mute 0 gain 0x33 51 index 0x00 left 0 right 1 output 0 input 1  right  input

}

void cs_8409_linein_volume_unmute_nouse(struct hda_codec *codec)
{
	int retval;

	struct cs8409_apple_spec *spec = codec->spec;

	// nodes 0x45 which are connected to 0x23 is labelled as line input (macbook pro)

	retval = snd_hda_codec_read_check(codec, spec->linein_amp_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x000000b3, 0); // 0x023b2000
	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00002000, 0x000000b3, 0); // 0x023b2000
//      snd_hda:     amp gain/mute 35 0x2000 index 0x00 left/right 1 left output/input 0 input
//      snd_hda:     amp gain/mute 35 0x00b3 mute 1 gain 0x33 51
	snd_hda_codec_write(codec, spec->linein_amp_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x00006033); // 0x02336033
	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x00006033); // 0x02336033
//      snd_hda:     amp gain/mute 35 0x6033 mute 0 gain 0x33 51 index 0x00 left 1 right 0 output 0 input 1 left   input
	retval = snd_hda_codec_read_check(codec, spec->linein_amp_nid, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x000000b3, 0); // 0x023b0000
	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_AMP_GAIN_MUTE, 0x00000000, 0x000000b3, 0); // 0x023b0000
//      snd_hda:     amp gain/mute 35 0x0000 index 0x00 left/right 0 right output/input 0 input
//      snd_hda:     amp gain/mute 35 0x00b3 mute 1 gain 0x33 51
	snd_hda_codec_write(codec, spec->linein_amp_nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x00005033); // 0x02335033
	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x00005033); // 0x02335033
//      snd_hda:     amp gain/mute 35 0x5033 mute 0 gain 0x33 51 index 0x00 left 0 right 1 output 0 input 1  right  input

}

void cs_8409_inputs_power_nids_on(struct hda_codec *codec)
{

	struct cs8409_apple_spec *spec = codec->spec;

	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 2785); // 0x022f0500
	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 2786); // 0x023f0500

	hda_set_node_power_state(codec, spec->intmike_adc_nid, AC_PWRST_D0);
	hda_set_node_power_state(codec, spec->linein_amp_nid, AC_PWRST_D0);

}

void cs_8409_inputs_power_nids_off(struct hda_codec *codec)
{

	struct cs8409_apple_spec *spec = codec->spec;

	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 10741); // 0x022f0500
	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 10742); // 0x023f0500

	hda_set_node_power_state(codec, spec->intmike_adc_nid, AC_PWRST_D3);
	hda_set_node_power_state(codec, spec->linein_amp_nid, AC_PWRST_D3);

}

static void cs_8409_linein_format_setup_enable(struct hda_codec *codec)
{
	int ret_coef9 = 0;
	int new_coef9 = 0;

	struct cs8409_apple_spec *spec = codec->spec;

	// theres some weird issue here
	// index 0x0009 has bit 0x0080 set only after an unplug event with headset with mike
	// but then never seems to be turned off!!!

	// 0x45 -> 0x23 is line input

	// now updated to not write the Apple format but use my format setting routines
	// (remember we have limited the allowed formats to acceptable ones)
	// note that apparently we can set the format with the nid powered down but for setting the
	// stream id the nid has to be powered up
	// we may wish to ignore the power down here - because we reactivate the nid only a few steps
	// later

	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_STREAM_FORMAT, 0x00004033); // 0x02324033
//      snd_hda:     stream format 35 [('CHAN', 4), ('RATE', 44100), ('BITS', 24), ('RATE_MUL', 1), ('RATE_DIV', 1)]

	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 10555); // 0x023f0500
	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_POWER_STATE, 0x00000000); // 0x02370500
	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000030, 10558); // 0x023f0500
	hda_set_node_power_state(codec, spec->linein_amp_nid, AC_PWRST_D0);

	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000012); // 0x02370612
//      snd_hda:     conv stream channel map 35 [('CHAN', 2), ('STREAMID', 1)]

	// using the stored stream parameters update nid 0x23 stream parameters
	// we have limited the allowed formats so should only have working formats here
	cs_8409_really_update_stream_format(codec, spec->linein_amp_nid, 1, 1, 0);

	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_POWER_STATE, 0x00000003); // 0x02370503
	//retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 10561); // 0x023f0500
	hda_set_node_power_state(codec, spec->linein_amp_nid, AC_PWRST_D3);

//      snd_hda: # AppleHDAWidgetCS8409::setConnectionSelect:
	ret_coef9 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0009, 0x0000, 0x000000b3, 0); // AppleHDAWidgetCS8409::setConnectionSelect  coef read 10563
	//new_coef9 = ret_coef9 | 0x80; // I dont get this bit set - see above
	new_coef9 = ret_coef9 | spec->reg9_linein_dmic_mo; // I dont get this bit set - see above
	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0009, 0x00b3, 0x00000000, 10567 ); // AppleHDAWidgetCS8409::setConnectionSelect  coef write 10567
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0009, new_coef9, 0x00000000, 0); // AppleHDAWidgetCS8409::setConnectionSelect  coef write 10567
	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_CONNECT_SEL, 0x00000000); // 0x02370100
	snd_hda_codec_write(codec, spec->linein_amp_nid, 0, AC_VERB_SET_CONNECT_SEL, 0x00000000); // 0x02370100

}

static void cs_8409_linein_stream_on_nid(struct hda_codec *codec)
{
	int retval;
	int reg_coef82 = 0;
	int new_coef82 = 0;

	struct cs8409_apple_spec *spec = codec->spec;

	reg_coef82 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0082, 0x0000, 0x00005401, 0); //   coef read 10584

	new_coef82 = (reg_coef82 | spec->reg82_linein_dmic_scl);

	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0082, 0x5403, 0x00000000, 10588 ); //   coef write 10588
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0082, new_coef82, 0x00000000, 10588); //   coef write 10588

	//retval = snd_hda_codec_read_check(codec, 0x45, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000000, 0); // 0x045f0700
	retval = snd_hda_codec_read_check(codec, spec->linein_nid, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000000, 0); // 0x045f0700

	snd_hda_codec_write(codec, spec->linein_nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000020); // 0x04570720

	//snd_hda_codec_write(codec, 0x45, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000020); // 0x04570720
//      snd_hda:     69 ['AC_PINCTL_IN_EN']

}

void cs_8409_intmike_stream_conn_off_disable(struct hda_codec *codec)
{
	int retval;

	struct cs8409_apple_spec *spec = codec->spec;

	// more weird issue here
	// index 0x0009 has bit 0x0100 set only after an unplug event with headset with mike
	// it is reset

	// 0x44 -> 0x22 is internal (I think) mike input (macbook pro)

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, AC_VERB_SET_PROC_STATE, 0x00000001); // 0x04770301

	retval = snd_hda_codec_read_check(codec, spec->intmike_adc_nid, 0, AC_VERB_GET_CONV, 0x00000000, 0x00000000, 0); // 0x022f0600
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_CONV, 0x00000000, 0x00000000, 0); // 0x022f0600
//      snd_hda:     conv stream channel map 34 [('CHAN', 0), ('STREAMID', 0)]

	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 10596); // 0x022f0500
	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_POWER_STATE, 0x00000000); // 0x02270500
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000030, 10599); // 0x022f0500
	hda_set_node_power_state(codec, spec->intmike_adc_nid, AC_PWRST_D0);

	snd_hda_codec_write(codec, spec->intmike_adc_nid, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02270600
	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02270600
//      snd_hda:     conv stream channel map 34 [('CHAN', 0), ('STREAMID', 0)]

	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_POWER_STATE, 0x00000003); // 0x02270503
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 10602); // 0x022f0500
	hda_set_node_power_state(codec, spec->intmike_adc_nid, AC_PWRST_D3);

	// this is NOT from setConnectionSelect - unknown where from
	// very not clear what this does - it appears as part of the multiple disable/enables
//      snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0009, 0x01b3, 0xffff, 0x000000b3, 0, 10604 ); // coef write mask 10604
	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0009, 0x0100, 0x0000, 0x000000b3, 0x01b3, 0); // coef write mask 10604

}

static void cs_8409_intmike_stream_conn_off_enable(struct hda_codec *codec)
{
	int retval;

	struct cs8409_apple_spec *spec = codec->spec;

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, AC_VERB_SET_PROC_STATE, 0x00000001); // 0x04770301

	retval = snd_hda_codec_read_check(codec, spec->intmike_adc_nid, 0, AC_VERB_GET_CONV, 0x00000000, 0x00000000, 0); // 0x022f0600
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_CONV, 0x00000000, 0x00000000, 0); // 0x022f0600
//      snd_hda:     conv stream channel map 34 [('CHAN', 0), ('STREAMID', 0)]

	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 10676); // 0x022f0500
	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_POWER_STATE, 0x00000000); // 0x02270500
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000030, 10679); // 0x022f0500
	hda_set_node_power_state(codec, spec->intmike_adc_nid, AC_PWRST_D0);

	snd_hda_codec_write(codec, spec->intmike_adc_nid, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02270600
	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02270600
//      snd_hda:     conv stream channel map 34 [('CHAN', 0), ('STREAMID', 0)]

	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_POWER_STATE, 0x00000003); // 0x02270503
	//retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000033, 10682); // 0x022f0500
	hda_set_node_power_state(codec, spec->intmike_adc_nid, AC_PWRST_D3);

	// this is NOT from setConnectionSelect - unknown where from
	// very not clear what this does - it appears as part of the multiple disable/enables
//      snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0009, 0x00b3, 0xffff, 0x000001b3, 0, 10684 ); // coef write mask 10684
	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0009, 0x0000, 0x0100, 0x000001b3, 0x00b3, 0); // coef write mask 10684

}

void cs_8409_intmike_linein_resetup(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;

	// for some very strange reason we setup a 4 channel format after unplug of headset with mike

	cs_8409_intmike_format_setup_enable(codec, 0x4033, 1);

	cs_8409_intmike_volume_setup(codec, 0x27);

	cs_8409_intmike_stream_on_nid(codec);

	cs_8409_linein_format_setup_enable(codec);

	cs_8409_linein_volume_setup(codec, 0x27);

	cs_8409_linein_stream_on_nid(codec);

	cs_8409_intmike_stream_conn_off_disable(codec);

	cs_8409_linein_stream_conn_off(codec);

	cs_8409_intmike_stream_off_nid(codec);

	cs_8409_linein_stream_off_nid(codec);

	cs_8409_really_update_stream_format(codec, spec->intmike_adc_nid, 1, 0, 0);

	cs_8409_linein_volume_setup(codec, 0x27);

	cs_8409_linein_format_setup_disable(codec);

	cs_8409_intmike_stream_conn_off_enable(codec);

	cs_8409_linein_stream_conn_off(codec);

	cs_8409_intmike_stream_off_nid(codec);

	cs_8409_linein_stream_off_nid(codec);

	cs_8409_intmike_volume_mute(codec);

	cs_8409_linein_volume_mute(codec);

	cs_8409_intmike_volume_unmute(codec);

	cs_8409_linein_volume_unmute(codec);

	cs_8409_inputs_power_nids_off(codec);

}

void cs_8409_intmike_linein_disable(struct hda_codec *codec)
{

	cs_8409_intmike_stream_conn_off(codec);

	cs_8409_linein_stream_conn_off(codec);

	cs_8409_intmike_stream_off_nid(codec);

	cs_8409_linein_stream_off_nid(codec);

	cs_8409_intmike_volume_setup(codec, 0x27);

	cs_8409_intmike_format_setup_disable(codec);

	cs_8409_linein_volume_setup(codec, 0x27);

	cs_8409_linein_format_setup_disable(codec);

}

