// SPDX-License-Identifier: GPL-2.0
/*
 * Apple Touch Bar Driver
 *
 * Copyright (c) 2017-2018 Ronald Tschalär
 */

/*
 * Recent MacBookPro models (13,[23] and 14,[23]) have a touch bar, which
 * is exposed via several USB interfaces. MacOS supports a fancy mode
 * where arbitrary buttons can be defined; this driver currently only
 * supports the simple mode that consists of 3 predefined layouts
 * (escape-only, esc + special keys, and esc + function keys).
 *
 * The first USB HID interface supports two reports, an input report that
 * is used to report the key presses, and an output report which can be
 * used to set the touch bar "mode": touch bar off (in which case no touches
 * are reported at all), escape key only, escape + 12 function keys, and
 * escape + several special keys (including brightness, audio volume,
 * etc). The second interface supports several, complex reports, most of
 * which are unknown at this time, but one of which has been determined to
 * allow for controlling of the touch bar's brightness: off (though touches
 * are still reported), dimmed, and full brightness. This driver makes
 * use of these two reports.
 *
 * Unlike the T2 touch bar (hid-appletb-kbd), the T1 iBridge exposes the
 * mode and brightness reports as plain vendor/class control transfers
 * rather than as a backlight class device, and those transfers are
 * synchronous and may sleep. The mode/brightness changes are therefore
 * funnelled through a workqueue, while an inactivity timer (which runs in
 * atomic context) only schedules that work; the dim -> off staging and the
 * Fn-key mode toggle otherwise mirror the T2 driver.
 */

#define dev_fmt(fmt) "tb: " fmt

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/timer.h>
#include <linux/usb/ch9.h>
#include <linux/usb.h>
#include <linux/workqueue.h>

#include "apple-ibridge.h"
#include "../apple-touchbar.h"

#define HID_UP_APPLE		0xff120000
#define HID_USAGE_MODE		(HID_UP_CUSTOM | 0x0004)
#define HID_USAGE_APPLE_APP	(HID_UP_APPLE  | 0x0001)
#define HID_USAGE_DISP		(HID_UP_APPLE  | 0x0021)

/* touch bar display (brightness) report values - T1 specific */
#define APPLETB_DISP_ON		1
#define APPLETB_DISP_DIM	2
#define APPLETB_DISP_OFF	4

#define APPLETB_DEVID_KEYBOARD	1
#define APPLETB_DEVID_TOUCHPAD	2

static int appletb_tb_def_mode = APPLETB_MODE_SPCL;
module_param_named(mode, appletb_tb_def_mode, int, 0444);
MODULE_PARM_DESC(mode, "Default touch bar mode:\n"
			 "    0 - escape key only\n"
			 "    1 - function keys\n"
			 "    [2] - special keys");

static bool appletb_tb_fn_toggle = true;
module_param_named(fntoggle, appletb_tb_fn_toggle, bool, 0644);
MODULE_PARM_DESC(fntoggle, "Switch between Fn and media controls on pressing Fn key");

static bool appletb_tb_autodim = true;
module_param_named(autodim, appletb_tb_autodim, bool, 0644);
MODULE_PARM_DESC(autodim, "Automatically dim and turn off the touch bar after some time");

static int appletb_tb_dim_timeout = 60;
module_param_named(dim_timeout, appletb_tb_dim_timeout, int, 0644);
MODULE_PARM_DESC(dim_timeout, "Dim timeout in sec");

static int appletb_tb_idle_timeout = 15;
module_param_named(idle_timeout, appletb_tb_idle_timeout, int, 0644);
MODULE_PARM_DESC(idle_timeout, "Idle timeout (after dimming) in sec");

struct appletb_device {
	bool			active;
	struct device		*log_dev;

	struct appletb_report_info {
		struct hid_device	*hdev;
		struct usb_interface	*usb_iface;
		unsigned int		usb_epnum;
		unsigned int		report_id;
		unsigned int		report_type;
		bool			suspended;
	}			mode_info, disp_info;

	struct input_handler	inp_handler;
	struct input_handle	kbd_handle;
	struct input_handle	tpd_handle;

	struct delayed_work	tb_work;
	struct timer_list	inactivity_timer;

	/* protects the desired/current state below */
	spinlock_t		lock;
	u8			want_mode;
	u8			cur_mode;
	u8			want_disp;
	u8			cur_disp;
	u8			saved_mode;
	bool			has_dimmed;
	bool			has_turned_off;
};

static struct hid_driver appletb_hid_driver;

static int appletb_send_hid_report(struct appletb_report_info *rinfo,
				   __u8 requesttype, void *data, __u16 size)
{
	struct usb_device *dev = interface_to_usbdev(rinfo->usb_iface);
	u8 ifnum = rinfo->usb_iface->cur_altsetting->desc.bInterfaceNumber;
	bool autopm_off;
	void *buffer;
	int tries = 0;
	int rc;

	buffer = kmemdup(data, size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	/* make sure the device is awake for the duration of the transfer */
	autopm_off = usb_autopm_get_interface(rinfo->usb_iface) == 0;

	do {
		rc = usb_control_msg(dev,
				     usb_sndctrlpipe(dev, rinfo->usb_epnum),
				     HID_REQ_SET_REPORT, requesttype,
				     rinfo->report_type << 8 | rinfo->report_id,
				     ifnum, buffer, size, 2000);
		if (rc != -EPIPE)
			break;

		usleep_range(1000 << tries, 3000 << tries);
	} while (++tries < 5);

	if (autopm_off)
		usb_autopm_put_interface(rinfo->usb_iface);

	kfree(buffer);

	return (rc > 0) ? 0 : rc;
}

static int appletb_set_tb_mode(struct appletb_device *tb_dev, u8 mode)
{
	int rc;

	if (!tb_dev->mode_info.usb_iface)
		return -ENOTCONN;

	rc = appletb_send_hid_report(&tb_dev->mode_info,
				     USB_DIR_OUT | USB_TYPE_VENDOR |
						USB_RECIP_DEVICE,
				     &mode, 1);
	if (rc < 0)
		dev_err(tb_dev->log_dev,
			"Failed to set touch bar mode to %u (%d)\n", mode, rc);

	return rc;
}

static int appletb_set_tb_disp(struct appletb_device *tb_dev, u8 disp)
{
	u8 report[] = { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	int rc;

	if (!tb_dev->disp_info.usb_iface)
		return -ENOTCONN;

	report[0] = tb_dev->disp_info.report_id;
	report[2] = disp;

	rc = appletb_send_hid_report(&tb_dev->disp_info,
				     USB_DIR_OUT | USB_TYPE_CLASS |
						USB_RECIP_INTERFACE,
				     report, sizeof(report));
	if (rc < 0)
		dev_err(tb_dev->log_dev,
			"Failed to set touch bar display to %u (%d)\n", disp, rc);

	return rc;
}

/*
 * Push the desired mode/display to the device. Runs in process context so it
 * may issue the (sleeping) control transfers.
 */
static void appletb_set_tb_worker(struct work_struct *work)
{
	struct appletb_device *tb_dev =
		container_of(work, struct appletb_device, tb_work.work);
	unsigned long flags;
	u8 mode, disp;

	spin_lock_irqsave(&tb_dev->lock, flags);
	if (!tb_dev->active) {
		spin_unlock_irqrestore(&tb_dev->lock, flags);
		return;
	}
	mode = tb_dev->want_mode;
	disp = tb_dev->want_disp;
	spin_unlock_irqrestore(&tb_dev->lock, flags);

	if (mode != tb_dev->cur_mode && appletb_set_tb_mode(tb_dev, mode) == 0)
		tb_dev->cur_mode = mode;

	if (disp != tb_dev->cur_disp && appletb_set_tb_disp(tb_dev, disp) == 0)
		tb_dev->cur_disp = disp;
}

static void appletb_set_mode(struct appletb_device *tb_dev, u8 mode)
{
	unsigned long flags;

	spin_lock_irqsave(&tb_dev->lock, flags);
	if (tb_dev->want_mode != mode) {
		tb_dev->want_mode = mode;
		schedule_delayed_work(&tb_dev->tb_work, 0);
	}
	spin_unlock_irqrestore(&tb_dev->lock, flags);
}

/* Bring the display back to full brightness and re-arm the dim timer. */
static void appletb_reset_inactivity_timer(struct appletb_device *tb_dev)
{
	unsigned long flags;

	if (!appletb_tb_autodim)
		return;

	spin_lock_irqsave(&tb_dev->lock, flags);
	if (tb_dev->has_dimmed || tb_dev->has_turned_off) {
		tb_dev->want_disp = APPLETB_DISP_ON;
		tb_dev->has_dimmed = false;
		tb_dev->has_turned_off = false;
		schedule_delayed_work(&tb_dev->tb_work, 0);
	}
	spin_unlock_irqrestore(&tb_dev->lock, flags);

	mod_timer(&tb_dev->inactivity_timer,
		  jiffies + secs_to_jiffies(appletb_tb_dim_timeout));
}

static void appletb_inactivity_timer(struct timer_list *t)
{
	struct appletb_device *tb_dev =
		timer_container_of(tb_dev, t, inactivity_timer);
	unsigned long flags;

	if (!appletb_tb_autodim)
		return;

	spin_lock_irqsave(&tb_dev->lock, flags);
	if (!tb_dev->has_dimmed) {
		tb_dev->want_disp = APPLETB_DISP_DIM;
		tb_dev->has_dimmed = true;
		schedule_delayed_work(&tb_dev->tb_work, 0);
		mod_timer(&tb_dev->inactivity_timer,
			  jiffies + secs_to_jiffies(appletb_tb_idle_timeout));
	} else if (!tb_dev->has_turned_off) {
		tb_dev->want_disp = APPLETB_DISP_OFF;
		tb_dev->has_turned_off = true;
		schedule_delayed_work(&tb_dev->tb_work, 0);
	}
	spin_unlock_irqrestore(&tb_dev->lock, flags);
}

static ssize_t mode_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct appletb_device *tb_dev =
		appleib_get_drvdata(dev_get_drvdata(dev), &appletb_hid_driver);

	return sysfs_emit(buf, "%d\n", tb_dev->cur_mode);
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t size)
{
	struct appletb_device *tb_dev =
		appleib_get_drvdata(dev_get_drvdata(dev), &appletb_hid_driver);
	u8 mode;
	int rc;

	rc = kstrtou8(buf, 0, &mode);
	if (rc)
		return rc;

	if (mode > APPLETB_MODE_MAX)
		return -EINVAL;

	appletb_set_mode(tb_dev, mode);

	return size;
}
static DEVICE_ATTR_RW(mode);

static struct attribute *appletb_attrs[] = {
	&dev_attr_mode.attr,
	NULL,
};

static const struct attribute_group appletb_attr_group = {
	.attrs = appletb_attrs,
};

static int appletb_hid_event(struct hid_device *hdev, struct hid_field *field,
			     struct hid_usage *usage, __s32 value)
{
	struct appletb_device *tb_dev =
		appleib_get_drvdata(hid_get_drvdata(hdev), &appletb_hid_driver);
	struct key_entry *translation;
	struct input_dev *input;
	int slot;

	/* Only interested in touch bar keyboard events */
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_KEYBOARD ||
	    usage->type != EV_KEY)
		return 0;

	slot = appletb_tb_key_to_slot(usage->code);
	if (slot < 0)
		return 0;

	if (!tb_dev->active)
		return 0;

	appletb_reset_inactivity_timer(tb_dev);

	input = field->hidinput->input;
	translation = sparse_keymap_entry_from_scancode(input, usage->code);

	if (translation && tb_dev->cur_mode == APPLETB_MODE_SPCL) {
		input_event(input, usage->type, translation->keycode, value);
		return 1;
	}

	/* suppress all touch bar keys while the touch bar is off */
	return tb_dev->cur_mode == APPLETB_MODE_OFF;
}

static void appletb_inp_event(struct input_handle *handle, unsigned int type,
			      unsigned int code, int value)
{
	struct appletb_device *tb_dev = handle->private;

	if (!tb_dev->active)
		return;

	appletb_reset_inactivity_timer(tb_dev);

	if (type == EV_KEY && code == KEY_FN && appletb_tb_fn_toggle &&
	    (tb_dev->cur_mode == APPLETB_MODE_SPCL ||
	     tb_dev->cur_mode == APPLETB_MODE_FN)) {
		if (value == 1) {
			tb_dev->saved_mode = tb_dev->cur_mode;
			appletb_set_mode(tb_dev,
					 tb_dev->cur_mode == APPLETB_MODE_SPCL ?
						APPLETB_MODE_FN : APPLETB_MODE_SPCL);
		} else if (value == 0) {
			if (tb_dev->saved_mode != tb_dev->cur_mode)
				appletb_set_mode(tb_dev, tb_dev->saved_mode);
		}
	}
}

/* Find and save the usb-device associated with the touch bar input device */
static struct usb_interface *appletb_get_usb_iface(struct hid_device *hdev)
{
	struct device *dev = &hdev->dev;

	/* in kernel: is_usb_interface(dev) */
	while (dev && (!dev->type || strcmp(dev->type->name, "usb_interface")))
		dev = dev->parent;

	return dev ? to_usb_interface(dev) : NULL;
}

static int appletb_inp_connect(struct input_handler *handler,
			       struct input_dev *dev,
			       const struct input_device_id *id)
{
	struct appletb_device *tb_dev = handler->private;
	struct input_handle *handle;
	int rc;

	if (id->driver_info == APPLETB_DEVID_KEYBOARD) {
		handle = &tb_dev->kbd_handle;
		handle->name = "tbkbd";
	} else if (id->driver_info == APPLETB_DEVID_TOUCHPAD) {
		handle = &tb_dev->tpd_handle;
		handle->name = "tbtpad";
	} else {
		dev_err(tb_dev->log_dev, "Unknown device id (%lu)\n",
			id->driver_info);
		return -ENOENT;
	}

	if (handle->dev) {
		dev_err(tb_dev->log_dev,
			"Duplicate connect to %s input device\n", handle->name);
		return -EEXIST;
	}

	handle->open = 0;
	handle->dev = input_get_device(dev);
	handle->handler = handler;
	handle->private = tb_dev;

	rc = input_register_handle(handle);
	if (rc)
		goto err_free_dev;

	rc = input_open_device(handle);
	if (rc)
		goto err_unregister_handle;

	dev_dbg(tb_dev->log_dev, "Connected to %s input device\n", handle->name);

	return 0;

 err_unregister_handle:
	input_unregister_handle(handle);
 err_free_dev:
	input_put_device(handle->dev);
	handle->dev = NULL;
	return rc;
}

static void appletb_inp_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);

	input_put_device(handle->dev);
	handle->dev = NULL;
}

static int appletb_input_configured(struct hid_device *hdev,
				    struct hid_input *hidinput)
{
	int idx;
	struct input_dev *input = hidinput->input;

	/*
	 * Clear various input capabilities that are blindly set by the hid
	 * driver (usbkbd.c)
	 */
	memset(input->evbit, 0, sizeof(input->evbit));
	memset(input->keybit, 0, sizeof(input->keybit));
	memset(input->ledbit, 0, sizeof(input->ledbit));

	__set_bit(EV_REP, input->evbit);

	sparse_keymap_setup(input, appletb_fn_keymap, NULL);

	for (idx = 0; appletb_fn_keymap[idx].type != KE_END; idx++)
		input_set_capability(input, EV_KEY, appletb_fn_keymap[idx].code);

	return 0;
}

static int appletb_fill_report_info(struct appletb_device *tb_dev,
				    struct hid_device *hdev)
{
	struct appletb_report_info *report_info = NULL;
	struct usb_interface *usb_iface;
	struct hid_field *field;

	field = appleib_find_hid_field(hdev, HID_GD_KEYBOARD, HID_USAGE_MODE);
	if (field) {
		report_info = &tb_dev->mode_info;
	} else {
		field = appleib_find_hid_field(hdev, HID_USAGE_APPLE_APP,
					       HID_USAGE_DISP);
		if (field)
			report_info = &tb_dev->disp_info;
	}

	if (!report_info)
		return 0;

	usb_iface = appletb_get_usb_iface(hdev);
	if (!usb_iface) {
		dev_err(tb_dev->log_dev,
			"Failed to find usb interface for hid device %s\n",
			dev_name(&hdev->dev));
		return -ENODEV;
	}

	report_info->hdev = hdev;

	report_info->usb_iface = usb_get_intf(usb_iface);
	report_info->usb_epnum = 0;

	report_info->report_id = field->report->id;
	switch (field->report->type) {
	case HID_INPUT_REPORT:
		report_info->report_type = 0x01; break;
	case HID_OUTPUT_REPORT:
		report_info->report_type = 0x02; break;
	case HID_FEATURE_REPORT:
		report_info->report_type = 0x03; break;
	default:
		break;
	}

	return 1;
}

static struct appletb_report_info *
appletb_get_report_info(struct appletb_device *tb_dev, struct hid_device *hdev)
{
	if (hdev == tb_dev->mode_info.hdev)
		return &tb_dev->mode_info;
	if (hdev == tb_dev->disp_info.hdev)
		return &tb_dev->disp_info;
	return NULL;
}

static const struct input_device_id appletb_input_devices[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_BUS |
			INPUT_DEVICE_ID_MATCH_KEYBIT,
		.bustype = BUS_SPI,
		.keybit = { [BIT_WORD(KEY_FN)] = BIT_MASK(KEY_FN) },
		.driver_info = APPLETB_DEVID_KEYBOARD,
	},			/* Builtin keyboard device */
	{
		.flags = INPUT_DEVICE_ID_MATCH_BUS |
			INPUT_DEVICE_ID_MATCH_KEYBIT,
		.bustype = BUS_SPI,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.driver_info = APPLETB_DEVID_TOUCHPAD,
	},			/* Builtin touchpad device */
	{ },			/* Terminating zero entry */
};

static int appletb_probe(struct hid_device *hdev,
			 const struct hid_device_id *id)
{
	struct appletb_device *tb_dev =
		appleib_get_drvdata(hid_get_drvdata(hdev), &appletb_hid_driver);
	struct appletb_report_info *report_info;
	unsigned long flags;
	int rc;

	/* initialize the report info */
	rc = appletb_fill_report_info(tb_dev, hdev);
	if (rc < 0)
		goto error;

	/* do setup once we have both interfaces */
	if (!tb_dev->mode_info.hdev || !tb_dev->disp_info.hdev)
		return 0;

	if (appletb_tb_def_mode < 0 || appletb_tb_def_mode > APPLETB_MODE_MAX)
		appletb_tb_def_mode = APPLETB_MODE_SPCL;

	spin_lock_irqsave(&tb_dev->lock, flags);
	tb_dev->active = true;
	tb_dev->cur_mode = APPLETB_MODE_OFF;
	tb_dev->cur_disp = APPLETB_DISP_OFF;
	tb_dev->want_mode = appletb_tb_def_mode;
	tb_dev->want_disp = APPLETB_DISP_ON;
	tb_dev->saved_mode = appletb_tb_def_mode;
	tb_dev->has_dimmed = false;
	tb_dev->has_turned_off = false;
	spin_unlock_irqrestore(&tb_dev->lock, flags);

	/* set up the input handler */
	tb_dev->inp_handler.event = appletb_inp_event;
	tb_dev->inp_handler.connect = appletb_inp_connect;
	tb_dev->inp_handler.disconnect = appletb_inp_disconnect;
	tb_dev->inp_handler.name = "appletb";
	tb_dev->inp_handler.id_table = appletb_input_devices;
	tb_dev->inp_handler.private = tb_dev;

	rc = input_register_handler(&tb_dev->inp_handler);
	if (rc) {
		dev_err(tb_dev->log_dev,
			"Unable to register keyboard handler (%d)\n", rc);
		goto mark_inactive;
	}

	rc = sysfs_create_group(&tb_dev->mode_info.hdev->dev.kobj,
				&appletb_attr_group);
	if (rc) {
		dev_err(tb_dev->log_dev,
			"Failed to create sysfs attributes (%d)\n", rc);
		goto unreg_handler;
	}

	/* push the initial mode/brightness and start the dim timer */
	schedule_delayed_work(&tb_dev->tb_work, 0);
	if (appletb_tb_autodim)
		mod_timer(&tb_dev->inactivity_timer,
			  jiffies + secs_to_jiffies(appletb_tb_dim_timeout));

	dev_dbg(tb_dev->log_dev, "Touchbar activated\n");

	return 0;

unreg_handler:
	input_unregister_handler(&tb_dev->inp_handler);
mark_inactive:
	spin_lock_irqsave(&tb_dev->lock, flags);
	tb_dev->active = false;
	spin_unlock_irqrestore(&tb_dev->lock, flags);
	cancel_delayed_work_sync(&tb_dev->tb_work);

	report_info = appletb_get_report_info(tb_dev, hdev);
	if (report_info) {
		usb_put_intf(report_info->usb_iface);
		report_info->usb_iface = NULL;
		report_info->hdev = NULL;
	}
error:
	return rc;
}

static void appletb_remove(struct hid_device *hdev)
{
	struct appletb_device *tb_dev =
		appleib_get_drvdata(hid_get_drvdata(hdev), &appletb_hid_driver);
	struct appletb_report_info *report_info;
	unsigned long flags;

	if ((hdev == tb_dev->mode_info.hdev && tb_dev->disp_info.hdev) ||
	    (hdev == tb_dev->disp_info.hdev && tb_dev->mode_info.hdev)) {
		sysfs_remove_group(&tb_dev->mode_info.hdev->dev.kobj,
				   &appletb_attr_group);

		input_unregister_handler(&tb_dev->inp_handler);

		spin_lock_irqsave(&tb_dev->lock, flags);
		tb_dev->active = false;
		spin_unlock_irqrestore(&tb_dev->lock, flags);

		timer_delete_sync(&tb_dev->inactivity_timer);
		cancel_delayed_work_sync(&tb_dev->tb_work);

		appletb_set_tb_mode(tb_dev, APPLETB_MODE_OFF);
		appletb_set_tb_disp(tb_dev, APPLETB_DISP_ON);

		dev_info(tb_dev->log_dev, "Touchbar deactivated\n");
	}

	report_info = appletb_get_report_info(tb_dev, hdev);
	if (report_info) {
		usb_put_intf(report_info->usb_iface);
		report_info->usb_iface = NULL;
		report_info->hdev = NULL;
	}
}

#ifdef CONFIG_PM
static int appletb_suspend(struct hid_device *hdev, pm_message_t message)
{
	struct appletb_device *tb_dev =
		appleib_get_drvdata(hid_get_drvdata(hdev), &appletb_hid_driver);
	unsigned long flags;
	bool all_suspended = false;

	if (message.event != PM_EVENT_SUSPEND &&
	    message.event != PM_EVENT_FREEZE)
		return 0;

	spin_lock_irqsave(&tb_dev->lock, flags);

	if (!tb_dev->mode_info.suspended && !tb_dev->disp_info.suspended) {
		tb_dev->active = false;
		tb_dev->saved_mode = tb_dev->cur_mode;
	}

	appletb_get_report_info(tb_dev, hdev)->suspended = true;

	if ((!tb_dev->mode_info.hdev || tb_dev->mode_info.suspended) &&
	    (!tb_dev->disp_info.hdev || tb_dev->disp_info.suspended))
		all_suspended = true;

	spin_unlock_irqrestore(&tb_dev->lock, flags);

	timer_delete_sync(&tb_dev->inactivity_timer);
	cancel_delayed_work_sync(&tb_dev->tb_work);

	if (!all_suspended)
		return 0;

	/*
	 * Force both mode and display off so the touch bar resumes in a known
	 * (and responsive) state - this matches the state after boot.
	 */
	if (message.event == PM_EVENT_SUSPEND) {
		appletb_set_tb_mode(tb_dev, APPLETB_MODE_OFF);
		appletb_set_tb_disp(tb_dev, APPLETB_DISP_OFF);
	}

	spin_lock_irqsave(&tb_dev->lock, flags);
	tb_dev->cur_mode = APPLETB_MODE_OFF;
	tb_dev->cur_disp = APPLETB_DISP_OFF;
	spin_unlock_irqrestore(&tb_dev->lock, flags);

	dev_info(tb_dev->log_dev, "Touchbar suspended\n");

	return 0;
}

static int appletb_reset_resume(struct hid_device *hdev)
{
	struct appletb_device *tb_dev =
		appleib_get_drvdata(hid_get_drvdata(hdev), &appletb_hid_driver);
	unsigned long flags;

	spin_lock_irqsave(&tb_dev->lock, flags);

	appletb_get_report_info(tb_dev, hdev)->suspended = false;

	if ((tb_dev->mode_info.hdev && !tb_dev->mode_info.suspended) &&
	    (tb_dev->disp_info.hdev && !tb_dev->disp_info.suspended)) {
		tb_dev->active = true;
		tb_dev->want_mode = tb_dev->saved_mode;
		tb_dev->want_disp = APPLETB_DISP_ON;
		tb_dev->has_dimmed = false;
		tb_dev->has_turned_off = false;

		schedule_delayed_work(&tb_dev->tb_work, 0);
		if (appletb_tb_autodim)
			mod_timer(&tb_dev->inactivity_timer,
				  jiffies +
				  secs_to_jiffies(appletb_tb_dim_timeout));

		dev_info(tb_dev->log_dev, "Touchbar resumed\n");
	}

	spin_unlock_irqrestore(&tb_dev->lock, flags);

	return 0;
}
#endif

static struct appletb_device *appletb_alloc_device(struct device *log_dev)
{
	struct appletb_device *tb_dev;

	tb_dev = kzalloc(sizeof(*tb_dev), GFP_KERNEL);
	if (!tb_dev)
		return NULL;

	spin_lock_init(&tb_dev->lock);
	INIT_DELAYED_WORK(&tb_dev->tb_work, appletb_set_tb_worker);
	timer_setup(&tb_dev->inactivity_timer, appletb_inactivity_timer, 0);
	tb_dev->log_dev = log_dev;

	return tb_dev;
}

static void appletb_free_device(struct appletb_device *tb_dev)
{
	timer_delete_sync(&tb_dev->inactivity_timer);
	cancel_delayed_work_sync(&tb_dev->tb_work);
	kfree(tb_dev);
}

static struct hid_driver appletb_hid_driver = {
	.name = "apple-ib-touchbar",
	.probe = appletb_probe,
	.remove = appletb_remove,
	.event = appletb_hid_event,
	.input_configured = appletb_input_configured,
#ifdef CONFIG_PM
	.suspend = appletb_suspend,
	.reset_resume = appletb_reset_resume,
#endif
};

static int appletb_platform_probe(struct platform_device *pdev)
{
	struct appleib_device_data *ddata = pdev->dev.platform_data;
	struct appleib_device *ib_dev = ddata->ib_dev;
	struct appletb_device *tb_dev;
	int rc;

	tb_dev = appletb_alloc_device(ddata->log_dev);
	if (!tb_dev)
		return -ENOMEM;

	rc = appleib_register_hid_driver(ib_dev, &appletb_hid_driver, tb_dev);
	if (rc)
		goto error;

	platform_set_drvdata(pdev, tb_dev);

	return 0;

error:
	appletb_free_device(tb_dev);
	return rc;
}

static void appletb_platform_remove(struct platform_device *pdev)
{
	struct appleib_device_data *ddata = pdev->dev.platform_data;
	struct appleib_device *ib_dev = ddata->ib_dev;
	struct appletb_device *tb_dev = platform_get_drvdata(pdev);

	if (appleib_unregister_hid_driver(ib_dev, &appletb_hid_driver))
		return;

	appletb_free_device(tb_dev);
}

static const struct platform_device_id appletb_platform_ids[] = {
	{ .name = PLAT_NAME_IB_TB },
	{ }
};
MODULE_DEVICE_TABLE(platform, appletb_platform_ids);

static struct platform_driver appletb_platform_driver = {
	.id_table = appletb_platform_ids,
	.driver = {
		.name	= "apple-ib-tb",
	},
	.probe = appletb_platform_probe,
	.remove = appletb_platform_remove,
};

module_platform_driver(appletb_platform_driver);

MODULE_AUTHOR("Ronald Tschalär");
MODULE_DESCRIPTION("MacBookPro Touch Bar driver");
MODULE_LICENSE("GPL v2");
