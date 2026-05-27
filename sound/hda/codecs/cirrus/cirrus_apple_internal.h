/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Internal header shared between cirrus_apple.c and its split sub-units.
 * Not exported outside the cirrus codec directory.
 */

#ifndef __CS8409_CIRRUS_APPLE_INTERNAL_H
#define __CS8409_CIRRUS_APPLE_INTERNAL_H

#include "cs8409.h"

#define cs8409_apple_spec cs8409_spec

/* CS8409 */
#define CS8409_IDX_DEV_CFG     CS8409_PIN_AFG
#define CS8409_VENDOR_NID      CS8409_PIN_VENDOR_WIDGET
#define CS8409_BEEP_NID        CS8409_PIN_BEEP_GEN

/* nid devs based on Dell fixups */
#define CS8409_CS42L83_HP_PIN_NID                       CS8409_PIN_ASP2_TRANSMITTER_A
#define CS8409_CS42L83_HP_MIC_PIN_NID                   CS8409_PIN_ASP2_RECEIVER_A
#define CS8409_CS42L83_MACBOOK_MIC_PIN_NID              CS8409_PIN_DMIC1_IN
#define CS8409_CS42L83_MACBOOK_LINEIN_PIN_NID           CS8409_PIN_DMIC2_IN
#define CS8409_CS42L83_IMAC_MIC_PIN_NID                 CS8409_PIN_DMIC2_IN
#define CS8409_CS42L83_IMAC_LINEIN_PIN_NID              CS8409_PIN_DMIC1_IN
#define CS8409_CS42L83_MACBOOK_LINEIN_ADC_PIN_NID       CS8409_PIN_DMIC2
#define CS8409_CS42L83_IMAC_LINEIN_ADC_PIN_NID          CS8409_PIN_DMIC1

/* shared power-state helpers (cirrus_apple.c) */
unsigned int hda_sync_power_state_8409(struct hda_codec *codec, hda_nid_t nid,
				       unsigned int power_state);
unsigned int hda_set_node_power_state_dbg(struct hda_codec *codec, hda_nid_t nid,
					  unsigned int power_state, bool dbgflg);
unsigned int hda_set_node_power_state(struct hda_codec *codec, hda_nid_t nid,
				      unsigned int power_state);
void hda_check_power_state(struct hda_codec *codec, hda_nid_t nid, int flagint);

/* cirrus_apple_input.c */
void cs_8409_intmike_format_setup_enable(struct hda_codec *codec, int hda_format,
					 int powered_down);
void cs_8409_volume_set(struct hda_codec *codec, hda_nid_t nid, int volume);
void cs_8409_intmike_volume_set(struct hda_codec *codec, int volume);
void cs_8409_linein_volume_set(struct hda_codec *codec, int volume);
void cs_8409_intmike_volume_unmute(struct hda_codec *codec);
void cs_8409_linein_volume_unmute(struct hda_codec *codec);
void cs_8409_intmike_volume_mute(struct hda_codec *codec);
void cs_8409_linein_volume_mute(struct hda_codec *codec);
void cs_8409_intmike_volume_setup(struct hda_codec *codec, int volume);
void cs_8409_linein_volume_setup(struct hda_codec *codec, int volume);
void cs_8409_intmike_stream_on_nid(struct hda_codec *codec);
void cs_8409_intmike_format_setup_disable(struct hda_codec *codec);
void cs_8409_linein_format_setup_disable(struct hda_codec *codec);
void cs_8409_intmike_stream_conn_off(struct hda_codec *codec);
void cs_8409_linein_stream_conn_off(struct hda_codec *codec);
void cs_8409_intmike_stream_off_nid(struct hda_codec *codec);
void cs_8409_linein_stream_off_nid(struct hda_codec *codec);
void cs_8409_intmike_volume_mute_nouse(struct hda_codec *codec);
void cs_8409_linein_volume_mute_nouse(struct hda_codec *codec);
void cs_8409_intmike_volume_unmute_nouse(struct hda_codec *codec);
void cs_8409_linein_volume_unmute_nouse(struct hda_codec *codec);
void cs_8409_inputs_power_nids_on(struct hda_codec *codec);
void cs_8409_inputs_power_nids_off(struct hda_codec *codec);
void cs_8409_intmike_stream_conn_off_disable(struct hda_codec *codec);
void cs_8409_intmike_linein_resetup(struct hda_codec *codec);
void cs_8409_intmike_linein_disable(struct hda_codec *codec);

/* cirrus_apple_amp.c */
void cs_8409_setup_TDM_amps12(struct hda_codec *codec, int setrate, int nullformat);
void cs_8409_setup_TDM_amps34(struct hda_codec *codec, int nullformat);
void cs_8409_setup_amps12(struct hda_codec *codec, int amps_enable);
void cs_8409_setup_amps34(struct hda_codec *codec, int amps_enable);
void cs_8409_sync_converters_off(struct hda_codec *codec, int nullformat);
void cs_8409_sync_converters_on(struct hda_codec *codec, int nullformat);
void cs_8409_disable_TDM_amps12(struct hda_codec *codec);
void cs_8409_disable_TDM_amps34(struct hda_codec *codec);
void cs_8409_disable_amps12(struct hda_codec *codec);
void cs_8409_disable_amps34(struct hda_codec *codec);
void play_setup_TDM_amps12(struct hda_codec *codec, int setrate);
void play_setup_TDM_amps34(struct hda_codec *codec);
void play_setup_amps12(struct hda_codec *codec);
void play_setup_amps34(struct hda_codec *codec);
void play_sync_converters_on(struct hda_codec *codec);
void playstop_disable_TDM_amps12(struct hda_codec *codec);
void playstop_disable_TDM_amps34(struct hda_codec *codec);
void playstop_disable_amps12(struct hda_codec *codec);
void playstop_disable_amps34(struct hda_codec *codec);
void playstop_sync_converters_off(struct hda_codec *codec);

/* cirrus_apple_boot.c */
void setup_reset_and_clear(struct hda_codec *codec);
void init_read_all_nodes(struct hda_codec *codec);
void read_vendor_node(struct hda_codec *codec);
void init_read_coefs(struct hda_codec *codec);
void read_virtual_widgets(struct hda_codec *codec);
void enable_i2c(struct hda_codec *codec);
void init_for_node_vendor(struct hda_codec *codec);
void determine_speaker_id(struct hda_codec *codec);
void enable_GPIforUR(struct hda_codec *codec, int mask);
void cs42l83_external_control_GPIO(struct hda_codec *codec, int mask);
void cs42l83_reset(struct hda_codec *codec);
int cs42l83_device_id(struct hda_codec *codec);
void cs42l83_inithw(struct hda_codec *codec);
void setup_amps_reset_i2c_max(struct hda_codec *codec);
void setup_amps_reset_i2c_ssm3(struct hda_codec *codec);
void setup_amps_reset_i2c_tas576(struct hda_codec *codec);
void cs42l83_mic_detect(struct hda_codec *codec);
void cs42l83_tip_sense(struct hda_codec *codec, int invert);

/* cirrus_apple_l83_i2c.c */
void cs42l83_headset_button_detect_interrupts_off(struct hda_codec *codec);
void cs42l83_headset_set_hpout_clamp_disable(struct hda_codec *codec);
void cs42l83_complete_jack_detect(struct hda_codec *codec);
void cs42l83_power_hs_bias_on(struct hda_codec *codec);
void cs42l83_enable_hs_auto_int_on(struct hda_codec *codec);
void cs42l83_unplug_interrupt_setup(struct hda_codec *codec);
void cs42l83_set_hpout_pulldown_off(struct hda_codec *codec);
void cs42l83_headset_detect_on(struct hda_codec *codec);
void cs42l83_headset_detect_off(struct hda_codec *codec);
void cs42l83_enable_hs_auto_int_off(struct hda_codec *codec);
int cs42l83_headset_type(struct hda_codec *codec);
void cs42l83_set_hpout_pulldown_on(struct hda_codec *codec);
void cs42l83_set_hpout_clamp_enable(struct hda_codec *codec);
void cs42l83_headset_mike_detect_off(struct hda_codec *codec);
void cs42l83_power_hs_bias_off(struct hda_codec *codec);
void cs42l83_enable_hsbias_auto_clamp_on(struct hda_codec *codec);
void cs42l83_enable_hsbias_auto_clamp_off0(struct hda_codec *codec);
void cs42l83_setup_button_detect(struct hda_codec *codec);
void cs42l83_power_hs_bias_button_on(struct hda_codec *codec);
void cs42l83_enable_hsbias_auto_clamp_off1(struct hda_codec *codec);
int cs42l83_handle_button_detect(struct hda_codec *codec);
int cs42l83_mike_connected(struct hda_codec *codec);
void cs42l83_configure_int_mclk(struct hda_codec *codec);
void cs42l83_power_onoff(struct hda_codec *codec, bool onflag);
void cs42l83_configure_serial_port(struct hda_codec *codec);
void cs42l83_output_set_input_sample_rate(struct hda_codec *codec);
void cs42l83_setup_audio_output(struct hda_codec *codec);
void cs42l83_buffers_onoff(struct hda_codec *codec, bool onflag);
void cs42l83_headset_enable_on(struct hda_codec *codec);
void cs42l83_plugin_interrupt_setup(struct hda_codec *codec);
void cs42l83_headset_detect2_off(struct hda_codec *codec);
void cs42l83_enable_hsbias_auto_clamp_off3(struct hda_codec *codec);
void cs42l83_disable_button_interrupts(struct hda_codec *codec);
void cs42l83_unplug_headset_detect_off(struct hda_codec *codec);
void cs42l83_headset_switch_control(struct hda_codec *codec);
void cs42l83_headset_enable_off(struct hda_codec *codec);
void cs42l83_power_off_codec_output(struct hda_codec *codec);
void cs42l83_power_off_codec_input(struct hda_codec *codec);
void cs42l83_input_set_output_sample_rate(struct hda_codec *codec);
void cs42l83_mike_setup_audio_input(struct hda_codec *codec);
void cs42l83_mike_enable(struct hda_codec *codec);
void cs42l83_mike_disable(struct hda_codec *codec);
void cs42l83_headset_mike_pin_enable(struct hda_codec *codec);
void cs42l83_configure_headset_button_interrupts(struct hda_codec *codec);
void cs42l83_enable_hsbias_auto_clamp_off2(struct hda_codec *codec);
void cs42l83_hsbias_sense_on(struct hda_codec *codec);
void cs42l83_headset_mike_adc_unmutevol(struct hda_codec *codec, int unmute);

/* cirrus_apple_stream.c */
/*
 * Local definition of hda_cvt_setup (private to hda_codec.c upstream).
 * Must match the upstream layout exactly.
 */
struct hda_cvt_setup {
	hda_nid_t nid;
	u8 stream_tag;
	u8 channel_id;
	u16 format_id;
	unsigned char active;	/* cvt is currently used */
	unsigned char dirty;	/* setups should be cleared */
};
void cs_8409_dump_stream_format(struct hda_codec *codec, hda_nid_t nid);
void cs_8409_save_and_clear_stream_format(struct hda_codec *codec, hda_nid_t nid,
					  struct hda_cvt_setup *savep);
void cs_8409_update_from_save_stream_format(struct hda_codec *codec, hda_nid_t nid,
					    struct hda_cvt_setup *savep,
					    int update_stream_id, int update_format_id);
void cs_8409_really_update_stream_format(struct hda_codec *codec, hda_nid_t nid,
					 int update_format_id, int update_stream_id,
					 unsigned int new_channel_id);
void cs_8409_store_stream_format(struct hda_codec *codec, hda_nid_t nid,
				 unsigned int stream_tag, unsigned int format);
void switch_input_src(struct hda_codec *codec);

/* cirrus_apple_coef.c */
unsigned int cs_8409_vendor_coef_get(struct hda_codec *codec, unsigned int idx);
void cs_8409_vendor_coef_set(struct hda_codec *codec, unsigned int idx, unsigned int coef);
unsigned int cs_8409_vendor_coef_set_mask(struct hda_codec *codec, unsigned int idx,
					  unsigned int coef, unsigned int mask,
					  unsigned int srcval, int srcidx);
void cs_8409_vendor_enableI2Cclock(struct hda_codec *codec, unsigned int flag);
unsigned int cs_8409_vendor_i2cRead(struct hda_codec *codec, unsigned int i2c_address,
				    unsigned int i2c_reg, unsigned int paged);
unsigned int cs_8409_vendor_i2cWrite(struct hda_codec *codec, unsigned int i2c_address,
				     unsigned int i2c_reg, unsigned int i2c_data,
				     unsigned int paged);
unsigned int cs_8409_vendor_i2cWriteMask(struct hda_codec *codec, unsigned int i2c_address,
					 unsigned int i2c_reg, unsigned int i2c_mask,
					 unsigned int i2c_data, unsigned int paged);
void snd_hda_coef_item(struct hda_codec *codec, u16 write_flag, hda_nid_t nid,
		       u32 idx, u32 param, u32 retdata, int srcidx);
int snd_hda_coef_item_check(struct hda_codec *codec, u16 write_flag, hda_nid_t nid,
			    u32 idx, u32 param, u32 retdata, int srcidx);
void snd_hda_coef_item_masked(struct hda_codec *codec, u16 write_flag, hda_nid_t nid,
			      u32 idx, u32 param, u32 mask, u32 retdata,
			      u32 srcval, int srcidx);
unsigned int snd_hda_codec_read_check(struct hda_codec *codec, hda_nid_t nid, int flags,
				      unsigned int verb, unsigned int parm,
				      unsigned int check_val, int srcidx);
void snd_hda_double_reset(struct hda_codec *codec);

/* cirrus_apple.c */
void cs_8409_boot_setup_data_ssm3(struct hda_codec *codec);
int cs_8409_data_config(struct hda_codec *codec);
void cs_8409_external_device_unsolicited_response_data(struct hda_codec *codec, unsigned int res);
void cs_8409_headplay_data(struct hda_codec *codec);
void cs_8409_headplaystop_data(struct hda_codec *codec);
void cs_8409_play_data(struct hda_codec *codec);
void cs_8409_play_data_ssm3(struct hda_codec *codec);
void cs_8409_playstop_data(struct hda_codec *codec);
int cs_8409_real_config(struct hda_codec *codec);
void cs_8409_cs42l83_mark_jack(struct hda_codec *codec);
void cs_8409_cs42l83_jack_report_sync(struct hda_codec *codec);

/* cirrus_apple_jack.c */
int tdm_in_use(struct hda_codec *codec, int where_flag);
int cs42l83_headphone_sense(struct hda_codec *codec);
int read_gpio_status_check(struct hda_codec *codec);
void cs_8409_external_device_unsolicited_response(struct hda_codec *codec, int skipcheck, int perform);
void cs_8409_check_status(struct hda_codec *codec, int msleeptim, int trycount);
int cs_8409_wait_for_interrupt(struct hda_codec *codec, int msleeptim, int trycount);
void cs42l83_set_power_state_on(struct hda_codec *codec, int instate);
void cs_8409_plugin_handle_detect(struct hda_codec *codec);
void cs_8409_plugin_complete_detect(struct hda_codec *codec, int unplug);
void cs_8409_headset_mike_buttons_enable(struct hda_codec *codec);

/* cirrus_apple_play.c */
int cs_8409_boot_setup_real(struct hda_codec *codec);
void cs_8409_play_real(struct hda_codec *codec);
void cs_8409_amps_disable_streaming(struct hda_codec *codec);
void cs_8409_playstop_real(struct hda_codec *codec);
void cs43l83_headset_amp_format_setup(struct hda_codec *codec, int set_stream_id, int full);
void cs_8409_headset_amp_format_setup_disable(struct hda_codec *codec, int full);
void cs42l83_headset_mike_format_setup_enable(struct hda_codec *codec, int nullformat, int full);
void cs42l83_headset_amp_disable_and_mike_format_setup_disable(struct hda_codec *codec);
void cs42l83_headset_play_setup_on(struct hda_codec *codec);
void cs_8409_enable_headset_streaming(struct hda_codec *codec);
void cs_8409_headplay_real(struct hda_codec *codec);
void cs_8409_headplaystop_real(struct hda_codec *codec);
int cs_8409_boot_setup(struct hda_codec *codec);
void cs_8409_perform_external_device_unsolicited_responses(struct hda_codec *codec);
void cs_8409_cs42l83_unsolicited_response(struct hda_codec *codec, unsigned int res);
void cs_8409_pcm_playback_pre_prepare_hook(struct hda_pcm_stream *hinfo, struct hda_codec *codec,
					   unsigned int stream_tag, unsigned int format,
					   struct snd_pcm_substream *substream, int action);
void cs_8409_playback_pcm_hook(struct hda_pcm_stream *hinfo, struct hda_codec *codec,
			       struct snd_pcm_substream *substream, int action);
void cs_8409_pcm_capture_pre_prepare_hook(struct hda_pcm_stream *hinfo, struct hda_codec *codec,
					  unsigned int stream_tag, unsigned int format,
					  struct snd_pcm_substream *substream, int action);
void cs_8409_capture_pcm_hook(struct hda_pcm_stream *hinfo, struct hda_codec *codec,
			      struct snd_pcm_substream *substream, int action);

#endif
