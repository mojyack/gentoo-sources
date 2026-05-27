// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Apple-specific support for the Cirrus Logic CS8409 HDA bridge chip.
 * Play / capture / headset orchestration split out from cirrus_apple.c.
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

int cs_8409_boot_setup_real(struct hda_codec *codec)
{
	int headset_on_boot = 0, retval;

	struct cs8409_apple_spec *spec = codec->spec;

	setup_reset_and_clear(codec);

	// read parameters from all nodes - excluding VirtualWidgets

	// the loop over node counts calls AppleHDAWidgetFactory::createAppleHDAWidget(DevIdStruct*)
	// which Im assuming calls the initForNodeID functions

	init_read_all_nodes(codec);

	read_vendor_node(codec);

	init_read_coefs(codec);

	hda_set_node_power_state(codec, codec->core.afg, AC_PWRST_D0);

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, AC_VERB_SET_PROC_STATE, 0x00000001); // 0x04770301

	read_virtual_widgets(codec);

	init_for_node_vendor(codec);

	// this is determineSpeakerID
	// this does not make sense - this just checks GPIO pin(s)??

	determine_speaker_id(codec);

	//snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_POWER_STATE, 0x00000003); // 0x00170503

	// why is this commented??

	enable_i2c(codec);

	if (codec->core.subsystem_id == 0x106b3900) {
		enable_GPIforUR(codec, 0x5);
	} else if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600) {
		enable_GPIforUR(codec, 0xd);
	} else if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		enable_GPIforUR(codec, 0xd);
	} else {
		dev_info(hda_codec_dev(codec), "UNKNOWN subsystem id 0x%08x",codec->core.subsystem_id);
	}

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, AC_VERB_SET_PROC_STATE, 0x00000001); // 0x04770301
	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, AC_VERB_SET_PROC_STATE, 0x00000001); // 0x04770301

	if (codec->core.subsystem_id == 0x106b3900) {
		cs42l83_external_control_GPIO(codec, 0x7);
	} else if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600) {
		cs42l83_external_control_GPIO(codec, 0xf);
	} else if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		cs42l83_external_control_GPIO(codec, 0xf);
	} else {
		dev_info(hda_codec_dev(codec), "UNKNOWN subsystem id 0x%08x",codec->core.subsystem_id);
	}

	// we have confirmed that the disabling all the cs42l83 setup does not affect the amps

	cs42l83_reset(codec);

	if (codec->core.subsystem_id == 0x106b3900) {
		cs42l83_external_control_GPIO(codec, 0x7);
	} else if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600) {
		cs42l83_external_control_GPIO(codec, 0xf);
	} else if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		cs42l83_external_control_GPIO(codec, 0xf);
	} else {
		dev_info(hda_codec_dev(codec), "UNKNOWN subsystem id 0x%08x",codec->core.subsystem_id);
	}

	cs42l83_device_id(codec);

	cs42l83_inithw(codec);

	if (codec->core.subsystem_id == 0x106b3900) {
		setup_amps_reset_i2c_max(codec);
	} else if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600) {
		//setup_amps_reset_i2c_ssm3
		setup_amps_reset_i2c_ssm3(codec);
	} else if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		setup_amps_reset_i2c_tas576(codec);
	} else {
		dev_info(hda_codec_dev(codec), "UNKNOWN subsystem id 0x%08x",codec->core.subsystem_id);
	}

	retval = read_gpio_status_check(codec);

	cs42l83_mic_detect(codec);

	// apparently the imacs use an inverted circuit for physical sensing of jack being plugged in
	if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00)
		cs42l83_tip_sense(codec, 1);
	else
		cs42l83_tip_sense(codec, 0);

	cs42l83_plugin_interrupt_setup(codec);

	// should check here if have headphones plugged in
	// likely additional code here if we did boot with headphones plugged in
	retval = cs42l83_headphone_sense(codec);

	if ((retval & 0x80)) {
		dev_info(hda_codec_dev(codec), "%s headphone ALREADY PLUGGED IN!!\n", __func__);
		// store for after init
		headset_on_boot = 1;
	}

	// cs_8409_intmike_format_setup_enable is done before we get a capture prepare
	// so we need to store some dummy initial setup
	// unlike OSX make it a stream id of 0
	// (note this only updates our local cached data - need a cs_8409_really_update_stream_format to actually update nid)
	cs_8409_store_stream_format(codec, spec->intmike_adc_nid, 0, 0x4031);

	cs_8409_intmike_format_setup_enable(codec, 0x4031, 1);
	cs_8409_intmike_volume_setup(codec, 0x27);
	cs_8409_intmike_stream_on_nid(codec);

	cs_8409_really_update_stream_format(codec, spec->intmike_adc_nid, 1, 0, 0);

	cs_8409_linein_volume_setup(codec, 0x27);
	cs_8409_linein_format_setup_disable(codec);

	cs_8409_intmike_stream_conn_off(codec);

	cs_8409_linein_stream_conn_off(codec);

	cs_8409_intmike_stream_off_nid(codec);

	cs_8409_linein_stream_off_nid(codec);

	cs_8409_intmike_volume_mute_nouse(codec);

	cs_8409_linein_volume_mute_nouse(codec);

	cs_8409_intmike_volume_unmute_nouse(codec);

	cs_8409_linein_volume_unmute_nouse(codec);

	if (headset_on_boot) {
		// so the big question is what to do about unsolicited responses
		// should we disable them in this boot section
		// and if so when should we re-enable them
		// I suspect at some point the code for headset already plugged in
		// switches to unsolicited response code

		// now thinking we dont do any of the sound output setup here at all
		// - that will be done by an open pcm call
		// - we do need to detect the headset tho
		// (also note that turning the power on/off seems to mess up unsol responses)

		// so I think we need to enable unsol response at this stage and block it till end of this function
		// - because we seem to get an UNSOL response in the middle of this code

		// so far we do not seem to get any UNSOL responses here if headset plugged in
		// - only after unplug do they restart
		// except after waiting manually for the headset detect interrupt
		// then executing the cs_8409_external_device_unsolicited_response
		// the UNSOL responses seem to be reactivated

		// Im guessing the following code should be done explicitly
		// because we have not seen a cs42l83_read_status_and_clear_interrupt/cs42l83_disambiguate_ur_from_int

		// for some reason the stream id is set to 0 here

		//#2880: cs42l83_configure_int_mclk
		//#3152: cs42l83_power_onoff
		//#3216: cs42l83_configure_serial_port
		//#3376: cs42l83_output_set_input_sample_rate
		//#3504: cs42l83_setup_audio_output
		//#3680: cs42l83_buffers_onoff

		// this is essentially from cs42l83_headset_play_setup_on

		// weird - we have confirmed that this function seems to prevent restoration of
		// headphone plugin
		// NOTA BENE - unplugging does not cause UNSOL event
		//             unplugging followed by plugin if this function is commented does
		//             cause a plugin event

			       // headset_setup_SPDIF_output(codec); - presumably if is SPDIF setup

		// so we seem to get an UNSOL response at this point
		// - Im thinking we need to store this response

		// well Im not seeing this any more

		// and now turn off - this is now cs42l83_headset_disable

		//#3698: cs42l83_buffers_onoff
		//#3714: cs42l83_headset_power_off

		// and back on again
		// we dont set stream id, do full TDM setup and not enable the pin
		// then set stream id, turn on pin but partial TDM setup

		//#3920: cs42l83_configure_int_mclk
		//#4192: cs42l83_power_onoff
		//#4256: cs42l83_configure_serial_port
		//#4416: cs42l83_output_set_input_sample_rate
		//#4544: cs42l83_setup_audio_output
		//#4720: cs42l83_buffers_onoff

		// this is essentially from cs42l83_headset_play_setup_on

			       // headset_setup_SPDIF_output(codec); - presumably if is SPDIF setup

		//#4738: cs42l83_headphone_sense

		// this may be the same as one of the multiple headphone sense calls seen if no headset plugged in

		retval = cs42l83_headphone_sense(codec);

		if ((retval & 0x80)) {
			dev_info(hda_codec_dev(codec), "%s headphone still plugged in!!\n", __func__);

			//#4756: cs42l83_buffers_onoff
			//#4772: cs42l83_headset_power_off

			// there seems to be a delay here
			cs_8409_check_status(codec, 10, 10);

			// these codes are not cs42l83 i2c calls but plain HDA verbs

			// so I think we need to try performing the unsol responses here
			// - because we get a call to cs42l83_read_status_and_clear_interrupt/cs42l83_disambiguate_ur_from_int here
			// well all the evidence is altho these routines are called they dont actually do anything
			// - the cs_8409_read_status_and_clear_interrupt call does not see a TIP SENSE interrupt
			// (cf actually plugging in headphones post-boot)
			// - calling these routines does clear the interrupt - which may be its only function

			// we need to update headset_phase so we dont ignore headset unsol responses in the following call
			spec->headset_phase = 1;

			// Im now not seeing any UNSOL responses here

			// I think we need to block here - because clearing the interrupts causes interrupts!!
			// this is similar to cs_8409_cs42l83_unsolicited_response where we block
			// before calling the perform function

		} else {
			dev_info(hda_codec_dev(codec), "%s boot headphone REMOVED - UNIMPLEMENTED!!\n", __func__);
		}

		//#4946: cs42l83_read_status_and_clear_interrupt
		//#5164: cs42l83_disambiguate_ur_from_int

		// this is pointless here because we have no interrupt
		// - but this is what happens in the OSX log
		cs_8409_external_device_unsolicited_response(codec, 1, 0);

		//#5220: cs42l83_headphone_sense

		// so now just going to continue with the logged boot code
		// we get a lot of checks the headset is still plugged in
		// Im assuming if these detect the headset has been removed other things may happen
		// - this would likely be setting up the amps

		retval = cs42l83_headphone_sense(codec);

		if ((retval & 0x80)) {
		} else {
			dev_info(hda_codec_dev(codec), "%s boot headphone REMOVED 2 - UNIMPLEMENTED!!\n", __func__);
		}

		//#5238: cs42l83_headphone_sense

		retval = cs42l83_headphone_sense(codec);

		if ((retval & 0x80)) {

		} else {
			dev_info(hda_codec_dev(codec), "%s boot headphone REMOVED 3 - UNIMPLEMENTED!!\n", __func__);
		}

		//#5310: cs42l83_headphone_sense

		retval = cs42l83_headphone_sense(codec);

		if ((retval & 0x80)) {
			//#5328: cs42l83_headset_button_detect_interrupts_off
			//#5392: cs42l83_headset_set_hpout_clamp_disable

			// these 2 functions seem to be this function
			// - additional code is setting jack_present and an msleep
			cs_8409_plugin_handle_detect(codec);
		} else {
			dev_info(hda_codec_dev(codec), "%s boot headphone REMOVED 4 - UNIMPLEMENTED!!\n", __func__);
		}

		//#5424: cs42l83_headphone_sense

		retval = cs42l83_headphone_sense(codec);

		if ((retval & 0x80)) {
			// so there are 4 gpio status reads here
			// why 4??
			retval = read_gpio_status_check(codec);

			retval = read_gpio_status_check(codec);

			retval = read_gpio_status_check(codec);

			retval = read_gpio_status_check(codec);

			// why - this just sets nid 0x22 format???

			//cs_8409_linein_volume_setup_new(codec, 0x27)

		} else {
			dev_info(hda_codec_dev(codec), "%s boot headphone REMOVED 5 - UNIMPLEMENTED!!\n", __func__);
		}

		// we see a delay here - this is I think the equivalent of the 1800 msleep
		// in cs_8409_headset_plugin_event
		// should we just make it 1800??
		//msleep(1500); // what we actually see is 1470

		// so Im not seeing this delay now
		// only delay Im seeing is at 4772

		// the cs_8409_plugin_complete_detect function seems to encapsulate what happens next
		// - this functions ends with initiating headset detection
		//#5551: cs42l83_headphone_sense
		//#5567: cs42l83_complete_jack_detect
		//#5631: cs42l83_power_hs_bias_on
		//#5727: cs42l83_enable_hs_auto_int_on
		//#5759: cs42l83_unplug_interrupt_setup
		//#5807: cs42l83_set_hpout_pulldown_off
		//#5839: cs42l83_headset_detect_on

		cs_8409_plugin_complete_detect(codec,1);

		if (0) {

			cs42l83_complete_jack_detect(codec);

			cs42l83_power_hs_bias_on(codec);

			// this seems to be setting an interrupt on 0x131b for headset detect
			// - but there doesnt seem to be a delay anywhere here
			// so it must be immediately triggered

			cs42l83_enable_hs_auto_int_on(codec);

			cs42l83_unplug_interrupt_setup(codec);

			cs42l83_set_hpout_pulldown_off(codec);

			// so this seems to be the function which stops jack unsol events
		}

		// testing status checks to see if we get an interrupt
		// so at this point the status is still 0x27 - no interrupt
		// can we just use the msleep??
		//#5917: snd_hda_codec_read_check

		//#5919: snd_hda_codec_read_check
		// but here the status is 0x26 ie interrupt

		// I think we need to block here - because clearing the interrupts causes interrupts!!
		// this is similar to cs_8409_cs42l83_unsolicited_response where we block
		// before calling the perform function

		// add debug check status for a while

		// use new function to wait for the interrupt
		retval = cs_8409_wait_for_interrupt(codec, 1, 10);

		// so next we should get some unsol responses
		//#5923: cs42l83_read_status_and_clear_interrupt
		//#6117: cs42l83_disambiguate_ur_from_int
		//#6165:  cs_8409_headset_type_detect_event
		//#6165: cs42l83_enable_hs_auto_int_off
		//#6197: cs42l83_headset_type
		//#6213: cs42l83_unplug_headset_detect_off
		//#6277: cs42l83_set_hpout_pulldown_onoff
		//#6309: cs42l83_set_hpout_clamp_enable
		//#6341: cs42l83_enable_hsbias_auto_clamp_on
		//#6373: cs42l83_enable_hsbias_auto_clamp_off
		//#6421: cs42l83_power_hs_bias_off
		//#6517: cs42l83_setup_button_detect
		//#6709: cs42l83_power_hs_bias_button_on
		//#6805: cs42l83_enable_hsbias_auto_clamp_off1

		if (retval)
			cs_8409_external_device_unsolicited_response(codec, 1, 1);
		else
			dev_info(hda_codec_dev(codec), "%s boot - headset detect - FAILED TO GET INTERRUPT!!\n", __func__);

		// it does appear we assume no buttons if no mike

		if (spec->have_mike) {

			dev_info(hda_codec_dev(codec), "%s boot - end of headset detect - ready for button detect\n", __func__);

			// and yet again no UNSOL response just need to wait for the interrupt

			// so far only just hit the interrupt at the 9th count
			// so sleep for 5 ms - although no delay in logged ops - except maybe 2 ms
			// well 5 not long enough
			// neither does 10 seem to make a difference

			// use new function to wait for the interrupt
			// try increased sleep time
			// doesnt seem to change - always on 9th status read???
			// so far either 9 attempts (1ms wait) or 8 attempts (4ms wait) seen
			// so definitely seems require a number of reads
			// - not enough to just sleep for a while
			retval = cs_8409_wait_for_interrupt(codec, 4, 20);

			if (retval)
				cs_8409_external_device_unsolicited_response(codec, 1, 1);
			else
				dev_info(hda_codec_dev(codec), "%s boot - button detect - FAILED TO GET INTERRUPT!!\n", __func__);

			// as long as we do the cs_8409_external_device_unsolicited_response then unplug
			// seems to work
			// - so it appears if setup for headset detect or button detect and dont check and clear
			// the interrupts after then the unplug UNSOL response does not work

			dev_info(hda_codec_dev(codec), "%s boot - end of button detect\n", __func__);

			// cant figure where these came from
			// and the delay doesnt seem to exist

			//#6851: snd_hda_codec_read_check

			//// so the delay here seems to be around 130 ms
			//// with delay of 1 ms dont see any interrupts

			//#6853: snd_hda_codec_read_check

			//#6857: cs42l83_read_status_and_clear_interrupt
			//#7051: cs42l83_disambiguate_ur_from_int
			// cs_8409_headset_button_detect_event
			//#7099: cs42l83_handle_button_detect
			//#7243: cs42l83_mike_connected

			// cs_8409_perform_external_device_unsolicited_responses then calls cs_8409_plugin_event_continued
			// - but here we have a divergence from plugin post-boot
			// fixed up cs_8409_plugin_event_continued to only do things for plugin post-boot
			// - at boot we drop back to here
			// NOTA BENE - MUST set up the button interrupts here now - otherwise buttons wont work

		} else {
			dev_info(hda_codec_dev(codec), "%s boot - end of headset detect - NO BUTTONS\n", __func__);
		}

		//#7553:  cs_8409_enable_headset_streaming
		//#7553: cs43l83_headset_amp_format_setup
		//#7417:  cs42l83_headset_play_setup_on
		//#7417: cs42l83_configure_int_mclk
		//#7689: cs42l83_power_onoff
		//#7753: cs42l83_configure_serial_port
		//#7913: cs42l83_output_set_input_sample_rate
		//#8041: cs42l83_setup_audio_output
		//#8217: cs42l83_buffers_onoff

		// and yet again turn off
		//#8233: cs42l83_buffers_onoff
		//#8249: cs42l83_power_onoff
		//#8279: cs_8409_headset_amp_format_setup_disable

		// and back on again
		// we dont set stream id, do full TDM setup and not enable the pin
		// then set stream id, turn on pin but partial TDM setup

		//#8455:  cs42l83_headset_play_setup_on
		//#8455: cs42l83_configure_int_mclk
		//#8727: cs42l83_power_onoff
		//#8791: cs42l83_configure_serial_port
		//#8951: cs42l83_output_set_input_sample_rate
		//#9079: cs42l83_setup_audio_output
		//#9255: cs42l83_buffers_onoff

		//#9273: cs42l83_headphone_sense

		retval = cs42l83_headphone_sense(codec);

		if ((retval & 0x80)) {
			//#9287: cs_8409_intmike_stream_conn_off_disable
			//#9299: cs_8409_linein_stream_conn_off

			//       snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02270600
			//       snd_hda:     conv stream channel map 34 [('CHAN', 0), ('STREAMID', 0)]

			//       snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02370600
			//       snd_hda:     conv stream channel map 35 [('CHAN', 0), ('STREAMID', 0)]

			//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02270600
			//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02370600

			//#9324:  cs_8409_intmike_volume_setup
			//#9357: cs_8409_intmike_format_setup_disable
			//#9324:  cs_8409_linein_volume_setup
			//#9401: cs_8409_linein_format_setup_disable

			// as the following relates to the headset mike
			// Im guessing we should only do it if we have a mike
			// spec->have_mike should have been set above
			// by the unsol response function cs_8409_headset_type_detect_event

			if (spec->have_mike) {
				// for the moment deciding to do this only if have mike in headset

				// this is just calling this routine
				//cs_8409_headset_mike_setup_nouse

				cs_8409_intmike_linein_disable(codec);

				//#9412:  cs42l83_headset_mike_format_setup_enable
				//#9451: cs42l83_input_set_output_sample_rate
				//#9579: cs42l83_mike_setup_audio_input
				//#9643: cs42l83_mike_enable

				// this is part of cs_8409_enable_headset_mike_streaming

				// whats missing is no actual cs42l83 power on calls
				// or unmuting input

				cs42l83_headset_mike_format_setup_enable(codec, 0, 1);

				cs42l83_input_set_output_sample_rate(codec);

				cs42l83_mike_setup_audio_input(codec);

				cs42l83_mike_enable(codec);

				// and disable it all!!
				// this is part of cs_8409_disable_headset_mike_streaming
				// as usual disabling this duplicate setup

				//#9675: cs42l83_mike_disable
				//#9705: cs42l83_headset_mike_format_setup_disable

				//#9738:  cs42l83_headset_mike_format_setup_enable
				//#9738:  cs42l83_headset_mike_format_setup_enable
				//#9810: cs42l83_input_set_output_sample_rate
				//#9938: cs42l83_mike_setup_audio_input
				//#10002: cs42l83_mike_enable

				// why duplicate setup - 1st dont set stream id/pin
				// 2nd set stream id and pin

				//#10032: snd_hda_codec_read_check
				//#10033: snd_hda_codec_read_check

				//// this doesnt make sense double read of nid 0x3c then double set??
				//retval = snd_hda_codec_read_check(codec, 0x3c, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000020, 25649); // 0x03cf0700
				//retval = snd_hda_codec_read_check(codec, 0x3c, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000020, 25650); // 0x03cf0700
				//snd_hda_codec_write(codec, 0x3c, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000020); // 0x03c70720
				////       snd_hda:     60 ['AC_PINCTL_IN_EN']
				//snd_hda_codec_write(codec, 0x3c, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000020); // 0x03c70720
				////       snd_hda:     60 ['AC_PINCTL_IN_EN']

				cs42l83_headset_mike_pin_enable(codec);

				// is this a good position to switch the inputs??
				// just before enable buttons is where we switch inputs on plugin
				// note that this is just telling Alsa the input source has changed
				// - no changes to audio setup at all
				switch_input_src(codec);

				//#10040:  cs_8409_headset_mike_buttons_enable
				//#10040: cs42l83_configure_headset_button_interrupts
				//#10088: cs42l83_enable_hsbias_auto_clamp_off2
				//#10136: cs42l83_enable_hsbias_auto_clamp_on3

				cs_8409_headset_mike_buttons_enable(codec);

				//#10166: snd_hda_codec_read_check
				//#10168: snd_hda_codec_read_check

				//#10172: cs42l83_read_status_and_clear_interrupt
				//#10362: snd_hda_codec_read_check
				//#10366: cs42l83_read_status_and_clear_interrupt
				//#10556: snd_hda_codec_read_check
				//#10560: cs42l83_disambiguate_ur_from_int

				// so at the moment Im not seeing any interrupts here
				retval = cs_8409_wait_for_interrupt(codec, 1, 20);

				// ah - the check for interrupts is for button handling
				// actually I dont get any of this - I have checked and we have setup the
				// unsol event handler by the time cs_8409_boot_setup_real is called
				// so we shouldnt have to be manually waiting for interrupts???

				if (retval)
					cs_8409_external_device_unsolicited_response(codec, 1, 1);
				else
					dev_info(hda_codec_dev(codec), "%s boot - button press response - no interrupts\n ", __func__);

				// so a dump_stack at this shows we are under
				// cs8409_driver_init
				//   (... -> hda_codec_driver_probe -> patch_cs8409 -> cs_8409_real_config -> cs_8409_boot_setup_real)
				// - maybe UNSOL responses not activated till out of the init routine??

				// we get some calls to cs42l83_read_status_and_clear_interrupt/cs42l83_disambiguate_ur_from_int here
				// clear out any stored unsol responses

				// I think we need to block here - because clearing the interrupts causes interrupts!!
				// this is similar to cs_8409_cs42l83_unsolicited_response where we block
				// before calling the perform function
				spec->block_unsol = 1;

				cs_8409_perform_external_device_unsolicited_responses(codec);

				spec->block_unsol = 0;

			}

		} else {
			dev_info(hda_codec_dev(codec), "%s boot headphone REMOVED 6 - UNIMPLEMENTED!!\n", __func__);
		}

		spec->block_unsol = 0;

		dev_info(hda_codec_dev(codec), "%s boot CURRENT IMPLEMENTATION END!!\n", __func__);

	} else {

		// NOTE - OSX sets a stream format here but a null (ie 0) stream id
		//        on linux we set the OSX format - it will be updated with actual stream format later

		cs_8409_setup_TDM_amps12(codec, 1, 1);

		cs_8409_setup_amps12(codec, 0);

		cs_8409_setup_TDM_amps34(codec,  1);

		cs_8409_setup_amps34(codec, 0);

		cs_8409_sync_converters_on(codec, 1);

		cs_8409_sync_converters_off(codec, 1);

		cs_8409_disable_amps12(codec);

		cs_8409_disable_TDM_amps12(codec);

		cs_8409_disable_amps34(codec);

		cs_8409_disable_TDM_amps34(codec);

		// I think Im going to disable the following as we appear to have a stream id here
		// but under linux we do not have a stream at all at this boot stage

		if (0) {

			// this is not quite correct - cs_8409_setup_TDM_amps12 will write a null stream id etc
			// but the actual logged version does not - although does update format
			// this may be because Apple is caching the stream format/id similar to linux and
			// at this point we have already set a null stream id - but because the above disable also
			// cleared the format we get a re-setup of the the format
			cs_8409_setup_TDM_amps12(codec, 1, 1);

			cs_8409_disable_amps12(codec);

			// see above notes for putative_enable1_TDM_6462
			cs_8409_setup_TDM_amps34(codec, 1);

			cs_8409_disable_amps34(codec);

			// so this does not set the channel id for node 0x03 to 0x2 but cs_8409_sync_converters_on does
			// is this significant??
			// does suggest this function reads the initial stream id then rewrites at the end
			cs_8409_sync_converters_on(codec, 1);

			// so this also not quite same - we actually have a stream id here on OSX
			// but at the boot stage dont think we have this in linux
			cs_8409_setup_TDM_amps12(codec, 1, 1);

			cs_8409_setup_amps12(codec, 0);

			// see above
			cs_8409_setup_TDM_amps34(codec, 1);

			cs_8409_setup_amps34(codec, 0);

			cs_8409_sync_converters_on(codec, 1);

			// I dont get this - sync_converters3 sets the stream id/channel id to non-zero
			// but here when we read the stream id/channel id its 0??
			cs_8409_sync_converters_off(codec, 1);

			cs_8409_disable_amps12(codec);

			cs_8409_disable_TDM_amps12(codec);

			cs_8409_disable_amps34(codec);

			cs_8409_disable_TDM_amps34(codec);
		}

		// this is best guess what these volume functions are doing
		// as from the log there is no change in output volume or muting
		// - but if already unmuted thats what you would expect

		cs_8409_intmike_volume_unmute_nouse(codec);

		cs_8409_linein_volume_unmute_nouse(codec);

		cs_8409_intmike_volume_unmute_nouse(codec);

		cs_8409_linein_volume_unmute_nouse(codec);

		cs_8409_inputs_power_nids_off(codec);

		// why 3 reads here - they seem to return the exact same data each time
		retval = read_gpio_status_check(codec);

		retval = read_gpio_status_check(codec);

		retval = read_gpio_status_check(codec);

		retval = cs42l83_headphone_sense(codec);

		cs_8409_intmike_volume_unmute_nouse(codec);

		cs_8409_linein_volume_unmute_nouse(codec);

		cs_8409_intmike_volume_unmute_nouse(codec);

		cs_8409_linein_volume_unmute_nouse(codec);

		retval = cs42l83_headphone_sense(codec);

		retval = cs42l83_headphone_sense(codec);

		cs_8409_really_update_stream_format(codec, spec->intmike_adc_nid, 1, 0, 0);

		cs_8409_linein_volume_setup(codec, 0x27);

		cs_8409_linein_format_setup_disable(codec);

		cs_8409_intmike_stream_conn_off(codec);

		cs_8409_linein_stream_conn_off(codec);

		cs_8409_intmike_stream_off_nid(codec);

		cs_8409_linein_stream_off_nid(codec);

		cs_8409_volume_set(codec, spec->intmike_adc_nid, 0x33);

		cs_8409_volume_set(codec, spec->linein_amp_nid, 0x33);
	}

	return 0;
}

void cs_8409_play_real(struct hda_codec *codec)
{
	int retval;
	struct cs8409_apple_spec *spec = codec->spec;

	// so I have seen an UNSOL response in cs_8409_playstop_real
	// which suggests need to block responses here
	spec->block_unsol = 1;

	retval = snd_hda_codec_read_check(codec, 0x00, 0, AC_VERB_PARAMETERS, 0x00000000, 0x10138409, 1); // 0x000f0000

	//snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_POWER_STATE, 0x00000000); // 0x00170500
	hda_set_node_power_state(codec, codec->core.afg, AC_PWRST_D0);

	play_setup_TDM_amps12(codec, 1);

	play_setup_amps12(codec);

	play_setup_TDM_amps34(codec);

	play_setup_amps34(codec);

	play_sync_converters_on(codec);

	cs_8409_perform_external_device_unsolicited_responses(codec);

	spec->block_unsol = 0;

}

void cs_8409_amps_disable_streaming(struct hda_codec *codec)
{

	playstop_sync_converters_off(codec);

	playstop_disable_amps12(codec);

	playstop_disable_TDM_amps12(codec);

	playstop_disable_amps34(codec);

	playstop_disable_TDM_amps34(codec);

	// for some reason Apple duplicates the amp disable here??

	playstop_disable_amps12(codec);

	playstop_disable_amps34(codec);

}

void cs_8409_playstop_real(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;

	// so I have seen an UNSOL response in cs_8409_playstop_real
	// which suggests need to block responses here
	spec->block_unsol = 1;

	cs_8409_amps_disable_streaming(codec);

	//snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_POWER_STATE, 0x00000003); // 0x00170503
	hda_set_node_power_state(codec, codec->core.afg, AC_PWRST_D3);

	// weird - why set the inputs to powered off when stop playing??
	cs_8409_inputs_power_nids_off(codec);

	cs_8409_perform_external_device_unsolicited_responses(codec);

	spec->block_unsol = 0;

}

static void cs_8409_capture_real(struct hda_codec *codec)
{
	int retval;
	struct cs8409_apple_spec *spec = codec->spec;

	// so I have seen an UNSOL response in cs_8409_playstop_real
	// which suggests need to block responses here
	spec->block_unsol = 1;

	retval = snd_hda_codec_read_check(codec, 0x00, 0, AC_VERB_PARAMETERS, 0x00000000, 0x10138409, 1); // 0x000f0000

	//snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_POWER_STATE, 0x00000000); // 0x00170500
	hda_set_node_power_state(codec, codec->core.afg, AC_PWRST_D0);

	// NOTE - there are big ordering issues here
	//        - here we setup the speaker output before the internal mike
	//        - this maybe because Quicktime defaults to enabling play when recording
	//        unfortunately looks as tho linux tends to open the capture stream before the playback stream
	//        - so going to ignore this here

	cs_8409_inputs_power_nids_on(codec);

	cs_8409_intmike_format_setup_enable(codec, 0x4031, 0);

	cs_8409_intmike_volume_setup(codec, 0x27);
	cs_8409_intmike_stream_on_nid(codec);

	cs_8409_intmike_volume_unmute(codec);
	cs_8409_linein_volume_unmute(codec);

	// so here we get AMP_GAIN_MUTE setups but nothing changes
	// - so either this is a volume update with no change or unmute with no change
	// - which to do with??
	//cs_8409_intmike_volume_setup - (no change)
	//cs_8409_linein_volume_setup - (no change)

	cs_8409_perform_external_device_unsolicited_responses(codec);

	spec->block_unsol = 0;

}

static void cs_8409_intmike_stream_reset_nid_on(struct hda_codec *codec)
{

	struct cs8409_apple_spec *spec = codec->spec;

	// OK dont get this  - we turn the stream back on for the internal mike
	// - but assume pin is OK??
	// now think these 2 functions are resetting to original state - which happens
	// to be stream on for intmike and stream off for linein
	// NOT PROPERLY FUNCTIONAL YET!!!!!

//      retval = snd_hda_codec_read_check(codec, 0x22, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000000, 23); // 0x022f0500
	hda_set_node_power_state(codec, spec->intmike_adc_nid, AC_PWRST_D0);

	snd_hda_codec_write(codec, spec->intmike_adc_nid, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000010); // 0x02270610
	//snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000010); // 0x02270610
//      snd_hda:     conv stream channel map 34 [('CHAN', 0), ('STREAMID', 1)]

}

static void cs_8409_linein_stream_reset_nid_off(struct hda_codec *codec)
{

	struct cs8409_apple_spec *spec = codec->spec;

	// OK dont get this  - we turn the stream off for the linein
	// - but assume pin is OK??

//      retval = snd_hda_codec_read_check(codec, 0x23, 0, AC_VERB_GET_POWER_STATE, 0x00000000, 0x00000000, 25); // 0x023f0500
	hda_set_node_power_state(codec, spec->linein_amp_nid, AC_PWRST_D0);

	snd_hda_codec_write(codec, spec->linein_amp_nid, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02370600
	//snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x02370600
//      snd_hda:     conv stream channel map 35 [('CHAN', 0), ('STREAMID', 0)]

}

static void cs_8409_capturestop_real(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;

	// so I have seen an UNSOL response in cs_8409_playstop_real
	// which suggests need to block responses here
	spec->block_unsol = 1;

	cs_8409_intmike_stream_conn_off(codec);
	cs_8409_linein_stream_conn_off(codec);

	// this I think is re-setting to pre-capture state
	// THIS NEEDS FIXING!!!
	cs_8409_intmike_stream_reset_nid_on(codec);
	cs_8409_linein_stream_reset_nid_off(codec);

	cs_8409_intmike_volume_set(codec, 0x27);
	cs_8409_intmike_volume_mute(codec);

	// we also reset the pin - however no change to volume or unmute
	// so cant say if should just set unmute or set volume to 0
	// just choosing one
	cs_8409_volume_set(codec, spec->intmike_nid, 0x00);

	cs_8409_intmike_format_setup_disable(codec);

	cs_8409_linein_volume_set(codec, 0x27);
	cs_8409_linein_volume_mute(codec);

	// we also reset the pin - however no change to volume or unmute
	// so cant say if should just set unmute or set volume to 0
	// just choosing one
	cs_8409_volume_set(codec, spec->linein_nid, 0x00);

	cs_8409_linein_format_setup_disable(codec);

	cs_8409_inputs_power_nids_off(codec);

	// using Quicktime we get a play disable when we stop recording
	//cs_8409_sync_converters_off
	//cs_8409_disable_amps12
	//cs_8409_disable_TDM_amps12
	//cs_8409_disable_amps34
	//cs_8409_disable_TDM_amps34
	//cs_8409_disable_amps12
	//cs_8409_disable_amps34

	cs_8409_perform_external_device_unsolicited_responses(codec);

	spec->block_unsol = 0;

}

static void cs42l83_headset_amp_setup_TDM_sample_rate(struct hda_codec *codec)
{

//      snd_hda: # AppleHDATDMBusManagerCS8409::setSampleRate:
	//snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0001, 0x0200, 0xffff, 0x00000200, 0, 3892 ); // coef write mask 3892
	//snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0008, 0x0042, 0xffff, 0x00000040, 0, 3898 ); // coef write mask 3898
	//snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0007, 0x10ff, 0xffff, 0x000010ff, 0, 3904 ); // coef write mask 3904

	// we need to use proper masked versions here - in particular for register 1 which seems to be enabling the Audio Serial Port
	// for the subsystems and bits 0x7f need to pass thro here

	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0001, 0x0200, 0x0380, 0x00000200, 0, 0); // coef write mask 25

	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0008, 0x0042, 0x0007, 0x00000040, 0, 0); // coef write mask 3898
	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0007, 0x10ff, 0x01ff, 0x000010ff, 0, 0); // coef write mask 3904

}

static void cs42l83_headset_amp_setup_TDM_proper(struct hda_codec *codec, int full)
{
	int ret_coef0 = 0;
	int new_coef0 = 0;
	int ret_coef1 = 0;
	int new_coef1 = 0;
	int ret_coef71 = 0;
	int new_coef71 = 0;

	if (full) {
		ret_coef1 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0001, 0x0000, 0x00000200, 0); //   coef read 3810
		new_coef1 = (ret_coef1 & 0xffff); // not clear what this is setting - no difference between read and write so far
					// however if used in different places the actual value may be different

		//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0001, 0x0200, 0x00000000, 3814 ); //   coef write 3814
		snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0001, new_coef1, 0x00000000, 3814); //   coef write 3814
	}

	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0015, 0xaaaa, 0xffff, 0x0000aaaa, 0, 0); // coef write mask 3819
	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0014, 0x0100, 0xffff, 0x00000000, 0, 0); // coef write mask 3825

//      snd_hda: # AppleHDATDMBusManagerCS8409::setupTDMPath:
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0029, 0x0000, 0x00008000, 0); //   coef read 3832
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0029, 0x0800, 0x00000000, 0); //   coef write 3836
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x002a, 0x0000, 0x00008000, 0); //   coef read 3840
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x002a, 0x0820, 0x00000000, 0); //   coef write 3844

	if (full) {
		ret_coef0 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0000, 0x0000, 0x00009000, 0); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef read 3848
		snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0000, 0xb000, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef write 3852
		snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0007, 0x0000, 0x000028ff, 0); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef read 3856
		snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0007, 0x10ff, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef write 3860
		snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0000, 0xb000, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef write 3864
		// NOTA BENE - here we update from 0x9000 to 0xb000 - which is then never removed - even after unplugging headphones!!
		new_coef0 = ret_coef0 | 0x2000;
		snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0000, new_coef0, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::setupTDMPath  coef write 76

	//      snd_hda: # AppleHDATDMBusManagerCS8409::setupTDMPath:
		snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0006, 0x8000, 0xffff, 0x00008000, 0, 0); // coef write mask 3868
		snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0008, 0x0040, 0xffff, 0x00000000, 0, 0); // coef write mask 3874

	//      snd_hda: # AppleHDATDMBusManagerCS8409::setupTDMPath:
	//      snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0082, 0xa801, 0xffff, 0x00000001, 0, 3880 ); // coef write mask 3880
		snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0082, 0xa800, 0x0000, 0x00000001, 0xa801, 0); // coef write mask 3880

		snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0002, 0x0280, 0xffff, 0x00000280, 0, 0); // coef write mask 3886

		cs42l83_headset_amp_setup_TDM_sample_rate(codec);

		ret_coef1 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0001, 0x0000, 0x00000200, 0); //   coef read 3910

		new_coef1 = (ret_coef1 & 0xffff) | 0x40;

		//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0001, 0x0240, 0x00000000, 3914 ); //   coef write 3914
		snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0001, new_coef1, 0x00000000, 0); //   coef write 3914
	}

//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR: AppleHDATDMBusManagerCS8409::tdmInUse:
	//snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0019, 0x0000, 0x00008800, 3918 ); //   coef read 3918

	tdm_in_use(codec, 1);

//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR:
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x006c, 0x001f, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 3987

	ret_coef71 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0071, 0x0000, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef read 3991

	new_coef71 = (ret_coef71 & 0xffff) | 0x800f;

	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0071, 0x800f, 0x00000000, 3995 ); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 3995
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0071, new_coef71, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 3995

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, 0x7f0, 0x00b6); // AppleHDATDMBusManagerCS8409::configureTDMUR  write verb 3998

}

void cs43l83_headset_amp_format_setup(struct hda_codec *codec, int set_stream_id, int full)
{
	int retval;

	//snd_hda_codec_write(codec, 0x0a, 0, AC_VERB_SET_STREAM_FORMAT, 0x00004031); // 0x00a24031
//      snd_hda:     stream format 10 [('CHAN', 2), ('RATE', 44100), ('BITS', 24), ('RATE_MUL', 1), ('RATE_DIV', 1)]

	if (set_stream_id) {
		//snd_hda_codec_write(codec, 0x0a, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000010); // 0x00a70610
	//      snd_hda:     conv stream channel map 10 [('CHAN', 0), ('STREAMID', 1)]

		// using the stored stream parameters update nid 0x0a stream parameters
		// we have limited the allowed formats so should only have working formats here
		cs_8409_really_update_stream_format(codec, 0x0a, 1, 2, 0);
	} else {
	       snd_hda_codec_write(codec, 0x0a, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x00a70600
	}

	cs42l83_headset_amp_setup_TDM_proper(codec, full);

	retval = snd_hda_codec_read_check(codec, 0x2c, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000000, 0); // 0x02cf0700

	snd_hda_codec_write(codec, 0x2c, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x000000c0); // 0x02c707c0
//      snd_hda:     44 ['AC_PINCTL_OUT_EN', 'AC_PINCTL_HP_EN']

}

static void cs_8409_headset_amp_disable_TDM_proper(struct hda_codec *codec, int full)
{
	int ret_coef1 = 0;
	int new_coef1 = 0;
	int ret_coef71 = 0;
	int new_coef71 = 0;

	// AppleHDATDMBusManagerCS8409::disableTDMPath
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0029, 0x0000, 0x00000800, 0); //   coef read 2411
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0029, 0x8800, 0x00000000, 0); //   coef write 2415
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x002a, 0x0000, 0x00000820, 0); //   coef read 2419
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x002a, 0x8820, 0x00000000, 0); //   coef write 2423

	if (full) {
		// AppleHDATDMBusManagerCS8409::disableTDMPath
	//      snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0082, 0x0001, 0xffff, 0x0000a801, 0, 2185 ); // coef write mask 2185
		snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0082, 0x0000, 0xa800, 0x0000a801, 0x0001, 0); // coef write mask 2185

		// AppleHDATDMBusManagerCS8409::disableTDMPath
		ret_coef1 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0001, 0x0000, 0x00000240, 0); // AppleHDATDMBusManagerCS8409::disableTDMPath  coef read 2191

		new_coef1 = (ret_coef1 & 0xffbf); // clear our 0x40 bit

		//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0001, 0x0200, 0x00000000, 2195 ); // AppleHDATDMBusManagerCS8409::disableTDMPath  coef write 2195
		snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0001, new_coef1, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::disableTDMPath  coef write 2195
	}

//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR: AppleHDATDMBusManagerCS8409::tdmInUse:
	//snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0019, 0x0000, 0x00008800, 2427 ); //   coef read 2427

	tdm_in_use(codec, 301);

	if (full) {
		snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0071, 0x0000, 0x00000000, 0); //   coef write 2452

		snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, 0x7f0, 0x00000000);
	} else {

	//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR:
		snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x006c, 0x001f, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 2624

		ret_coef71 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0071, 0x0000, 0x0000800f, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef read 2628

		new_coef71 = (ret_coef71 & 0xffff); // why doesnt this really disable this here??

		snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0071, 0x800f, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 2632

		snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, 0x7f0, 0x00b6); // AppleHDATDMBusManagerCS8409::configureTDMUR  write verb 2635

	}

}

void cs_8409_headset_amp_format_setup_disable(struct hda_codec *codec, int full)
{
	int retval;

	cs_8409_headset_amp_disable_TDM_proper(codec, full);

	// note this means the cached stream data in the hda_cvt_setup struct will now be inconsistent
	// we need to ensure any further stream format re-update MUST be a forced update
	// still not clear if should be calling eg __snd_hda_codec_cleanup_stream

	snd_hda_codec_write(codec, 0x0a, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x00a70600
//      snd_hda:     conv stream channel map 10 [('CHAN', 0), ('STREAMID', 0)]

	snd_hda_codec_write(codec, 0x0a, 0, AC_VERB_SET_STREAM_FORMAT, 0x00000000); // 0x00a20000
//      snd_hda:     stream format 10 [('CHAN', 1), ('RATE', 48000), ('BITS', 8), ('RATE_MUL', 1), ('RATE_DIV', 1)]

	retval = snd_hda_codec_read_check(codec, 0x2c, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000040, 0); // 0x02cf0700

	snd_hda_codec_write(codec, 0x2c, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000000); // 0x02c70700
//      snd_hda:     44 []

}

void cs42l83_headset_mike_format_setup_enable(struct hda_codec *codec, int nullformat, int full)
{
	int retval = 0;
	int ret_coef71 = 0;
	int new_coef71 = 0;

	//snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_STREAM_FORMAT, 0x00004031); // 0x01a24031
//      snd_hda:     stream format 26 [('CHAN', 2), ('RATE', 44100), ('BITS', 24), ('RATE_MUL', 1), ('RATE_DIV', 1)]

	//snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000010); // 0x01a70610
//      snd_hda:     conv stream channel map 26 [('CHAN', 0), ('STREAMID', 1)]

	if (nullformat) {
		// note that 0x4031 is Apples fixed format - but this is for plugin stage when we have
		// not defined any format yet so just use it - we overwrite below when actually play
		snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_STREAM_FORMAT, 0x00004031); // 0x01a24031
		if (full)
			snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000010); // 0x01a70610
	} else {
		if (full)
			cs_8409_really_update_stream_format(codec, 0x1a, 1, 2, 0);
		else
			cs_8409_really_update_stream_format(codec, 0x1a, 1, 0, 0);
	}

	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0012, 0xaccc, 0xffff, 0x0000cccc, 0, 0); // coef write mask 12272
	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0011, 0x0001, 0xffff, 0x00000000, 0, 0); // coef write mask 12278

//      snd_hda: # AppleHDATDMBusManagerCS8409::setupTDMPath:
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0049, 0x0000, 0x00008000, 0); //   coef read 12285
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0049, 0x0800, 0x00000000, 0); //   coef write 12289
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x004a, 0x0000, 0x00008000, 0); //   coef read 12293
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x004a, 0x0820, 0x00000000, 0); //   coef write 12297

//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR: AppleHDATDMBusManagerCS8409::tdmInUse:
	//snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0019, 0x0000, 0x00008800, 12301 ); //   coef read 12301

	tdm_in_use(codec, 201);

//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR:
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x006c, 0x001f, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 12370

	ret_coef71 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0071, 0x0000, 0x0000800f, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef read 12374

	new_coef71 = (ret_coef71 & 0xffff);

	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0071, 0x800f, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 12378

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, 0x7f0, 0x00b6); // AppleHDATDMBusManagerCS8409::configureTDMUR  write verb 12381

	if (full) {

		retval = snd_hda_codec_read_check(codec, 0x3c, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000000, 0); // 0x03cf0700

		snd_hda_codec_write(codec, 0x3c, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000020); // 0x03c70720
	//      snd_hda:     60 ['AC_PINCTL_IN_EN']
	}

}

static void cs42l83_headset_mike_format_setup_disable(struct hda_codec *codec)
{
	int retval = 0;
	int ret_coef71 = 0;
	int new_coef71 = 0;

//      snd_hda: # AppleHDATDMBusManagerCS8409::disableTDMPath:
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0049, 0x0000, 0x00000800, 0); //   coef read 13141
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0049, 0x8800, 0x00000000, 0); //   coef write 13145
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x004a, 0x0000, 0x00000820, 0); //   coef read 13149
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x004a, 0x8820, 0x00000000, 0); //   coef write 13153

//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR: AppleHDATDMBusManagerCS8409::tdmInUse:
	//snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0019, 0x0000, 0x00008800, 13157 ); //   coef read 13157

	tdm_in_use(codec, 202);

//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR:
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x006c, 0x001f, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 13226

	ret_coef71 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0071, 0x0000, 0x0000800f, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef read 13230

	new_coef71 = (ret_coef71 & 0xffff);

	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0071, 0x800f, 0x00000000, 13234 ); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 13234
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0071, new_coef71, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 13234

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, 0x7f0, 0x00b6); // AppleHDATDMBusManagerCS8409::configureTDMUR  write verb 13237

	// note this means the cached stream data in the hda_cvt_setup struct will now be inconsistent
	// we need to ensure any further stream format re-update MUST be a forced update
	// still not clear if should be calling eg __snd_hda_codec_cleanup_stream

	snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x01a70600
//      snd_hda:     conv stream channel map 26 [('CHAN', 0), ('STREAMID', 0)]

	snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_STREAM_FORMAT, 0x00000000); // 0x01a20000
//      snd_hda:     stream format 26 [('CHAN', 1), ('RATE', 48000), ('BITS', 8), ('RATE_MUL', 1), ('RATE_DIV', 1)]

	retval = snd_hda_codec_read_check(codec, 0x3c, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000020, 0); // 0x03cf0700

	snd_hda_codec_write(codec, 0x3c, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000000); // 0x03c70700
//      snd_hda:     60 []

}

// NOTE - the following routines NOT fixed up yet - currently not used!!

// NOTE - end of unfixed routines

static void cs42l83_headset_amp_disable_and_mike_disable_TDM_proper(struct hda_codec *codec)
{
	int ret_coef1 = 0;
	int new_coef1 = 0;
	int ret_coef71 = 0;
	int new_coef71 = 0;

//      snd_hda: # AppleHDATDMBusManagerCS8409::disableTDMPath:
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0049, 0x0000, 0x00000800, 0); //   coef read 10459
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0049, 0x8800, 0x00000000, 0); //   coef write 10463
	snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x004a, 0x0000, 0x00000820, 0); //   coef read 10467
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x004a, 0x8820, 0x00000000, 0); //   coef write 10471

	// this section is actually disabling the headset amp TDM setup

//      snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0082, 0x5400, 0xffff, 0x0000fc00, 0, 10475 ); // coef write mask 10475
	snd_hda_coef_item_masked(codec, 2, CS8409_VENDOR_NID, 0x0082, 0x0000, 0xa800, 0x0000fc00, 0x5400, 0); // coef write mask 10475

	ret_coef1 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0001, 0x0000, 0x00000260, 0); // AppleHDATDMBusManagerCS8409::disableTDMPath  coef read 10481

	new_coef1 = (ret_coef1 & 0xffbf);

	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0001, 0x0220, 0x00000000, 10485 ); // AppleHDATDMBusManagerCS8409::disableTDMPath  coef write 10485
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0001, new_coef1, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::disableTDMPath  coef write 10485

	// end of section disabling the headset amp TDM setup

//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR: AppleHDATDMBusManagerCS8409::tdmInUse:
	//snd_hda_coef_item(codec, 0, CS8409_VENDOR_NID, 0x0019, 0x0000, 0x00000800, 10489 ); //   coef read 10489

	tdm_in_use(codec, 401);

//      snd_hda: # AppleHDATDMBusManagerCS8409::configureTDMUR:
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x006c, 0x001f, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 10494

	ret_coef71 = snd_hda_coef_item_check(codec, 0, CS8409_VENDOR_NID, 0x0071, 0x0000, 0x0000c00f, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef read 10498

	new_coef71 = (ret_coef71 & 0xffff); // I dont get this - it doesnt seem to change this at all - but this is for output 0x800f (headphones) or 0x400f (amps)

	//snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0071, 0xc00f, 0x00000000, 10502 ); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 10502
	snd_hda_coef_item(codec, 1, CS8409_VENDOR_NID, 0x0071, new_coef71, 0x00000000, 0); // AppleHDATDMBusManagerCS8409::configureTDMUR  coef write 10502

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, 0x7f0, 0x00b6); // AppleHDATDMBusManagerCS8409::configureTDMUR  write verb 10505

}

void cs42l83_headset_amp_disable_and_mike_format_setup_disable(struct hda_codec *codec)
{
	int retval = 0;

	cs42l83_headset_amp_disable_and_mike_disable_TDM_proper(codec);

	// note this means the cached stream data in the hda_cvt_setup struct will now be inconsistent
	// we need to ensure any further stream format re-update MUST be a forced update
	// still not clear if should be calling eg __snd_hda_codec_cleanup_stream

	snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000); // 0x01a70600
//      snd_hda:     conv stream channel map 26 [('CHAN', 0), ('STREAMID', 0)]

	snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_STREAM_FORMAT, 0x00000000); // 0x01a20000
//      snd_hda:     stream format 26 [('CHAN', 1), ('RATE', 48000), ('BITS', 8), ('RATE_MUL', 1), ('RATE_DIV', 1)]

	retval = snd_hda_codec_read_check(codec, 0x3c, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00000000, 0x00000020, 0); // 0x03cf0700
	snd_hda_codec_write(codec, 0x3c, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00000000); // 0x03c70700
//      snd_hda:     60 []

}

void cs42l83_headset_play_setup_on(struct hda_codec *codec)
{

	// this is the function AppleHDATDM_CS42L83::enable for turning on headset for play

	// the following section is often done pre-play

	cs42l83_configure_int_mclk(codec);

	cs42l83_power_onoff(codec, 1);

	cs42l83_configure_serial_port(codec);

	// the following section always done before play

	cs42l83_output_set_input_sample_rate(codec);

	cs42l83_setup_audio_output(codec);

	       // headset_setup_SPDIF_output(codec); - presumably if is SPDIF setup

	cs42l83_buffers_onoff(codec, 1);

}

static void cs42l83_headset_disable(struct hda_codec *codec)
{

	cs42l83_buffers_onoff(codec, 0);

	cs42l83_power_onoff(codec, 0);

}

void cs_8409_enable_headset_streaming(struct hda_codec *codec)
{

	// debug status check - 0x27 here
	read_gpio_status_check(codec);

	// dont really have any idea how to get to here
	// Im guessing from messaging

	cs43l83_headset_amp_format_setup(codec, 1, 1);

	cs42l83_headset_play_setup_on(codec);

	// power on audio output
	cs42l83_set_power_state_on(codec, 0);

	cs42l83_headset_enable_on(codec);

}

static void cs_8409_disable_headset_streaming(struct hda_codec *codec)
{

	// why do we do the headphone disable/poweroff codec output twice??
	// but we do - repeatedly seen in logs

	cs42l83_headset_enable_off(codec);

	cs42l83_power_off_codec_output(codec);

	cs42l83_headset_disable(codec);

	cs_8409_headset_amp_format_setup_disable(codec, 1);

	cs42l83_headset_enable_off(codec);

	cs42l83_power_off_codec_output(codec);

}

void cs_8409_headplay_real(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;

	if (spec->headset_enable == 0) {
	} else if (spec->headset_enable == 1) {
		cs_8409_enable_headset_streaming(codec);
	} else if (spec->headset_enable == 2) {
	}

}

void cs_8409_headplaystop_real(struct hda_codec *codec)
{

	cs_8409_disable_headset_streaming(codec);

}

static void cs_8409_enable_headset_mike_streaming(struct hda_codec *codec)
{

	// debug status check - 0x27 here
	read_gpio_status_check(codec);

	// dont really have any idea how to get to here
	// Im guessing from messaging

	// NOTE - there are big ordering issues here
	//        - here we setup the headphone output before the mike
	//        - this maybe because Quicktime defaults to enabling play when recording
	//        unfortunately looks as tho linux tends to open the capture stream before the playback stream
	//        - so going to ignore this here

	// this sets up the headphones
	// note this only does a partial headset amp setup compared to a base headset play
	// power on audio output

	//if (!(retval & 0x80))
	//if (!(retval & 0x80))

	////cs43l83_headset_amp_format_setup_partial

	cs42l83_headset_mike_format_setup_enable(codec, 0, 1);

	cs42l83_input_set_output_sample_rate(codec);

	cs42l83_mike_setup_audio_input(codec);

	cs42l83_mike_enable(codec);

	// power on the codec/audio input
	cs42l83_set_power_state_on(codec, 1);

	// unmute audio input
	cs42l83_headset_mike_adc_unmutevol(codec, 1);

	// for partial setup only

	// this is all done in the capture hook after this call
	//read_gpio_status
	//read_gpio_status
	//cs42l83_read_status_and_clear_interrupt
	//read_gpio_status
	//cs42l83_disambiguate_ur_from_int
	//read_gpio_status
	//read_gpio_status
	//read_gpio_status
	//cs42l83_read_status_and_clear_interrupt
	//read_gpio_status
	//cs42l83_disambiguate_ur_from_int
	//read_gpio_status

}

static void cs_8409_disable_headset_mike_streaming(struct hda_codec *codec)
{

	// NOTE - there are big ordering issues here
	//        although here the mike is turned off before the headphone output

	// mute ADC
	cs42l83_headset_mike_adc_unmutevol(codec, 0);

	cs42l83_power_off_codec_input(codec);

	cs42l83_mike_disable(codec);

	cs42l83_headset_mike_format_setup_disable(codec);

	// and duplicate the above!!
	cs42l83_headset_mike_adc_unmutevol(codec, 0);
	cs42l83_power_off_codec_input(codec);

	// the following is disabling the headphone component
	// - assuming this is done by the playback hooks

	//cs42l83_headset_enable_off
	//cs42l83_power_off_codec_output
	//cs42l83_buffers_onoff
	//cs42l83_headset_power_off
	//cs_8409_headset_amp_disable_TDM_proper (full)
	//cs_8409_headset_amp_format_setup_disable
	//cs42l83_headset_enable_off
	//cs42l83_power_off_codec_output

	//cs_8409_external_device_unsolicited_response
	//cs_8409_read_status_and_clear_interrupt
	//read_gpio_status
	//read_gpio_status
	//cs42l83_read_status_and_clear_interrupt
	//read_gpio_status
	//cs42l83_disambiguate_ur_from_int
	//read_gpio_status

	// and re-enabling the headphone component?????
	// igoring all the following for the moment

	// note there was a 7 second delay here - this is recextstop2/2c

	//cs43l83_headset_amp_format_setup (full)
	//cs42l83_configure_int_mclk
	//cs42l83_power_onoff
	//cs42l83_configure_serial_port
	//cs42l83_output_set_input_sample_rate
	//cs42l83_setup_audio_output
	//cs42l83_buffers_onoff
	//cs42l83_set_power_state_on
	//cs42l83_headset_enable_on

	//cs_8409_external_device_unsolicited_response
	//cs_8409_read_status_and_clear_interrupt
	//read_gpio_status
	//read_gpio_status
	//cs42l83_read_status_and_clear_interrupt
	//read_gpio_status
	//cs42l83_read_status_and_clear_interrupt
	//read_gpio_status
	//cs42l83_disambiguate_ur_from_int
	//read_gpio_status
	//cs_8409_external_device_unsolicited_response (continued)
	//cs_8409_read_status_and_clear_interrupt
	//read_gpio_status
	//read_gpio_status
	//cs42l83_read_status_and_clear_interrupt
	//read_gpio_status
	//cs42l83_disambiguate_ur_from_int
	//read_gpio_status

	// then re-disabling!!

	// note there was a 5 second delay here - this is recextstop3

	//cs42l83_headset_enable_off
	//cs42l83_power_off_codec_output
	//cs42l83_headset_rcv_enable_off
	//cs42l83_headset_power_off
	//cs_8409_headset_amp_disable_TDM_proper - full
	//cs42l83_headset_enable_off
	//cs42l83_power_off_codec_output

	//cs_8409_external_device_unsolicited_response
	//cs_8409_read_status_and_clear_interrupt
	//read_gpio_status
	//read_gpio_status
	//cs42l83_read_status_and_clear_interrupt
	//read_gpio_status
	//cs42l83_disambiguate_ur_from_int
	//read_gpio_status

}

static void cs_8409_headcapture_real(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;

	if (spec->headset_enable == 0) {
	} else if (spec->headset_enable == 1) {
		hda_check_power_state(codec, 0x1a, 11);
		hda_check_power_state(codec, 0x3c, 11);
		cs_8409_enable_headset_mike_streaming(codec);
		hda_check_power_state(codec, 0x1a, 12);
		hda_check_power_state(codec, 0x3c, 12);
	} else if (spec->headset_enable == 2) {
	}

}

static void cs_8409_headcapturestop_real(struct hda_codec *codec)
{

	cs_8409_disable_headset_mike_streaming(codec);

}

/* ---- end inlined patch_cirrus_real84.h ---- */

// only needed if wish to test the version using the mb141 logs
// cs_8409_boot_setup_real now supposed to do both machines
//#include "patch_cirrus_mb141_real84.h"

int cs_8409_boot_setup(struct hda_codec *codec)
{
	int err = 0;
	struct cs8409_apple_spec *spec = codec->spec;

	// so it appears we break up the subsystem_id into 2 parts
	// a codec vendor id (16 bits) and a subvendor id (8 bits) plus an assembly id
	// so here the codec vendor is 0x106b, the subvendor id is 0x39 and the assembly id is 0x00
	if (codec->core.subsystem_id == 0x106b3900) {
		if (spec->use_data) {

			err = cs_8409_data_config(codec);

		} else {

			err = cs_8409_real_config(codec);

		}
	} else if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600) {
		if (spec->use_data) {
			cs_8409_boot_setup_data_ssm3(codec);
		} else {
			err = cs_8409_real_config(codec);
		}
	} else if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		if (spec->use_data) {
			codec_info(codec, "%s pre data not implemented for subsystem id 0x%08x", __func__, codec->core.subsystem_id);
		} else {
			err = cs_8409_real_config(codec);
		}
	} else {
		codec_err(codec, "UNKNOWN subsystem id 0x%08x", codec->core.subsystem_id);
		err = -1;
	}

	return err;
}

static void cs_8409_play_setup(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;
	if (codec->core.subsystem_id == 0x106b3900) {
		if (spec->use_data) {
			cs_8409_play_data(codec);
		} else {
			cs_8409_play_real(codec);
		}
	} else if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600) {
		if (spec->use_data) {
			cs_8409_play_data_ssm3(codec);
		} else {
			cs_8409_play_real(codec);
		}
	} else if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		if (spec->use_data) {
			codec_info(codec, "%s data not implemented for subsystem id 0x%08x", __func__, codec->core.subsystem_id);
		} else {
			cs_8409_play_real(codec);
		}
	} else {
		codec_err(codec, "UNKNOWN subsystem id 0x%08x", codec->core.subsystem_id);
	}
}

static void cs_8409_play_cleanup(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;
	if (codec->core.subsystem_id == 0x106b3900) {
		if (spec->use_data) {
			cs_8409_playstop_data(codec);
		} else {
			cs_8409_playstop_real(codec);
		}
	} else if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600) {
		if (spec->use_data) {
		} else {
			cs_8409_playstop_real(codec);
		}
	} else if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		if (spec->use_data) {
			codec_info(codec, "%s data not implemented for subsystem id 0x%08x", __func__, codec->core.subsystem_id);
		} else {
			cs_8409_playstop_real(codec);
		}
	} else {
		codec_err(codec, "UNKNOWN subsystem id 0x%08x", codec->core.subsystem_id);
	}

}

// NOTE - so far all systems use the same inputs for internal mike capturing - not sure if
// there are any subsystem_id differences

static void cs_8409_capture_setup(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;
	if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600 || codec->core.subsystem_id == 0x106b3900
		|| codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		if (spec->use_data) {
		} else {
			cs_8409_capture_real(codec);
		}
	} else {
		codec_err(codec, "UNKNOWN subsystem id 0x%08x", codec->core.subsystem_id);
	}
}

static void cs_8409_capture_cleanup(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;
	if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600 || codec->core.subsystem_id == 0x106b3900
		|| codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		if (spec->use_data) {
		} else {
		       cs_8409_capturestop_real(codec);
		}
	} else {
		codec_err(codec, "UNKNOWN subsystem id 0x%08x", codec->core.subsystem_id);
	}

}

// routine to clear unsol list

static void cs_8409_cs42l83_unsolicited_response_finalize(struct hda_codec *codec, unsigned int res);

void cs_8409_perform_external_device_unsolicited_responses(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;
	struct unsol_item *unsol_entry = NULL;
	struct unsol_item *unsol_temp = NULL;
	if (!list_empty(&spec->unsol_list)) {
		codec_info(codec, "%s UNSOL start\n", __func__);
		list_for_each_entry_safe(unsol_entry, unsol_temp, &spec->unsol_list, list) {
			list_del_init(&unsol_entry->list);
			// pigs this gets complicated - these might issue other unsol responses
			cs_8409_cs42l83_unsolicited_response_finalize(codec, unsol_entry->res);
			spec->unsol_items_prealloc_used[unsol_entry->idx] = 0;
			memset(unsol_entry, 0, sizeof(struct unsol_item));
		}
		codec_info(codec, "%s UNSOL end\n", __func__);
	}
}

void cs_8409_cs42l83_unsolicited_response(struct hda_codec *codec, unsigned int res)
{
	struct cs8409_apple_spec *spec = codec->spec;

	// not clear if want to use the GPIO pins apparently passed in res to determine
	// if want to do interrupt checking here and if no interrupts then to do
	// some other unsolicited response function (not seen any such unsolicited responses yet)
	// without checking the unsolicited block status

	// now think dont need a list - we can only have 1 outstanding unsolicted interrupt request
	// we may get multiple unsolicited interrupt requests - but they all will have same GPIO status (0x26)
	// and we determine the exact interrupt by reading the cs42l83 registers - which we are trying to avoid
	// clashing with other verbs
	// it may be that we get multiple interrupt flags to handle when we do read - not seen so far

	if (spec->block_unsol) {
		int itm;
		int new_itm = -1;
		codec_info(codec, "%s -     UNSOL BLOCKED\n", __func__);
		for (itm=0; itm<10; itm++)
			if (spec->unsol_items_prealloc_used[itm] == 0) { new_itm = itm; break; }
		if (new_itm < 0) {
			codec_info(codec, "%s - IGNORING UNSOL RESPONSE!!\n", __func__);
			return;
		}
		spec->unsol_items_prealloc_used[new_itm] = 1;
		memset(&(spec->unsol_items_prealloc[new_itm]), 0, sizeof(struct unsol_item));
		spec->unsol_items_prealloc[new_itm].res = res;
		spec->unsol_items_prealloc[new_itm].idx = new_itm;
		list_add_tail(&(spec->unsol_items_prealloc[new_itm].list), &spec->unsol_list);
		codec_info(codec, "%s - UNSOL response stored\n", __func__);
		return;
	} else
		codec_info(codec, "%s - NOT UNSOL BLOCKED\n", __func__);

	// so it appears we need to block unsol responses while doing unsol responses
	// this is probably not the way to do this but still havent figured out how to use locking properly
	// as this needs to be interruptible because some of these functions take a long time
	// I think if we get here we cannot have been blocked so list maybe always empty
	// whats not clear is if list_for_each_entry_safe is safe for addition also
	spec->block_unsol = 1;

	cs_8409_cs42l83_unsolicited_response_finalize(codec, res);

	if (!list_empty(&spec->unsol_list)) {
		cs_8409_perform_external_device_unsolicited_responses(codec);
	}

	spec->block_unsol = 0;
}

static void cs_8409_cs42l83_unsolicited_response_finalize(struct hda_codec *codec, unsigned int res)
{
	struct cs8409_apple_spec *spec = codec->spec;

	if (spec->use_data)
		cs_8409_external_device_unsolicited_response_data(codec, res);
	else {
		if (spec->headset_phase == 0) {
			return;
		}

		// note the data version will only play thro the headphones for a single time
		cs_8409_external_device_unsolicited_response(codec, 0, 1);
	}
}

static void cs_8409_headplay_setup(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;
	if (codec->core.subsystem_id == 0x106b3900) {
		if (spec->use_data) {
			cs_8409_headplay_data(codec);
		} else {
			cs_8409_headplay_real(codec);
		}
	} else if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600) {
		if (spec->use_data) {
		} else {
			cs_8409_headplay_real(codec);
		}
	} else if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		if (spec->use_data) {
			codec_info(codec, "%s data not implemented for subsystem id 0x%08x", __func__, codec->core.subsystem_id);
		} else {
			cs_8409_headplay_real(codec);
		}
	} else {
		codec_err(codec, "UNKNOWN subsystem id 0x%08x", codec->core.subsystem_id);
	}

	// decided this needs moving till all stream setup verbs done

	//if (!list_empty(&spec->unsol_list))
}

static void cs_8409_headplay_cleanup(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;
	if (codec->core.subsystem_id == 0x106b3900) {
		if (spec->use_data) {
			cs_8409_headplaystop_data(codec);
		} else {
			cs_8409_headplaystop_real(codec);
		}
	} else if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600) {
		if (spec->use_data) {
		} else {
			cs_8409_headplaystop_real(codec);
		}
	} else if (codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		if (spec->use_data) {
			codec_info(codec, "%s data not implemented for subsystem id 0x%08x", __func__, codec->core.subsystem_id);
		} else {
			cs_8409_headplaystop_real(codec);
		}
	} else {
		codec_err(codec, "UNKNOWN subsystem id 0x%08x", codec->core.subsystem_id);
	}

	// decided this needs moving till all stream cleanup verbs done

	//if (!list_empty(&spec->unsol_list))
}

// NOTE - so far all systems use the same chip (cs42l83) for headset mike capturing - not sure if
// there are any subsystem_id differences

static void cs_8409_headcapture_setup(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;
	if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600 || codec->core.subsystem_id == 0x106b3900
		|| codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		if (spec->use_data) {
		} else {
			cs_8409_headcapture_real(codec);
		}
	} else {
		codec_err(codec, "UNKNOWN subsystem id 0x%08x", codec->core.subsystem_id);
	}

	// decided this needs moving till all stream setup verbs done

	//if (!list_empty(&spec->unsol_list))
}

static void cs_8409_headcapture_cleanup(struct hda_codec *codec)
{
	struct cs8409_apple_spec *spec = codec->spec;
	if (codec->core.subsystem_id == 0x106b3300 || codec->core.subsystem_id == 0x106b3600 || codec->core.subsystem_id == 0x106b3900
		|| codec->core.subsystem_id == 0x106b1000 || codec->core.subsystem_id == 0x106b0f00 || codec->core.subsystem_id == 0x106b0e00) {
		if (spec->use_data) {
		} else {
			cs_8409_headcapturestop_real(codec);
		}
	} else {
		codec_err(codec, "UNKNOWN subsystem id 0x%08x", codec->core.subsystem_id);
	}

	// decided this needs moving till all stream cleanup verbs done

	//if (!list_empty(&spec->unsol_list))
}

void cs_8409_pcm_playback_pre_prepare_hook(struct hda_pcm_stream *hinfo, struct hda_codec *codec,
			       unsigned int stream_tag, unsigned int format, struct snd_pcm_substream *substream,
			       int action)
{
	struct cs8409_apple_spec *spec = codec->spec;

	if (action == HDA_GEN_PCM_ACT_PREPARE) {
		struct timespec64 curtim;
		ktime_get_real_ts64(&curtim);
		spec->play_init_count++;
		// for some reason this is being called twice within short time so setup is being done twice
		// with no intervening cleanup
		// which may be introducing glitches in the headset/headphone stream - which it is!!!
		// try suppressing any further calls
		// well great apparently its a feature this function can be called multiple times!!
		// looking at examples and Alsa driver docs it appears the idea of this function is to set
		// the stream parameters - so lets let it set the stream parameters every time
		// but only do the main apple setup once
		if (1) {

			// in the new way we set the stream up here using the passed data
			// - this does not actually update the stream format here but sets the cached parameters
			// so the cs_8409_really_update_stream_format will cause the updates to occur
			// note we explicitly set the channel id - dont see another way yet

			cs_8409_store_stream_format(codec, 0x02, stream_tag, format);

			cs_8409_store_stream_format(codec, 0x03, stream_tag, format);

			cs_8409_store_stream_format(codec, 0x0a, stream_tag, format);

			// save number of actual stream channels
			spec->stream_channels = substream->runtime->channels;

			hda_check_power_state(codec, 0x1a, 1);
			hda_check_power_state(codec, 0x3c, 1);
		}

		if (spec->play_init_count == 1) {

			// for the moment have junky test here
			if (spec->jack_present) {
				// for the moment I think this works for both MB 14,1 and 14,3 - same hda and headphone chip
				// note that so far only the headphone chip seems to generate unsol responses usually
				spec->block_unsol = 1;
				// we need to split this to deal with capture only setup
				// and capture with play setup
				// note this does mean we setup the mike in a different order to OSX
				// if we are capturing with playing - because the capture setup seems to be done
				// first on Linux and we dont know at that stage if we will be playing
				if (spec->have_mike) {
					// actually we always need to do cs_8409_headplay_setup - here we are about to play
					// - what this possibly would allow is the apple way of doing a pre-setup
					// so here we would switch between doing a full setup or a partial setup
					if (spec->headset_play_format_setup_needed) {
						cs_8409_headplay_setup(codec);
						spec->headset_play_format_setup_needed = 0;
					}
					// we only setup capturing if we are actually doing capturing
					//if (spec->headset_capture_format_setup_needed)
				} else {
					cs_8409_headplay_setup(codec);
				}
			} else {
				cs_8409_play_setup(codec);
			}

			hda_check_power_state(codec, 0x1a, 2);
			hda_check_power_state(codec, 0x3c, 2);

			// I dont now understand how this worked - the codes above ALWAYS reset the stream format
			// to the OSX format
			// and unless I force a stream update here there will be a stream format difference
			// yet it appears it worked - even tho sometimes there was no format update after this routine
			// now I dont know why

			// so we need to force the stream to be re-set here
			// problem is it appears hda_codec caches the stream format and id and only updates if changed
			// and there doesnt seem to be a good way to force an update

			// this routine doesnt seem to be nid specific - so explicitly fix the known nids here
			// no longer needed now we set the stream format correctly above
			// so when snd_hda_multi_out_analog_prepare is called after this routine it should do nothing
			// as we will have cached and set the right format now

			spec->playing = 0;

			spec->play_init = 1;
		}
	}
}

void cs_8409_playback_pcm_hook(struct hda_pcm_stream *hinfo, struct hda_codec *codec, struct snd_pcm_substream *substream, int action)
{

	struct cs8409_apple_spec *spec = codec->spec;

	// so finally getting a handle on ordering here
	// we need to do the OSX setup in the OPEN section
	// as the generic hda format and stream setup is done BEFORE the PREPARE hook
	// (theres a good chance we only need to do this once at least as long as machine doesnt sleep)
	// (or we could just override the prepare function completely)
	// I now think the noise was caused by mis-match between the stream format and the nid setup format
	// (because the generic setup was done before the OSX setup and the actual streamed format is slightly different)
	// (the hda documentation says these really need to match)
	// It appears the 8409 setup can handle at least some differences in the stream format
	// as long as we set the nid to format the kernel is sending
	// certainly seems to handle S24_LE or S32_LE differences (OSX format is always S24_3LE)

	if (action == HDA_GEN_PCM_ACT_OPEN) {

		spec->play_init_count = 0;

	} else if (action == HDA_GEN_PCM_ACT_PREPARE) {
		// so this comes AFTER the stream format, frequency setup verbs are sent for the pcm stream
		// note that this can be called multiple times apparently
		// not clear what if any the differences are for those multiple calls
		// - does mean we need to ensure we only do most operations once
		// (most of the work is done in the pre prepare function)
		struct timespec64 curtim;
		ktime_get_real_ts64(&curtim);
		// this is where we need to finally unset the block_unsol
		// - which also means this is where we should check for unsolicited responses
		spec->block_unsol = 0;
		if (!list_empty(&spec->unsol_list)) {
			codec_info(codec, "%s - performing UNSOL responses\n", __func__);
			cs_8409_perform_external_device_unsolicited_responses(codec);
		}
		spec->playing = 1;
	} else if (action == HDA_GEN_PCM_ACT_CLEANUP) {
		// so this also comes AFTER the stream format, frequency cleanup verbs are sent for the pcm stream
		int power_chk = 0;
		power_chk = snd_hda_codec_read(codec, codec->core.afg, 0, AC_VERB_GET_POWER_STATE, 0);
		// for the moment have junky test here
		if (spec->jack_present) {
			// for the moment I think this works for both MB 14,1 and 14,3 - same hda and headphone chip
			// note that so far only the headphone chip seems to generate unsol responses usually
			spec->block_unsol = 1;
			// so dont think need to anything about capturing here
			cs_8409_headplay_cleanup(codec);
			spec->headset_play_format_setup_needed = 1;
		} else
			cs_8409_play_cleanup(codec);
		spec->block_unsol = 0;
		if (!list_empty(&spec->unsol_list)) {
			codec_info(codec, "%s - performing UNSOL responses\n", __func__);
			cs_8409_perform_external_device_unsolicited_responses(codec);
		}
		spec->play_init_count = 0;
		// not sure of this position yet
		spec->playing = 0;
		power_chk = snd_hda_codec_read(codec, codec->core.afg, 0, AC_VERB_GET_POWER_STATE, 0);
	} else if (action == HDA_GEN_PCM_ACT_CLOSE) {
	}

}

void cs_8409_pcm_capture_pre_prepare_hook(struct hda_pcm_stream *hinfo, struct hda_codec *codec,
			       unsigned int stream_tag, unsigned int format, struct snd_pcm_substream *substream,
			       int action)
{
	struct cs8409_apple_spec *spec = codec->spec;

	if (action == HDA_GEN_PCM_ACT_PREPARE) {
		spec->capture_init_count++;

		// for some reason this is being called twice within short time
		// well great apparently its a feature this function can be called multiple times!!
		// looking at examples and Alsa driver docs it appears the idea of this function is to set
		// the stream parameters - so lets let it set the stream parameters every time
		// but only do the main apple setup once

		// so the first action for internal mike recording (via Quicktime)
		// is a headphone sense
		// followed by amp setup for playing - is this just a feature of Quicktime??
		// maybe Quicktime just auto sets up play just in case
		// we dont seem to have a headphone sense if we have already plugged in the headset
		// not that we can do anything - except abort if no headset plugged in??

		//if (hinfo->nid == 0x22)

		// I think this is the same for intmike or headset mike
		cs_8409_store_stream_format(codec, hinfo->nid, stream_tag, format);

		// ensure the setup is only done once
		if (spec->capture_init_count == 1) {

			// for the moment have junky test here
			if (spec->jack_present) {
				spec->block_unsol = 1;
				if (spec->have_mike) {
					// so it seems if we have a headset mike we always enable the
					// headphones even if just capturing
					if (spec->headset_play_format_setup_needed) {
						cs_8409_headplay_setup(codec);
						spec->headset_play_format_setup_needed = 0;
					}
					if (spec->headset_capture_format_setup_needed) {
						cs_8409_headcapture_setup(codec);
						spec->headset_capture_format_setup_needed = 0;
					}
				}
				// I think this is impossible - this would say we tried to capture
				// using a headset without mike
				// NOTE - still not fixed linein/lineout working - this may need
				// changing here
			} else
				cs_8409_capture_setup(codec);
		}

		spec->capturing = 0;

		spec->capture_init = 1;
	}
}

void cs_8409_capture_pcm_hook(struct hda_pcm_stream *hinfo, struct hda_codec *codec, struct snd_pcm_substream *substream, int action)
{

	struct cs8409_apple_spec *spec = NULL;

	// - so this seems to be the critical issue - this can apparently be called with a NULL codec!!!
	// only thing to do seems to be to return!!
	if (codec == NULL) {
		struct hda_codec *badptr = NULL;
		pr_err("snd_hda_intel: command %s HOOK init  - CODEC NULL\n", __func__);
		// so if we are here it looks as tho we have been called from call_hp_automute
		// - in which the codec is the 1st arg
		badptr = (struct hda_codec *) hinfo;
		spec = badptr->spec;
		codec_dbg(badptr, "%s -  pcm_playback_hook %p\n", __func__, spec->gen.pcm_playback_hook);
		codec_dbg(badptr, "%s -   pcm_capture_hook %p\n", __func__, spec->gen.pcm_capture_hook);
		codec_dbg(badptr, "%s -   hp_automute_hook %p\n", __func__, spec->gen.hp_automute_hook);
		codec_dbg(badptr, "%s - line_automute_hook %p\n", __func__, spec->gen.line_automute_hook);
		codec_dbg(badptr, "%s - line_automute_hook %p\n", __func__, spec->gen.mic_autoswitch_hook);
		pr_err("snd_hda_intel: command %s HOOK init  - CODEC NULL exit\n", __func__);
		return;
	} else {
	}

	spec = codec->spec;

	// so now no setup is done here - we only check for unsolicited responses
	// - we do do cleanup for the CLEANUP action

	if (action == HDA_GEN_PCM_ACT_OPEN) {
		spec->capture_init_count = 0;

	} else if (action == HDA_GEN_PCM_ACT_PREPARE) {
		// so this comes AFTER the stream format, frequency setup verbs are sent for the pcm stream
		// note that this can be called multiple times apparently
		// not clear what if any the differences are for those multiple calls
		// - does mean we need to ensure we only do most operations once
		// (most of the work is done in the pre prepare function)
		// this is where we need to finally unset the block_unsol
		// - which also means this is where we should check for unsolicited responses
		spec->block_unsol = 0;
		if (!list_empty(&spec->unsol_list)) {
			codec_info(codec, "%s - performing UNSOL responses\n", __func__);
			cs_8409_perform_external_device_unsolicited_responses(codec);
		}
		spec->capturing = 1;
	} else if (action == HDA_GEN_PCM_ACT_CLEANUP) {
		// so this also comes AFTER the stream format, frequency cleanup verbs are sent for the pcm stream
		int power_chk = 0;
		power_chk = snd_hda_codec_read(codec, codec->core.afg, 0, AC_VERB_GET_POWER_STATE, 0);
		// for the moment have junky test here
		if (spec->jack_present) {
			// for the moment I think this works for both MB 14,1 and 14,3 - same hda and headphone chip
			// note that so far only the headphone chip seems to generate unsol responses usually
			spec->block_unsol = 1;
			if (spec->headset_capture_format_setup_needed == 0) {
				cs_8409_headcapture_cleanup(codec);
				spec->headset_capture_format_setup_needed = 1;
			}
			if (!spec->playing) {
				if (spec->headset_play_format_setup_needed == 0) {
					cs_8409_headplay_cleanup(codec);
					spec->headset_play_format_setup_needed = 1;
				}
			}
		} else
			cs_8409_capture_cleanup(codec);
		spec->block_unsol = 0;
		if (!list_empty(&spec->unsol_list)) {
			codec_info(codec, "%s - performing UNSOL responses\n", __func__);
			cs_8409_perform_external_device_unsolicited_responses(codec);
		}
		spec->capture_init_count = 0;
		// not sure of this position yet
		spec->capturing = 0;
		power_chk = snd_hda_codec_read(codec, codec->core.afg, 0, AC_VERB_GET_POWER_STATE, 0);
	} else if (action == HDA_GEN_PCM_ACT_CLOSE) {
	}

}

