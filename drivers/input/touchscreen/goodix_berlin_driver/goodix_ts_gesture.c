/*
 * Goodix Gesture Module
 *
 * Copyright (C) 2019 - 2020 Goodix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the GOODiX's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/atomic.h>
#include <linux/input/mt.h>
#include "goodix_ts_core.h"

#define GOODIX_GESTURE_DOUBLE_TAP		0xCC
#define GOODIX_GESTURE_SINGLE_TAP		0x4C
#define GOODIX_GESTURE_FOD_DOWN			0x46
#define GOODIX_GESTURE_FOD_UP			0x55

/*
 * struct gesture_module - gesture module data
 * @registered: module register state
 * @sysfs_node_created: sysfs node state
 * @gesture_type: valid gesture type, each bit represent one gesture type
 * @gesture_data: store latest gesture code get from irq event
 * @gesture_ts_cmd: gesture command data
 */
struct gesture_module {
	struct goodix_ts_core *ts_core;
	struct goodix_ext_module module;
	struct kobject kobj;
};

static ssize_t goodix_double_en_show(struct gesture_module *gsx, char *buf)
{
	unsigned char type = gsx->ts_core->gesture_type;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			(type & GESTURE_DOUBLE_TAP) ? "enable" : "disable");
}

static ssize_t goodix_double_en_store(struct gesture_module *gsx,
				      const char *buf, size_t count)
{
	if (buf[0] == '1') {
		ts_info("enable double tap");
		gsx->ts_core->gesture_type |= GESTURE_DOUBLE_TAP;
	} else if (buf[0] == '0') {
		ts_info("disable double tap");
		gsx->ts_core->gesture_type &= ~GESTURE_DOUBLE_TAP;
	} else
		ts_err("invalid cmd[%c]", buf[0]);

	return count;
}

static ssize_t goodix_single_en_show(struct gesture_module *gsx, char *buf)
{
	unsigned char type = gsx->ts_core->gesture_type;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			(type & GESTURE_SINGLE_TAP) ? "enable" : "disable");
}

static ssize_t goodix_single_en_store(struct gesture_module *gsx,
				      const char *buf, size_t count)
{
	if (buf[0] == '1') {
		ts_info("enable single tap");
		gsx->ts_core->gesture_type |= GESTURE_SINGLE_TAP;
	} else if (buf[0] == '0') {
		ts_info("disable single tap");
		gsx->ts_core->gesture_type &= ~GESTURE_SINGLE_TAP;
	} else
		ts_err("invalid cmd[%c]", buf[0]);

	return count;
}

static ssize_t goodix_fod_en_show(struct gesture_module *gsx, char *buf)
{
	unsigned char type = gsx->ts_core->gesture_type;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			(type & GESTURE_FOD_PRESS) ? "enable" : "disable");
}

static ssize_t goodix_fod_en_store(struct gesture_module *gsx,
		const char *buf, size_t count)
{
	if (buf[0] == '1') {
		ts_info("enable fod");
		gsx->ts_core->gesture_type |= GESTURE_FOD_PRESS;
	} else if (buf[0] == '0') {
		ts_info("disable fod");
		gsx->ts_core->gesture_type &= ~GESTURE_FOD_PRESS;
	} else
		ts_err("invalid cmd[%c]", buf[0]);

	return count;
}

struct gesture_module_attribute {
	struct attribute attr;
	ssize_t (*show)(struct gesture_module *gsx, char *buf);
	ssize_t (*store)(struct gesture_module *gsx, const char *buf,
			 size_t len);
};

#define GOODIX_GESTURE_ATTR(_name, _mode, _show, _store)	{	\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show  = _show,	\
	.store = _store,	\
}

const struct gesture_module_attribute gesture_attrs[] = {
	GOODIX_GESTURE_ATTR(double_en, 0664, goodix_double_en_show,
		      goodix_double_en_store),
	GOODIX_GESTURE_ATTR(single_en, 0664, goodix_single_en_show,
		      goodix_single_en_store),
	GOODIX_GESTURE_ATTR(fod_en, 0664, goodix_fod_en_show, goodix_fod_en_store),
};

static void goodix_gesture_sysfs_release(struct kobject *kobj)
{
	ts_info("Kobject released!");
}

#define to_gsx(kobj) container_of(kobj, struct gesture_module, kobj)
#define to_gesture_attr(attr)                                                  \
	container_of(attr, struct gesture_module_attribute, attr)

static ssize_t goodix_gesture_sysfs_show(struct kobject *kobj,
				     struct attribute *attr, char *buf)
{
	struct gesture_module *gsx = to_gsx(kobj);
	struct gesture_module_attribute *gesture_attr = to_gesture_attr(attr);

	if (gesture_attr->show)
		return gesture_attr->show(gsx, buf);

	return -EIO;
}

static ssize_t goodix_gesture_sysfs_store(struct kobject *kobj,
				      struct attribute *attr, const char *buf,
				      size_t count)
{
	struct gesture_module *gsx = to_gsx(kobj);
	struct gesture_module_attribute *gesture_attr = to_gesture_attr(attr);

	if (gesture_attr->store)
		return gesture_attr->store(gsx, buf, count);

	return -EIO;
}

static const struct sysfs_ops goodix_gesture_sysfs_ops = {
	.show = goodix_gesture_sysfs_show,
	.store = goodix_gesture_sysfs_store
};

static struct kobj_type goodix_gesture_sysfs_kobj_type = {
	.release = goodix_gesture_sysfs_release,
	.sysfs_ops = &goodix_gesture_sysfs_ops,
};

static int goodix_gesture_init(struct goodix_ts_core *cd,
		struct goodix_ext_module *module)
{
	struct gesture_module *gsx = module->priv_data;
	int ret, i;

	// Currently BerlinA gesture support is not implemented
	if (!cd || cd->bus->ic_type == CHIP_TYPE_BRA) {
		ts_err("gesture unsupported");
		return -EINVAL;
	}

	/* gesture sysfs init */
	ret = kobject_init_and_add(&gsx->kobj, &goodix_gesture_sysfs_kobj_type,
				   goodix_get_default_kobj(), "gesture");
	if (ret < 0) {
		ts_err("failed create gesture sysfs node!");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(gesture_attrs) && !ret; i++)
		ret = sysfs_create_file(&gsx->kobj, &gesture_attrs[i].attr);
	if (ret < 0) {
		ts_err("failed create gst sysfs files");
		while (--i >= 0)
			sysfs_remove_file(&gsx->kobj, &gesture_attrs[i].attr);

		kobject_put(&gsx->kobj);
		return ret;
	}

	gsx->ts_core = cd;
	gsx->ts_core->gesture_type = GESTURE_DOUBLE_TAP;

	return 0;
}

static int goodix_gesture_exit(struct goodix_ts_core *cd,
		struct goodix_ext_module *module)
{
	struct gesture_module *gsx = module->priv_data;
	int i;

	if (!cd) {
		ts_err("gesture unsupported");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(gesture_attrs); i++)
		sysfs_remove_file(&gsx->kobj, &gesture_attrs[i].attr);

	kobject_put(&gsx->kobj);

	return 0;
}

/**
 * goodix_gesture_irq_event - Gesture Irq handle
 * This functions is excuted when interrupt happended and
 * ic in doze mode.
 *
 * @cd: pointer to touch core data
 * @module: pointer to goodix_ext_module struct
 * return: 0 goon execute, EVT_CANCEL stop execute
 */
static irqreturn_t goodix_gesture_irq_event(struct goodix_ts_core *cd,
					    struct goodix_ext_module *module)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_ts_event *ts_event = &cd->ts_event;
	int fodx, fody, overlay_area;

	if (atomic_read(&cd->suspended) == 0 || cd->gesture_type == 0)
		return IRQ_NONE;

	if (!(ts_event->event_type & EVENT_GESTURE)) {
		if (hw_ops->gesture(cd, true))
			ts_info("warning: failed re_send gesture cmd");
		return IRQ_NONE;
	}

	switch (ts_event->gesture_type) {
	case GOODIX_GESTURE_SINGLE_TAP:
		if (cd->gesture_type & GESTURE_SINGLE_TAP) {
			ts_debug("get SINGLE-TAP gesture");
			input_report_key(cd->input_dev, KEY_WAKEUP, 1);
			input_sync(cd->input_dev);
			input_report_key(cd->input_dev, KEY_WAKEUP, 0);
			input_sync(cd->input_dev);
		} else {
			ts_debug("not enable SINGLE-TAP");
		}
		break;
	case GOODIX_GESTURE_DOUBLE_TAP:
		if (cd->gesture_type & GESTURE_DOUBLE_TAP) {
			ts_debug("get DOUBLE-TAP gesture");
			input_report_key(cd->input_dev, KEY_WAKEUP, 1);
			input_sync(cd->input_dev);
			input_report_key(cd->input_dev, KEY_WAKEUP, 0);
			input_sync(cd->input_dev);
		} else {
			ts_debug("not enable DOUBLE-TAP");
		}
		break;
	case GOODIX_GESTURE_FOD_DOWN:
		if (cd->gesture_type & GESTURE_FOD_PRESS) {
			ts_debug("get FOD-DOWN gesture");
			fodx = le16_to_cpup((__le16 *)ts_event->gesture_data);
			fody = le16_to_cpup((__le16 *)(ts_event->gesture_data + 2));
			overlay_area = ts_event->gesture_data[4];
			ts_debug("fodx:%d fody:%d overlay_area:%d", fodx, fody, overlay_area);
			input_report_key(cd->input_dev, BTN_TOUCH, 1);
			input_mt_slot(cd->input_dev, 0);
			input_mt_report_slot_state(cd->input_dev, MT_TOOL_FINGER, 1);
			input_report_abs(cd->input_dev, ABS_MT_POSITION_X, fodx);
			input_report_abs(cd->input_dev, ABS_MT_POSITION_Y, fody);
			input_report_abs(cd->input_dev, ABS_MT_WIDTH_MAJOR, overlay_area);
			input_sync(cd->input_dev);
		} else {
			ts_debug("not enable FOD-DOWN");
		}
		break;
	case GOODIX_GESTURE_FOD_UP:
		if (cd->gesture_type & GESTURE_FOD_PRESS) {
			ts_debug("get FOD-UP gesture");
			fodx = le16_to_cpup((__le16 *)ts_event->gesture_data);
			fody = le16_to_cpup((__le16 *)(ts_event->gesture_data + 2));
			overlay_area = ts_event->gesture_data[4];
			input_report_key(cd->input_dev, BTN_TOUCH, 0);
			input_mt_slot(cd->input_dev, 0);
			input_mt_report_slot_state(cd->input_dev,
					MT_TOOL_FINGER, 0);
			input_sync(cd->input_dev);
		} else {
			ts_debug("not enable FOD-UP");
		}
		break;
	default:
		ts_err("not support gesture type[%02X]", ts_event->gesture_type);
		break;
	}

	return IRQ_HANDLED;
}

static int goodix_gesture_suspend(struct goodix_ts_core *cd,
	struct goodix_ext_module *module)
{
	const struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	int ret;

	if (cd->gesture_type == 0)
		return EVT_CONTINUE;

	ret = hw_ops->gesture(cd, true);
	if (ret < 0) {
		ts_err("failed enter gesture mode");
		return EVT_CONTINUE;
	}

	hw_ops->irq_enable(cd, true);
	enable_irq_wake(cd->irq);

	return EVT_CANCEL;
}

static int goodix_gesture_resume(struct goodix_ts_core *cd,
	struct goodix_ext_module *module)
{
	const struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	int ret;

	if (cd->gesture_type == 0)
		return EVT_CONTINUE;

	disable_irq_wake(cd->irq);
	hw_ops->irq_enable(cd, false);

	ret = hw_ops->gesture(cd, false);
	if (ret < 0) {
		ts_err("failed exit gesture mode");
		return EVT_CONTINUE;
	}

	return EVT_CANCEL;
}

static struct goodix_ext_module_funcs goodix_gesture_funcs = {
	.irq_event = goodix_gesture_irq_event,
	.init = goodix_gesture_init,
	.exit = goodix_gesture_exit,
	.suspend = goodix_gesture_suspend,
	.resume = goodix_gesture_resume,
};

static struct gesture_module *goodix_gesture;

int gesture_module_init(void)
{
	int ret;

	goodix_gesture = kzalloc(sizeof(*goodix_gesture), GFP_KERNEL);
	if (!goodix_gesture)
		return -ENOMEM;

	goodix_gesture->module.funcs = &goodix_gesture_funcs;
	goodix_gesture->module.priority = EXTMOD_PRIO_GESTURE;
	goodix_gesture->module.name = "Goodix_goodix_gesture";
	goodix_gesture->module.priv_data = goodix_gesture;

	ret = goodix_register_ext_module(&goodix_gesture->module);
	if (ret < 0)
		goto err_out;

	return 0;

err_out:
	ts_err("gesture module init failed!");
	kfree(goodix_gesture);
	return ret;
}

void gesture_module_exit(void)
{
	ts_info("gesture module exit");

	if (!goodix_gesture)
		return;

	goodix_unregister_ext_module(&goodix_gesture->module);
	kfree(goodix_gesture);
	goodix_gesture = NULL;
}
