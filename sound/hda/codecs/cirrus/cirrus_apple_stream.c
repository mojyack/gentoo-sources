// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Apple-specific support for the Cirrus Logic CS8409 HDA bridge chip.
 * Stream format helpers split out from cirrus_apple.c.
 */

#include <linux/init.h>
#include <linux/slab.h>
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

/* struct hda_cvt_setup now lives in cirrus_apple_internal.h */

// we now setup our local cache data in the spec structure
// - cvt_setups is an opaque pointer type so we can see it here
// but we dont know how to access the data - except by re-defining hda_cvt_setup as above

/* get or create a cache entry for the given audio converter NID */
static struct hda_cvt_setup *
get_hda_cvt_setup_8409(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_cvt_setup *p;
	int i;

	snd_array_for_each(&codec->cvt_setups, i, p) {
		if (p->nid == nid)
			return p;
	}
	p = snd_array_new(&codec->cvt_setups);
	if (p)
		p->nid = nid;
	return p;
}

// so we actually need both versions - one using the hda_cvt_setup struct
// and one using our local hda_cvt_setup_apple struct

static struct hda_cvt_setup_apple *
get_hda_cvt_setup_apple_8409(struct hda_codec *codec, hda_nid_t nid)
{
	struct cs8409_apple_spec *spec = codec->spec;

	switch (nid) {
		case 0x02:
			return &spec->nid_0x02;
		case 0x03:
			return &spec->nid_0x03;
		case 0x0a:
			return &spec->nid_0x0a;
		case 0x22:
			return &spec->nid_0x22;
		case 0x23:
			return &spec->nid_0x23;
		case 0x1a:
			return &spec->nid_0x1a;
		default:
			break;
	}

	codec_err(codec, "%s: UNKNOWN NID!! 0x%02x\n", __func__, nid);

	return NULL;
}

void cs_8409_dump_stream_format(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_cvt_setup_apple *p = NULL;
	int i;

	// use explicit search so we dont create one if doesnt exist

	for (i = 0; i < codec->cvt_setups.used; i++) {
		p = snd_array_elem(&codec->cvt_setups, i);
		if (p->nid == nid)
			break;
	}

	if (p != NULL) {
	} else {  }
}

// so what do I want this to do
// the stream format will be stored in the hda_cvt_setup (at what stage is this valid??)
// - we want to remove the Apple specific stream format/channel setup
// and just call snd_hda_setup_stream - but we need the actual stream format for this
// - hopefully getting from the hda_cvt_setup struct
// unfortunately this idea of storing in the hda_cvt_setup table turns out to be not useful
// as at end of snd_hda_codec_prepare it clears out (ie zeros) all unused/inactive cache entries
// so we have to store in a separate cache using our own copied definition for hda_cvt_setup
// hda_cvt_setup_apple

// the following 2 functions are used in the sync converter functions
// where apple essentially disables streaming (set stream id to 0) updates some vendor nid parameters
// then restores streaming
// so we store the stream info in a local variable copy and set it to the unused stream id ie stream id of 0
// then cs_8409_update_from_save_stream_format sets it back to what it was
// note that the format is unchanged for these operations
// the main reason for doing it this way is because of the caching used in snd_hda_codec_setup_stream
// - if we just sent the hda verbs then the cached data in snd_hda_codec_setup_stream
// would be inconsistent with the actual state of streaming on the nid

void cs_8409_save_and_clear_stream_format(struct hda_codec *codec, hda_nid_t nid, struct hda_cvt_setup *savep)
{
	struct hda_cvt_setup *p = NULL;

	// use this to save the stream format and clear the stream id and channel

	p = get_hda_cvt_setup_8409(codec, nid);

	savep->stream_tag = p->stream_tag;
	savep->channel_id = p->channel_id;
	savep->format_id = p->format_id;

	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_CHANNEL_STREAMID, 0x00000000);
}

void cs_8409_update_from_save_stream_format(struct hda_codec *codec, hda_nid_t nid, struct hda_cvt_setup *savep, int update_stream_id, int update_format_id)
{
	struct hda_cvt_setup *p = NULL;

	// so this will ensure the format is re-updated

	p = get_hda_cvt_setup_8409(codec, nid);

	if (update_stream_id) {
		p->stream_tag = 0;
		p->channel_id = 0;
	}
	if (update_format_id)
		p->format_id = 0;

	snd_hda_codec_setup_stream(codec, nid, savep->stream_tag, savep->channel_id, savep->format_id);
}

// so these are the crucial routines for setting our local cached copy of the stream info (in the spec structure)
// we use a different struct definition (hda_cvt_setup_apple) to keep the re-definition of hda_cvt_setup more local
// note that this stream info is only stored once per stream prepare function call
//      and this routine always updates from that initial data

void cs_8409_really_update_stream_format(struct hda_codec *codec, hda_nid_t nid, int update_format_id, int update_stream_id, unsigned int new_channel_id)
{
	struct hda_cvt_setup *p = NULL;
	struct hda_cvt_setup_apple *papl = NULL;
	u32 stream_tag_sv = 0;
	int channel_id_sv = 0;
	int format_id_sv = 0;

	// so here we take our local cached format and save locally, clear out the cached values
	// then call snd_hda_codec_setup_stream with the cached values
	// this will ensure we update the HDA with the stream format

	// maybe now we should just update from our local stored version??

	papl = get_hda_cvt_setup_apple_8409(codec, nid);

	if (papl != NULL) {
		stream_tag_sv = papl->stream_tag;
		channel_id_sv = papl->channel_id;
		format_id_sv = papl->format_id;
	} else {
		codec_err(codec, "%s bad nid 0x%02x FAIL!!\n", __func__, nid);
		return;
	}

	p = get_hda_cvt_setup_8409(codec, nid);

	if (update_stream_id) {
		p->stream_tag = 0;
		p->channel_id = 0;
	}
	if (update_format_id)
		p->format_id = 0;

	if (update_stream_id == 2) {
	} else {  }

	cs_8409_dump_stream_format(codec, nid);

	if (update_stream_id == 2)
	    snd_hda_codec_setup_stream(codec, nid, stream_tag_sv, new_channel_id, format_id_sv);
	else
	    snd_hda_codec_setup_stream(codec, nid, stream_tag_sv, channel_id_sv, format_id_sv);
}

void cs_8409_store_stream_format(struct hda_codec *codec, hda_nid_t nid, unsigned int stream_tag, unsigned int format)
{
	struct hda_cvt_setup_apple *papl = NULL;

	cs_8409_dump_stream_format(codec, nid);

	// this functions sets up our local cached stream save store
	// NOTA BENE we do not do the update here - we are relying that this will be done by a call to
	// cs_8409_really_update_stream_format now we have set the format correctly

	papl = get_hda_cvt_setup_apple_8409(codec, nid);

	if (papl != NULL) {
		// NOTA BENE - we do not set the channel id here - this will be done by cs_8409_really_update_stream_format

		papl->stream_tag = stream_tag;
		papl->channel_id = 0;
		papl->format_id = format;

	} else
		codec_err(codec, "%s bad nid 0x%02x FAIL!!\n", __func__, nid);

}

//static struct nid_path *get_input_path(struct hda_codec *codec, int adc_idx, int imux_idx)

// modified from init_input_src to switch the inputs on headset plugin/unplug events
void switch_input_src(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->input_mux;
	struct nid_path *path;
	int i, c, nums;

	nums = spec->num_adc_nids;

	for (c = 0; c < nums; c++) {
		for (i = 0; i < imux->num_items; i++) {
			path = snd_hda_get_path_from_idx(codec, spec->input_paths[i][c]);
			if (path) {
				int in;
				for (in = path->depth - 1; in >= 0; in--) {
				}
				if (path->active) {
					snd_hda_activate_path(codec, path, false, false);
				} else {
					snd_hda_activate_path(codec, path, true, false);
				}
			} else {
			}
		}
	}

}

