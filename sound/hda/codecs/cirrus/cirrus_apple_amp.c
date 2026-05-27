// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Apple-specific support for the Cirrus Logic CS8409 HDA bridge chip.
 * Amp control (MAX98706/SSM3515/TAS5764L) split out from cirrus_apple.c.
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

static void cs_8409_setup_TDM_sample_rate(struct hda_codec *codec)
{

	// codes from windows cs4208_38.inf file
	// 0x0001 undocumented (0x0066 = ASP1/2_EN = 1, ASP1/2_STP = 1 ie ASP1_EN = 0x40, ASP2_EN = 0x20, ASP1_STP = 0x4, ASP2_STP = 0x2)
	// 0x0005 0x0001 SCDIV 1:4 (0x005a = ASP1: MCEN = 0, FSD = 010 (0x40), SCPOL_IN/OUT = 1 (0x10), SCDIV = 1:4 ie 0x0-0xf)
	// 0x0004 0x08ff SC_SRCSEL? = PLL1, LCPR = 0xff (0x28FF = (ASP1: MC/SC_SRCSEL = PLL1, LCPR = FFh), 0x2801 = (ASP1: MC/SC_SRCSEL = PLL1, LCPR = 01h))

//      snd_hda: # AppleHDATDMBusManagerCS8409::setSampleRate:
	//snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0001, 0x0200, 0xffff, 0x00000200, 0, 25 ); // coef write mask 25

	//snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0005, 0x0001, 0xffff, 0x00000001, 0, 31 ); // coef write mask 31
	//snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0004, 0x08ff, 0xffff, 0x000008ff, 0, 37 ); // coef write mask 37

	// we need to use proper masked versions here - in particular for register 1 which seems to be some form of enable control
	// for the subsystems and bits 0x7f need to pass thro here
	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0001, 0x0200, 0x0380, 0x00000200, 0, 0); // coef write mask 25

	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0005, 0x0001, 0x0007, 0x00000001, 0, 0); // coef write mask 31
	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0004, 0x08ff, 0x01ff, 0x000008ff, 0, 0); // coef write mask 37
}

static void cs_8409_setup_TDM_proper_amps12(struct hda_codec *codec)
{
	int ret_coef0 = 0;
	int ret_coef1 = 0;
	int new_coef1 = 0;
	int ret_coef71 = 0;
	int new_coef71 = 0;

	// codes from windows cs4208_38.inf file
	// 0x0019 0x0800 = (ASP1.A: TX.LAP = 0, TX.LSZ = 24 bits, TX.LCS = 0)
	// 0x001a 0x0820 = (ASP1.A: TX.RAP = 0, TX.RSZ = 24 bits, TX.RCS = 32)

//      snd_hda: # AppleHDATDMBusManagerCS8409::setupTDMPath:
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0019, 0x0000, 0x00008800, 0); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef read 44
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0019, 0x0800, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef write 48
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x001a, 0x0000, 0x00008820, 0); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef read 52
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x001a, 0x0820, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef write 56

	// codes from windows cs4208_38.inf file
	// 0x0000 0xb000 = PLL2_EN (0x2000) PLL1_EN (0x1000) I2C disabled
	// 0x0004 0x08ff SC_SRCSEL? = PLL1, LCPR = 0xff (0x28FF = (ASP1: MC/SC_SRCSEL = PLL1, LCPR = FFh), 0x2801 = (ASP1: MC/SC_SRCSEL = PLL1, LCPR = 01h))
	// 0x0000 0x9000 = PLL1_EN (0x1000) I2C disabled

	ret_coef0 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0000, 0x0000, 0x00009000, 0); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef read 60
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0000, 0xb000, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef write 64
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0004, 0x0000, 0x000008ff, 0); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef read 68
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0004, 0x08ff, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef write 72
	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0000, 0x9000, 0x00000000, 76 ); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef write 76
	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0000, 0xb000, 0x00000000, 76 ); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef write 76
	// it seems we re-write what we read just above here - if never plugin headphones its 0x9000
	// after headphone plugin its 0xb0000 - even after unplugging headphones!!
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0000, ret_coef0, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef write 76

	// codes from windows cs4208_38.inf file
	// 0x0003 0x8000 = (ASP1: LCHI = 00h)
	// 0x0005 0x0001 = SCDIV = 1:4 (0x005A = (ASP1: MCEN = 0, FSD = 010 (0x40), SCPOL_IN/OUT = 1 (0x10), SCDIV = 1:4))

//      snd_hda: # AppleHDATDMBusManagerCS8409::setupTDMPath:
	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0003, 0x8000, 0xffff, 0x00008000, 0, 0); // coef write mask 80
	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0005, 0x0001, 0xffff, 0x00000001, 0, 0); // coef write mask 86

	// codes from windows cs4208_38.inf file
	// 0x0082 0x5401 = ASP1_xxx_EN (0x4000) ASP1_xxx_EN (0x1000) ASP1_xxx_EN (0x400) DMIC1_SCL_EN = 1 (0xFC01 = (ASP1/2_xxx_EN = 1, ASP1/2_MCLK_EN = 0, DMIC1_SCL_EN = 1))

//      snd_hda: # AppleHDATDMBusManagerCS8409::setupTDMPath:
	//snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0082, 0x5401, 0xffff, 0x00000001, 0, 92 ); // coef write mask 92
	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0082, 0x5400, 0x0000, 0x00000001, 0x5401, 0); // coef write mask 92

	// codes from windows cs4208_38.inf file
	// 0x0002 0x0280 = ASP2_BUS_IDLE (0x02) GPIO_I2C (0x0A80 = (ASP1/2_BUS_IDLE = 10, +GPIO_I2C))

	// no evidence this is anything other than 0x0280 yet
	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0002, 0x0280, 0xffff, 0x00000280, 0, 0); // coef write mask 98

	ret_coef1 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0001, 0x0000, 0x00000200, 0); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef read 104

	new_coef1 = (ret_coef1 & 0xffff) | 0x20;

	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0001, 0x0220, 0x00000000, 108 ); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef write 108
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0001, new_coef1, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef write 108

//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR: AppleHDATDMBusManagerCS8409::tdmInUse:
	//snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0019, 0x0000, 0x00000800, 112 ); //   coef read 112

	tdm_in_use(codec, 1);

//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR:
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x006b, 0x001f, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 117

	ret_coef71 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0071, 0x0000, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef read 121

	new_coef71 = (ret_coef71 & 0xffff) | 0x400f;

	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0071, 0x400f, 0x00000000, 125 ); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 125
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0071, new_coef71, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 125

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, 0x7f0, 0x00b6); // AppleHDATDMBusManagerCS8409::configureTDMUR  write verb 128
}

void cs_8409_setup_TDM_amps12(struct hda_codec *codec, int setrate, int nullformat)
{
	int retval;
	int ret_coef1 = 0;
	int new_coef1 = 0;

	// this seems to be setup for node 0x02 chain - which seems to use node 0x24 and amps 0x64 and 0x62 (or 0x28 0x2a)

	//snd_hda_codec_write(codec, 0x02, 0, AC_VERB_SET_STREAM_FORMAT, 0x00004033); // 0x00224033
//      snd_hda:     stream format 2 [('CHAN', 4), ('RATE', 44100), ('BITS', 24), ('RATE_MUL', 1), ('RATE_DIV', 1)]

	//snd_hda_codec_write(codec, 0x02, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000010); // 0x00270610
//      snd_hda:     conv stream channel map 2 [('CHAN', 0), ('STREAMID', 1)]

	if (nullformat) {
		// note that 0x4033 is Apples fixed format - but this is for boot stage when we have
		// not defined any format yet so just use it - we overwrite below when actually play
		snd_hda_codec_write(codec, 0x02, 0, AC_VERB_SET_STREAM_FORMAT, 0x00004033); // 0x00224033
		snd_hda_codec_write(codec, 0x02, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x00270600
	} else {
		// using the stored stream parameters update nid 0x2 stream parameters
		// we have limited the allowed formats so should only have working formats here
		cs_8409_really_update_stream_format(codec, 0x02, 1, 2, 0);
	}

//      snd_hda: # AppleHDATDMBusManagerCS8409::setupTDMPath:
	ret_coef1 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0001, 0x0000, 0x00000200, 0); //   coef read 16

	new_coef1 = (ret_coef1 & 0xffff); // not clear what this is setting - no difference between read and write
					  // however if used in different places the actual value may be different

	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0001, 0x0200, 0x00000000, 20 ); //   coef write 20
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0001, new_coef1, 0x00000000, 0); //   coef write 20

	if (setrate)
		cs_8409_setup_TDM_sample_rate(codec);

	cs_8409_setup_TDM_proper_amps12(codec);

	// enable output node 0x24

	//retval = snd_hda_codec_read_check(codec, 0x24, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000000, 132); // 0x024f0700
	retval = snd_hda_codec_read(codec, 0x24, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000); // 0x024f0700
	snd_hda_codec_write(codec, 0x24, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000040); // 0x02470740
//      snd_hda:     36 ['AC_PINCTL_OUT_EN']

}

void play_setup_TDM_amps12(struct hda_codec *codec, int setrate)
{
	cs_8409_setup_TDM_amps12(codec, setrate, 0);
}

static void cs_8409_setup_amp_max(struct hda_codec *codec, int amp_address, int amp_volume, int channel);

static void cs_8409_setup_amp_ssm3(struct hda_codec *codec, int amp_address, int amp_volume, int channel);

static void cs_8409_setup_amp_tas576(struct hda_codec *codec, int amp_address, int amp_volume, int channel, int amp_enable);

void cs_8409_setup_amps12(struct hda_codec *codec, int amps_enable)
{
	if (codec->core.subsystem_id == 0x106b3900) {
		// use reduced volume - from 0x01 to 0x30 - now passing as argument
		// change channel mapping - make a standard left/right pair
		// so 1st 2 channels are tweeters, 2nd 2 channels are woofers
		cs_8409_setup_amp_max(codec, 0x64, 0x01, 0x00);
		cs_8409_setup_amp_max(codec, 0x62, 0x01, 0x02);
		// use reduced volume - from 0x01 to 0x30 - now passing as argument
		// original mapping:
	} else if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600) {
		// use reduced volume - from 0x01 to 0x30 - now passing as argument
		// change channel mapping - make a standard left/right pair
		// so 1st 2 channels are tweeters, 2nd 2 channels are woofers
		cs_8409_setup_amp_ssm3(codec, 0x28, 0x48, 0x00);
		cs_8409_setup_amp_ssm3(codec, 0x2a, 0x48, 0x02);
		// use reduced volume - from 0x48 to 0x80 - same reduction as for MAXs -24dB
		// original mapping:
	} else if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		// NOTE that the TAS57642 amps setup during boot seems to skip the actual speaker power on
		//      hence the addition of the amps_enable parameter
		// use reduced volume - from 0xcf to 0x9f - same reduction as for MAXs -24dB (0.5dB steps)
		// change channel mapping - make a standard left/right pair
		// so 1st 2 channels are tweeters, 2nd 2 channels are woofers
		cs_8409_setup_amp_tas576(codec, 0xd8, 0xcf, 0x00, amps_enable);
		cs_8409_setup_amp_tas576(codec, 0xda, 0xcf, 0x02, amps_enable);
		// use reduced volume - from 0xcf to 0x9f - same reduction as for MAXs -24dB (0.5dB steps)
		// original mapping:
	} else {
		dev_info(hda_codec_dev(codec), "UNKNOWN subsystem id 0x%08x",codec->core.subsystem_id);
	}
}

void play_setup_amps12(struct hda_codec *codec)
{
	cs_8409_setup_amps12(codec, 1);
}

static void cs_8409_setup_TDM_proper_amps34(struct hda_codec *codec)
{
	int ret_coef71 = 0;
	int new_coef71 = 0;

	// codes from windows cs4208_38.inf file
	// 0x001b 0x0840 = (ASP1.B: TX.LAP = 0, TX.LSZ = 24 bits, TX.LCS = 64)
	// 0x001c 0x0860 = (ASP1.B: TX.RAP = 0, TX.RSZ = 24 bits, TX.RCS = 96)

	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x001b, 0x0000, 0x00008840, 0); //   coef read 1407
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x001b, 0x0840, 0x00000000, 0); //   coef write 1411
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x001c, 0x0000, 0x00008860, 0); //   coef read 1415
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x001c, 0x0860, 0x00000000, 0); //   coef write 1419

	// the following actually reads from 0x19 to 0x57 in a loop if the snd_hda_coef_item returns 0 till the read value
	// does not have the word sign bit set (ie 0x8000) or finish all 0x57

//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR: AppleHDATDMBusManagerCS8409::tdmInUse:
	//snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0019, 0x0000, 0x00000800, 1423 ); //   coef read 1423

	tdm_in_use(codec, 2);

//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR:
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x006b, 0x001f, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 1428

	ret_coef71 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0071, 0x0000, 0x0000400f, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef read 1432

	new_coef71 = (ret_coef71 & 0xffff) | 0x400f;

	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0071, 0x400f, 0x00000000, 1436 ); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 1436
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0071, new_coef71, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 1436

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, 0x7f0, 0x00b6); // AppleHDATDMBusManagerCS8409::configureTDMUR  write verb 1439

}

void cs_8409_setup_TDM_amps34(struct hda_codec *codec, int nullformat)
{
	int retval;
	struct cs8409_apple_spec *spec = codec->spec;

	// this seems to be setup for node 0x03 chain - which seems to use node 0x25 and amps 0x74 and 0x72 (or 0x2c and 0x2e)

	//snd_hda_codec_write(codec, 0x03, 0, AC_VERB_SET_STREAM_FORMAT, 0x00004033); // 0x00324033
//      snd_hda:     stream format 3 [('CHAN', 4), ('RATE', 44100), ('BITS', 24), ('RATE_MUL', 1), ('RATE_DIV', 1)]

	//snd_hda_codec_write(codec, 0x03, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000012); // 0x00370612
//      snd_hda:     conv stream channel map 3 [('CHAN', 2), ('STREAMID', 1)]

	if (nullformat) {
		// note that 0x4033 is Apples fixed format - but this is for boot stage when we have
		// not defined any format yet so just use it - we overwrite below when actually play
		// note this updates the channel but not stream id????
		snd_hda_codec_write(codec, 0x03, 0, AC_VERB_SET_STREAM_FORMAT, 0x00004033); // 0x00224033
		snd_hda_codec_write(codec, 0x03, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000002); // 0x00270602
	} else {
		// using the stored stream parameters update nid 0x3 stream parameters
		// we have limited the allowed formats so should only have working formats here
		// now realised if we want to pass 2 channel streams to driver then we need to apply same
		// fixup as in snd_hda_multi_out_analog_prepare ie set the channel id of the second nid
		// to same as first nid for 2 channel inputs - rather than 2 if have a 4 channel stream
		if (spec->stream_channels == 2)
			cs_8409_really_update_stream_format(codec, 0x03, 1, 2, 0);
		else
			cs_8409_really_update_stream_format(codec, 0x03, 1, 2, 2);
	}

	cs_8409_setup_TDM_proper_amps34(codec);

	// enable output on node 0x25

	//retval = snd_hda_codec_read_check(codec, 0x25, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000000, 1443); // 0x025f0700
	retval = snd_hda_codec_read(codec, 0x25, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000); // 0x025f0700
	snd_hda_codec_write(codec, 0x25, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000040); // 0x02570740
//      snd_hda:     37 ['AC_PINCTL_OUT_EN']

}

void play_setup_TDM_amps34(struct hda_codec *codec)
{
	cs_8409_setup_TDM_amps34(codec, 0);
}

void cs_8409_setup_amps34(struct hda_codec *codec, int amps_enable)
{
	if (codec->core.subsystem_id == 0x106b3900) {
		// use reduced volume - from 0x01 to 0x30 - now passing as argument
		// change channel mapping - make a standard left/right pair
		// so 1st 2 channels are tweeters, 2nd 2 channels are woofers
		cs_8409_setup_amp_max(codec, 0x74, 0x01, 0x01);
		cs_8409_setup_amp_max(codec, 0x72, 0x01, 0x03);
		// use reduced volume - from 0x01 to 0x30 - now passing as argument
		// original mapping:
	} else if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600) {
		// use reduced volume - from 0x48 to 0x80 - same reduction as for MAXs -24dB
		// change channel mapping - make a standard left/right pair
		// so 1st 2 channels are tweeters, 2nd 2 channels are woofers
		cs_8409_setup_amp_ssm3(codec, 0x2c, 0x48, 0x01);
		cs_8409_setup_amp_ssm3(codec, 0x2e, 0x48, 0x03);
		// use reduced volume - from 0x48 to 0x80 - same reduction as for MAXs -24dB
		// original mapping:
	} else if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		// NOTE that the TAS57642 amps setup during boot seems to skip the actual speaker power on
		//      hence the addition of the amps_enable parameter
		// use reduced volume - from 0x48 to 0x80 - same reduction as for MAXs -24dB
		// change channel mapping - make a standard left/right pair
		// so 1st 2 channels are tweeters, 2nd 2 channels are woofers
		cs_8409_setup_amp_tas576(codec, 0xdc, 0xcf, 0x01, amps_enable);
		cs_8409_setup_amp_tas576(codec, 0xde, 0xcf, 0x03, amps_enable);
		// use reduced volume - from 0xcf to 0x9f - same reduction as for MAXs -24dB (0.5dB steps)
		// original mapping:
	} else {
		dev_info(hda_codec_dev(codec), "UNKNOWN subsystem id 0x%08x",codec->core.subsystem_id);
	}
}

void play_setup_amps34(struct hda_codec *codec)
{
	cs_8409_setup_amps34(codec, 1);
}

void cs_8409_sync_converters_on(struct hda_codec *codec, int nullformat)
{

	struct hda_cvt_setup p2;
	struct hda_cvt_setup p3;

	// this stops streaming on nodes 0x2 and 0x3 by switching to stream index 0
	// then updates vendor node coef index 0x0017 twice
	// first with 0x1 for node 0x2 and then with 0x2 for node 0x3
	// giving a final 0x3
	// then sets up ready for streaming again

	// so coef index 0x17 is likely turning on the TDM stream

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, AC_VERB_SET_PROC_STATE, 0x00000001); // 0x04770301

	// remove normal channel mapping

//      snd_hda: # AppleHDAFunctionGroupCS8409::syncConverters:
	//retval = snd_hda_codec_read_check(codec, 0x02, 0, AC_VERB_GET_CONV, 0x00000000, 0x00000010, 2714); // 0x002f0600
	//retval = snd_hda_codec_read(codec, 0x02, 0, AC_VERB_GET_CONV, 0x00000000); // 0x002f0600
//      snd_hda:     conv stream channel map 2 [('CHAN', 0), ('STREAMID', 1)]

	//snd_hda_codec_write(codec, 0x02, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x00270600
//      snd_hda:     conv stream channel map 2 [('CHAN', 0), ('STREAMID', 0)]

	cs_8409_save_and_clear_stream_format(codec, 0x02, &p2);

	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0017, 0x0001, 0xffff, 0x00000000, 0, 0); // coef write mask 2716

//      snd_hda: # AppleHDAFunctionGroupCS8409::syncConverters:
	//retval = snd_hda_codec_read_check(codec, 0x03, 0, AC_VERB_GET_CONV, 0x00000000, 0x00000012, 2722); // 0x003f0600
	//retval = snd_hda_codec_read(codec, 0x03, 0, AC_VERB_GET_CONV, 0x00000000); // 0x003f0600
//      snd_hda:     conv stream channel map 3 [('CHAN', 2), ('STREAMID', 1)]

	//snd_hda_codec_write(codec, 0x03, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x00370600
//      snd_hda:     conv stream channel map 3 [('CHAN', 0), ('STREAMID', 0)]

	cs_8409_save_and_clear_stream_format(codec, 0x03, &p3);

	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0017, 0x0003, 0xffff, 0x00000001, 0, 0); // coef write mask 2724

	// and reset back to normal channel mapping

	//snd_hda_codec_write(codec, 0x02, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000010); // 0x00270610
//      snd_hda:     conv stream channel map 2 [('CHAN', 0), ('STREAMID', 1)]

	//snd_hda_codec_write(codec, 0x03, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000012); // 0x00370612
//      snd_hda:     conv stream channel map 3 [('CHAN', 2), ('STREAMID', 1)]

	if (nullformat) {
		snd_hda_codec_write(codec, 0x02, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x00270600
		snd_hda_codec_write(codec, 0x03, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000002); // 0x00270602
	} else {
		cs_8409_update_from_save_stream_format(codec, 0x02, &p2, 1, 0);
		cs_8409_update_from_save_stream_format(codec, 0x03, &p3, 1, 0);
	}
}

void play_sync_converters_on(struct hda_codec *codec)
{
	cs_8409_sync_converters_on(codec, 0);
}

static void cs_8409_setup_amp_max(struct hda_codec *codec, int amp_address, int amp_volume, int channel)
{

	// NOTA BENE - reduced volume from 0x01 to 0x30 - seems to fit with linux levels much better
	// this is semi arbitrary - just hears about OK - not based on rational analysis of amp gains

	// HPFDCBlocker new as of June 2019

//      snd_hda: # i2cWrite:
//      snd_hda i2cWrite      i2c address 0x64 i2c            reg 0x1c01 i2c data 0x0001   reg anal: DigitalFilter           : HPFDCBlocker
//      snd_hda i2cWrite      i2c address 0x64 i2c            reg 0x1008 i2c data 0x0008   reg anal: PCMClockSetup           : 256 Bclks
//      snd_hda i2cWrite      i2c address 0x64 i2c            reg 0x14e4 i2c data 0x00e4   reg anal: PCMModeConfig           : 32 bits TDM mode 2
//      snd_hda i2cWrite      i2c address 0x64 i2c            reg 0x1501 i2c data 0x0001   reg anal: PCMRXEnablesA
//      snd_hda i2cWrite      i2c address 0x64 i2c            reg 0x1600 i2c data 0x0000   reg anal: PCMRXEnablesB
//      snd_hda i2cWrite      i2c address 0x64 i2c            reg 0x1800 i2c data 0x0000   reg anal: MonoMixChannelSource
//      snd_hda i2cWrite      i2c address 0x64 i2c            reg 0x2d01 i2c data 0x0001   reg anal: DigitalVolCtrl
//      snd_hda i2cWrite      i2c address 0x64 i2c            reg 0x2e05 i2c data 0x0005   reg anal: PathGain
//      snd_hda i2cWrite      i2c address 0x64 i2c            reg 0x4a21 i2c data 0x0021   reg anal: SpeakerEnable           : AmpEnabled
//      snd_hda i2cWrite      i2c address 0x64 i2c            reg 0x4d07 i2c data 0x0007   reg anal: RestartBehavior
//      snd_hda i2cWrite      i2c address 0x64 i2c            reg 0x5534 i2c data 0x0034   reg anal: LimiterAttackRelease
//      snd_hda i2cRead       i2c address 0x64 i2c            reg 0x1100 i2c data 0x1107   reg anal: PCMSampleSetup          : 44.1kHz
//      snd_hda i2cWrite      i2c address 0x64 i2c            reg 0x1107 i2c data 0x0007   reg anal: PCMSampleSetup          : 44.1kHz
//      snd_hda i2cWrite      i2c address 0x64 i2c            reg 0x093f i2c data 0x003f   reg anal: InterruptClears0
//      snd_hda i2cWrite      i2c address 0x64 i2c            reg 0x0a7f i2c data 0x007f   reg anal: InterruptClears1
//      snd_hda i2cWrite      i2c address 0x64 i2c            reg 0x0f0e i2c data 0x000e   reg anal: IRQClear1
//      snd_hda i2cWrite      i2c address 0x64 i2c            reg 0x5001 i2c data 0x0001   reg anal: GlobalEnable            : Enable

	cs_8409_vendor_i2cWrite(codec, amp_address, 0x001c, 0x0001, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0010, 0x0008, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0014, 0x00e4, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0015, (1<<channel), 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0016, 0x0000, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0018, channel, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0019, 0x0000, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x002d, amp_volume, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x002e, 0x0005, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x004a, 0x0021, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x004d, 0x0007, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0055, 0x0034, 0); // snd_hda
	cs_8409_vendor_i2cRead(codec, amp_address, 0x0011, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0011, 0x0007, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0009, 0x003f, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x000a, 0x007f, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x000f, 0x000e, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0050, 0x0001, 0); // snd_hda

}

static void cs_8409_setup_amp_ssm3(struct hda_codec *codec, int amp_address, int amp_volume, int channel)
{

//      snd_hda: # i2cWrite:
//      snd_hda i2cWrite      i2c address 0x28 i2c            reg 0x0500 i2c data 0x0000   reg anal: SAIControl2             : ChipSlot 1 SlotByTDMRReg 24 bit Audio
//      snd_hda i2cWrite      i2c address 0x28 i2c            reg 0x0111 i2c data 0x0011   reg anal: GainEdgeControl         : 12.6V FSGainMap LowEMI
//      snd_hda i2cWrite      i2c address 0x28 i2c            reg 0x0348 i2c data 0x0048   reg anal: DACVolume               : -3.0dB
//      snd_hda i2cWrite      i2c address 0x28 i2c            reg 0x0451 i2c data 0x0051   reg anal: SAIControl1             : TDMPCMMode 24 BClks per Chip Delay1 PulsedSync Falling
//      snd_hda i2cRead       i2c address 0x28 i2c            reg 0x0200 i2c data 0x0232   reg anal: DACControl              : 32-48kHz SampleRate DACLowPower DACHighPass DACSoftVol
//      snd_hda i2cWrite      i2c address 0x28 i2c            reg 0x0232 i2c data 0x0032   reg anal: DACControl              : 32-48kHz SampleRate DACLowPower DACHighPass DACSoftVol
//      snd_hda i2cRead       i2c address 0x28 i2c            reg 0x0200 i2c data 0x0232   reg anal: DACControl              : 32-48kHz SampleRate DACLowPower DACHighPass DACSoftVol
//      snd_hda i2cWrite      i2c address 0x28 i2c            reg 0x0232 i2c data 0x0032   reg anal: DACControl              : 32-48kHz SampleRate DACLowPower DACHighPass DACSoftVol
//      snd_hda i2cWrite      i2c address 0x28 i2c            reg 0x0000 i2c data 0x0000   reg anal: PowerControl            : PowerOn BVSenseOn

	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0005, channel, 1); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0001, 0x0011, 1); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0003, amp_volume, 1); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0004, 0x0051, 1); // snd_hda
	cs_8409_vendor_i2cRead(codec, amp_address, 0x0002, 1); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0002, 0x0032, 1); // snd_hda
	cs_8409_vendor_i2cRead(codec, amp_address, 0x0002, 1); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0002, 0x0032, 1); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0000, 0x0000, 1); // snd_hda

}

static void cs_8409_setup_amp_tas576(struct hda_codec *codec, int amp_address, int amp_volume, int channel, int amp_enable)
{

	// this data from the TAS5760md spec sheet (closest one I can find)
	// there are a number of inconsistencies
	// first - the MAX/SSM amps are mono amps but the TAS5760md seems to be a stereo amp
	// (there is a PBTL mode which can use both amps to drive a single speaker which
	//  may be whats going on - now this can be set in hardware but its does not appear to be done in software)
	// - note that the HDA node setup seems to be 4 channel audio as 2 left channels (node 0x02) and 2 right channels (0x03)
	// hence 4 mono amps required
	// some regs seem valid - others not
	// reg 0x01 does seem to be the power control and is used to enable/disable the amps and values are consistent
	// reg 0x04 is labelled the Left Volume but there are no reg 0x05 Right Volume setup calls at all
	// reg 0x03 is labelled Volume Control and the Fade may be right
	// but the low order mute bits dont make sense - simply seems to index the amps ie 0,1,2,3
	// - which according to tas5760 docs would be no mute, mute left, mute right, mute all
	// what makes more sense is that its indexing the individual channels of the 4 channel TDM stream
	// so yes I think this is being used in PBTL mode to drive a single speaker with reg 0x04 being
	// the volume control and reg 0x03 low order bits determining which channel to use

	// NOTE that the TAS57642 amps setup during boot seems to skip the actual speaker power on
	//      hence the addition of the amp_enable parameter

//      snd_hda i2cRead       i2c address 0xd8 i2c            reg 0x0200 i2c data 0x0204   reg anal: Digital Control
//      snd_hda i2cWrite      i2c address 0xd8 i2c            reg 0x0244 i2c data 0x0044   reg anal: Digital Control         : I2S format, Undocumented
//      snd_hda i2cRead       i2c address 0xd8 i2c            reg 0x0300 i2c data 0x0380   reg anal: Volume Control
//      snd_hda i2cWrite      i2c address 0xd8 i2c            reg 0x0380 i2c data 0x0080   reg anal: Volume Control          : Fade, Muted??
//      snd_hda i2cRead       i2c address 0xd8 i2c            reg 0x0600 i2c data 0x0651   reg anal: Analog Control
//      snd_hda i2cWrite      i2c address 0xd8 i2c            reg 0x0655 i2c data 0x0055   reg anal: Analog Control          : PWM Rate x16, Gain 22.6dB
//      snd_hda i2cRead       i2c address 0xd8 i2c            reg 0x0800 i2c data 0x0808   reg anal: Fault Config & Error
//      snd_hda i2cWrite      i2c address 0xd8 i2c            reg 0x0818 i2c data 0x0018   reg anal: Fault Config & Error    : Threshold 75%, Clock Error??
//      snd_hda i2cWrite      i2c address 0xd8 i2c            reg 0x04cf i2c data 0x00cf   reg anal: Left Chan Vol Control   : 0dB
//      snd_hda i2cRead       i2c address 0xd8 i2c            reg 0x1300 i2c data 0x1300   reg anal: Undocumented
//      snd_hda i2cWrite      i2c address 0xd8 i2c            reg 0x1300 i2c data 0x0000   reg anal: Undocumented
//      snd_hda i2cRead       i2c address 0xd8 i2c            reg 0x0200 i2c data 0x0244   reg anal: Digital Control
//      snd_hda i2cWrite      i2c address 0xd8 i2c            reg 0x0244 i2c data 0x0044   reg anal: Digital Control         : I2S format, Undocumented

	// here we have read/write pairs suggesting this is done as masked i2c writes
	// but we have no idea what the masks are

	cs_8409_vendor_i2cRead(codec, amp_address, 0x0002, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0002, 0x0044, 0); // snd_hda
	cs_8409_vendor_i2cRead(codec, amp_address, 0x0003, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0003, 0x0080|channel, 0); // snd_hda
	cs_8409_vendor_i2cRead(codec, amp_address, 0x0006, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0006, 0x0055, 0); // snd_hda
	cs_8409_vendor_i2cRead(codec, amp_address, 0x0008, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0008, 0x0018, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0004, amp_volume, 0); // snd_hda
	cs_8409_vendor_i2cRead(codec, amp_address, 0x0013, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0013, 0x0000, 0); // snd_hda
	cs_8409_vendor_i2cRead(codec, amp_address, 0x0002, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0002, 0x0044, 0); // snd_hda

//      snd_hda i2cRead       i2c address 0xd8 i2c            reg 0x0100 i2c data 0x01fc   reg anal: Power Control
//      snd_hda i2cWrite      i2c address 0xd8 i2c            reg 0x01fd i2c data 0x00fd   reg anal: Power Control           : Not sleep, Spkr Amp On

	if (amp_enable) {
		cs_8409_vendor_i2cRead(codec, amp_address, 0x0001, 0); // snd_hda
		cs_8409_vendor_i2cWrite(codec, amp_address, 0x0001, 0x00fd, 0); // snd_hda
	}
}

void cs_8409_sync_converters_off(struct hda_codec *codec, int nullformat)
{

	struct hda_cvt_setup p2;
	struct hda_cvt_setup p3;

	// this stops streaming on nodes 0x2 and 0x3 by switching to stream index 0
	// then updates vendor node coef index 0x0017 twice
	// first with 0x2 for node 0x1 and then with 0x0 for node 0x2
	// giving a final 0x0
	// then sets up ready for streaming again

	// so coef index 0x17 is likely turning off the TDM stream

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, AC_VERB_SET_PROC_STATE, 0x00000001); // 0x04770301

	// remove normal channel mapping

//      snd_hda: # AppleHDAFunctionGroupCS8409::syncConverters:
	//retval = snd_hda_codec_read_check(codec, 0x02, 0, AC_VERB_GET_CONV, 0x00000000, 0x00000010, 4); // 0x002f0600
	//retval = snd_hda_codec_read(codec, 0x02, 0, AC_VERB_GET_CONV, 0x00000000); // 0x002f0600
//      snd_hda:     conv stream channel map 2 [('CHAN', 0), ('STREAMID', 1)]

	//snd_hda_codec_write(codec, 0x02, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x00270600
//      snd_hda:     conv stream channel map 2 [('CHAN', 0), ('STREAMID', 0)]

	cs_8409_save_and_clear_stream_format(codec, 0x02, &p2);

	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0017, 0x0002, 0xffff, 0x00000003, 0, 0); // coef write mask 6

//      snd_hda: # AppleHDAFunctionGroupCS8409::syncConverters:
	//retval = snd_hda_codec_read_check(codec, 0x03, 0, AC_VERB_GET_CONV, 0x00000000, 0x00000012, 12); // 0x003f0600
	//retval = snd_hda_codec_read(codec, 0x03, 0, AC_VERB_GET_CONV, 0x00000000); // 0x003f0600
//      snd_hda:     conv stream channel map 3 [('CHAN', 2), ('STREAMID', 1)]

	//snd_hda_codec_write(codec, 0x03, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x00370600
//      snd_hda:     conv stream channel map 3 [('CHAN', 0), ('STREAMID', 0)]

	cs_8409_save_and_clear_stream_format(codec, 0x03, &p3);

	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0017, 0x0000, 0xffff, 0x00000002, 0, 0); // coef write mask 14

	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0017, 0x0000, 0x00000000, 0); //   coef read 20
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0018, 0x0000, 0x00000000, 0); //   coef read 24

	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0001, 0x0220, 0xffff, 0x00000220, 0, 0); // coef write mask 28

	// and reset back to normal channel mapping

	//snd_hda_codec_write(codec, 0x02, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000010); // 0x00270610
//      snd_hda:     conv stream channel map 2 [('CHAN', 0), ('STREAMID', 1)]

	//snd_hda_codec_write(codec, 0x03, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000012); // 0x00370612
//      snd_hda:     conv stream channel map 3 [('CHAN', 2), ('STREAMID', 1)]

	if (nullformat) {
		snd_hda_codec_write(codec, 0x02, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x00270600
		snd_hda_codec_write(codec, 0x03, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x00370602
	} else {
		cs_8409_update_from_save_stream_format(codec, 0x02, &p2, 1, 0);
		cs_8409_update_from_save_stream_format(codec, 0x03, &p3, 1, 0);
	}
}

void playstop_sync_converters_off(struct hda_codec *codec)
{
	cs_8409_sync_converters_off(codec, 0);
}

static void cs_8409_disable_amp_max(struct hda_codec *codec, int amp_address);

static void cs_8409_disable_amp_ssm3(struct hda_codec *codec, int amp_address);

static void cs_8409_disable_amp_tas576_amp0(struct hda_codec *codec);
static void cs_8409_disable_amp_tas576_amp1(struct hda_codec *codec);
static void cs_8409_disable_amp_tas576_amp2(struct hda_codec *codec);
static void cs_8409_disable_amp_tas576_amp3(struct hda_codec *codec);

void cs_8409_disable_amps12(struct hda_codec *codec)
{
	if (codec->core.subsystem_id == 0x106b3900) {
		cs_8409_disable_amp_max(codec, 0x64);
		cs_8409_disable_amp_max(codec, 0x62);
	} else if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600) {
		cs_8409_disable_amp_ssm3(codec, 0x28);
		cs_8409_disable_amp_ssm3(codec, 0x2a);
	} else if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		cs_8409_disable_amp_tas576_amp0(codec);
		cs_8409_disable_amp_tas576_amp1(codec);
	} else {
		dev_info(hda_codec_dev(codec), "UNKNOWN subsystem id 0x%08x",codec->core.subsystem_id);
	}
}

void playstop_disable_amps12(struct hda_codec *codec)
{
	cs_8409_disable_amps12(codec);
}

static void cs_8409_disable_TDM_proper_amps12(struct hda_codec *codec)
{
	int ret_coef71 = 0;
	int new_coef71 = 0;

	// this seems to be setup for node 0x02 chain - which seems to use node 0x24 and amps 0x64 and 0x62 (or 0x28 and 0x2a)

//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR:
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0019, 0x0000, 0x00000800, 0); //   coef read 341
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0019, 0x8800, 0x00000000, 0); //   coef write 345
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x001a, 0x0000, 0x00000820, 0); //   coef read 349
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x001a, 0x8820, 0x00000000, 0); //   coef write 353

	//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR: AppleHDATDMBusManagerCS8409::tdmInUse:
	//snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0019, 0x0000, 0x00008800, 357 ); //   coef read 357
	//snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x001a, 0x0000, 0x00008820, 361 ); //   coef read 361
	//snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x001b, 0x0000, 0x00000840, 365 ); //   coef read 365

	tdm_in_use(codec, 30);

//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR:
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x006b, 0x001f, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 370

	ret_coef71 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0071, 0x0000, 0x0000400f, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef read 374

	new_coef71 = (ret_coef71 & 0xffff);

	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0071, 0x400f, 0x00000000, 378 ); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 378
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0071, new_coef71, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 378

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, 0x7f0, 0x00b6); // AppleHDATDMBusManagerCS8409::configureTDMUR  write verb 381
}

void cs_8409_disable_TDM_amps12(struct hda_codec *codec)
{
	int retval;

	cs_8409_disable_TDM_proper_amps12(codec);

	// set to defaults and disable output
	// note here we really reset to 0 format in addition to stream id 0/channel id 0

	// note this means the cached stream data in the hda_cvt_setup struct will now be inconsistent
	// we need to ensure any further stream format re-update MUST be a forced update
	// still not clear if should be calling eg __snd_hda_codec_cleanup_stream

	snd_hda_codec_write(codec, 0x02, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x00270600
//      snd_hda:     conv stream channel map 2 [('CHAN', 0), ('STREAMID', 0)]

	snd_hda_codec_write(codec, 0x02, 0, AC_VERB_SET_STREAM_FORMAT, 0x00000000); // 0x00220000
//      snd_hda:     stream format 2 [('CHAN', 1), ('RATE', 48000), ('BITS', 8), ('RATE_MUL', 1), ('RATE_DIV', 1)]

	//retval = snd_hda_codec_read_check(codec, 0x24, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000040, 387); // 0x024f0700
	retval = snd_hda_codec_read(codec, 0x24, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000); // 0x024f0700
	snd_hda_codec_write(codec, 0x24, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000000); // 0x02470700
//      snd_hda:     36 []

}

void playstop_disable_TDM_amps12(struct hda_codec *codec)
{
	cs_8409_disable_TDM_amps12(codec);
}

void cs_8409_disable_amps34(struct hda_codec *codec)
{
	if (codec->core.subsystem_id == 0x106b3900) {
		cs_8409_disable_amp_max(codec, 0x74);
		cs_8409_disable_amp_max(codec, 0x72);
	} else if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600) {
		cs_8409_disable_amp_ssm3(codec, 0x2c);
		cs_8409_disable_amp_ssm3(codec, 0x2e);
	} else if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		cs_8409_disable_amp_tas576_amp2(codec);
		cs_8409_disable_amp_tas576_amp3(codec);
	} else {
		dev_info(hda_codec_dev(codec), "UNKNOWN subsystem id 0x%08x",codec->core.subsystem_id);
	}
}

void playstop_disable_amps34(struct hda_codec *codec)
{
	cs_8409_disable_amps34(codec);
}

static void cs_8409_disable_TDM_proper_amps34(struct hda_codec *codec)
{
	int ret_coef1 = 0;
	int new_coef1 = 0;

	// AppleHDATDMBusManagerCS8409::disableTDMPath
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x001b, 0x0000, 0x00000840, 0); //   coef read 694
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x001b, 0x8840, 0x00000000, 0); //   coef write 698
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x001c, 0x0000, 0x00000860, 0); //   coef read 702
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x001c, 0x8860, 0x00000000, 0); //   coef write 706

	// AppleHDATDMBusManagerCS8409::disableTDMPath
//      snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0082, 0x0001, 0xffff, 0x00005401, 0, 710 ); // coef write mask 710
	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0082, 0x0000, 0x5400, 0x00005401, 0x0001, 0); // coef write mask 710

	// AppleHDATDMBusManagerCS8409::disableTDMPath
	ret_coef1 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0001, 0x0000, 0x00000220, 0); //   coef read 716

	new_coef1 = (ret_coef1 & 0xffdf); // clear out 0x20 bit

	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0001, 0x0200, 0x00000000, 720 ); //   coef write 720
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0001, new_coef1, 0x00000000, 0); //   coef write 720

	// AppleHDATDMBusManagerCS8409::setupTDMPath or disableTDMPath calls AppleHDATDMBusManagerCS8409::configureTDMUR
	// AppleHDATDMBusManagerCS8409::configureTDMUR only place calls this
	// this is AppleHDATDMBusManagerCS8409::tdmInUse
	// which reads from 0x19 to 0x57 in a loop if the snd_hda_coef_item returns 0 till the read value
	// does not have the word sign bit set (ie 0x8000) or finish all 0x57

//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR: AppleHDATDMBusManagerCS8409::tdmInUse:
	//snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0019, 0x0000, 0x00008800, 724 ); //   coef read 724
	//snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x001a, 0x0000, 0x00008820, 728 ); //   coef read 728
	// ....
	//snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0056, 0x0000, 0x00008000, 968 ); //   coef read 968
	//snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0057, 0x0000, 0x00008000, 972 ); //   coef read 972

	tdm_in_use(codec, 40);

	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0071, 0x0000, 0x00000000, 0); //   coef write 977

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, 0x7f0, 0x00000000);

}

void cs_8409_disable_TDM_amps34(struct hda_codec *codec)
{
	int retval;

	cs_8409_disable_TDM_proper_amps34(codec);

	// set to defaults and disable output
	// note here we really reset to 0 format in addition to stream id 0/channel id 0

	// note this means the cached stream data in the hda_cvt_setup struct will now be inconsistent
	// we need to ensure any further stream format re-update MUST be a forced update
	// still not clear if should be calling eg __snd_hda_codec_cleanup_stream

	snd_hda_codec_write(codec, 0x03, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x00370600
//      snd_hda:     conv stream channel map 3 [('CHAN', 0), ('STREAMID', 0)]

	snd_hda_codec_write(codec, 0x03, 0, AC_VERB_SET_STREAM_FORMAT, 0x00000000); // 0x00320000
//      snd_hda:     stream format 3 [('CHAN', 1), ('RATE', 48000), ('BITS', 8), ('RATE_MUL', 1), ('RATE_DIV', 1)]

	//retval = snd_hda_codec_read_check(codec, 0x25, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000040, 986); // 0x025f0700
	retval = snd_hda_codec_read(codec, 0x25, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000); // 0x025f0700
	snd_hda_codec_write(codec, 0x25, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000000); // 0x02570700
//      snd_hda:     37 []

}

void playstop_disable_TDM_amps34(struct hda_codec *codec)
{
	cs_8409_disable_TDM_amps34(codec);
}

static void cs_8409_disable_amp_max(struct hda_codec *codec, int amp_address)
{

	// interrupt read/state read new as of June 2019

//      snd_hda: # i2cWrite:
//      snd_hda i2cWrite      i2c address 0x64 i2c            reg 0x5000 i2c data 0x0000   reg anal: GlobalEnable            : Disable
//      snd_hda i2cRead       i2c address 0x64 i2c            reg 0x0300 i2c data 0x0300   reg anal: InterruptState0
//      snd_hda i2cRead       i2c address 0x64 i2c            reg 0x0400 i2c data 0x0400   reg anal: InterruptState1
//      snd_hda i2cRead       i2c address 0x64 i2c            reg 0x0c00 i2c data 0x0c00   reg anal: State1

	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0050, 0x0000, 0); // snd_hda
	cs_8409_vendor_i2cRead(codec, amp_address, 0x0003, 0); // snd_hda
	cs_8409_vendor_i2cRead(codec, amp_address, 0x0004, 0); // snd_hda
	cs_8409_vendor_i2cRead(codec, amp_address, 0x000c, 0); // snd_hda

}

static void cs_8409_disable_amp_ssm3(struct hda_codec *codec, int amp_address)
{

//      snd_hda: # i2cWrite:
//      snd_hda i2cWrite      i2c address 0x2c i2c            reg 0x0001 i2c data 0x0001   reg anal: PowerControl            : PowerDown BVSenseOn

	cs_8409_vendor_i2cWrite(codec, amp_address, 0x0000, 0x0001, 1); // snd_hda

}

static void cs_8409_disable_amp_tas576_amp0(struct hda_codec *codec)
{

//      snd_hda: # i2cWrite:
//      snd_hda i2cRead       i2c address 0xd8 i2c            reg 0x0100 i2c data 0x01fc   reg anal: Power Control
//      snd_hda i2cWrite      i2c address 0xd8 i2c            reg 0x01fc i2c data 0x00fc   reg anal: Power Control           : Not sleep, Spkr Amp Shutdown

	cs_8409_vendor_i2cRead(codec, 0xd8, 0x0001, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, 0xd8, 0x0001, 0x00fc, 0); // snd_hda

}

static void cs_8409_disable_amp_tas576_amp1(struct hda_codec *codec)
{

//      snd_hda: # i2cWrite:
//      snd_hda i2cRead       i2c address 0xda i2c            reg 0x0100 i2c data 0x01fc   reg anal: Power Control
//      snd_hda i2cWrite      i2c address 0xda i2c            reg 0x01fc i2c data 0x00fc   reg anal: Power Control           : Not sleep, Spkr Amp Shutdown

	cs_8409_vendor_i2cRead(codec, 0xda, 0x0001, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, 0xda, 0x0001, 0x00fc, 0); // snd_hda

}

static void cs_8409_disable_amp_tas576_amp2(struct hda_codec *codec)
{

//      snd_hda: # i2cWrite:
//      snd_hda i2cRead       i2c address 0xdc i2c            reg 0x0100 i2c data 0x01fc   reg anal: Power Control
//      snd_hda i2cWrite      i2c address 0xdc i2c            reg 0x01fc i2c data 0x00fc   reg anal: Power Control           : Not sleep, Spkr Amp Shutdown

	cs_8409_vendor_i2cRead(codec, 0xdc, 0x0001, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, 0xdc, 0x0001, 0x00fc, 0); // snd_hda

}

static void cs_8409_disable_amp_tas576_amp3(struct hda_codec *codec)
{

//      snd_hda: # i2cWrite:
//      snd_hda i2cRead       i2c address 0xde i2c            reg 0x0100 i2c data 0x01fc   reg anal: Power Control
//      snd_hda i2cWrite      i2c address 0xde i2c            reg 0x01fc i2c data 0x00fc   reg anal: Power Control           : Not sleep, Spkr Amp Shutdown

	cs_8409_vendor_i2cRead(codec, 0xde, 0x0001, 0); // snd_hda
	cs_8409_vendor_i2cWrite(codec, 0xde, 0x0001, 0x00fc, 0); // snd_hda

}

