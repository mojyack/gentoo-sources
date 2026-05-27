// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Apple-specific support for the Cirrus Logic CS8409 HDA bridge chip used
 * in MacBookPro / iMac systems (T2-era). Drives the CS42L83 codec, headset
 * detection, and external MAX98706 / SSM3515 / TAS5764L amplifiers.
 *
 * Based on the out-of-tree davidjo/snd_hda_macbookpro driver.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <linux/mutex.h>
#include <linux/iopoll.h>

#include <linux/timer.h>
#include <linux/bitops.h>
#include <linux/version.h>

#include "cs8409.h"
#include "cirrus_apple.h"
#include "cirrus_apple_internal.h"

#define TESTING 0

#define mycodec_info(...)
#define mycodec_i2c_info(...)
#define mydev_info(...)
#define mycodec_dbg(...)
#define myprintk_dbg(...)
#define myprintk(...)

/*

     info for enum for checking initial assignment

enum {
	AC_JACK_LINE_OUT,                 0x0
	AC_JACK_SPEAKER,                  0x1
	AC_JACK_HP_OUT,                   0x2
	AC_JACK_CD,                       0x3
	AC_JACK_SPDIF_OUT,                0x4
	AC_JACK_DIG_OTHER_OUT,            0x5
	AC_JACK_MODEM_LINE_SIDE,          0x6
	AC_JACK_MODEM_HAND_SIDE,          0x7
	AC_JACK_LINE_IN,                  0x8
	AC_JACK_AUX,                      0x9
	AC_JACK_MIC_IN,                   0xa
	AC_JACK_TELEPHONY,                0xb
	AC_JACK_SPDIF_IN,                 0xc
	AC_JACK_DIG_OTHER_IN,             0xd
	AC_JACK_OTHER = 0xf,              0xf
};
 */

/*

	it appears from the cs42l42 definitions in cs8409.h that
	each nid is associated with a specific Audio Serial Port

	nids as used in Apple
	output
	speaker       0x002 -> 0x024   CS8409_PIN_ASP1_OUT_A -> CS8409_PIN_ASP1_TRANSMITTER_A
	speaker       0x003 -> 0x025   CS8409_PIN_ASP1_OUT_B -> CS8409_PIN_ASP1_TRANSMITTER_B
	headphones    0x00a -> 0x02c   CS8409_PIN_ASP2_OUT_A -> CS8409_PIN_ASP2_TRANSMITTER_A
	input
	headset mike  0x03c -> 0x01a   CS8409_PIN_ASP2_RECEIVER_A -> CS8409_PIN_ASP2_IN_A
	macbook pros
	internal mike 0x044 -> 0x022   CS8409_PIN_DMIC1_IN -> CS8409_PIN_DMIC1
	linein        0x045 -> 0x023   CS8409_PIN_DMIC2_IN -> CS8409_PIN_DMIC2
	imacs
	linein        0x044 -> 0x022   CS8409_PIN_DMIC1_IN -> CS8409_PIN_DMIC1
	internal mike 0x045 -> 0x023   CS8409_PIN_DMIC2_IN -> CS8409_PIN_DMIC2

 */

/* CS8409 register / NID defines moved to cirrus_apple_internal.h */

// add in coding based on Dell fixups

#define CS42L83_I2C_ADDR	0x90	// for some reason given as (0x48 << 1)
#define CS8409_CS42L83_RESET	0x02	// gpio interrupt mask - see cs42l83_external_control_GPIO
#define CS8409_CS42L83_INT	0x01	// gpio interrupt mask

// this is a list of i2c commands for init
static const struct cs8409_i2c_param cs42l83_init_reg_seq[] = {
	//{ 0x0000, 0x00 },
};

struct sub_codec cs8409_cs42l83_codec = {
	.addr = CS42L83_I2C_ADDR,
	.reset_gpio = CS8409_CS42L83_RESET,
	.irq_mask = CS8409_CS42L83_INT,
	.init_seq = cs42l83_init_reg_seq,
	.init_seq_num = ARRAY_SIZE(cs42l83_init_reg_seq),
	.hp_jack_in = 0,
	.mic_jack_in = 0,
	.linein_jack_in = 0,
	.paged = 1,
	.suspended = 1,
	.no_type_dect = 0,
};

// define the fixed stream format

const struct hda_pcm_stream cs42l83_apple_pcm_analog_playback = {
	.rates = SNDRV_PCM_RATE_44100, /* fixed rate */
};

const struct hda_pcm_stream cs42l83_apple_pcm_analog_capture = {
	.rates = SNDRV_PCM_RATE_44100, /* fixed rate */
};

/*

	register mask values collated from Microsoft cs4208_39.inf file plus the new Dell cs8409 module

	CS8409_DEV_CFG1,                // reg 0x00
	       0xb008     +PLL1/2_EN, +I2C_EN - .inf file
	       0xb000     +PLL1/2_EN          - .inf file
	       0x9008     +PLL1_EN, +I2C_EN   - .inf file
	       0x9000     +PLL1_EN            - .inf file
	CS8409_DEV_CFG2,                // reg 0x01
	       0x0006     ASP1/2_EN = 0, ASP1/2_STP = 1
	       0x0066     ASP1/2_EN = 1, ASP1/2_STP = 1
	       0x0022     ASP1_EN = 1, ASP1_STP = 1 ?
	       0x0044     ASP2_EN = 1, ASP2_STP = 1 ?
	       0x0200     seen in Apple but unknown
	CS8409_DEV_CFG3,                // reg 0x02
	       0x0a80     ASP1/2_BUS_IDLE=10, +GPIO_I2C
	       0x0280     seen in Apple ASP1_BUS_IDLE=10?, +GPIO_I2C
	CS8409_ASP1_CLK_CTRL1,          // reg 0x03
	       0x8000     ASP1: LCHI = 00h
	CS8409_ASP1_CLK_CTRL2,          // reg 0x04
	       0x28ff     ASP1: MC/SC_SRCSEL=PLL1, LCPR=FFh
	CS8409_ASP1_CLK_CTRL3,          // reg 0x05
	       0x0062     ASP1: MCEN=0, FSD=011, SCPOL_IN/OUT=0, SCDIV=1:4
	       0b0110 0b0010
			  so FSD = 0x0060
	       0x005a     ASP1: MCEN = 0, FSD = 010, SCPOL_IN/OUT = 1, SCDIV = 1:4
	       0b0101 0b1010
			  so FSD = 0x0040, SCPOL_IN/OUT = 0x0010 or 0x0008?, SCDIV = 0x0002
	       0x0001     seen in Apple but what bit is this?? MCEN??
	CS8409_ASP2_CLK_CTRL1,          // reg 0x06
	       0x8000     ASP1: LCHI = 00h
	CS8409_ASP2_CLK_CTRL2,          // reg 0x07
	       0x283f     ASP2: MC/SC_SRCSEL=PLL1, LCPR=3Fh
	CS8409_ASP2_CLK_CTRL3,          // reg 0x08
	       0x805c     ASP2: 5050=1, MCEN=0, FSD=010, SCPOL_IN/OUT=1, SCDIV=1:16
	       0b0101 0b1100
			  5050 = 0x8000?, FSD = 0x0040, SCPOL_IN/OUT = 0x0010 or 0x0008?, SCDIV=1:16 = 0x0004?
	CS8409_DMIC_CFG,                // reg 0x09
	       0x0023     DMIC1_MO=10b, DMIC1/2_SR=1
	       0x0033     seen in Apple
	       0x00b3     seen in Apple
	       0x01b3     seen in Apple
	CS8409_BEEP_CFG,                // reg 0x0a

	ASP2_Rx_NULL_INS_RMV,           // reg 0x11
	       0x0001     seen in Apple
	ASP2_Rx_RATE1,                  // reg 0x12
	       0xaccc     seen in Apple
	ASP2_Tx_NULL_INS_RMV,           // reg 0x14
	       0x0100     seen in Apple
	ASP2_Tx_RATE1,                  // reg 0x15
	       0xaaaa     seen in Apple
	ASP1_SYNC_CTRL,                 // reg 0x17
	       0x0000     seen in Apple sync converters
	       0x0001     seen in Apple sync converters
	       0x0002     seen in Apple sync converters
	       0x0003     seen in Apple sync converters
	ASP2_SYNC_CTRL,                 // reg 0x18
	       0x0000     seen in Apple sync converters

	the following registers are defined per output nid
	from A to H for  nid 0x02 to 0x09 ASP1
	from A to H for  nid 0x0a to 0x11 ASP2
	the following registers are defined per input nid
	from A to H for  nid 0x12 to 0x19 ASP1
	from A to H for  nid 0x1a to 0x21 ASP2

	ASP1_A_TX_CTRL1                 // reg 0x19
	       nide 0x02
	       0x0800     seen in Apple - on
	       0x8800     seen in Apple - off
	ASP1_A_TX_CTRL2                 // reg 0x1a
	       nide 0x02
	       0x0820     seen in Apple - on
	       0x8820     seen in Apple - off
	ASP1_B_TX_CTRL1,                // reg 0x1b
	       nide 0x03
	       0x0840     seen in Apple - on
	       0x8840     seen in Apple - off
	ASP1_B_TX_CTRL2,                // reg 0x1c
	       nide 0x03
	       0x0860     seen in Apple - on
	       0x8860     seen in Apple - off

	ASP2_A_TX_CTRL1,                // reg 0x29
	       nide 0x0a
	       0x0800     seen in Apple - on
	       0x8800     seen in Apple - off
	ASP2_A_TX_CTRL2,                // reg 0x2a
	       nide 0x0a
	       0x0820     seen in Apple - on
	       0x8820     seen in Apple - off

	ASP2_A_RX_CTRL1,                // reg 0x49
	       nide 0x1a
	       0x0800     seen in Apple - on
	       0x8800     seen in Apple - off
	ASP2_A_RX_CTRL2,                // reg 0x4a
	       nide 0x1a
	       0x0820     seen in Apple - on
	       0x8820     seen in Apple - off

	CS8409_ASP1_INTRN_STS,          // reg 0x6b
	       0x001f     seen in Apple
	CS8409_ASP2_INTRN_STS,          // reg 0x6c
	       0x001f     seen in Apple

	CS8409_ASP_UNS_RESP_MASK,       // reg 0x71
	       0x400f     seen in Apple
	       0x800f     seen in Apple
	       0xc00f     seen in Apple

	CS8409_PAD_CFG_SLW_RATE_CTRL    // reg 0x82
	       it appears to contain 2 sort of separate items - the ASP1 and ASP2 enables and the DMIC1/DMIC2 SCL enables
	       0xfc03     ASP1/2_xxx_EN=1, ASP1/2_MCLK_EN=0, DMIC1/2_SCL_EN=1 (was DMIC1_SCL_EN in comments but thinks thats wrong given below)
	       0xfc01     (ASP1/2_xxx_EN = 1, ASP1/2_MCLK_EN = 0, DMIC1_SCL_EN = 1)
	       0xff03     (ASP1/2_xxx_EN = 1, DMIC1/2_SCL_EN = 1)
	       0xfd02     (ASP1/2_xxx_EN = 1, ASP1_MCLK_EN = 1, ASP2_MCLK_EN = 0, DMIC2_SCL_EN = 1)
	       0xfe03     (ASP1/2_xxx_EN = 1, ASP1_MCLK_EN = 0, ASP2_MCLK_EN = 1, DMIC1/2_SCL_EN = 1)
			  so (ASP1_MCLK_EN is 0x0100 and ASP2_MCLK_EN is 0x0200)
	       from the OSX codes we seem to have
	       0xa800     ASP2_xxx_EN = 1, ASP1/2_MCLK_EN = 0
	       0x5400     ASP1_xxx_EN = 1, ASP1/2_MCLK_EN = 0

 */

/*

	Interrupt analysis (mainly from cs42l42 manual)

	actual button presses are 0x01, 0x02 and button release 0x10
	for 0x1b7c 0x02 is a short release for buttons, 0x08 is reserved
	the mask bits for 0x1b7a seem to be 0xe7 for button detect defining 0x18 as the button detect interrupt(s)
	and 0xdc for actual button interrupts
	(0x1b79 is mask, 0x1b7b status; 0x1b7a is mask, 0x1b7c is presumed status, 0x131b is mask, 0x1308 status,
	 0x1320 is mask, 0x130f status)

	0x1b79 Detect Interrupt Mask 1
	       0x80     M_HSBIAS_SENSE
	       0x40     M_TIP_SENSE_PLUG
	       0x20     M_TIP_SENSE_UNPLUG
	0x1b7b Detect Interrupt Status 1 - assumed - not documented in cs42l42 but listed in fig 4-45
	       0x80     M_HSBIAS_SENSE - assumed
	       0x40     M_TIP_SENSE_PLUG - assumed
	       0x20     M_TIP_SENSE_UNPLUG - assumed
	0x1b7a Detect Interrupt Mask 2 (Buttons)
	       cs42l42 values documented:
	       0x80     M_DETECT_TRUE_FALSE
	       0x40     M_DETECT_FALSE_TRUE
	       0x04     M_HSBIAS_HIZ
	       0x02     M_SHORT_RELEASE
	       0x01     M_SHORT_DETECTED
	       OSX button masks seen are 0xff, 0xe7 and 0xdc
			so for 0xe7 mask status bits are 0x18
			so for 0xdc mask status bits are 0x23
			bits 0x38 are not documented for cs42l42
	       0xe7     is used for button detection
	       0xdc     is used for button responses
	0x1b7c Detect Interrupt Status 2 (Buttons)
			- cs42l42 documents a 0x130a as Detect Interrupt Status 2
			  no such register ever used on OSX
			  assuming this is a difference for the cs42l83
	       0x80     M_DETECT_TRUE_FALSE
	       0x40     M_DETECT_FALSE_TRUE
	       0x04     M_HSBIAS_HIZ
	       0x02     M_SHORT_RELEASE
	       0x01     M_SHORT_DETECTED
	       OSX status values seen
	       0x0a     after 0xe7 mask set
	       0x40     pre configure button response mask
	       0x04     pre disable button interrupts
	0x1b78 Detect Status 2 - seems to be associated with buttons and/or mike
	       0x02     HS_TRUE - cs42l42
	       0x01     SHORT_TRUE - cs42l42
	       OSX values seen
	       0x40     button detect
	       0x02     mike sense
	       new values seen for imacs with non-Apple headsets
	       0x20     button detect??
	0x131b Codec Interrupt Mask
	       0x02     M_HSDET_AUTO_DONE
	       0x01     M_PDN_DONE
	0x1308 Codec Interrupt Status
	       0x02     M_HSDET_AUTO_DONE
	       0x01     M_PDN_DONE
	0x1320 Tip/Ring Sense Plug/Unplug Interrupt Mask
	       0x08     M_TS_UNPLUG
	       0x04     M_TS_PLUG
	       0x02     M_RS_UNPLUG
	       0x01     M_RS_PLUG
	0x130f Tip/Ring Sense Plug/Unplug Interrupt Status
	       0x08     M_TS_UNPLUG
	       0x04     M_TS_PLUG
	       0x02     M_RS_UNPLUG
	       0x01     M_RS_PLUG

	register 0x1b7b:

	TIP_SENSE_PLUG 0x40
	TIP_SENSE_UNPLUG 0x20

	BUTTON_DOWN_PRESS 0x1
	BUTTON_UP_PRESS 0x2
	BUTTON_RELEASE 0x10

	BUTTONS (BUTTON_UP_PRESS | BUTTON_DOWN_PRESS)

	register 0x1b7c:

	BUTTON_DETECT_MAIN 0x18  // we only see 0x08 but the mask allows for these 2 bits

	register 0x1b78:

	BUTTON_DETECT_MASK 0x60 // now seen 2 bits used - plausibly depends if Apple with buttons (0x40) or non-Apple with buttons (0x20)
	BUTTON_DETECT1 0x40
	BUTTON_DETECT2 0x20
	MIKE_CONNECT 0x02

	register 0x131b:

	HSDET_AUTO_DONE 0x02
	PDN_DONE 0x01

 */

/*
 */

// coding copied from hda_generic.c to print the nid path details

#define debug_badness(fmt, ...)                                         \
	mycodec_dbg(codec, fmt, ##__VA_ARGS__)

// attempt at an explicit setup ie not generic
//#include "patch_cirrus_explicit.h"

// definitions for patch_cirrus_apple.h

// this is a copy from playback_pcm_prepare in hda_generic.c
// initially I needed to do the Apple setup BEFORE the snd_hda_multi_out_analog_prepare
// in order to overwrite the Apple setup with the actual format/stream id
// NOTA BENE - if playback_pcm_prepare is changed in hda_generic.c then
// those changes must be re-implemented here
// we need this order because snd_hda_multi_out_analog_prepare writes the
// the format and stream id's to the audio nodes
//// so far we have left the Apple setup of the nodes format and stream id's in
// now updated to set the actual format where Apple does the format/stream id setup
// Apples format is very specifically S24_3LE (24 bit), 4 channel, 44.1 kHz
// S24_3LE seems to be very difficult to create so best Ive done is
// S24_LE (24 in 32 bits) or S32_LE
// it seems the digital setup is able to handle this with the Apple TDM
// setup but if we use the normal prepare hook order this overrwites
// the node linux 0x2, 0x3 setup with the Apple setup which leads to noise
// (the HDA specs say the node format setup must match the data)
// if we do the Apple setup and then the snd_hda_multi_out_analog_prepare
// the nodes will have the slightly different but working format
// with proper update of stream format at same point as in Apple log we need to pass
// the actual playback format as passed to this routine to our new "hook"
// cs_8409_pcm_playback_pre_prepare_hook
// to define the cached format correctly in that routine
// so far my analysis is that hinfo stores the stream format in the kernel format style
// but what is passed to cs_8409_playback_pcm_prepare is the format in HDA style
// not yet figured how to convert from kernel format style to HDA style

static int cs_8409_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				struct hda_codec *codec,
				unsigned int stream_tag,
				unsigned int format,
				struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;
	int err;

	cs_8409_pcm_playback_pre_prepare_hook(hinfo, codec, stream_tag, format, substream,
			       HDA_GEN_PCM_ACT_PREPARE);

	// now dont think we need this - we now explicitly copy the 1st 2 channels to 2nd 2 channels
	// if given 2 channel input
	// plus all headset output is also explicitly being done
	// well thats unexpected - if we comment this we loose headphone output
	//err = snd_hda_multi_out_analog_prepare(codec, &spec->multiout,
	err = 0;

	// we cant call directly as call_pcm_playback_hook is local to hda_generic.c
	//if (!err)
	//        call_pcm_playback_hook(hinfo, codec, substream,
	// but its a trivial function - at least for the moment!!
	if (err) {  }
	if (!err)
		if (spec->pcm_playback_hook)
			spec->pcm_playback_hook(hinfo, codec, substream, HDA_GEN_PCM_ACT_PREPARE);
	return err;
}

// this is a copy from capture_pcm_prepare in hda_generic.c
// NOTA BENE - if capture_pcm_prepare is changed in hda_generic.c then
// those changes must be re-implemented here
static int cs_8409_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
			       struct hda_codec *codec,
			       unsigned int stream_tag,
			       unsigned int format,
			       struct snd_pcm_substream *substream)
{
	struct cs8409_apple_spec *spec = codec->spec;

	cs_8409_pcm_capture_pre_prepare_hook(hinfo, codec, stream_tag, format, substream,
			      HDA_GEN_PCM_ACT_PREPARE);

	// we have a problem - this has to handle 2 different types of stream - the internal mike
	// and the external headset mike (cs42l83)

	// NOTE - the following snd_hda_codec_stream no longer do anything
	//        we have already set the stream data in the pre prepare hook
	//        - so as the format here is same (or at least should be!!) as that setup there is no format difference to that
	//        cached and snd_hda_coded_setup_stream does nothing

	if (hinfo->nid == spec->intmike_adc_nid) {

	// so this is getting stranger and stranger
	// the most valid recording is S24_3LE (0x4031) - except that the data we get out is S32_LE (low byte 0)
	// - so it doesnt play right - and it messes with arecords vumeter
	// (S32_LE is officially 0x4041 - but using that format doesnt seem to have valid data - audio very low)
	//// so now try forcing the format here to 0x4031
	//// well that fails miserably - the format mismatch stops data totally
	// it now appears we get the same data with either 0x4031 or 0x4041 - both are low volume
	// - however scaling (normalizing) in audacity we get the right sound with similar quality to OSX
	// so now think the low volume is right - and OSX must be scaling/processing the data in CoreAudio
	// (is the internal mike a fake 24 bits - ie its actually 16 bits but stuffed in the low end of the
	//  24 bits - hence low volume - preliminary scaling attempts in audacity suggest this might be true!!)

	snd_hda_codec_setup_stream(codec, hinfo->nid, stream_tag, 0, format);

	} else if (hinfo->nid == 0x1a) {

	// do we need a pre-prepare function??
	// maybe for this the external mike ie cs42l83 input

	snd_hda_codec_setup_stream(codec, hinfo->nid, stream_tag, 0, format);

	} else
		dev_info(hda_codec_dev(codec), "%s - UNIMPLEMENTED input nid 0x%x\n", __func__,hinfo->nid);

	// we cant call directly as call_pcm_capture_hook is local to hda_generic.c
	//call_pcm_capture_hook(hinfo, codec, substream,
	// but its a trivial function - at least for the moment!!
	// note this hook if defined also needs to switch between the 2 versions of input!!
	if (spec->gen.pcm_capture_hook)
		spec->gen.pcm_capture_hook(hinfo, codec, substream, HDA_GEN_PCM_ACT_PREPARE);

	return 0;
}

// quick debug callback list function

// this is a copy of local routine call_jack_callback from hda_jack.c

// so now think multi in the path is different from multiout
// - now think its about if there are multiple connections as listed by AC_VERB_GET_CONNECT_LIST
// - still havent figured out idx tho

// renamed this function as we dont want it called on resume
// because we are using an explicit version of build controls we can add it there

static int cs_8409_apple_boot_init(struct hda_codec *codec)
{
	struct hda_pcm *info = NULL;
	// originally made non-const for fixup attempts in old kernels - pre 5.13
	const struct hda_pcm_stream *hinfo = NULL;
	struct cs8409_apple_spec *spec = NULL;
	int pcmcnt = 0;

	// so apparently if we do not define a resume function
	// then this init function will be called on resume
	// is that what we want here??
	// NOTE this is called for either playback or capture

	// NOTE that this function is called after the build pcm functions

	// dump the rates/format of the afg node
	// so analog_playback_stream is still NULL here - maybe only defined when doing actual playback
	// the info stream is now defined
	spec = codec->spec;
	hinfo = spec->gen.stream_analog_playback;
	if (hinfo != NULL) {
	} else {
	}

	// think this is what I need to fixup

	list_for_each_entry(info, &codec->pcm_list_head, list) {
		int stream;

		for (stream = 0; stream < 2; stream++) {
			struct hda_pcm_stream *hinfo = &info->stream[stream];

			if (hinfo != NULL) {
			} else {
			}
		}
		pcmcnt++;
	}

	// update the streams specifically by nid
	// we seem to have only 1 stream here with the nid of 0x02
	// (I still dont really understand the linux generic coding here)
	// with capture devices we seem to get 2 pcm streams (0 and 1)
	// each pcm stream has an output stream (0) and an input stream (1)
	// the 1st pcm stream (0) is assigned nid 0x02 for output and nid 0x22 (macbook pro) for input (internal mike)
	// the 2nd pcm stream (1) has a dummy output stream and nid 0x1a for input (headset mike via cs42l83)
	// (NOTE this means the line input stream (0x45->0x23) (macbook pro) is not assigned currently ie not useable)

	list_for_each_entry(info, &codec->pcm_list_head, list) {
		int stream;

		for (stream = 0; stream < 2; stream++) {
			struct hda_pcm_stream *hinfo = &info->stream[stream];

			if (hinfo != NULL) {
				if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
					if (hinfo->nid == 0x02) {
						// so now we need to force the rates and formats to the single one Apple defines ie 44.1 kHz and S24_LE
						// probably can leave S32_LE
						// we can still handle 2/4 channel (what about 1 channel?)
						hinfo->rates = SNDRV_PCM_RATE_44100;
						hinfo->formats = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S24_LE;

						// update the playback function
						hinfo->ops.prepare = cs_8409_playback_pcm_prepare;
					}
				} else if (stream == SNDRV_PCM_STREAM_CAPTURE) {
					//if (hinfo->nid == 0x22)
					if (hinfo->nid == spec->intmike_adc_nid) {
						// this is the internal mike
						// this is a bit weird - the output nodes are id'ed by input pin nid
						// but the input nodes are done by the input (adc) nid - not the input pin nid
						// so now we could force the rates and formats to the single one Apple defines ie 44.1 kHz and S24_LE
						// but this internal mike seems to be a standard HDA input setup so we could have any format here
						hinfo->rates = SNDRV_PCM_RATE_44100;
						hinfo->formats = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_3LE;
						// update the capture function
						hinfo->ops.prepare = cs_8409_capture_pcm_prepare;
					} else if (hinfo->nid == 0x1a) {
						// this is the external mike ie headset mike
						// this is a bit weird - the output nodes are id'ed by input pin nid
						// but the input nodes are done by the input (adc) nid - not the input pin nid
						// so now we force the rates and formats to the single one Apple defines ie 44.1 kHz and S24_LE
						// - because this format is the one being returned by the cs42l83 which is setup by undocumented i2c commands
						hinfo->rates = SNDRV_PCM_RATE_44100;
						hinfo->formats = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_3LE;
						// update the capture function
						hinfo->ops.prepare = cs_8409_capture_pcm_prepare;
					}
					// still not sure what we do about the linein nid
					// is this bidirectional - because we have no lineout as far as I can see
				}
			} else {
			}
		}
		pcmcnt++;
	}

	//list_for_each_entry(kctl, &codec->card->controls, list) {

	return 0;
}

static int cs_8409_apple_init(struct hda_codec *codec)
{

	// not sure what the init function is supposed to be doing
	// its called in snd_hda_codec_build_controls
	// but also from the default resume function hda_call_codec_resume

	// so apparently if we do not define a resume function
	// then this init function will be called on resume
	// is that what we want here??
	// NOTE this is called for either playback or capture

	//if (spec->vendor_nid == CS420X_VENDOR_NID) {
	//	/* init_verb sequence for C0/C1/C2 errata*/
	//} else if (spec->vendor_nid == CS4208_VENDOR_NID) {

	//// so it looks as tho we have an issue when using headsets
	//// - because the 8409 is totally messed up it does not switch the inputs
	//// when a headset is plugged in
	//// not sure about this here - maybe move to where disable internal mike nodes
	//if (spec->jack_present) {

	// so the following powers on all active nodes - but if we have just plugged
	// in a headset thats still the internal mike and amps

	// commenting this as we dont need it
	// it powers on the nids, plus updates the mute/volume
	// via path_power_update and activate_amp_in or activate_amp_out

	// read UNSOL enable data to see what initial setup is

	//if (spec->gpio_mask) {
	//	snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_MASK,
	//	snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DIRECTION,
	//	snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DATA,

	//if (spec->vendor_nid == CS420X_VENDOR_NID) {

	return 0;
}

static int cs_8409_apple_resume(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;

	// code copied from default resume ops
	snd_hda_codec_init(codec);
	snd_hda_regmap_sync(codec);

	cs_8409_boot_setup(codec);

	spec->headset_phase = 2;

	spec->play_init = 0;
	spec->capture_init = 0;

	spec->play_init_count = 0;
	spec->capture_init_count = 0;

	// init the last play time
	ktime_get_real_ts64(&(spec->last_play_time));

	ktime_get_real_ts64(&(spec->first_play_time));

	return 0;
}

static int cs_8409_apple_suspend(struct hda_codec *codec)
{
	// no additional code for this so just dummy it
	return 0;
}

static int cs_8409_apple_check_power_status(struct hda_codec *codec, hda_nid_t nid)
{
	// no additional code for this so just dummy it
	return 0;
}

static int cs_8409_apple_build_controls(struct hda_codec *codec)
{
	int err;

	// try here - its mainly dumping the pcms which have been built by now
	cs_8409_apple_boot_init(codec);

	// so if we have paths (active??) then snd_hda_gen_build_controls will add the end point
	// (ie adc input nid or output nid (often dac but just pass through for 8409) as jacks
	// if the nid has no PIN_SENSE then these will be phantom jacks (which means treated as always plugged in)

	err = snd_hda_gen_build_controls(codec);
	if (err < 0)
		return err;
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_BUILD);

	// this seems better as snd_hda_gen_build_controls does an hda_exec_init_verbs call
	// before calling the init patch_ops function
	// - on the other hand currently it is just dumping data doesnt actually do anything

	// before or after the snd_hda_apply_fixup??

	return 0;
}

static int cs_8409_apple_build_pcms(struct hda_codec *codec)
{
	int retval;
	struct hda_pcm *pcm;

	// so I dont get how this leads to the observed controls in pulse/alsa
	// so far snd_hda_gen_build_pcms seems to add channel maps of stereo and 2.1
	// - cant figure out where the surround is generated from
	// (also cant see any way to disable the 2.1 without explicit recoding)

	// to replace this with explicit code would require a lot of static function copying
	// from hda_generic.c (and maybe hda_codec.c)
	// current issue is the default code will add a 2.1 chmap for 4 channels - I want to remove that
	retval =  snd_hda_gen_build_pcms(codec);

	list_for_each_entry(pcm, &codec->pcm_list_head, list) {
		if (pcm != NULL) {
			if (pcm->stream[SNDRV_PCM_STREAM_PLAYBACK].chmap != NULL) {
				const struct snd_pcm_chmap_elem *elem;
				elem = &pcm->stream[SNDRV_PCM_STREAM_PLAYBACK].chmap[0];
				elem = &pcm->stream[SNDRV_PCM_STREAM_PLAYBACK].chmap[1];
			} else {
			}
	//		if (pcm->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max == 4) {
		}
	}

	// we still dont have the pcm streams defined by here
	// ah this is all done in snd_hda_codec_build_pcms
	// which calls this patch routine or snd_hda_gen_build_pcms
	// but the query supported pcms is only done after this
	return retval;
}

// copy of cs8409_fix_caps with debug prints
// - rather than adding the prints to the cs8409.c routine

// set up some local routines we can call in our main code to call the hda functions we need

void cs_8409_cs42l83_mark_jack(struct hda_codec *codec)
{
	struct hda_jack_tbl *event;

	// this is how the Dell fixups do it
	// first explicitly get the jacktbl entry using the known jack nids
	// so can create the res data tag to pass to the standard snd_hda_jack_unsol_event
	// (minus actual GPIO etc data usually seen in the res)
	// - which then scans the jacktbl for the tag etc
	// but primarily marks the jack as dirty prior to snd_hda_jack_report_sync call
	// plus calls the jack callbacks

	event = snd_hda_jack_tbl_get_mst(codec, CS8409_CS42L83_HP_PIN_NID, 0);

	// note that the only real functions of snd_hda_jack_unsol_event are to call the jack callbacks
	// and snd_hda_jack_report_sync
	// for Apple we have disabled power on/power off callbacks (power_save_node = 0)
	// so now only have call_hp_automute and call_mic_autoswitch to figure out
	// - currently call_hp_automute is disabled because of suppress_auto_mute

	if (event) {
		// now perform the functions of snd_hda_jack_unsol_event explicitly
		// ignoring key_report_jack at the moment
		event->jack_dirty = 1;
	}

	// note that the Dell fixups do calls to snd_hda_jack_unsol_event for each jack nid
	// so we have multiple calls to snd_hda_jack_report_sync
	// so far looks as though this is OK as eventually snd_hda_jack_report_sync calls snd_kctl_jack_report
	// to update user side which first checks if stored data is same as updated data and if so does nothing

}

void cs_8409_cs42l83_jack_report_sync(struct hda_codec *codec)
{
	snd_hda_jack_report_sync(codec);
}

static void cs_8409_cs42l83_unsol_event_handler(struct hda_codec *codec, unsigned int unsol_res);

// so I think this is what gets called for any unsolicited event - including jack plug events
// so anything we do to switch amp/headphone should be done from here

static void cs_8409_cs42l83_jack_unsol_event(struct hda_codec *codec, unsigned int res)
{
	int tag = (res & AC_UNSOL_RES_TAG) >> AC_UNSOL_RES_TAG_SHIFT;

	// so we have confirmed that these unsol responses are not in linux kernel interrupt state
	//if (in_interrupt())
	//else

	//// read UNSOL enable data to see what current setup is

	// so it seems the low order byte of the res for the 8409 is a copy of the GPIO register state
	//// - except that we dont seem to pass this to the callback functions!! - well that was for linux kernel version 4

	codec_info(codec, "%s UNSOL 0x%08x tag 0x%02x\n", __func__,res,tag);

	// so now think we dont follow snd_hda_jack_unsol_event - that assumes multiple jacks (nids) from which we have
	// to find one from the tag - hence the use of the jacbktbl.used count for the tag
	// for Apple we have one tag because we have one jack and multiple nids
	// hda_generic implements a gated jack which maybe for handling the case of
	// 2 nids per jack (one for headphones and one for head mike)
	// (see path_realtek.c which seems to use that for hp nids with mic nids)
	// for the apple case even more problematic as we have more than 2 nids, one for headphone output, one for
	// headset input and one for linein input (currently have no idea how to handle lineout) all in same jack

	// so far it appears Apple enables speakers and disables intmike and linein with no headphone
	// intmike only enabled if perform capture
	// dont have any linein enable examples

	// now updating as per the Dell module - which looks up each jacktbl entry explicitly
	// not sure where to assign the jacktbl/event
	// standard snd_hda_jack_unsol_event does the lookup by tag here
	// Dell does it in the interrupt handler for the subcodec by reading/writing to the subcodec
	// it now seems we have only one jack nid to worry about so could find it here
	// currently going with doing it in cs_8409_cs42l83_mark_jack which we will call
	// from the appropriate place as called by the cs_8409_cs42l83_unsol_event_handler call chain

	// this likely was for kernel version 4 - all kernel 5 versions seem to pass res to call_jack_callback
	//// its the callback struct thats passed as an argument to the callback function
	//// so stuff the res data in the private_data member which seems to be used for such a purpose

	// leave this as is even tho so far have only 1 tag so not really needed
	// so could just call the callback routine directly here
	// now removing this - the main callbacks are power up, power down and mic_autoswitch
	// at the moment we are ignoring any power up/down calls - everything is permanently powered on
	// we still have to deal with mic_autoswitch
	// we could do this in the new cs_8409_cs42l83_mark_jack function

	cs_8409_cs42l83_unsol_event_handler(codec, res);

	// we seem to get a weird UNSOL interrupt during initialization - we want to skip the following
	if (((struct cs8409_spec*)(codec->spec))->headset_phase == 0)
	       return;

	// this is the code that generates the AC_VERB_GET_PIN_SENSE (0xf09) verb
	// however if we define the jack as a phantom_jack we do not send the AC_VERB_GET_PIN_SENSE (0xf09) verb

	// NOTA BENE - this is a very significant routine which notifies user space of the
	// jack plug/unplug event
	// however now think we need to move the actual call to when we know we have a jack
	// (as per the Dell code to after the cs42l83 headphone sense)
}

// Im pretty convinced that Apple uses a timed event from the plugin event
// before performing further setup
// not clear how to set this up in linux
// timer might be way to go but there are some limitations on the timer function
// which is not clear is going to work here
// now think just using msleeps is the way to go - this is similar to code in patch_realtek.c
// for dealing with similar issues

//static void cs_8409_hp_timer_callback(struct timer_list *tlist)

// have an explict one for 8409
// cs_free is just a definition
//#define cs_8409_apple_free		snd_hda_gen_free

static void cs_8409_apple_remove(struct hda_codec *codec)
{
	snd_hda_gen_remove(codec);
}

// note this must come after any function definitions used

static const struct hda_codec_ops cs_8409_apple_ops = {
	.build_controls = cs_8409_apple_build_controls,
	.build_pcms = cs_8409_apple_build_pcms,
	.init = cs_8409_apple_init,
	.remove = cs_8409_apple_remove,
	.unsol_event = cs_8409_cs42l83_jack_unsol_event,
#ifdef CONFIG_PM
	.resume = cs_8409_apple_resume,
	.suspend = cs_8409_apple_suspend,
	.check_power_status = cs_8409_apple_check_power_status,
#endif
};

//      jack handling analysis
//      now it appears that unsolicited events are assumed to be due to jack plug/unplug events
//      so the .unsol_event function is the primary handling function for this
//      and this does NOT appear to depend on PIN_SENSE capability
//      the PIN_SENSE capability seems to be ONLY needed for automute functionality
//      this includes enabling/disabling whether we see a Headphone entry in the settings sound dialog

static int cs_8409_apple_create_input_ctls(struct hda_codec *codec);

static int cs_8409_apple_parse_auto_config(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;
	int err;
	int i;

	err = snd_hda_parse_pin_defcfg(codec, &spec->gen.autocfg, NULL, 0);
	if (err < 0)
		return err;

	err = snd_hda_gen_parse_auto_config(codec, &spec->gen.autocfg);
	if (err < 0)
		return err;

	// note that create_input_ctls is called towards the end of snd_hda_gen_parse_auto_config

	// it appears the auto config assumes that inputs are connected to ADCs
	// (not true for outputs)

	// according to the Dell version the issue is the 8409 nids have no pin sense capabilities
	// and they get set to phantom jacks - the Dell version fixes the 8409 inputs/outputs connected to the cs42l42 companion codec
	// with PIN SENSE and DETECT capabilities (cs8409_fix_caps) then uses the cs8409_cs42l42_exec_verb replacement
	// for the primary spec->exec_verb function to update the returns from the AC_VERB_GET_PIN_SENSE verb for those
	// nids with actual state from the companion codec
	// note this has no bearing on the auto config - its for handling the unsol events properly

	// as of 5.13 the definition of AUTO_CFG_MAX_INS has been increased to handle the 8409
	// so the above may not be needed
	// (without the above no input pins were recognised at all)
	// need to check if fixed other input definitions in cs_8409_apple_create_input_ctls
	// and redo the updated input definitions here
	cs_8409_apple_create_input_ctls(codec);

	// so do I keep this or not??
	/* keep the ADCs powered up when it's dynamically switchable */
	if (spec->gen.dyn_adc_switch) {
		unsigned int done = 0;
		for (i = 0; i < spec->gen.input_mux.num_items; i++) {
			int idx = spec->gen.dyn_adc_idx[i];
			if (done & (1 << idx))
				continue;
			snd_hda_gen_fix_pin_power(codec,
						  spec->gen.adc_nids[idx]);
			done |= 1 << idx;
		}
	}

	return 0;
}

// this is only needed if kernel is < 5.13

/* we still need some fixups with the new version
   as it doesnt seem to setup the inputs correctly
 */

// the culprit function seems to be check_dyn_adc_switch in hda_generic.c
// - it should reduce the adc_nids list down to the 2 connected input adcs - but it does not
// - its current implementation appears to have a fundamental flaw
// - it assumes adcs are in a non-null list that ends in null
// but the 8409 has a random non-null element in an essentially null list

static int cs_8409_apple_create_input_ctls(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->input_mux;
	int i, n, nums;

	// determine for which input items we have a non-zero adc
	nums = 0;
	for (i = 0; i < imux->num_items; i++) {
		for (n = 0; n < spec->num_adc_nids; n++) {
			if (spec->input_paths[i][n]) nums++;
		}
	}

	// this code essentially taken from check_dyn_adc_switch
	// reduce the adc_nids list to connected items
	if (nums != spec->num_adc_nids) {
		/* shrink the invalid adcs and input paths */
		codec_dbg(codec, "hda_generic_check_dyn_adc_switch shrinking\n");
		nums = 0;
		for (i = 0; i < imux->num_items; i++) {
			for (n = 0; n < spec->num_adc_nids; n++) {
				if (spec->input_paths[i][n]) {
					struct nid_path *path = NULL;
					spec->adc_nids[nums] = spec->adc_nids[n];
					// this is explicit coding of simple function invalidate_nid_path from hda_generic.c
					path = snd_hda_get_path_from_idx(codec, spec->input_paths[i][nums]);
					if (path)
						memset(path, 0, sizeof(*path));
					spec->input_paths[i][nums] = spec->input_paths[i][n];
					spec->input_paths[i][n] = 0;
					nums++;
					// only store first non-zero path per adc
					break;
				}
			}
		}
		spec->num_adc_nids = nums;
	}

	for (n=0; n < spec->num_adc_nids; n++) {
	}

	return 0;
}

/* do I need this for 8409 - I certainly need some gpio patching */

// this is from a previous 8409 fixup - remove when see what need to be replaced by

#ifdef APPLE_FIXUPS

/* CS8409 */
enum {
       CS8409_MBP131,
       CS8409_APPLE_GPIO_0,
       CS8409_MBP143,
       CS8409_APPLE_GPIO,
};

static const struct hda_model_fixup cs8409_apple_models[] = {
       { .id = CS8409_MBP131, .name = "mbp131" },
       { .id = CS8409_MBP143, .name = "mbp143" },
       {}
};

static const struct snd_pci_quirk cs8409_apple_fixup_tbl[] = {
       SND_PCI_QUIRK(0x106b, 0x3300, "MacBookPro 13,1", CS8409_MBP131),
       //SND_PCI_QUIRK(0x106b, 0x3600, "MacBookPro 14,2", CS8409_MBP143),
       SND_PCI_QUIRK(0x106b, 0x3900, "MacBookPro 14,3", CS8409_MBP143),
       //SND_PCI_QUIRK(0x106b, 0x0f00, "Imac 18,2", CS8409_MBP143),
       //SND_PCI_QUIRK(0x106b, 0x1000, "Imac 18,3", CS8409_MBP143),
       //SND_PCI_QUIRK(0x106b, 0x1000, "Imac 19,1", CS8409_MBP143),
       {} /* terminator */
};

static const struct hda_pintbl mbp131_pincfgs[] = {
       {} /* terminator */
};

static const struct hda_pintbl mbp143_pincfgs[] = {
       {} /* terminator */
};

static const struct hda_fixup cs8409_apple_fixups[] = {
       [CS8409_MBP131] = {
	       .type = HDA_FIXUP_PINS,
	       .v.pins = mbp131_pincfgs,
	       .chained = true,
	       .chain_id = CS8409_APPLE_GPIO_0,
       },
       [CS8409_APPLE_GPIO_0] = {
	       .type = HDA_FIXUP_FUNC,
	       .v.func = cs_8409_apple_fixup_gpio,
       },
       [CS8409_MBP143] = {
	       .type = HDA_FIXUP_PINS,
	       .v.pins = mbp143_pincfgs,
	       .chained = true,
	       .chain_id = CS8409_APPLE_GPIO,
       },
       [CS8409_APPLE_GPIO] = {
	       .type = HDA_FIXUP_FUNC,
	       .v.func = cs_8409_apple_fixup_gpio,
       },
};
#endif

// as per Apple we need to fix the pin config for the linein nid (it defaults as no conn, Line Out which messes the auto config)
// (assuming this swaps as nid swaps for macbook pro/imac)
// except define device as line in not mic in as per Apple, remove the 0x00000100 which is apparently AC_DEFCFG_MISC_NO_PRESENCE
// plus make it a jack like the other jack inputs (so 0x00800001 not 0x90a00101 as per Apple)

static const struct hda_pintbl macbook_pro_pincfgs[] = {
	{ 0x45, 0x00800101 },
	{ }
};
static const struct hda_pintbl imac_pincfgs[] = {
	{ 0x44, 0x00800101 },
	{ }
};

static void cs_8409_cs42l83_unsol_event_handler(struct hda_codec *codec, unsigned int unsol_res)
{

	// print the stored unsol res which seems to be the GPIO pins state

	cs_8409_cs42l83_unsolicited_response(codec, unsol_res);

	// now think timers not the way to go
	// patch_realtek.c has to deal with similar issues of plugin, headset detection
	// and just uses msleep calls

	// the delayed_work feature might be a way to go tho

}

// we have 4 automute hooks

static void cs_8409_automute(struct hda_codec *codec)
{
	dev_info(hda_codec_dev(codec), "%s called\n", __func__);
}

// for Apple we need multiple versions because so far macbook pro and imacs use different nids

static int cs8409_cs42l83_exec_verb(struct hdac_device *dev, unsigned int cmd, unsigned int flags,
				    unsigned int *res)
{
	struct hda_codec *codec = container_of(dev, struct hda_codec, core);
	struct cs8409_apple_spec *spec = codec->spec;

	unsigned int nid = ((cmd >> 20) & 0x07f);
	unsigned int verb = ((cmd >> 8) & 0x0fff);

	// we have confirmed this is being called

	if (nid == CS8409_CS42L83_HP_PIN_NID) {
		if (verb == AC_VERB_GET_PIN_SENSE) {
			// initially use my jack_present flag integer
			*res = (spec->jack_present) ? AC_PINSENSE_PRESENCE : 0;
			return 0;
		}
	} else if (nid == CS8409_CS42L83_HP_MIC_PIN_NID) {
		if (verb == AC_VERB_GET_PIN_SENSE) {
			// initially use my jack_present flag integer
			*res = (spec->jack_present) ? AC_PINSENSE_PRESENCE : 0;
			return 0;
		}
	} else if (nid == spec->linein_nid) {
		if (verb == AC_VERB_GET_PIN_SENSE) {
			// initially use my jack_present flag integer
			*res = (spec->jack_present) ? AC_PINSENSE_PRESENCE : 0;
			return 0;
		}
	}

	return spec->exec_verb(dev, cmd, flags, res);
}

static struct cs8409_apple_spec *cs8409_apple_alloc_spec(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return NULL;
	codec->spec = spec;
	spec->codec = codec;

	// this allows power control per nid - for Apple ignoring all power control for now
	// this removes eg power up/down on jack plugin/unplug
	// looks like we need an explicit set - 1 maybe default
	codec->power_save_node = 0;
	snd_hda_gen_spec_init(&spec->gen);

	return spec;
}

int cs8409_apple(struct hda_codec *codec)
{
	struct hda_codec_driver *driver;
	struct cs8409_apple_spec *spec;
	int err;
	int itm;

	int explicit = 0;

	/* Reached only via the CS8409_MBP fixup match, so the machine is already
	 * known to be a supported Apple model (see cs8409_fixup_tbl).
	 */
	spec = cs8409_apple_alloc_spec(codec);
	if (!spec)
		return -ENOMEM;

	spec->vendor_nid = CS8409_VENDOR_NID;

	spec->beep_nid = CS8409_BEEP_NID;

	spec->use_data = 0;

	// the following coding is the HDA_FIXUP_ACT_PRE_PROBE phase

	//issue init verbs here

	// now going with the Dell way of handling PIN_SENSE for jack plug/unplug events so need this

	spec->exec_verb = codec->core.exec_verb;
	codec->core.exec_verb = cs8409_cs42l83_exec_verb;
/*
	if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00)
	{
		codec->core.exec_verb = cs8409_cs42l83_imac_exec_verb;
	}
	else:
	{
		codec->core.exec_verb = cs8409_cs42l83_macbook_exec_verb;
	}
 */

	spec->scodecs[CS8409_CODEC0] = &cs8409_cs42l83_codec;
	spec->num_scodecs = 1;
	spec->scodecs[CS8409_CODEC0]->codec = codec;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)

	driver = hda_codec_to_driver(codec);

	if (explicit) {
	       }
	else
	       driver->ops = &cs_8409_apple_ops;

#else

	if (explicit) {
	       }
	else
	       codec->patch_ops = cs_8409_apple_ops;

#endif

	// not sure about these
	// the suppress_vmaster is likely reasonable as the Apple way has no dynamic volume controls on either the 8409 chip
	// or the amp/cs42l83 chips - the volume is set on boot to a fixed level and never changed
	spec->gen.suppress_auto_mute = 1;
	spec->gen.no_primary_hp = 1;
	spec->gen.suppress_vmaster = 1;

	// we probably also want to suppress auto mic
	spec->gen.suppress_auto_mic = 1;

	// dell GPIO pins are : /* GPIO 5 out, 3,4 in */
	// what is true is the cs8409 interrupt GPIO is pin 1
	// aah - the reset GPIO pin is 0x02
	spec->gpio_dir = spec->scodecs[CS8409_CODEC0]->reset_gpio;
	spec->gpio_data = 0;
	if (codec->core.subsystem_id == 0x106b3900) {
		spec->gpio_mask = 0x07;
	} else if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600) {
		spec->gpio_mask = 0x0f;
	} else if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		spec->gpio_mask = 0x0f;
	} else {
		dev_info(hda_codec_dev(codec), "UNKNOWN subsystem id 0x%08x",codec->core.subsystem_id);
	}

	// dell init : /* Basic initial sequence for specific hw configuration */

	//snd_array_for_each(&codec->init_pins, i, pin) {
	//snd_array_for_each(&codec->driver_pins, i, pin) {

	//for (i = 0; i < codec->core.num_nodes; i++)

	// as per Apple we need to fix the pin config for the linein nid (it defaults as no conn, Line Out which messes the auto config)
	// (assuming this swaps as nid swaps for macbook pro/imac)
	// except define device as line in not mic in as per Apple plus make it a jack like
	// the other jack inputs (so 0x00800101 not 0x90a00101 as per Apple)

	//static const struct hda_pintbl macbook_pro_pincfgs[] = {
	//        { 0x45, 0x00800101 },
	//        { }
	//static const struct hda_pintbl imac_pincfgs[] = {
	//        { 0x44, 0x00800101 },
	//        { }

	// cs8409_fix_caps overrides the jack nid to AC_PINCAP_IMP_SENSE and AC_PINCAP_PRES_DETECT parameters
	// and adds AC_WCAP_UNSOL_CAP to the wcaps
	// - not sure we should do this for AC_WCAP_UNSOL_CAP for Apple
	// - but that depends on how we implement features
	// currently we re-write snd_hda_jack_detect_enable_callback_mst and remove the AC_VERB_SET_UNSOLICITED_ENABLE
	// setup (because we get unsol responses by default)
	// but new idea is could fixup the exec_verb replacement and skip any AC_VERB_SET_UNSOLICITED_ENABLE
	// commands received

#if 1
	// we have one jack but 2 nids to setup - one for output and one for input
	// the headphone output/input is same for both imac and macbook pros
	cs8409_fix_caps(codec, CS8409_CS42L83_HP_PIN_NID);
	cs8409_fix_caps(codec, CS8409_CS42L83_HP_MIC_PIN_NID);

	// we also need to setup the linein nid (why is there no lineout??)
	// (note that the internal mike is permanent - on the other hand we do need to turn it off if headset with mike
	//  is plugged in???)
	if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		cs8409_fix_caps(codec, CS8409_CS42L83_IMAC_LINEIN_PIN_NID);
	} else {
		cs8409_fix_caps(codec, CS8409_CS42L83_MACBOOK_LINEIN_PIN_NID);
	}
#endif

	//for (i = 0; i < codec->core.num_nodes; i++)

	//if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
	//else

	// note that the auto config doesnt issue any PIN_SENSE commands but it does use the PINCAP data
	// check_auto_mute_availability uses the PINCAP to determine if a nid can have a jack
	// via is_jack_detectable - however because suppress_automute is set to 1
	// check_auto_mute_availability exits before calling this function
	// maybe because check_auto_mic_availability is also called which also calls is_jack_detectable
	// and is not suppressed
	// so from this we can gather that all auto_mute/automute functionality must be handled explicitly
	// (and not from eg snd_hda_gen_hp_automute/snd_hda_gen_line_automute)
	// whereas auto_mic functionality is handled by the generic functions eg snd_hda_gen_mic_autoswitch

	// Analysis of jack reporting so far:
	// the default coding for generic jack reporting seems to be as follows:
	// the default snd_hda_jack_unsol_event sets jack_dirty to 1 for the jack entry for the nid
	// connected to the jack
	// calls any callbacks for jack events (as set by snd_hda_jack_detect_enable_callback_mst)
	// and then calls snd_hda_jack_report_sync
	// snd_hda_jack_report_sync is the critically important function
	// this function calls jack_detect_update which uses an AC_VERB_GET_PIN_SENSE verb to detect
	// current jack status then unsets jack_dirty to 0
	// then for all jacks the current pin sense state AND button state is reported to the user level
	// via snd_jack_report (a function of jack.c in core directory)
	// which updates user level with a snd_kctl_jack_report - a function of ctljack.c in core
	// which issues a snd_ctl_notify using SNDRV_CTL_EVENT_MASK_VALUE and the previously prepared
	// pin sense state AND button state

	// tip sense setups done here

	// moved to post auto config

	// use this to cause unsolicited responses to be stored
	// but not run
	spec->block_unsol = 0;

	INIT_LIST_HEAD(&spec->unsol_list);

	for (itm=0; itm<10; itm++) { spec->unsol_items_prealloc_used[itm] = 0; }

	// for the moment set initial jack status to not present
	// we will detect if have jack plugged in on boot later
	spec->jack_present = 0;

	spec->headset_type = 0;

	spec->have_mike = 0;

	spec->have_buttons = 0;

	spec->playing = 0;
	spec->capturing = 0;

	spec->headset_play_format_setup_needed = 1;
	spec->headset_capture_format_setup_needed = 1;

	spec->headset_presetup_done = 0;

	// use this to distinguish which unsolicited phase we are in
	// for the moment - we only seem to get a tag of 0x37 and dont see any
	// different tags being setup in OSX logs
	spec->headset_phase = 0;

	spec->headset_enable = 0;

	// setup the intmike and linein nids
	// these are swapped between macbook pros and imacs
	if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		spec->intmike_nid = 0x45;
		spec->intmike_adc_nid = 0x23;
		spec->linein_nid = 0x44;
		spec->linein_amp_nid = 0x22;
	} else {
		spec->intmike_nid = 0x44;
		spec->intmike_adc_nid = 0x22;
		spec->linein_nid = 0x45;
		spec->linein_amp_nid = 0x23;
	}

	// ASP is Audio Serial Port
	// DMIC is Digital Microphone Input
	//
	// so it appears that while the intmike/linein nid functions are swapped between macbook pro and imacs
	// the digital mike paths associated with the nids are fixed - which means the digital mike path
	// swaps between macbooks and imac
	// ie nid 0x44 -> 0x22 path 0x82 : 0x0001 ie DMIC1
	// ie nid 0x45 -> 0x23 path 0x82 : 0x0002 ie DMIC2

	// current status 0x82 reg:
	// macbook pro and imac use 0x5400 for speaker amps - ASP1 (nid indpendent??)
	// macbook pro and imac use 0xa800 for headset  amp - ASP2 (nid indpendent??)
	// macbook pro          use 0x0001 for internal mike? - DMIC1 (nid dependent)
	// macbook imac         use 0x0002 for internal mike? - DMIC2 (nid dependent)
	// macbook pro          use 0x0002 for linein? - DMIC2 (nid dependent)
	// macbook imac         use 0x0001 for linein? - DMIC1 (nid dependent)

	// headphone/headset output path
	// nid 0x0a -> 0x2c

	// headset mike path
	// nid 0x3c -> 0x1a

	// combining data from cs4208_...inf with new 8409 updates in patch_cirrus for DEL laptops
	// it seems reg 0x0009 is associated with the digital mikes
	// vendor reg 0x0009 - 0x0023 : DMIC1_MO=10b, DMIC1/2_SR=1
	//                   - 0x0003 : DMIC1_MO=00b, DMIC1/2_SR=1
	//                   - 0x0043 : DMIC2_MO=01b, DMIC1/2_SR=1
	//                   - 0x0083 : DMIC2_MO=10b, DMIC1/2_SR=1 - this is my guess
	// note that Apple also sets bit 0x0010 which so far is undocumented as is not consistent with above data
	//                   - 0x0033 : DMIC1_MO=11b, DMIC1/2_SR=1
	//                   - 0x0093 : DMIC2_MO=10b, DMIC1_MO=01b?, DMIC1/2_SR=1

	// it appears we need to swap a number of vendor node register fields because of the above path swap
	// vendor reg 0x0009 - associated with internal mike from macbook pros
	//      OSX function setConnectionSelect
	//      macbook pro init        0x0013 -> 0x0033 ie 0x0020 bit set
	//             imac init        0x0013 -> 0x0093 ie 0x0080 bit set
	if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		spec->reg9_intmike_dmic_mo = 0x0080;    // DMIC2_MO=10b
	} else {
		spec->reg9_intmike_dmic_mo = 0x0020;    // DMIC1_MO=10b
	}

	// vendor reg 0x0082 - this is a very complex reg
	//                   - it appears to contain 2 sort of separate items - the ASP1 and ASP2 enables and the DMIC1/DMIC2 SCL enables
	//                   - 0xfc03 : ASP1/2_xxx_EN=1, ASP1/2_MCLK_EN=0, DMIC1/2_SCL_EN=1 (was DMIC1_SCL_EN in comments but thinks thats wrong given below)
	//                   - 0xfc01 : (ASP1/2_xxx_EN = 1, ASP1/2_MCLK_EN = 0, DMIC1_SCL_EN = 1)
	//                   - 0xff03 : (ASP1/2_xxx_EN = 1, DMIC1/2_SCL_EN = 1)
	//                   - 0xfd02 : (ASP1/2_xxx_EN = 1, ASP2_MCLK_EN = 0, DMIC2_SCL_EN = 1)
	//                   - 0xfe03 : (ASP1/2_xxx_EN = 1, ASP1_MCLK_EN = 0, DMIC1/2_SCL_EN = 1)
	//                   - so (ASP1_MCLK_EN is 0x0100 and ASP2_MCLK_EN is 0x0200)
	//                   - from the OSX codes we seem to have
	//                   - 0xa800 : ASP2_xxx_EN = 1, ASP1/2_MCLK_EN = 0
	//                   - 0x5400 : ASP1_xxx_EN = 1, ASP1/2_MCLK_EN = 0

	// vendor reg 0x0082 - we need to update the DMIC bit fields but not the ASP bit fields for macbook/imac switch
	if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		spec->reg82_intmike_dmic_scl = 0x0002;   // DMIC2_SCL_EN
		spec->reg82_linein_dmic_scl = 0x0001;    // DMIC1_SCL_EN
	} else {
		spec->reg82_intmike_dmic_scl = 0x0001;   // DMIC1_SCL_EN
		spec->reg82_linein_dmic_scl = 0x0002;    // DMIC2_SCL_EN
	}

	// the jack detect enable callback can be either in PRE_PROBE or PROBE phase

	// so it appears we dont get interrupts in the auto config stage

       if (!explicit) {

	      err = cs_8409_apple_parse_auto_config(codec);
	      if (err < 0)
		      goto error;
       }

       // dump the rates/format of the afg node
       // still havent figured out how the user space gets the allowed formats
       // ah - may have figured this
       // except that at this point this is NULL - we need to be after build pcms
       //if (info != NULL)
       //       if (hinfo != NULL)
       //       else
       //else

	// the following code would be the HDA_FIXUP_ACT_PROBE phase

	// new idea to try and use the gated/gating jack function as we have only one jack
	// and this seems to be what gating/gated jacks seem to be designed for

	// this is how to fix the format for the streams
	// dell : /* Fix Sample Rate to 48kHz */

	spec->gen.pcm_playback_hook = cs_8409_playback_pcm_hook;

	spec->gen.pcm_capture_hook = cs_8409_capture_pcm_hook;

	spec->gen.automute_hook = cs_8409_automute;

	// so this seems to be how we setup volume controls for the headphones
	// - but this sets up controls on the 8409 to directly update the cs42l42
	// - this is not how apple does things - all volume controls must occur
	// prior to this driver
	// the problem is this means we need some pulse/alsa configuration to setup
	// such controls

	// del : /* Set initial DMIC volume to -26 dB */
	//snd_hda_codec_amp_init_stereo(codec, CS8409_CS42L42_DMIC_ADC_PIN_NID,
	//snd_hda_gen_add_kctl(&spec->gen, "Headphone Playback Volume",
	//snd_hda_gen_add_kctl(&spec->gen, "Mic Capture Volume",

	// del : /* Disable Unsolicited Response during boot */

	snd_hda_codec_set_name(codec, "CS8409/CS42L83");

       err = cs_8409_boot_setup(codec);
       if (err < 0)
	       goto error;

       // update the headset phase
       spec->headset_phase = 2;

       spec->play_init = 0;
       spec->capture_init = 0;

       spec->play_init_count = 0;
       spec->capture_init_count = 0;

       // init the last play time
       ktime_get_real_ts64(&(spec->last_play_time));

       ktime_get_real_ts64(&(spec->first_play_time));

       return 0;

 error:
       cs_8409_apple_remove(codec);
       return err;
}

// for the moment split the new code into an include file

/* ---- begin inlined patch_cirrus_new84.h ---- */

// definitions for patch_cirrus_new84.h

// this sets the power state of the AFG node - ie node 0x1
// this calls hda_sync_power_state

// this checks the node has reached the requested power state
//

// pigs need local definition as this is a static local function

/*
 * wait until the state is reached, returns the current state
 */
unsigned int hda_sync_power_state_8409(struct hda_codec *codec,
					 hda_nid_t nid,
					 unsigned int power_state)
{
	unsigned long end_time = jiffies + msecs_to_jiffies(500);
	unsigned int state, actual_state;

	for (;;) {
		state = snd_hda_codec_read(codec, nid, 0,
					   AC_VERB_GET_POWER_STATE, 0);
		if (state & AC_PWRST_ERROR)
			break;
		actual_state = (state >> 4) & 0x0f;
		if (actual_state == power_state)
			break;
		if (time_after_eq(jiffies, end_time))
			break;
		/* wait until the codec reachs to the target state */
		usleep_range(1000, 2000);
	}
	return state;
}

// pigs - need my own power state
// Apple seems to set node 0x01 - the AFG - primarily
// hda_set_power_state sets all nodes to the required power state
// so apparently node 0x01 does not have the power capability - but is powerable!!
// if we wish to use this for all nodes then need to check for this

unsigned int hda_set_node_power_state_dbg(struct hda_codec *codec, hda_nid_t nid, unsigned int power_state, bool dbgflg)
{
	unsigned int wcaps = get_wcaps(codec, nid);
	unsigned int state = power_state;
	if (dbgflg) {  }
	state = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_POWER_STATE, 0);
	if (!(state & AC_PWRST_ERROR)) {
		if (state != power_state) {
			if (nid == 0x01 || (wcaps & AC_WCAP_POWER)) {
				if (nid != 0x01 && codec->power_filter) {
					state = codec->power_filter(codec, nid, power_state);
					// ah - this is for preventing a node from being turned off
					// we are not in AC_PWRST_D3 but we are requesting AC_PWRST_D3
					// (Im assuming we assume if not in AC_PWRST_D3 we are in AC_PWRST_D0
					if (state != power_state && power_state == AC_PWRST_D3) {
					} else
						snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_POWER_STATE, power_state);
				} else
					snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_POWER_STATE, power_state);

				state = hda_sync_power_state_8409(codec, nid, power_state);
			} else
				dev_info(hda_codec_dev(codec), "hda_set_node_power_state no power cap!!\n");
		}
	} else {
		dev_info(hda_codec_dev(codec), "hda_set_node_power_state ERROR!! nid 0x%02x 0x%04x\n",nid, state);
	}
	if (dbgflg) {  }

	return state;
}

unsigned int hda_set_node_power_state(struct hda_codec *codec, hda_nid_t nid, unsigned int power_state)
{
	return hda_set_node_power_state_dbg(codec, nid, power_state, 0);
}

void hda_check_power_state(struct hda_codec *codec, hda_nid_t nid, int flagint)
{
	unsigned int state;
	state = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_POWER_STATE, 0);
}

// go with Apple way??
// this always does a get with index 0 initially and terminates with a set to 0 finally

static const struct hda_verb cs8409_init_verbs[] = {
	//{0x01, AC_VERB_SET_POWER_STATE, 0x00}, /* AFG: D0 */
	//{0x24, AC_VERB_SET_PROC_STATE, 0x01},  /* VPW: processing on */
	{} /* terminator */
};

struct hda_coef {
	u16 write;
	hda_nid_t nid;
	u32 idx;
	u32 param;
	u32 retdata;
	int srcidx;
};

// new feature to do a sequence of coef read/writes
// (seems to be used a lot for cs8409)
// note that we ignore the return for gets for the moment!!
// ooh - new idea - save the logged return and check
static const struct hda_coef cs8409_init_coef[] = {
	//{0, 0x01, idx, 0x00, retdata, 0}, read
	//{1, 0x01, idx, param, dmydata, 0}, write
	//{2, 0x01, idx, param, retdata, 0}, write mask
};

// this is very hacky but until get more understanding of what we can do with the 8409 setup
// re-define these from hda_codec.c here
// NOTA BENE - need to check this is consistent with any hda_codec.c updates!!

#ifdef USE_DATA

#include "patch_cirrus_data84.h"

#include "patch_cirrus_plugin.h"

#include "patch_cirrus_headplay.h"

#include "patch_cirrus_unplug.h"

#include "patch_cirrus_plugin3.h"

#include "patch_cirrus_plugin23.h"

#include "patch_cirrus_mb141_data84.h"

#else

// error definitions
void cs_8409_external_device_unsolicited_response_data(struct hda_codec *codec, unsigned int res)
{
	dev_err(hda_codec_dev(codec), "ERROR - to use data functions need to define USE_DATA\n");
}

static void cs_8409_boot_setup_data(struct hda_codec *codec)
{
	dev_err(hda_codec_dev(codec), "ERROR - to use data functions need to define USE_DATA\n");
}

void cs_8409_play_data(struct hda_codec *codec)
{
	dev_err(hda_codec_dev(codec), "ERROR - to use data functions need to define USE_DATA\n");
}

void cs_8409_playstop_data(struct hda_codec *codec)
{
	dev_err(hda_codec_dev(codec), "ERROR - to use data functions need to define USE_DATA\n");
}

void cs_8409_headplay_data(struct hda_codec *codec)
{
	dev_err(hda_codec_dev(codec), "ERROR - to use data functions need to define USE_DATA\n");
}

void cs_8409_headplaystop_data(struct hda_codec *codec)
{
	dev_err(hda_codec_dev(codec), "ERROR - to use data functions need to define USE_DATA\n");
}

void cs_8409_boot_setup_data_ssm3(struct hda_codec *codec)
{
	dev_err(hda_codec_dev(codec), "ERROR - to use data functions need to define USE_DATA\n");
}

void cs_8409_play_data_ssm3(struct hda_codec *codec)
{
	dev_err(hda_codec_dev(codec), "ERROR - to use data functions need to define USE_DATA\n");
}

#endif

// macbook pro subsystem ids
// 14,1 0x106b3300
// 14,2 0x106b3600
// 14,3 0x106b3900

// imac subsystem ids
// 18,1 0x106b0e00
// 18,2 0x106b0f00
// 18,3 0x106b1000
// 19,1 0x106b1000

// this version runs all explicit commands as logged on OSX
int cs_8409_data_config(struct hda_codec *codec)
{

	unsigned int tmpstate1 = -1;
	unsigned int tmpstate2 = -1;
	unsigned int tmpstate3 = -1;
	unsigned int tmpstate4 = -1;

	cs_8409_boot_setup_data(codec);

	// check what power state of these nodes is - Apple does not do this
	tmpstate1 = hda_sync_power_state_8409(codec, 0x48, AC_PWRST_D0);
	tmpstate2 = hda_sync_power_state_8409(codec, 0x49, AC_PWRST_D0);
	tmpstate3 = hda_sync_power_state_8409(codec, 0x4a, AC_PWRST_D0);
	tmpstate4 = hda_sync_power_state_8409(codec, 0x4b, AC_PWRST_D0);

	return 0;
}

// this version runs the setup using functions based on the setup using the logged data
int cs_8409_real_config(struct hda_codec *codec)
{

	unsigned int tmpstate1 = -1;
	unsigned int tmpstate2 = -1;
	unsigned int tmpstate3 = -1;
	unsigned int tmpstate4 = -1;

	cs_8409_boot_setup_real(codec);

	// check what power state of these nodes is - Apple does not do this
	tmpstate1 = hda_sync_power_state_8409(codec, 0x48, AC_PWRST_D0);
	tmpstate2 = hda_sync_power_state_8409(codec, 0x49, AC_PWRST_D0);
	tmpstate3 = hda_sync_power_state_8409(codec, 0x4a, AC_PWRST_D0);
	tmpstate4 = hda_sync_power_state_8409(codec, 0x4b, AC_PWRST_D0);

	return 0;
}

/* ---- end inlined patch_cirrus_new84.h ---- */

