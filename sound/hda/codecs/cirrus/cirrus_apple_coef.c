// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Apple-specific support for the Cirrus Logic CS8409 HDA bridge chip.
 * Coef/I2C primitives split out from cirrus_apple.c.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/iopoll.h>
#include <sound/core.h>

#include "cs8409.h"
#include "cirrus_apple_internal.h"

#define mycodec_info(...)
#define mycodec_i2c_info(...)
#define mydev_info(...)
#define mycodec_dbg(...)
#define myprintk_dbg(...)
#define myprintk(...)

inline unsigned int cs_8409_vendor_coef_get(struct hda_codec *codec, unsigned int idx)
{
	unsigned int retval;
	snd_hda_codec_read(codec, CS8409_VENDOR_NID, 0,
			    AC_VERB_GET_COEF_INDEX, 0);
	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0,
			    AC_VERB_SET_COEF_INDEX, idx);
	retval = snd_hda_codec_read(codec, CS8409_VENDOR_NID, 0,
				  AC_VERB_GET_PROC_COEF, 0);
	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0,
			    AC_VERB_SET_COEF_INDEX, 0);
	return retval;
}

inline void cs_8409_vendor_coef_set(struct hda_codec *codec, unsigned int idx,
				      unsigned int coef)
{
	snd_hda_codec_read(codec, CS8409_VENDOR_NID, 0,
			    AC_VERB_GET_COEF_INDEX, 0);
	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0,
			    AC_VERB_SET_COEF_INDEX, idx);
	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0,
			    AC_VERB_SET_PROC_COEF, coef);
	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0,
			    AC_VERB_SET_COEF_INDEX, 0);
	// appears to return 0
}

inline unsigned int cs_8409_vendor_coef_set_mask(struct hda_codec *codec, unsigned int idx,
				      unsigned int coef, unsigned int mask, unsigned int srcval, int srcidx)
{
	// for the moment hackily add srcidx argument while debugging
	unsigned int retval;
	unsigned int mask_coef;
	snd_hda_codec_read(codec, CS8409_VENDOR_NID, 0,
			    AC_VERB_GET_COEF_INDEX, 0);
	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0,
			    AC_VERB_SET_COEF_INDEX, idx);
	retval = snd_hda_codec_read(codec, CS8409_VENDOR_NID, 0,
				  AC_VERB_GET_PROC_COEF, 0);
	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0,
			    AC_VERB_SET_COEF_INDEX, idx);
	mask_coef = (retval & ~mask) | coef;
	if (srcval != 0) {
		if (srcidx != 0 && mask_coef != srcval) {
		} else {  }
	} else {
		//if (mask != 0xffff)
	}
	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0,
			    AC_VERB_SET_PROC_COEF, mask_coef);
	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0,
			    AC_VERB_SET_COEF_INDEX, 0);
	// appears to return 0
	// lets return the read value for checking
	return retval;
}

inline void cs_8409_vendor_enableI2Cclock(struct hda_codec *codec, unsigned int flag)
{

	unsigned int retval = 0;
	unsigned int newval = 0;

	// note that apple returns the status value with data value in returned parameter
	// snd_hda_codec_read just returns value - not sure what happens about errors
	// looks as tho its assumed -1 is not a valid return value
	// ah yes - because max val is 16 bit quantity

	retval = cs_8409_vendor_coef_get(codec, 0x0);
	//if (retval == -1)

	if (retval == -1)
		return;

	newval = retval;
	if (flag)
		newval |= 0x8;
	else
		newval = (retval & 0xfffffff7);

	cs_8409_vendor_coef_set(codec, 0x0, newval);

}

// define i2cRead and i2cWrite functions
// following Apple
unsigned int cs_8409_vendor_i2cRead(struct hda_codec *codec, unsigned int i2c_address,
					    unsigned int i2c_reg, unsigned int paged)
{
	// AppleHDAFunctionGroupCS8409::_i2cRead(bool, unsigned short, unsigned short, unsigned int*)
	// note that last argument is return data
	unsigned int i2c_reg_data;
	unsigned int retval;
	int rdcnt;

	hda_set_node_power_state_dbg(codec, codec->core.afg, AC_PWRST_D0, 0);
	// exit on error

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, AC_VERB_SET_PROC_STATE, 0x00000001);
	// exit on error

	cs_8409_vendor_enableI2Cclock(codec, 0x1);

	cs_8409_vendor_coef_set(codec, 0x59, i2c_address);

	if (paged) {
		unsigned int retval1;

		cs_8409_vendor_coef_set(codec, 0x5d, i2c_reg >> 8);

		rdcnt = -8;
sleep1:
		retval1 = cs_8409_vendor_coef_get(codec, 0x5c);

		if (retval1 != -1) {
			retval1 &= 0x18;
			if (retval1 != 0x18) {
				if (rdcnt < 0) {
					rdcnt++;
					// need 0x2 according to Apple
					usleep_range(2000,4000);
					goto sleep1;
				}
			}
		}
	}

	// so the i2c register is stored in the low byte of i2c_reg
	// shift it 8 bits to left for sending as coefficient data (16 bits)
	// hmm - why do I need a mask??
	// think either we mask here or in cs_8409_vendor_coef_set
	// Apple is using short ints so likely automasked
	i2c_reg_data = (i2c_reg << 8) & 0x0ffff;

	cs_8409_vendor_coef_set(codec, 0x5e, i2c_reg_data);
	//if (retval == -1)

	retval = cs_8409_vendor_coef_get(codec, 0x5c);
	//if (retval == -1)

	rdcnt = -8;
sleep2:
	retval = cs_8409_vendor_coef_get(codec, 0x5c);
	//if (retval == -1)

	if (retval != -1) {
		retval &= 0x18;
		if (retval != 0x18) {
			if (rdcnt < 0) {
				rdcnt++;
				// need 0x2 according to Apple
				usleep_range(2000,4000);
				goto sleep2;
			}
		}
	}

	// well thats interesting - looks as though the 16 bit return
	// has the register in bits 15-8 and the data in 7-0
	// probably should mask the data out
	retval = cs_8409_vendor_coef_get(codec, 0x5e);
	//if (retval == -1)

	cs_8409_vendor_enableI2Cclock(codec, 0x0);
	// exit on error

	// exit on error

	return retval;

}

unsigned int cs_8409_vendor_i2cWrite(struct hda_codec *codec, unsigned int i2c_address,
				      unsigned int i2c_reg, unsigned int i2c_data, unsigned int paged)
{
	// AppleHDAFunctionGroupCS8409::_i2cWrite(bool, unsigned short, unsigned short, unsigned short)
	unsigned int retval;
	unsigned int i2c_reg_data;
	int rdcnt;

	hda_set_node_power_state(codec, codec->core.afg, AC_PWRST_D0);
	// exit on error

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, AC_VERB_SET_PROC_STATE, 0x00000001);
	// exit on error

	cs_8409_vendor_enableI2Cclock(codec, 0x1);

	cs_8409_vendor_coef_set(codec, 0x59, i2c_address);

	if (paged) {
		unsigned int retval1;

		cs_8409_vendor_coef_set(codec, 0x5d, i2c_reg >> 8);

		retval1 = cs_8409_vendor_coef_get(codec, 0x5c);

		rdcnt = -8;
sleep1:
		retval1 = cs_8409_vendor_coef_get(codec, 0x5c);

		if (retval1 != -1) {
			retval1 &= 0x18;
			if (retval1 != 0x18) {
				if (rdcnt < 0) {
					rdcnt++;
					// need 0x2 according to Apple
					usleep_range(2000,4000);
					goto sleep1;
				}
			}
		}
	}

	// so the i2c register is stored in the low byte of i2c_reg
	// shift it 8 bits to left for sending as coefficient data (16 bits)
	// then or in the 8 byte data
	// mask here or in cs_8409_vendor_coef_set?
	i2c_reg_data = ((i2c_reg << 8) & 0x0ff00) | ( i2c_data & 0x0ff);

	cs_8409_vendor_coef_set(codec, 0x5d, i2c_reg_data);
	//if (retval == -1)

	retval = cs_8409_vendor_coef_get(codec, 0x5c);
	//if (retval == -1)

	rdcnt = -8;
sleep2:
	retval = cs_8409_vendor_coef_get(codec, 0x5c);
	//if (retval == -1)

	if (retval != -1) {
		retval &= 0x18;
		if (retval != 0x18) {
			if (rdcnt < 0) {
				rdcnt++;
				// need 0x2 according to Apple
				usleep_range(2000,4000);
				goto sleep2;
			}
		}
	}

	cs_8409_vendor_enableI2Cclock(codec, 0x0);
	// exit on error

	// exit on error

	return retval;
}

unsigned int cs_8409_vendor_i2cWriteMask(struct hda_codec *codec, unsigned int i2c_address,
				      unsigned int i2c_reg, unsigned int i2c_mask, unsigned int i2c_data, unsigned int paged)
{
	// masked version to emulate AppleHDATDMDevice::maskWriteReg(unsigned short, unsigned char, unsigned char)

	unsigned int retval;
	unsigned int mask_val;

	retval = cs_8409_vendor_i2cRead(codec, i2c_address, i2c_reg, paged);

	mask_val = (retval & ~i2c_mask);
	mask_val |= (i2c_data & i2c_mask);

	retval = cs_8409_vendor_i2cWrite(codec, i2c_address, i2c_reg, mask_val, paged);

	return retval;
}

// this seems to be how to do a list of verbs
// there is command to do a sequence of these
// snd_hda_sequence_write

void snd_hda_coef_item(struct hda_codec *codec, u16 write_flag, hda_nid_t nid, u32 idx, u32 param, u32 retdata, int srcidx)
{
	if (write_flag == 2) {
		// NOTA BENE - just for initial debugging differentiation - pass a mask of 0xffff for total overwrite
		// use snd_hda_coef_item_masked for actual masked setup
		unsigned int retreadval = cs_8409_vendor_coef_set_mask(codec, idx, param, 0xffff, 0, srcidx);
		if (retreadval != retdata) {
			if (srcidx > 0)
				codec_dbg(codec, "command BAD mask return value at %d: 0x%08x 0x%08x (0x%02x, 0x%04x, 0x%04x)\n",srcidx,retreadval,retdata,nid,idx,param);
			//else
		}
	} else if (write_flag == 1)
		cs_8409_vendor_coef_set(codec, idx, param);
	else {
		unsigned int retval = cs_8409_vendor_coef_get(codec, idx);
		if (retval != retdata) {
			if (srcidx > 0)
				codec_dbg(codec, "command BAD      return value at %d: 0x%08x 0x%08x (0x%02x, 0x%04x, 0x%04x)\n",srcidx,retval,retdata,nid,idx,param);
			//else
		}
	}
}

// just create a special routine if we wish to return the actual value for the moment
int snd_hda_coef_item_check(struct hda_codec *codec, u16 write_flag, hda_nid_t nid, u32 idx, u32 param, u32 retdata, int srcidx)
{
	int retval = 0;

	if (write_flag == 2)
		codec_dbg(codec, "command BAD usage of %s %d\n", __func__, write_flag);
	else if (write_flag == 1)
		codec_dbg(codec, "command BAD usage of %s %d\n", __func__, write_flag);
	else {
		unsigned int retval1 = cs_8409_vendor_coef_get(codec, idx);
		if (retval1 != retdata) {
			if (srcidx > 0)
				codec_dbg(codec, "command BAD      return value at %d: 0x%08x 0x%08x (0x%02x, 0x%04x, 0x%04x)\n",srcidx,retval1,retdata,nid,idx,param);
			//else
		}
		retval = retval1;
	}

	return retval;
}

void snd_hda_coef_item_masked(struct hda_codec *codec, u16 write_flag, hda_nid_t nid, u32 idx, u32 param, u32 mask, u32 retdata, u32 srcval, int srcidx)
{
	if (write_flag != 2)
		codec_dbg(codec, "command BAD usage of %s %d\n", __func__, write_flag);
	else {
		unsigned int retreadval = cs_8409_vendor_coef_set_mask(codec, idx, param, mask, srcval, srcidx);
		if (retreadval != retdata) {
			if (srcidx > 0)
				codec_dbg(codec, "command BAD mask return value at %d: 0x%08x 0x%08x (0x%02x, 0x%04x, 0x%04x)\n",srcidx,retreadval,retdata,nid,idx,param);
			//else
		}
	}
}

inline unsigned int snd_hda_codec_read_check(struct hda_codec *codec, hda_nid_t nid, int flags, unsigned int verb, unsigned int parm, unsigned int check_val, int srcidx)
{
	unsigned int retval;
	retval = snd_hda_codec_read(codec, nid, flags, verb, parm);

	if (retval == -1)
		return retval;

	if (srcidx > 0)
		if (retval != check_val)
			codec_dbg(codec, "command BAD read check return value at %d: 0x%08x 0x%08x (0x%02x, 0x%03x 0x%04x)\n",srcidx,retval,check_val,nid,verb,parm);

	return retval;
}

void snd_hda_double_reset(struct hda_codec *codec)
{
	// still not clear if this does anything
	snd_hda_codec_write(codec, codec->core.afg, 0, 0xfff, 0);
	// so far the double reset seems to give bad results - lots of registers dont compare
	usleep_range(1000, 2000);
	// apparently should use usleep_range for a few ms
}

