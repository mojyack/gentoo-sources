// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Apple-specific support for the Cirrus Logic CS8409 HDA bridge chip.
 * Jack / headset detect / interrupt handling split out from cirrus_apple.c.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/hda_codec.h>

#include "cs8409.h"
#include "cirrus_apple_internal.h"

#define mycodec_info(...)
#define mycodec_i2c_info(...)
#define mydev_info(...)
#define mycodec_dbg(...)
#define myprintk_dbg(...)
#define myprintk(...)

/* ---- begin inlined patch_cirrus_real84.h ---- */

int tdm_in_use(struct hda_codec *codec, int where_flag)
{
	int coef_ret = 0;
	int coef_idx = 0;

	// re-implementation of AppleHDATDMBusManagerCS8409::tdmInUse

	// note on OSX the coef get functions returns a status value with read value stored in passed address
	// on linux it seems -1 is an error return

	coef_ret = cs_8409_vendor_coef_get(codec, 0x19);

	coef_idx = 0x1a;

	do {

		if ((short)coef_ret >= 0) {
			return 1;
		}

		coef_ret = cs_8409_vendor_coef_get(codec, coef_idx);

		coef_idx++;

	} while (coef_idx <= 0x57);

	return 0;

}

int cs42l83_headphone_sense(struct hda_codec *codec)
{
	int retval = 0;

	// AppleHDATDM_Codec::getHeadphonePinSense(bool*, unsigned int*)

	// register 0x1b77 - Detect Status 1
	//                   value 0x96 0x80 HP plugged bias 0x16

//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x1b lo 0x7700 i2c data 0x7796

	retval = cs_8409_vendor_i2cRead(codec, 0x90, 0x1b77, 1); // snd_hda

	return retval;
}

int read_gpio_status_check(struct hda_codec *codec)
{
	int retval;

	// should these be done powered down??
	// lets check power state here

	retval = snd_hda_codec_read(codec, codec->core.afg, 0, AC_VERB_GET_GPIO_DATA, 0x00000000); // 0x001f1500

	return retval;
}

void cs_8409_plugin_handle_detect(struct hda_codec *codec);

void cs_8409_external_device_unsolicited_response(struct hda_codec *codec, int skipcheck, int perform);

void cs_8409_plugin_complete_detect(struct hda_codec *codec, int unplug);

void cs_8409_headset_mike_buttons_enable(struct hda_codec *codec);

void cs_8409_check_status(struct hda_codec *codec, int msleeptim, int trycount);

int cs_8409_wait_for_interrupt(struct hda_codec *codec, int msleeptim, int trycount);

// this is the function which handles unsolicited responses
// - which seem to be enabled by default on the 8409 and set to be from GPIO pin 1
// which seems to be handling the cs42l83

static int cs_8409_read_status_and_clear_interrupt(struct hda_codec *codec);
static int cs42l83_disambiguate_ur_from_int(struct hda_codec *codec);
static void cs_8409_interrupt_action(struct hda_codec *codec, int int_response);

void cs_8409_external_device_unsolicited_response(struct hda_codec *codec, int skipcheck, int perform)
{
	int retval;
	int retval_int;
	int ret_disambig;
	int int_response;
	int int_masked;
	int intcnt = 0;

	if (!skipcheck) {

		retval = read_gpio_status_check(codec);

		if ((retval & 0x01) == 0x01) {
			return;
		}

	}

	// so retval_int is a bit shifted combination of a number of primary interrupt status registers

	retval_int = cs_8409_read_status_and_clear_interrupt(codec);

	// and ret_disambig is the same bit shifted combination of a number of primary interrupt mask registers

	ret_disambig = cs42l83_disambiguate_ur_from_int(codec);

	// move prints to after so not spaced by other prints

	codec_info(codec, "%s - UNSOL interrupt 0x%08x\n", __func__,retval_int);

	codec_info(codec, "%s - UNSOL disambig  0x%08x\n", __func__,ret_disambig);

	// determine masked interrupts

	int_masked = (ret_disambig & retval_int);

	codec_info(codec, "%s - UNSOL masked    0x%08x\n", __func__,int_masked);

	// determine unmasked interrupts

	int_response = ((~ret_disambig) & retval_int);

	codec_info(codec, "%s - UNSOL unmasked  0x%08x\n", __func__,int_response);

	intcnt = hweight_long(int_response);

	codec_info(codec, "%s - UNSOL number interrupt actions %d\n", __func__, intcnt);

	// do we call a mapping function here??

	if (int_response != 0) {

		if (perform)
			cs_8409_interrupt_action(codec, int_response);

	} else
		codec_info(codec, "%s - UNSOL NO unmasked interrupt\n", __func__);

	return;
}

// this is a function used when booting with headsets in
// - it appears somehow UNSOL responses are disabled

void cs_8409_check_status(struct hda_codec *codec, int msleeptim, int trycount)
{
	int retval = 0;
	int loopcnt = 0;

	while (loopcnt < trycount) {
		retval = read_gpio_status_check(codec);

		codec_info(codec, "%s - status 0x%08x\n", __func__,retval);

		msleep(msleeptim);

		loopcnt++;
	}

}

// another new function for booting with headset plugged in
// for some reason UNSOL responses seem disabled at the point of boot_setup_real we need this
int cs_8409_wait_for_interrupt(struct hda_codec *codec, int msleeptim, int trycount)
{
	int retval = 0;
	int loopcnt = 0;

	while (loopcnt < trycount) {
		retval = read_gpio_status_check(codec);

		codec_info(codec, "%s - status 0x%08x\n", __func__,retval);

		if ((retval & 0x01) != 0x01) {
			return 1;
		}

		msleep(msleeptim);

		loopcnt++;
	}

	return 0;
}

static int cs42l83_read_status_and_clear_interrupt(struct hda_codec *codec);

static int cs_8409_read_status_and_clear_interrupt(struct hda_codec *codec)
{
	int retval = 0;
	int retint = 0;
	int last_retint = 0;
	int loopmax = 11;
	int loopcnt = 0;

	// AppleHDAFunctionGroupCS8409::readStatusAndClearInterrupt

	retval = read_gpio_status_check(codec);

	if ((retval & 0x01) == 0x01) {
		return 0;
	}

	// so this function causes an unsol response because of clearing the interrupt
	// what this means is we need to add a check in the unsol response callbacks
	// to ignore GPIO 1 changes from 0 to 1 - 1 seems to be the default
	// and a 1 to 0 transition means interrupt has been triggered

	while (1) {

		retint = cs42l83_read_status_and_clear_interrupt(codec);

		codec_info(codec, "%s - UNSOL status 0x%08x\n", __func__,retint);

		retval = read_gpio_status_check(codec);

		if (loopcnt >= loopmax) {
			dev_info(hda_codec_dev(codec), "%s - ERROR - max count exceeded\n", __func__);
			break;
		}

		loopcnt++;

		if ((retval & 0x01) == 0x01) {
			codec_info(codec, "%s - interrupt %d clear\n", __func__,loopcnt);
			break;
		}

		last_retint = retint;

		// so this code definitely has a 10 IOSleep sleep call ie 10 ms
		// but from the logs it is much closer to 50 ms
		usleep_range(10000, 12000);

	}

	codec_info(codec, "%s 0x%08x end\n", __func__,retint);

	return retint;
}

static int cs42l83_read_status_and_clear_interrupt(struct hda_codec *codec)
{
	int retval;
	int retval_det1;
	int retval_det2;
	int retval_cdc;

	// AppleHDATDM_CS42L83::readStatusAndClearInterrupt

	// so I think Ive finally figured whats going on with interrupts and the 8409
	// cs42l83 interrupts trigger a state change on GPIO pin 1 which is set to
	// trigger an Unsolicited Response (UR) (enableGPIforUR function)
	// so we get a UR from high(1) (default) to low(0) when the interrupt is set
	// plus a UR from low(0) to high(1) when the interrupt is cleared
	// interrupt clearing seems to be triggered by reading registers in this routine (hence the name)
	// Im now pretty certain 0x1b7b and 0x1b7c indicate which interrupt of
	// 0x1b79 and 0x1b7a (Detect Interrupt Mask 1 and Detect Interrupt Mask 2)
	// was triggered

	// Im suspecting the 0x1b7b (maybe 0x1b7c) which are undocumented
	// - the other registers seem to be flagged as status registers
	// 0x1b7b is listed in figure 4-45 as an interrupt register but otherwise undocumented!!

	// moved to above
	//snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_POWER_STATE, 0x00000000); // 0x00170500
	hda_set_node_power_state(codec, codec->core.afg, AC_PWRST_D0);

	// note that a lot of i2cPagedRead followed by i2cPagedWrite are likely AppleHDATDMDevice::maskWriteReg
	// ie read from register and mask of bits and set certain bits without affecting others
	// we do not know the mask being used from the HDA commands!!

	// finally know what ASP refers to in lot of Cirrus docs - Audio Serial Port

	// register 0x1b7b - this is undocumented for 42l42 but labelled in fig 4-45 as Detect Interrupt 1 Status
	//                   Detect Interrupt 1 Status
	//                   value 0x40 (TIP_SENSE_PLUG interrupt from 0xa0 TIP_SENSE_PLUG unmasked)
	// register 0x1b7c - this is undocumented for 42l42 (reserved)
	//                   Detect Interrupt 2 Status
	//                   value 0x00 (none ie inverse of 0xff state of 0x1b7a)
	// register 0x1308 - Codec Interrupt Status
	//                   value 0x01 - Headset disabled, Powered down
	// register 0x1301 - ADC Overflow Interrupt Status
	// register 0x1302 - Mixer Interrupt Status
	// register 0x1303 - SRC Interrupt Status
	// register 0x1304 - ASP RX Interrupt Status
	// register 0x1305 - ASP TX Interrupt Status
	// register 0x130b - SRC Partial Lock Interrupt Status
	// register 0x130d - VP Monitor Interrupt Status
	// register 0x130e - PLL Lock Interrupt Status
	// register 0x130f - Tip/Ring Sense Plug/Unplug Interrupt Status
	//                   value 0x00

//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x1b lo 0x7b00 i2c data 0x7b40
//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x1b lo 0x7c00 i2c data 0x7c00
//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x13 lo 0x0800 i2c data 0x0801
//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x13 lo 0x0100 i2c data 0x0100
//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x13 lo 0x0200 i2c data 0x0200
//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x13 lo 0x0300 i2c data 0x030c
//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x13 lo 0x0400 i2c data 0x0400
//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x13 lo 0x0500 i2c data 0x0500
//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x13 lo 0x0b00 i2c data 0x0b60
//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x13 lo 0x0d00 i2c data 0x0d01
//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x13 lo 0x0e00 i2c data 0x0e00
//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x13 lo 0x0f00 i2c data 0x0f00

	// note that these functions contain power on/power off calls

	retval_det1 = cs_8409_vendor_i2cRead(codec, 0x90, 0x1b7b, 1); // snd_hda

	retval_det2 = cs_8409_vendor_i2cRead(codec, 0x90, 0x1b7c, 1); // snd_hda

	retval_cdc = cs_8409_vendor_i2cRead(codec, 0x90, 0x1308, 1); // snd_hda

	cs_8409_vendor_i2cRead(codec, 0x90, 0x1301, 1); // snd_hda

	cs_8409_vendor_i2cRead(codec, 0x90, 0x1302, 1); // snd_hda

	cs_8409_vendor_i2cRead(codec, 0x90, 0x1303, 1); // snd_hda

	cs_8409_vendor_i2cRead(codec, 0x90, 0x1304, 1); // snd_hda

	cs_8409_vendor_i2cRead(codec, 0x90, 0x1305, 1); // snd_hda

	cs_8409_vendor_i2cRead(codec, 0x90, 0x130b, 1); // snd_hda

	cs_8409_vendor_i2cRead(codec, 0x90, 0x130d, 1); // snd_hda

	cs_8409_vendor_i2cRead(codec, 0x90, 0x130e, 1); // snd_hda

	cs_8409_vendor_i2cRead(codec, 0x90, 0x130f, 1); // snd_hda

	//snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_POWER_STATE, 0x00000003); // 0x00170503

	retval = 0;
	retval = ((retval_det1 & 0xff) << 16) | ((retval_det2 & 0xff) << 8) | (retval_cdc & 0xff);

	return retval;
}

static int cs42l83_disambiguate_ur_from_int(struct hda_codec *codec)
{
	int retval_det1;
	int retval_det2;
	int retval_cdc;
	int retval;

	// from AppleHDAFunctionGroupCS8409::externalDeviceUnsolicitedResponse

	// AppleHDATDM_CS42L83::disambiguateURfromINT

	//snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_POWER_STATE, 0x00000000); // 0x00170500
	hda_set_node_power_state(codec, codec->core.afg, AC_PWRST_D0);

	// register 0x1b79 - Detect Interrupt Mask 1
	//                   value 0xa0 TIP_SENSE_PLUG unmasked
	// register 0x1b7a - Detect Interrupt Mask 2
	//                   value 0xff all masked
	// register 0x131b - Codec Interrupt Mask
	//                   value 0x3 all masked

//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x1b lo 0x7900 i2c data 0x79a0
//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x1b lo 0x7a00 i2c data 0x7aff
//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x13 lo 0x1b00 i2c data 0x1b03

	retval_det1 = cs_8409_vendor_i2cRead(codec, 0x90, 0x1b79, 1); // snd_hda

	retval_det2 = cs_8409_vendor_i2cRead(codec, 0x90, 0x1b7a, 1); // snd_hda

	retval_cdc = cs_8409_vendor_i2cRead(codec, 0x90, 0x131b, 1); // snd_hda

	// there is a bunch of code here presumably figuring out what happened

	retval = 0;
	retval = ((retval_det1 & 0xff) << 16) | ((retval_det2 & 0xff) << 8) | (retval_cdc & 0xff);
	return retval;

}

void cs42l83_set_power_state_on(struct hda_codec *codec, int instate)
{
	int retval;
	int loopcnt;

	// likely in AppleHDATDM_CS42L83::enable
	// (only place AppleHDATDM_CS42L83::setPowerState is called from is AppleHDATDM_CS42L83::enable)
	// AppleHDATDM_CS42L83::setPowerState

	// register 0x1101 - Power Down Control 1
	// register 0x130b - SRC Partial Lock Interrupt Status

	// seems to me this powers on either the input path ie mike (instate=1)
	// or output path ie headphones (instate=0)

	// I think that the use of cs_8409_vendor_i2cWriteMask means that we can
	// call this function multiple times and set both input and output power on
	// because only the bits in the mask are affected

//      codec headphone (digital in/audio out)

//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x11 lo 0x0100 i2c data 0x01fe
//      snd_hda i2cPagedWrite i2c address 0x90 i2c reg hi 0x11 lo 0x019e i2c data 0x009e
//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x13 lo 0x0b00 i2c data 0x0b60
//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x11 lo 0x0100 i2c data 0x019e
//      snd_hda i2cPagedWrite i2c address 0x90 i2c reg hi 0x11 lo 0x0196 i2c data 0x0096

//      codec capture (audio in/digital out)

//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x11 lo 0x0100 i2c data 0x0196
//      snd_hda i2cPagedWrite i2c address 0x90 i2c reg hi 0x11 lo 0x0116 i2c data 0x0016
//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x13 lo 0x0b00 i2c data 0x0b24
//      snd_hda i2cPagedRead  i2c address 0x90 i2c reg hi 0x11 lo 0x0100 i2c data 0x0116
//      snd_hda i2cPagedWrite i2c address 0x90 i2c reg hi 0x11 lo 0x0112 i2c data 0x0012

	if (instate) {
		// power setup for codec (0x01) and output (0x80)

		// this converts 0xfe to 0x7e (or 0x96 to 0x16)
		cs_8409_vendor_i2cWriteMask(codec, 0x90, 0x1101, 0x81, 0x0, 1); // snd_hda

		loopcnt = 0;
		while (loopcnt < 0x14) {
			retval = cs_8409_vendor_i2cRead(codec, 0x90, 0x130b, 1); // snd_hda
			if ((retval & 0x1) == 0x1)
				break;
			loopcnt++;
			usleep_range(2000, 3000);
		}

		// power setup for ADC (0x04)

		// this converts 0x7e to 0x7a (or 0x16 to 0x12)
		cs_8409_vendor_i2cWriteMask(codec, 0x90, 0x1101, 0x04, 0x0, 1); // snd_hda
	} else {
		// power setup for codec (0x01), input (0x40) and mixer (0x20)

		//cs_8409_vendor_i2cRead(codec, 0x90, 0x1101, 1); // snd_hda
		//cs_8409_vendor_i2cWrite(codec, 0x90, 0x1101, 0x009e, 1); // snd_hda

		// this converts 0xfe to 0x9e
		cs_8409_vendor_i2cWriteMask(codec, 0x90, 0x1101, 0x61, 0x0, 1); // snd_hda

		loopcnt = 0;
		while (loopcnt < 0x14) {
			retval = cs_8409_vendor_i2cRead(codec, 0x90, 0x130b, 1); // snd_hda
			if ((retval & 0x4) == 0x4)
				break;
			loopcnt++;
			usleep_range(2000, 3000);
		}

		// power setup for headphone (0x08)

		//cs_8409_vendor_i2cRead(codec, 0x90, 0x1101, 1); // snd_hda
		//cs_8409_vendor_i2cWrite(codec, 0x90, 0x1101, 0x0096, 1); // snd_hda

		// this converts 0x9e to 0x96
		cs_8409_vendor_i2cWriteMask(codec, 0x90, 0x1101, 0x08, 0x0, 1); // snd_hda

	}

}

// this is where we need to decode the actions to be taken
// note that the button interrupts are undocumented for the cs42l42 (reserved)
// not yet clear which one is up and which one is down!!
// so after button detect 0x1b7b is 0x14 and 0x1b7c is 0x0a
// for 0x1b7b 0x14 are reserved bits for cs42l42 - but the 0x04 only seen on detection
// actual button presses are 0x01, 0x02 and button release 0x10
// for 0x1b7c 0x02 is a short release for buttons, 0x08 is reserved
// the mask bits for 0x1b7a seem to be 0xe7 for buttons defining 0x08 as the button detect interrupt
// (0x1b79 is mask, 0x1b7b status; 0x1b7a is mask, 0x1b7c is presumed status, 0x131b is mask, 0x1308 status
//  0x1320 is mask, 0x130f status)
#define TIP_SENSE_PLUG 0x400000
#define TIP_SENSE_UNPLUG 0x200000
#define BUTTON_DOWN_PRESS 0x10000
#define BUTTON_UP_PRESS 0x20000
#define BUTTON_RELEASE 0x100000
// pressing the play/pause button on earbuds yields 0x100 on down and 0x200 on up
#define BUTTON_TOGGLE_DOWN_PRESS 0x100
#define BUTTON_TOGGLE_UP_PRESS 0x200
#define BUTTON_DETECT_MAIN 0x1800  // we only see 0x800 but the mask allows for these 2 bits
#define BUTTON_DETECT_MASK 0x60
#define BUTTON_DETECT1 0x40
#define BUTTON_DETECT2 0x20
#define MIKE_CONNECT 0x02
#define BUTTONS (BUTTON_UP_PRESS | BUTTON_DOWN_PRESS | BUTTON_TOGGLE_UP_PRESS | BUTTON_TOGGLE_DOWN_PRESS)
#define HSDET_AUTO_DONE 0x02
#define PDN_DONE 0x01

static void cs_8409_headset_plugin_event(struct hda_codec *codec);
static void cs_8409_headset_unplug_event(struct hda_codec *codec);
static void cs_8409_headset_type_detect_event(struct hda_codec *codec);
static int cs_8409_set_power_state(struct hda_codec *codec, int power_state);
static void cs_8409_headset_button_detect_event(struct hda_codec *codec);
static void cs_8409_headset_button_event(struct hda_codec *codec, int buttons);
static void cs_8409_plugin_event_continued(struct hda_codec *codec);

static void cs_8409_interrupt_action(struct hda_codec *codec, int int_response)
{
	int update_jacks = 0;
	struct cs8409_apple_spec *spec = codec->spec;

	// so if Im analyzing the Dell code correctly
	// I think we should only do the snd_hda_jack_report_sync after all jack detection
	// plus all nid mute/unmute and widget output/input enable
	// because the Dell code first does tip sense, then (if wanted) jack type detection
	// then runs snd_hda_jack_unsol_event which does any callbacks
	// and finally the snd_hda_jack_report_sync

	if ((int_response & TIP_SENSE_PLUG) == TIP_SENSE_PLUG) {
		dev_info(hda_codec_dev(codec), "%s - plug in\n", __func__);
		cs_8409_headset_plugin_event(codec);
	} else if ((int_response & TIP_SENSE_UNPLUG) == TIP_SENSE_UNPLUG) {
		dev_info(hda_codec_dev(codec), "%s - unplug\n", __func__);
		cs_8409_cs42l83_mark_jack(codec);
		cs_8409_headset_unplug_event(codec);
		// so although this more consistent with linux way (all automute etc callbacks done before report sync)
		// it seems we need to update the linux user side before doing the amp reset when playing
		// with cs_8409_cs42l83_jack_report_sync here there is a few 10s milliseconds period where get anomalous volume
		// because we start playing through the amps while linux user side still says its headphone output
		// - this may be because updating the linux user side takes a little while
	} else if ((int_response & HSDET_AUTO_DONE) == HSDET_AUTO_DONE) {
		dev_info(hda_codec_dev(codec), "%s - headset detected\n", __func__);

		cs_8409_headset_type_detect_event(codec);

		// and this is where life gets really complicated
		// if we have a mike we do a button detect - but that leads to an unsolicited response
		// so we only continue here I think if we dont have a mike
		if (!(spec->have_mike)) {
			cs_8409_cs42l83_mark_jack(codec);
			cs_8409_plugin_event_continued(codec);
			dev_info(hda_codec_dev(codec), "%s - headset detect no mike jack_report_sync\n", __func__);
			cs_8409_cs42l83_jack_report_sync(codec);
		}
	}
	// not clear what test is here - but this should check what we see - one button interrupt seems to be activated
	// when doing the button detect
	// not sure what the exact button interrupt is - we get 0x140800
	// so the button detect interrupt is 0x0800 - the 0x140000 are actual button interrupts (undocumented for cs42l42)
	else if (int_response & BUTTON_DETECT_MAIN) {
		dev_info(hda_codec_dev(codec), "%s - buttons detected\n", __func__);
		cs_8409_headset_button_detect_event(codec);

		cs_8409_cs42l83_mark_jack(codec);

		cs_8409_plugin_event_continued(codec);

		dev_info(hda_codec_dev(codec), "%s - button detect jack_report_sync\n", __func__);
		cs_8409_cs42l83_jack_report_sync(codec);

	} else if (((int_response & BUTTON_UP_PRESS) == BUTTON_UP_PRESS) ||
		 ((int_response & BUTTON_DOWN_PRESS) == BUTTON_DOWN_PRESS) ||
		 ((int_response & BUTTON_TOGGLE_UP_PRESS) == BUTTON_TOGGLE_UP_PRESS) ||
		 ((int_response & BUTTON_TOGGLE_DOWN_PRESS) == BUTTON_TOGGLE_DOWN_PRESS)) {
		dev_info(hda_codec_dev(codec), "%s - button event on \n", __func__);
		cs_8409_headset_button_event(codec, int_response);
		update_jacks = 1;

	} else if (((int_response & BUTTON_RELEASE) == BUTTON_RELEASE)) {
		dev_info(hda_codec_dev(codec), "%s - button event off \n", __func__);
		cs_8409_headset_button_event(codec, int_response);
		update_jacks = 1;

	} else if ((int_response & PDN_DONE) == PDN_DONE) {
		dev_info(hda_codec_dev(codec), "%s - power down\n", __func__);
	} else {
		dev_info(hda_codec_dev(codec), "%s - UNKNOWN INTERRUPT 0x%08x\n", __func__, int_response);
	}

	return;
}

void cs_8409_headset_mike_buttons_enable(struct hda_codec *codec);

static void cs_8409_plugin_event_continued(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;
	int headset_plugged_in = 0;
	int retval = 0;

	// 0 is power state - 0 is powered on +ve powered/powering down
	// headset_plugged_in indicates if headset still plugged in or not
	headset_plugged_in = cs_8409_set_power_state(codec, 0);

	if (headset_plugged_in) {
		// now handle plugin while playing
		if (spec->playing) {

			cs_8409_amps_disable_streaming(codec);

			cs_8409_enable_headset_streaming(codec);

			// the following is just cs_8409_enable_headset_streaming
			// power on audio output

			// so OSX now does another one of its enable off/enable on - ignoring

			// we need to reset formats here - so we follow the same path as a simple amp
			// or headphone play ie after the pre-prepare we force a reset of the
			// of the stream format

			// using explicit nid here!!

		} else {

			// try removing this - we still do a partial setup when actually play on OSX
			// and if we stop play then do another play we do a full setup
			// - why not just enable when we play??
			// in any case we initially made this a full setup and it worked

			// so now think on OSX we pre-setup the headphone and mike here
			// when we dont know if we will be playing or capturing
			// in particular realised that cs42l83_configure_serial_port is only called
			// for the headphone setup - but it sets both primary ASP (Audio Serial Port)
			// transmit and receive frequencies - which would seem to be important for
			// capturing!!

		}

		// NOTA BENE - no concept/implementation of plugging in while capturing!!

		// this event now gets called if boot with headset plugged in
		// but from this point the boot phase setup is different
		// -  now a headset_phase of 1 indicates booted with headset plugged in
		// - headset phase of 2 or more means post boot headset plugin
		if (spec->headset_phase >= 2) {

			// ensure the intmike/linein nids are powered off
			cs_8409_inputs_power_nids_off(codec);

			retval = cs42l83_headphone_sense(codec);

			if (!(retval & 0x80)) {
				dev_info(hda_codec_dev(codec), "%s JACK DISCONNECT UNIMPLEMENTED!!\n", __func__);
			}

			if (spec->have_mike) {
				if (spec->capturing) {
					dev_info(hda_codec_dev(codec), "%s PLUGIN WHILE CAPTURING UNIMPLEMENTED!!\n", __func__);
				}

				// this is just calling this routine
				//cs_8409_headset_mike_setup_nouse

				cs_8409_intmike_linein_disable(codec);

				// is this a good position to switch the inputs??
				switch_input_src(codec);

				// confirmed that if do a second recording we get a full setup as for playing above
				// - so why not just enable when we capture??
				// (only plausible reason so far is to reduce setup time because of the long time
				//  to send the i2c commands??)
				// NOTE - this is complicated because on OSX it appears the headphone setup is always
				//        done - even if just capturing
				//        going with OSX way and doing the headphone setup as well

				// so this is a problem - at this point we dont have a stream
				// so our format is null
				// what to do??
				// now moving all this setup into the actual capture setup
				// - as did with the play setup

				cs_8409_headset_mike_buttons_enable(codec);

			}
		}

	} else {} // headset unplugged - should be handled by the unplug interrupt

}

void cs_8409_plugin_handle_detect(struct hda_codec *codec);
void cs_8409_plugin_complete_detect(struct hda_codec *codec, int unplug);

static void cs_8409_headset_plugin_event(struct hda_codec *codec)
{
	int retval;
	struct cs8409_apple_spec *spec = codec->spec;

	cs_8409_plugin_handle_detect(codec);

	//retval = snd_hda_codec_read_check(codec, codec->core.afg, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000000, 1066); // 0x001f0500

	// this seems to be here but no idea where coming from
	retval = read_gpio_status_check(codec);

	// following code likely from AppleHDAMikeyInternalCS8409::handleJackDetectUR
	// moved from cs_8409_plugin_handle_detect

	// then call setTimer to initiate function after a time period

	// this is NOT a debug sleep - it occurs on all plugin events on OSX for some reason
	msleep(1800);

	//retval = snd_hda_codec_read_check(codec, codec->core.afg, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000000, 1069); // 0x001f0500

	cs_8409_plugin_complete_detect(codec,1);

	spec->headset_phase = 3;

}

void cs_8409_plugin_handle_detect(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;

	// now think this is AppleHDAMikeyInternalCS8409::handleJackDetectUR

	// which calls:
	// AppleHDAMikeyInternalCS8409::disableButtonDetection
	// AppleHDAMikeyInternalCS8409::enableHPClamps

	// AppleHDAMikeyInternalCS8409::enableHPClamps calls:
	// AppleHDATDM_Codec::setHPOutClamp

	// this is a pre-value - indicates we have had a jack detect
	// but set to 1 when have checked with cs42l83_headphone_sense for headset
	// at the moment not used
	spec->jack_present = 2;

	cs42l83_headset_button_detect_interrupts_off(codec);

	cs42l83_headset_set_hpout_clamp_disable(codec);

	// IOSleep(1) here
	usleep_range(1000, 2000);

}

void cs_8409_plugin_complete_detect(struct hda_codec *codec, int unplug)
{
	int retval;
	struct cs8409_apple_spec *spec = codec->spec;

	// so AppleHDAMikeyInternalCS8409::generalTimerCallback calls AppleHDAMikeyInternalCS8409::completeJackDetectUR
	// AppleHDAMikeyInternalCS8409::generalTimerCallback is set as a timer callback in the AppleHDAMikeyInternal::init
	// using the IOTimerEventSource::timerEventSource function

	// this is AppleHDAMikeyInternalCS8409::completeJackDetectUR
	// its first call is to AppleHDATDM_Codec::getHeadphonePinSense
	// which if returns 0 in the bool arg jumps to AppleHDAMikeyInternalCS8409::handleJackDisconnectUR
	// - which does the disconnect

	// so thats weird - the first call is a power call which doesnt seem to exist in the log

	retval = cs42l83_headphone_sense(codec);

	if ((retval & 0x80)) {

		spec->jack_present = 1;
		spec->headset_enable = 1;

		cs42l83_complete_jack_detect(codec);

		cs42l83_power_hs_bias_on(codec);

		// this seems to be setting an interrupt on 0x131b for headset detect
		// - but there doesnt seem to be a delay anywhere here
		// so it must be immediately triggered

		cs42l83_enable_hs_auto_int_on(codec);

		if (unplug)
			cs42l83_unplug_interrupt_setup(codec);

		cs42l83_set_hpout_pulldown_off(codec);

		cs42l83_headset_detect_on(codec);

	} else {

		spec->jack_present = 0;
		spec->headset_enable = 0;

		// AppleHDAMikeyInternalCS8409::handleJackDisconnectUR

		dev_info(hda_codec_dev(codec), "%s no headphone UNIMPLEMENTED!!\n", __func__);

	}

}

static void cs_8409_headset_type_detect_event(struct hda_codec *codec)
{
	int flag = 1;
	int headset_type = 0;
	struct cs8409_apple_spec *spec = codec->spec;

	// this is AppleHDAMikeyInternalCS8409::handleTypeDetectUR
	// dont yet see the path to call this - Im guessing from some messaging call

	// I think we get to here if have either an unplug event or headset detect event
	// - in both cases we need to turn off the headset detect interrupt
	// and unset for headset detect

	cs42l83_enable_hs_auto_int_off(codec);

	headset_type = cs42l83_headset_type(codec);

	// we then have options based on the headset type
	headset_type = headset_type & 0x3;

	// types are 0x00 (1), 0x01 (2), 0x02 (3) and 0x3 (4)
	// type 1 Pin 1 Left, Pin 2 Right, Pin 3 Gnd, Pin 4 Mic
	// type 2 Pin 1 Left, Pin 2 Right, Pin 3 Mic, Pin 4 Gnd
	// type 3 Pin 1 Left, Pin 2 Right, Pin 3 Gnd, Pin 4 Gnd
	// type 4 Optical!!

	if (headset_type == 0x00 && flag == 0) {
		// 0x74df1

		// if ?? goto 0x74ee2

		// if ?? goto 0x751e8

			// 0x74e42

			// possible AppleHDAMikeyInternalCS8409::handleJackDetectUR

			// goto 0x7546d
			// IOLog()
			// goto 0x75198
			// return

		// else

			// 0x751e8

			// if dont sense headphone guess we do AppleHDAMikeyInternalCS8409::handleJackDisconnectUR

			//if (!(retval & 0x80))
			//        // AppleHDAMikeyInternalCS8409::handleJackDisconnectUR
			//        goto 0x74ee2

			// error!!
	}

	if (headset_type == 0x00) {
		// 0x74df1
		// goto 0x74ee2
		spec->headset_type = 1;
		spec->have_mike = 1;
		dev_info(hda_codec_dev(codec), "%s headset has mike!!\n", __func__);
	} else if (headset_type == 0x01) {
		// 0x74e98
		// insert Mikey event 0xfe
		// goto 0x74ee2
		spec->headset_type = 2;
		spec->have_mike = 1;
		dev_info(hda_codec_dev(codec), "%s headset has mike!!\n", __func__);
		}
	else if (headset_type == 0x02) {
		// 0x74eb9
		// goto 0x74ee2
		spec->headset_type = 3;
		dev_info(hda_codec_dev(codec), "%s headset does not have mike!!\n", __func__);
	} else if (headset_type == 0x03) {
		// this is SPDIF!!
		// 0x74ec3
		// insert Mikey event 0xfc
		spec->headset_type = 4;
		dev_info(hda_codec_dev(codec), "%s headset does not have mike!!\n", __func__);
	}

	// 0x74ee2

	cs42l83_headset_detect_off(codec);

	cs42l83_set_hpout_pulldown_on(codec);

	usleep_range(1000, 2000);

	// 0x74ff1

	cs42l83_set_hpout_clamp_enable(codec);

	usleep_range(1000, 2000);

	if (spec->have_mike) {

		cs42l83_enable_hsbias_auto_clamp_on(codec);

		cs42l83_enable_hsbias_auto_clamp_off0(codec);

		// I dont see a difference in these 2 functions
		cs42l83_power_hs_bias_off(codec);

		// difference from no mike headphones

		cs42l83_setup_button_detect(codec);

		cs42l83_power_hs_bias_button_on(codec);

		cs42l83_enable_hsbias_auto_clamp_off1(codec);

	} else {
		// goto 0x75a02

		// 0x750a2

		cs42l83_headset_mike_detect_off(codec);

		cs42l83_power_hs_bias_off(codec);
	}

	// 0x75149

	// there is a call to dispatchStatelessTagToEngines which is likely what initiates the stream setup etc

	// 0x7546d

	// 0x75198

	// there is an unknown call here - possible setPowerState
	// cannot figure out if this is doing anything - none of the functions seem to fit the log
	// so now think this function isnt really doing anything
	// and the setPowerState is from some other function call path ie the result
	// of dispatchStatelessTagToEngines

	// exit routine after this

}

static int cs_8409_set_power_state(struct hda_codec *codec, int power_state)
{
	int retval = 0;
	int retstate = 0;
	int flag = 0;

	// this is likely AppleHDAMikeyInternalCS8409::setPowerState as there is
	// a pin sense and handleJackDisconnectUR in AppleHDAMikeyInternalCS8409::setPowerState
	// in fact all AppleHDAMikeyInternalCS8409::setPowerState does is check the headphone sense
	// and then either do a handleJackDisconnectUR or handleJackDetectUR

	if (flag) {
		// check for headphone whatever power state of HDA is

		retval = cs42l83_headphone_sense(codec);

		// if sense headphone guess we do AppleHDAMikeyInternalCS8409::handleJackDetectUR

		if ((retval & 0x80)) {
			// AppleHDAMikeyInternalCS8409::handleJackDetectUR
			dev_info(hda_codec_dev(codec), "%s JACK DETECT UNIMPLEMENTED!!\n", __func__);
		}

		retstate = 0;
	} else {
		// only check for headphone if HDA powered up
		if (power_state == 0) {
			retval = cs42l83_headphone_sense(codec);

			// if dont sense headphone guess we do AppleHDAMikeyInternalCS8409::handleJackDisconnectUR

			if (!(retval & 0x80)) {
				// AppleHDAMikeyInternalCS8409::handleJackDisconnectUR
				dev_info(hda_codec_dev(codec), "%s JACK DISCONNECT UNIMPLEMENTED!!\n", __func__);
				retstate = 0;
			} else
				retstate = 1;
		}
	}

	return retstate;
}

static void cs_8409_headset_button_detect_event(struct hda_codec *codec)
{
	int ret_button;
	int ret_mike;
	struct cs8409_apple_spec *spec = codec->spec;

	// this returns significant state - headphone sense (shift 16), and 2 reads from register 0x1b78 (second one shifted 8)
	ret_button = cs42l83_handle_button_detect(codec);

	// so now seen on imacs we have a button detect of 0x20 rather than 0x40 previously seen
	// - this maybe an Apple headset/non-Apple headset issue rather than imac issue (the headset was non-Apple)
	//if ((ret_button & BUTTON_DETECT) == BUTTON_DETECT)
	if (ret_button & BUTTON_DETECT_MASK) {
		spec->have_buttons = 1;
	}

	// this is a read from same register 0x1b78 - which seems to contain both senses
	// - button sense 0x40/0x20 (assumed) and mike sense 0x02 - known but undocumented
	// do we do anything with this??
	// we have aleady set have_mike prior to this
	// could log an error here
	ret_mike = cs42l83_mike_connected(codec);

	if ((ret_mike & MIKE_CONNECT) != MIKE_CONNECT)
		dev_err(hda_codec_dev(codec), "ERROR - has mike but mike not connected - not analyzed!!\n");

}

static void cs_8409_headset_button_event(struct hda_codec *codec, int buttons)
{
}

void cs_8409_headset_mike_buttons_enable(struct hda_codec *codec)
{

	// part of AppleHDAMikeyInternalCS8409::handleButtonDetectUR

	cs42l83_configure_headset_button_interrupts(codec);

	cs42l83_enable_hsbias_auto_clamp_off2(codec);

	// following coded explicitly in handleButtonDetectUR

	cs42l83_hsbias_sense_on(codec);

}

static void cs_8409_unplug_handle_disconnect(struct hda_codec *codec);

static void cs_8409_headset_unplug_event(struct hda_codec *codec)
{
	int retval;

	// Im guessing we are ensuring headphone is unplugged here
	// what to do if not!!
	retval = cs42l83_headphone_sense(codec);

	if ((retval & 0x80)) {
		dev_info(hda_codec_dev(codec), "%s headphone still sensed - NOT HANDLED - UNIMPLEMENTED!!!\n", __func__);
	} else {

		cs_8409_unplug_handle_disconnect(codec);
	}

}

static void cs_8409_unplug_handle_disconnect(struct hda_codec *codec)
{
	int retval;
	struct cs8409_apple_spec *spec = codec->spec;

	cs42l83_plugin_interrupt_setup(codec);

	cs42l83_enable_hs_auto_int_off(codec);

	cs42l83_headset_detect2_off(codec);

	if (spec->have_mike) {
		cs42l83_power_hs_bias_off(codec);

		cs42l83_enable_hsbias_auto_clamp_off3(codec);

		cs42l83_disable_button_interrupts(codec);
	}

	// Im guessing we are ensuring headphone is unplugged here
	// what to do if not!!
	retval = cs42l83_headphone_sense(codec);

	if ((retval & 0x80)) {
		dev_err(hda_codec_dev(codec), "%s headphone still sensed - NOT HANDLED - UNIMPLEMENTED!!!\n", __func__);
	} else {

		// even here this still has audio glitch
		// - but with 100 ms wait later seems to fix it

		// silly me - we must update this here so jack_detect_update in jack_report_sync will determine the headset has been unplugged
		spec->jack_present = 0;

		// try setting ALL jacks dirty - likely not needed

		dev_info(hda_codec_dev(codec), "cs_8409_interrupt_action - unplug jack_report_sync\n");
		cs_8409_cs42l83_jack_report_sync(codec);

		if (spec->playing) {

			cs42l83_headset_enable_off(codec);

			cs42l83_power_off_codec_output(codec);

			cs42l83_buffers_onoff(codec, 0);

			if ((spec->have_mike))
				cs_8409_headset_amp_format_setup_disable(codec, 0);
			else {
				cs42l83_power_onoff(codec, 0);

				cs_8409_headset_amp_format_setup_disable(codec, 1);
			}

			cs42l83_headset_enable_off(codec);

			cs42l83_power_off_codec_output(codec);

		}

		// so we have determined the volume/glitch issues are after this

		// with previous jack_report_sync and this wait dont have a glitch
		msleep(100);

		// silly me - we must update this here so jack_detect_update in jack_report_sync will determine the headset has been unplugged

		// add a wait for user side update - still get a small glitch

		// this is done if playing or not??
		// - changing - only setup amps if still playing

		if (spec->playing) {

			//unplug23_play_setup_TDM_6462(struct hda_codec *codec)

			play_setup_TDM_amps12(codec, 1);

			//unplug23_setup_amps_6462(struct hda_codec *codec)

			play_setup_amps12(codec);

			//unplug23_play_setup_TDM_7472(struct hda_codec *codec)

			play_setup_TDM_amps34(codec);

			//unplug23_play_setup_amps_7472(struct hda_codec *codec)

			play_setup_amps34(codec);

			//unplug23_sync_converters_on(struct hda_codec *codec)

			play_sync_converters_on(codec);

			// so here linux user side reports still headphone output (because originally had not yet done
			// jack_report_sync) which leads to audio glitch with output now through speakers
			// so we need to update linux user side after headphone output disable above
			// (the volume mismatch previously heard was due to incorrect handling of nid 0x03 update for stereo (ie 2 channel) source)
			// can only think its delay from jack_report_sync till linux user side updated
			// (we dont really have a massive lot of commands from here till jack_report_sync
			// (here would be more consistent with linux way which does all power/automute/automic etc callbacks before jack_report_sync)

			// silly me - we must update this here so jack_detect_update in jack_report_sync will determine the headset has been unplugged

		}

		// more duplicated disable/enables

		if (!(spec->have_mike)) {
			cs_8409_inputs_power_nids_off(codec);
		}

		// and another headphone sense

		retval = cs42l83_headphone_sense(codec);

		if ((retval & 0x80)) {
			dev_err(hda_codec_dev(codec), "%s headphone sensed again - NOT HANDLED - UNIMPLEMENTED!!!\n", __func__);
		} else {

			if (spec->have_mike) {

				cs42l83_mike_disable(codec);

				cs42l83_power_onoff(codec, 0);

				cs42l83_headset_amp_disable_and_mike_format_setup_disable(codec);

				// is this a good position to switch the inputs??
				switch_input_src(codec);

				cs_8409_intmike_linein_resetup(codec);

			}

			if (spec->playing) {
				if (!(spec->have_mike)) {
					cs_8409_inputs_power_nids_off(codec);
				}
			}

			if (spec->have_mike)
				cs_8409_inputs_power_nids_off(codec);

			cs42l83_unplug_headset_detect_off(codec);

			cs42l83_headset_switch_control(codec);

		}

	}

	// and reset all headset variables

	spec->jack_present = 0;

	spec->headset_type = 0;

	spec->have_mike = 0;

	spec->have_buttons = 0;

	spec->headset_play_format_setup_needed = 1;
	spec->headset_capture_format_setup_needed = 1;

	spec->headset_presetup_done = 0;

}
