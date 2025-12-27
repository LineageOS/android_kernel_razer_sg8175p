 /*
  * Goodix Touchscreen Driver
  * Copyright (C) 2020 - 2021 Goodix, Inc.
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
  *
  */
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#if defined(CONFIG_DRM)
#include <drm/drm_panel.h>
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 38)
#include <linux/input/mt.h>
#endif

#include "goodix_ts_core.h"

#define GOODIX_DEFAULT_CFG_NAME		"goodix_cfg_group.cfg"
#define GOOIDX_INPUT_PHYS			"goodix_ts/input0"

#if defined(CONFIG_DRM)
static struct drm_panel *active_panel;
#endif

struct goodix_module goodix_modules;
int core_module_prob_sate = CORE_MODULE_UNPROBED;

static int goodix_send_ic_config(struct goodix_ts_core *core_data, int type);

static void goodix_core_module_init(void)
{
	if (goodix_modules.initilized)
		return;
	goodix_modules.initilized = true;
	INIT_LIST_HEAD(&goodix_modules.head);
	mutex_init(&goodix_modules.mutex);
}

/**
 * goodix_register_ext_module - interface for register external module
 * to the core. This will create a workqueue to finish the real register
 * work and return immediately. The user need to check the final result
 * to make sure registe is success or fail.
 *
 * @module: pointer to external module to be register
 * return: 0 ok, <0 failed
 */
int goodix_register_ext_module(struct goodix_ext_module *module)
{
	struct goodix_ext_module *ext_module, *next;
	struct list_head *insert_point = &goodix_modules.head;

	if (!module)
		return -EINVAL;

	goodix_core_module_init();

	/* driver probe failed */
	if (core_module_prob_sate != CORE_MODULE_PROB_SUCCESS) {
		ts_err("Can't register ext_module core error");
		return -EINVAL;
	}

	/* prority level *must* be set */
	if (module->priority == EXTMOD_PRIO_RESERVED) {
		ts_err("Priority of module [%s] needs to be set",
			module->name);
		return -EINVAL;
	}

	mutex_lock(&goodix_modules.mutex);
	/* find insert point for the specified priority */
	if (!list_empty(&goodix_modules.head)) {
		list_for_each_entry_safe(ext_module, next,
					 &goodix_modules.head, list) {
			if (ext_module == module) {
				ts_info("Module [%s] already exists",
					module->name);
				mutex_unlock(&goodix_modules.mutex);
				return 0;
			}
		}

		/* smaller priority value with higher priority level */
		list_for_each_entry_safe(ext_module, next,
					 &goodix_modules.head, list) {
			if (ext_module->priority >= module->priority) {
				insert_point = &ext_module->list;
				break;
			}
		}
	}

	if (module->funcs && module->funcs->init) {
		if (module->funcs->init(goodix_modules.core_data,
					module) < 0) {
			ts_err("Module [%s] init error",
				module->name ? module->name : " ");
			mutex_unlock(&goodix_modules.mutex);
			return -EFAULT;
		}
	}

	list_add(&module->list, insert_point->prev);
	mutex_unlock(&goodix_modules.mutex);

	ts_info("Module [%s] registered,priority:%u", module->name,
		module->priority);
	return 0;
}

/**
 * goodix_unregister_ext_module - interface for external module
 * to unregister external modules
 *
 * @module: pointer to external module
 * return: 0 ok, <0 failed
 */
int goodix_unregister_ext_module(struct goodix_ext_module *module)
{
	struct goodix_ext_module *ext_module, *next;
	bool found = false;

	if (!module)
		return -EINVAL;

	if (!goodix_modules.initilized)
		return -EINVAL;

	if (!goodix_modules.core_data)
		return -ENODEV;

	mutex_lock(&goodix_modules.mutex);
	if (!list_empty(&goodix_modules.head)) {
		list_for_each_entry_safe(ext_module, next,
					 &goodix_modules.head, list) {
			if (ext_module == module) {
				found = true;
				break;
			}
		}
	} else {
		mutex_unlock(&goodix_modules.mutex);
		return 0;
	}

	if (!found) {
		ts_debug("Module [%s] never registed",
				module->name);
		mutex_unlock(&goodix_modules.mutex);
		return 0;
	}

	list_del(&module->list);
	mutex_unlock(&goodix_modules.mutex);

	if (module->funcs && module->funcs->exit)
		module->funcs->exit(goodix_modules.core_data, module);

	ts_info("Moudle [%s] unregistered",
		module->name ? module->name : " ");
	return 0;
}

struct kobject *goodix_get_default_kobj(void)
{
	struct kobject *kobj = NULL;

	if (goodix_modules.core_data && goodix_modules.core_data->bus)
		kobj = &goodix_modules.core_data->bus->dev->kobj;
	return kobj;
}

/* show driver infomation */
static ssize_t driver_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "DriverVersion:%s\n",
			GOODIX_DRIVER_VERSION);
}

/* show chip infoamtion */
static ssize_t chip_info_show(struct device  *dev,
		struct device_attribute *attr, char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	struct goodix_fw_version fw_version;
	struct goodix_ic_info ic_info;
	u8 temp_pid[8] = {0};
	int ret;
	int cnt = -EINVAL;

	if (hw_ops->read_version) {
		ret = hw_ops->read_version(core_data, &fw_version);
		if (!ret) {
			memcpy(temp_pid, fw_version.rom_pid,
			       sizeof(fw_version.rom_pid));
			cnt = scnprintf(&buf[0], PAGE_SIZE, "rom_pid:%s\n", temp_pid);
			cnt += scnprintf(&buf[cnt], PAGE_SIZE,
					 "rom_vid:%02x%02x%02x\n",
					 fw_version.rom_vid[0],
					 fw_version.rom_vid[1],
					 fw_version.rom_vid[2]);
			cnt += scnprintf(&buf[cnt], PAGE_SIZE, "patch_pid:%s\n",
					 fw_version.patch_pid);
			cnt += scnprintf(&buf[cnt], PAGE_SIZE,
					 "patch_vid:%02x%02x%02x%02x\n",
					 fw_version.patch_vid[0],
					 fw_version.patch_vid[1],
					 fw_version.patch_vid[2],
					 fw_version.patch_vid[3]);
			cnt += scnprintf(&buf[cnt], PAGE_SIZE, "sensorid:%d\n",
					 fw_version.sensor_id);
		}
	}

	if (hw_ops->get_ic_info) {
		ret = hw_ops->get_ic_info(core_data, &ic_info);
		if (!ret) {
			cnt += scnprintf(&buf[cnt], PAGE_SIZE,
					"config_id:%x\n",
					ic_info.version.config_id);
			cnt += scnprintf(&buf[cnt], PAGE_SIZE,
					"config_version:%x\n",
					ic_info.version.config_version);
		}
	}

	return cnt;
}

/* reset chip */
static ssize_t goodix_ts_reset_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf,
				     size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;

	if (!buf || count <= 0)
		return -EINVAL;
	if (buf[0] != '0')
		hw_ops->reset(core_data);
	return count;
}

/* read config */
static ssize_t read_cfg_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret;
	int i;
	int offset;
	char *cfg_buf = NULL;

	cfg_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!cfg_buf)
		return -ENOMEM;

	if (hw_ops->read_config)
		ret = hw_ops->read_config(core_data, cfg_buf, PAGE_SIZE);
	else
		ret = -EINVAL;

	if (ret > 0) {
		offset = 0;
		for (i = 0; i < 200; i++) { // only print 200 bytes
			offset += scnprintf(&buf[offset], PAGE_SIZE - offset,
					"%02x,", cfg_buf[i]);
			if ((i + 1) % 20 == 0)
				buf[offset++] = '\n';
		}
	}

	kfree(cfg_buf);
	if (ret <= 0)
		return ret;

	return offset;
}

static u8 ascii2hex(u8 a)
{
	s8 value = 0;

	if (a >= '0' && a <= '9')
		value = a - '0';
	else if (a >= 'A' && a <= 'F')
		value = a - 'A' + 0x0A;
	else if (a >= 'a' && a <= 'f')
		value = a - 'a' + 0x0A;
	else
		value = 0xff;

	return value;
}

static int goodix_ts_convert_0x_data(const u8 *buf, int buf_size,
				     u8 *out_buf, int *out_buf_len)
{
	int i, m_size = 0;
	int temp_index = 0;
	u8 high, low;

	for (i = 0; i < buf_size; i++) {
		if (buf[i] == 'x' || buf[i] == 'X')
			m_size++;
	}

	if (m_size <= 1) {
		ts_err("cfg file ERROR, valid data count:%d", m_size);
		return -EINVAL;
	}
	*out_buf_len = m_size;

	for (i = 0; i < buf_size; i++) {
		if (buf[i] != 'x' && buf[i] != 'X')
			continue;

		if (temp_index >= m_size) {
			ts_err("exchange cfg data error, overflow, temp_index:%d,m_size:%d",
					temp_index, m_size);
			return -EINVAL;
		}
		high = ascii2hex(buf[i + 1]);
		low = ascii2hex(buf[i + 2]);
		if (high == 0xff || low == 0xff) {
			ts_err("failed convert: 0x%x, 0x%x",
				buf[i + 1], buf[i + 2]);
			return -EINVAL;
		}
		out_buf[temp_index++] = (high << 4) + low;
	}
	return 0;
}

/* send config */
static ssize_t goodix_ts_send_cfg_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	struct goodix_ic_config *config = NULL;
	const struct firmware *cfg_img = NULL;
	int ret;

	if (buf[0] != '1')
		return -EINVAL;

	hw_ops->irq_enable(core_data, false);

	ret = request_firmware(&cfg_img, GOODIX_DEFAULT_CFG_NAME, dev);
	if (ret < 0) {
		ts_err("cfg file [%s] not available,errno:%d",
			GOODIX_DEFAULT_CFG_NAME, ret);
		goto exit;
	} else
		ts_info("cfg file [%s] is ready", GOODIX_DEFAULT_CFG_NAME);

	config = kzalloc(sizeof(*config), GFP_KERNEL);
	if (!config)
		goto exit;

	if (goodix_ts_convert_0x_data(cfg_img->data, cfg_img->size,
			config->data, &config->len)) {
		ts_err("convert config data FAILED");
		goto exit;
	}

	if (hw_ops->send_config) {
		ret = hw_ops->send_config(core_data, config->data, config->len);
		if (ret < 0)
			ts_err("send config failed");
	}

exit:
	hw_ops->irq_enable(core_data, true);
	kfree(config);
	if (cfg_img)
		release_firmware(cfg_img);

	return count;
}

/* reg read/write */
static u32 rw_addr;
static u32 rw_len;
static u8 rw_flag;
static u8 store_buf[32];
static u8 show_buf[PAGE_SIZE];
static ssize_t goodix_ts_reg_rw_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret;

	if (!rw_addr || !rw_len) {
		ts_err("address(0x%x) and length(%d) can't be null",
			rw_addr, rw_len);
		return -EINVAL;
	}

	if (rw_flag != 1) {
		ts_err("invalid rw flag %d, only support [1/2]", rw_flag);
		return -EINVAL;
	}

	ret = hw_ops->read(core_data, rw_addr, show_buf, rw_len);
	if (ret < 0) {
		ts_err("failed read addr(%x) length(%d)", rw_addr, rw_len);
		return scnprintf(buf, PAGE_SIZE,
			"failed read addr(%x), len(%d)\n",
			rw_addr, rw_len);
	}

	return scnprintf(buf, PAGE_SIZE, "0x%x,%d {%*ph}\n",
		rw_addr, rw_len, rw_len, show_buf);
}

static ssize_t goodix_ts_reg_rw_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	char *pos = NULL;
	char *token = NULL;
	long result = 0;
	int ret;
	int i;

	if (!buf || !count) {
		ts_err("invalid parame");
		goto err_out;
	}

	if (buf[0] == 'r')
		rw_flag = 1;
	else if (buf[0] == 'w')
		rw_flag = 2;
	else {
		ts_err("string must start with 'r/w'");
		goto err_out;
	}

	/* get addr */
	pos = (char *)buf;
	pos += 2;
	token = strsep(&pos, ":");
	if (!token) {
		ts_err("invalid address info");
		goto err_out;
	} else {
		if (kstrtol(token, 16, &result)) {
			ts_err("failed get addr info");
			goto err_out;
		}
		rw_addr = (u32)result;
		ts_info("rw addr is 0x%x", rw_addr);
	}

	/* get length */
	token = strsep(&pos, ":");
	if (!token) {
		ts_err("invalid length info");
		goto err_out;
	} else {
		if (kstrtol(token, 0, &result)) {
			ts_err("failed get length info");
			goto err_out;
		}
		rw_len = (u32)result;
		ts_info("rw length info is %d", rw_len);
		if (rw_len > sizeof(store_buf)) {
			ts_err("data len > %lu", sizeof(store_buf));
			goto err_out;
		}
	}

	if (rw_flag == 1)
		return count;

	for (i = 0; i < rw_len; i++) {
		token = strsep(&pos, ":");
		if (!token) {
			ts_err("invalid data info");
			goto err_out;
		} else {
			if (kstrtol(token, 16, &result)) {
				ts_err("failed get data[%d] info", i);
				goto err_out;
			}
			store_buf[i] = (u8)result;
			ts_info("get data[%d]=0x%x", i, store_buf[i]);
		}
	}
	ret = hw_ops->write(core_data, rw_addr, store_buf, rw_len);
	if (ret < 0) {
		ts_err("failed write addr(%x) data %*ph", rw_addr,
			rw_len, store_buf);
		goto err_out;
	}

	ts_info("%s write to addr (%x) with data %*ph",
		"success", rw_addr, rw_len, store_buf);

	return count;
err_out:
	scnprintf(show_buf, PAGE_SIZE, "%s\n",
		"invalid params, format{r/w:4100:length:[41:21:31]}");
	return -EINVAL;

}

/* show irq infomation */
static ssize_t goodix_ts_irq_info_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct irq_desc *desc;
	size_t offset = 0;
	int r;

	r = scnprintf(&buf[offset], PAGE_SIZE, "irq:%u\n", core_data->irq);
	if (r < 0)
		return -EINVAL;
	offset += r;

	r = scnprintf(&buf[offset], PAGE_SIZE - offset, "state:%s\n",
			atomic_read(&core_data->irq_enabled) ? "enabled" : "disabled");
	if (r < 0)
		return -EINVAL;
	offset += r;

	desc = irq_to_desc(core_data->irq);
	if (!desc)
		return -EINVAL;

	r = scnprintf(&buf[offset], PAGE_SIZE - offset, "disable-depth:%d\n", desc->depth);
	if (r < 0)
		return -EINVAL;
	offset += r;

	r = scnprintf(&buf[offset], PAGE_SIZE - offset, "trigger-count:%zu\n",
		core_data->irq_trig_cnt);
	if (r < 0)
		return -EINVAL;
	offset += r;

	return offset;
}

/* enable/disable irq */
static ssize_t goodix_ts_irq_info_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;

	if (!buf || count <= 0)
		return -EINVAL;

	if (buf[0] != '0')
		hw_ops->irq_enable(core_data, true);
	else
		hw_ops->irq_enable(core_data, false);
	return count;
}

/* show esd status */
static ssize_t goodix_ts_esd_info_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_esd *ts_esd = &core_data->ts_esd;
	int r = 0;

	r = scnprintf(buf, PAGE_SIZE, "state:%s\n",
			atomic_read(&ts_esd->esd_on) ? "enabled" : "disabled");

	return r;
}

/* enable/disable esd */
static ssize_t goodix_ts_esd_info_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);

	if (!buf || count <= 0)
		return -EINVAL;

	if (buf[0] != '0')
		goodix_ts_esd_on(core_data);
	else
		goodix_ts_esd_off(core_data);

	return count;
}

/* debug level show */
static ssize_t goodix_ts_debug_log_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int r = 0;

	r = scnprintf(buf, PAGE_SIZE, "state:%s\n",
			debug_log_flag ? "enabled" : "disabled");

	return r;
}

/* debug level store */
static ssize_t goodix_ts_debug_log_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	if (!buf || count <= 0)
		return -EINVAL;

	if (buf[0] != '0')
		debug_log_flag = true;
	else
		debug_log_flag = false;
	return count;
}

static DEVICE_ATTR(driver_info, 0440,
		driver_info_show, NULL);
static DEVICE_ATTR(chip_info, 0440,
		chip_info_show, NULL);
static DEVICE_ATTR(reset, 0220,
		NULL, goodix_ts_reset_store);
static DEVICE_ATTR(send_cfg, 0220,
		NULL, goodix_ts_send_cfg_store);
static DEVICE_ATTR(read_cfg, 0440,
		read_cfg_show, NULL);
static DEVICE_ATTR(reg_rw, 0664,
		goodix_ts_reg_rw_show, goodix_ts_reg_rw_store);
static DEVICE_ATTR(irq_info, 0664,
		goodix_ts_irq_info_show, goodix_ts_irq_info_store);
static DEVICE_ATTR(esd_info, 0664,
		goodix_ts_esd_info_show, goodix_ts_esd_info_store);
static DEVICE_ATTR(debug_log, 0664,
		goodix_ts_debug_log_show, goodix_ts_debug_log_store);

static struct attribute *sysfs_attrs[] = {
	&dev_attr_driver_info.attr,
	&dev_attr_chip_info.attr,
	&dev_attr_reset.attr,
	&dev_attr_send_cfg.attr,
	&dev_attr_read_cfg.attr,
	&dev_attr_reg_rw.attr,
	&dev_attr_irq_info.attr,
	&dev_attr_esd_info.attr,
	&dev_attr_debug_log.attr,
	NULL,
};

static const struct attribute_group sysfs_group = {
	.attrs = sysfs_attrs,
};

static int goodix_ts_sysfs_init(struct goodix_ts_core *core_data)
{
	int ret;

	ret = sysfs_create_group(&core_data->bus->dev->kobj, &sysfs_group);
	if (ret) {
		ts_err("failed create core sysfs group");
		return ret;
	}

	return ret;
}

static void goodix_ts_sysfs_exit(struct goodix_ts_core *core_data)
{
	sysfs_remove_group(&core_data->bus->dev->kobj, &sysfs_group);
}

/* prosfs create */
static int rawdata_proc_show(struct seq_file *m, void *v)
{
	struct ts_rawdata_info *info;
	struct goodix_ts_core *core_data = m->private;
	int tx;
	int rx;
	int ret;
	int i;
	int index;

	if (!m || !v || !core_data)
		return -EIO;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	ret = core_data->hw_ops->get_capacitance_data(core_data, info);
	if (ret < 0) {
		ts_err("failed to get_capacitance_data, exit!");
		goto exit;
	}

	rx = info->buff[0];
	tx = info->buff[1];
	seq_printf(m, "TX:%d  RX:%d\n", tx, rx);
	seq_puts(m, "mutual_rawdata:\n");
	index = 2;
	for (i = 0; i < tx * rx; i++) {
		seq_printf(m, "%5d,", info->buff[index + i]);
		if ((i + 1) % tx == 0)
			seq_puts(m, "\n");
	}
	seq_puts(m, "mutual_diffdata:\n");
	index += tx * rx;
	for (i = 0; i < tx * rx; i++) {
		seq_printf(m, "%3d,", info->buff[index + i]);
		if ((i + 1) % tx == 0)
			seq_puts(m, "\n");
	}

exit:
	kfree(info);
	return ret;
}

static int rawdata_proc_open(struct inode *inode, struct file *file)
{
	return single_open_size(file, rawdata_proc_show,
			PDE_DATA(inode), PAGE_SIZE * 10);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops rawdata_proc_fops = {
	.proc_open = rawdata_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
#else
static const struct file_operations rawdata_proc_fops = {
	.open = rawdata_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

static void goodix_ts_procfs_init(struct goodix_ts_core *core_data)
{
	if (!proc_mkdir("goodix_ts", NULL))
		return;

	if (!proc_create_data("goodix_ts/tp_capacitance_data",
			0664, NULL, &rawdata_proc_fops, core_data))
		ts_err("failed to create proc entry");
}

static void goodix_ts_procfs_exit(struct goodix_ts_core *core_data)
{
	remove_proc_entry("goodix_ts/tp_capacitance_data", NULL);
	remove_proc_entry("goodix_ts", NULL);
}

#if IS_ENABLED(CONFIG_OF)
/**
 * goodix_parse_dt_resolution - parse resolution from dt
 * @node: devicetree node
 * @board_data: pointer to board data structure
 * return: 0 - no error, <0 error
 */
static int goodix_parse_dt_resolution(struct device_node *node,
		struct goodix_ts_board_data *board_data)
{
	int ret;

	ret = of_property_read_u32(node, "goodix,panel-max-x",
				&board_data->panel_max_x);
	if (ret) {
		ts_err("failed get panel-max-x");
		return ret;
	}

	ret = of_property_read_u32(node, "goodix,panel-max-y",
				&board_data->panel_max_y);
	if (ret) {
		ts_err("failed get panel-max-y");
		return ret;
	}

	ret = of_property_read_u32(node, "goodix,panel-max-w",
				&board_data->panel_max_w);
	if (ret) {
		ts_err("failed get panel-max-w");
		return ret;
	}

	ret = of_property_read_u32(node, "goodix,panel-max-p",
				&board_data->panel_max_p);
	if (ret) {
		ts_err("failed get panel-max-p, use default");
		board_data->panel_max_p = GOODIX_PEN_MAX_PRESSURE;
	}

	return 0;
}

/**
 * goodix_parse_dt - parse board data from dt
 * @dev: pointer to device
 * @board_data: pointer to board data structure
 * return: 0 - no error, <0 error
 */
static int goodix_parse_dt(struct device_node *node,
	struct goodix_ts_board_data *board_data)
{
	const char *name_tmp;
	int r;

	if (!board_data) {
		ts_err("invalid board data");
		return -EINVAL;
	}

	r = of_get_named_gpio(node, "goodix,reset-gpio", 0);
	if (r < 0) {
		ts_err("invalid reset-gpio in dt: %d", r);
		return -EINVAL;
	}
	ts_info("get reset-gpio[%d] from dt", r);
	board_data->reset_gpio = r;

	r = of_get_named_gpio(node, "goodix,irq-gpio", 0);
	if (r < 0) {
		ts_err("invalid irq-gpio in dt: %d", r);
		return -EINVAL;
	}
	ts_info("get irq-gpio[%d] from dt", r);
	board_data->irq_gpio = r;

	r = of_property_read_u32(node, "goodix,irq-flags",
			&board_data->irq_flags);
	if (r) {
		ts_err("invalid irq-flags");
		return -EINVAL;
	}

#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_FW_UPDATE
	/* get firmware file name */
	r = of_property_read_string(node, "goodix,firmware-name", &name_tmp);
	if (!r) {
		ts_info("firmware name from dt: %s", name_tmp);
		strlcpy(board_data->fw_name,
				name_tmp, sizeof(board_data->fw_name));
	} else {
		ts_info("can't find firmware name, use default: %s",
				TS_DEFAULT_FIRMWARE);
		strlcpy(board_data->fw_name,
				TS_DEFAULT_FIRMWARE,
				sizeof(board_data->fw_name));
	}
#endif

	/* get config file name */
	r = of_property_read_string(node, "goodix,config-name", &name_tmp);
	if (!r) {
		ts_info("config name from dt: %s", name_tmp);
		strlcpy(board_data->cfg_bin_name, name_tmp,
				sizeof(board_data->cfg_bin_name));
	} else {
		ts_info("can't find config name, use default: %s",
				TS_DEFAULT_CFG_BIN);
		strlcpy(board_data->cfg_bin_name,
				TS_DEFAULT_CFG_BIN,
				sizeof(board_data->cfg_bin_name));
	}

	/* get xyz resolutions */
	r = goodix_parse_dt_resolution(node, board_data);
	if (r) {
		ts_err("Failed to parse resolutions:%d", r);
		return r;
	}


	/*get pen-enable switch and pen keys, must after "key map"*/
	board_data->pen_enable = of_property_read_bool(node,
					"goodix,pen-enable");
	if (board_data->pen_enable)
		ts_info("goodix pen enabled");

	ts_debug("[DT]x:%d, y:%d, w:%d, p:%d", board_data->panel_max_x,
		 board_data->panel_max_y, board_data->panel_max_w,
		 board_data->panel_max_p);
	return 0;
}
#endif

static void goodix_ts_report_finger(struct input_dev *dev,
				    struct goodix_touch_data *touch_data)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(&dev->dev);
	unsigned int touch_num = touch_data->touch_num;
	int i;

	mutex_lock(&dev->mutex);

	for (i = 0; i < GOODIX_MAX_TOUCH; i++) {
		if (touch_data->coords[i].status == TS_TOUCH) {
			ts_debug("report: id[%d], x %d, y %d, w %d", i,
				 touch_data->coords[i].x,
				 touch_data->coords[i].y,
				 touch_data->coords[i].w);
			input_mt_slot(dev, i);
			input_mt_report_slot_state(dev, MT_TOOL_FINGER, true);
			input_report_abs(dev, ABS_MT_POSITION_X,
					 core_data->board_data.panel_max_x -
						 touch_data->coords[i].x);
			input_report_abs(dev, ABS_MT_POSITION_Y,
					 core_data->board_data.panel_max_y -
						 touch_data->coords[i].y);
			input_report_abs(dev, ABS_MT_TOUCH_MAJOR,
					 touch_data->coords[i].w);
		} else {
			input_mt_slot(dev, i);
			input_mt_report_slot_state(dev, MT_TOOL_FINGER, false);
		}
	}

	input_report_key(dev, BTN_TOUCH, touch_num > 0 ? 1 : 0);
	input_sync(dev);

	mutex_unlock(&dev->mutex);
}

static void goodix_ts_report_pen(struct input_dev *dev,
		struct goodix_pen_data *pen_data)
{
	int i;

	mutex_lock(&dev->mutex);

	if (pen_data->coords.status == TS_TOUCH) {
		input_report_key(dev, BTN_TOUCH, 1);
		input_report_key(dev, BTN_TOOL_PEN, 1);
		input_report_abs(dev, ABS_X, pen_data->coords.x);
		input_report_abs(dev, ABS_Y, pen_data->coords.y);
		input_report_abs(dev, ABS_PRESSURE, pen_data->coords.p);
		input_report_abs(dev, ABS_TILT_X, pen_data->coords.tilt_x);
		input_report_abs(dev, ABS_TILT_Y, pen_data->coords.tilt_y);
		ts_debug("pen_data:x %d, y %d, p %d, tilt_x %d tilt_y %d key[%d %d]",
				pen_data->coords.x, pen_data->coords.y,
				pen_data->coords.p, pen_data->coords.tilt_x,
				pen_data->coords.tilt_y,
				pen_data->keys[0].status == TS_TOUCH ? 1 : 0,
				pen_data->keys[1].status == TS_TOUCH ? 1 : 0);
	} else {
		input_report_key(dev, BTN_TOUCH, 0);
		input_report_key(dev, BTN_TOOL_PEN, 0);
	}
	/* report pen button */
	for (i = 0; i < GOODIX_MAX_PEN_KEY; i++) {
		if (pen_data->keys[i].status == TS_TOUCH)
			input_report_key(dev, pen_data->keys[i].code, 1);
		else
			input_report_key(dev, pen_data->keys[i].code, 0);
	}

	input_sync(dev);
	mutex_unlock(&dev->mutex);
}

static int goodix_ts_request_handle(struct goodix_ts_core *core_data,
				    struct goodix_ts_event *ts_event)
{
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret = -1;

	if (ts_event->request_code == REQUEST_TYPE_CONFIG)
		ret = goodix_send_ic_config(core_data, CONFIG_TYPE_NORMAL);
	else if (ts_event->request_code == REQUEST_TYPE_RESET) {
		hw_ops->power(core_data, false);
		hw_ops->power(core_data, true);
	} else
		ts_info("can not handle request type 0x%x",
			ts_event->request_code);
	if (ret)
		ts_err("failed handle request 0x%x",
			ts_event->request_code);
	else
		ts_info("success handle ic request 0x%x",
			ts_event->request_code);
	return ret;
}

/**
 * goodix_ts_threadirq_func - Bottom half of interrupt
 * This functions is excuted in thread context,
 * sleep in this function is permit.
 *
 * @data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static irqreturn_t goodix_ts_threadirq_func(int irq, void *data)
{
	struct goodix_ts_core *core_data = data;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	struct goodix_ext_module *ext_module, *next;
	struct goodix_ts_event *ts_event = &core_data->ts_event;
	struct goodix_ts_esd *ts_esd = &core_data->ts_esd;
	int ret;

	ts_esd->irq_status = true;
	core_data->irq_trig_cnt++;

	/* read touch data from touch device */
	ret = hw_ops->event_handler(core_data, ts_event);

	/* inform external module */
	mutex_lock(&goodix_modules.mutex);
	list_for_each_entry_safe(ext_module, next,
				 &goodix_modules.head, list) {
		irqreturn_t irq_ret;

		if (!ext_module->funcs->irq_event)
			continue;

		irq_ret = ext_module->funcs->irq_event(core_data, ext_module);
		if (irq_ret == IRQ_HANDLED) {
			mutex_unlock(&goodix_modules.mutex);
			goto out;
		}
	}
	mutex_unlock(&goodix_modules.mutex);

	if (likely(!ret)) {
		if (ts_event->event_type & EVENT_TOUCH)
			goodix_ts_report_finger(core_data->input_dev,
					&ts_event->touch_data);
		if (core_data->board_data.pen_enable &&
				ts_event->event_type & EVENT_PEN)
			goodix_ts_report_pen(core_data->input_dev,
					     &ts_event->pen_data);
		if (ts_event->event_type & EVENT_REQUEST)
			goodix_ts_request_handle(core_data, ts_event);
	}

out:
	ts_esd->irq_status = false;
	return IRQ_HANDLED;
}

/**
 * goodix_ts_init_irq - Requset interrput line from system
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_irq_setup(struct goodix_ts_core *core_data)
{
	const struct goodix_ts_board_data *ts_bdata = board_data(core_data);
	int ret;

	/* if ts_bdata-> irq is invalid */
	core_data->irq = gpio_to_irq(ts_bdata->irq_gpio);
	if (core_data->irq < 0) {
		ts_err("failed get irq num %d", core_data->irq);
		return -EINVAL;
	}

	ts_info("IRQ:%u,flags:%d", core_data->irq, (int)ts_bdata->irq_flags);
	ret = devm_request_threaded_irq(core_data->bus->dev, core_data->irq,
					NULL, goodix_ts_threadirq_func,
					ts_bdata->irq_flags | IRQF_ONESHOT,
					GOODIX_CORE_DRIVER_NAME, core_data);
	if (ret < 0)
		ts_err("Failed to requeset threaded irq:%d", ret);

	atomic_set(&core_data->irq_enabled, 0);

	return ret;
}

/**
 * goodix_ts_power_init - Get regulator for touch device
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_power_init(struct goodix_ts_core *core_data)
{
	struct device *dev = core_data->bus->dev;
	int ret = 0;

	core_data->avdd = devm_regulator_get(dev, "avdd");
	if (IS_ERR_OR_NULL(core_data->avdd)) {
		ret = PTR_ERR(core_data->avdd);
		ts_err("Failed to get regulator avdd:%d", ret);
		core_data->avdd = NULL;
		return ret;
	}

	core_data->iovdd = devm_regulator_get(dev, "iovdd");
	if (IS_ERR_OR_NULL(core_data->iovdd)) {
		ret = PTR_ERR(core_data->iovdd);
		ts_err("Failed to get regulator iovdd:%d", ret);
		core_data->iovdd = NULL;
	}

	return ret;
}

/**
 * goodix_ts_power_on - Turn on power to the touch device
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_power_on(struct goodix_ts_core *core_data)
{
	int ret = 0;

	ts_debug("Device power on");

	if (core_data->power_on)
		return 0;

	ret = core_data->hw_ops->power(core_data, true);
	if (ret < 0) {
		ts_err("failed power on, %d", ret);
		return ret;
	}

	core_data->power_on = 1;

	return 0;
}

/**
 * goodix_ts_power_off - Turn off power to the touch device
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_power_off(struct goodix_ts_core *core_data)
{
	int ret;

	ts_debug("Device power off");

	if (!core_data->power_on)
		return 0;

	ret = core_data->hw_ops->power(core_data, false);
	if (ret < 0) {
		ts_err("failed power off, %d", ret);
		return ret;
	}

	core_data->power_on = 0;

	return 0;
}

/**
 * goodix_ts_gpio_setup - Request gpio resources from GPIO subsysten
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_gpio_setup(struct goodix_ts_core *core_data)
{
	struct goodix_ts_board_data *ts_bdata = board_data(core_data);
	int r = 0;

	ts_info("GPIO setup,reset-gpio:%d, irq-gpio:%d",
		ts_bdata->reset_gpio, ts_bdata->irq_gpio);
	/*
	 * after kenerl3.13, gpio_ api is deprecated, new
	 * driver should use gpiod_ api.
	 */
	r = devm_gpio_request_one(core_data->bus->dev, ts_bdata->reset_gpio,
				  GPIOF_OUT_INIT_LOW, "ts_reset_gpio");
	if (r < 0) {
		ts_err("Failed to request reset gpio, r:%d", r);
		return r;
	}

	r = devm_gpio_request_one(core_data->bus->dev, ts_bdata->irq_gpio,
				  GPIOF_IN, "ts_irq_gpio");
	if (r < 0) {
		ts_err("Failed to request irq gpio, r:%d", r);
		return r;
	}

	return 0;
}

/**
 * goodix_ts_input_dev_config - Requset and config a input device
 *  then register it to input sybsystem.
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_input_dev_config(struct goodix_ts_core *core_data)
{
	struct goodix_ts_board_data *ts_bdata = board_data(core_data);
	struct input_dev *input_dev = NULL;
	int r;

	input_dev = input_allocate_device();
	if (!input_dev) {
		ts_err("Failed to allocated input device");
		return -ENOMEM;
	}

	core_data->input_dev = input_dev;
	input_set_drvdata(input_dev, core_data);

	input_dev->name = GOODIX_CORE_DRIVER_NAME;
	input_dev->phys = GOOIDX_INPUT_PHYS;
	input_dev->id.product = 0xDEAD;
	input_dev->id.vendor = 0xBEEF;
	input_dev->id.version = 10427;

	set_bit(EV_SYN, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);
	set_bit(EV_ABS, input_dev->evbit);
	set_bit(BTN_TOUCH, input_dev->keybit);
	set_bit(BTN_TOOL_FINGER, input_dev->keybit);
	set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

	/* set input parameters */
	input_set_abs_params(input_dev, ABS_MT_POSITION_X,
			     0, ts_bdata->panel_max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
			     0, ts_bdata->panel_max_y, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR,
			     0, ts_bdata->panel_max_w, 0, 0);
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 7, 0)
	input_mt_init_slots(input_dev, GOODIX_MAX_TOUCH,
			    INPUT_MT_DIRECT);
#elif LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 38)
	input_mt_init_slots(input_dev, GOODIX_MAX_TOUCH);
#endif

	input_set_capability(input_dev, EV_KEY, KEY_WAKEUP);

	if (core_data->board_data.pen_enable) {
		input_set_capability(input_dev, EV_ABS, ABS_X);
		input_set_capability(input_dev, EV_ABS, ABS_Y);
		input_set_capability(input_dev, EV_ABS, ABS_TILT_X);
		input_set_capability(input_dev, EV_ABS, ABS_TILT_Y);
		input_set_capability(input_dev, EV_KEY, BTN_TOUCH);
		input_set_capability(input_dev, EV_KEY, BTN_TOOL_PEN);
		input_set_capability(input_dev, EV_KEY, BTN_STYLUS);
		input_set_capability(input_dev, EV_KEY, BTN_STYLUS2);
		set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
		input_set_abs_params(input_dev, ABS_X,
					0, ts_bdata->panel_max_x, 0, 0);
		input_set_abs_params(input_dev, ABS_Y,
					0, ts_bdata->panel_max_y, 0, 0);
		input_set_abs_params(input_dev, ABS_PRESSURE,
					0, ts_bdata->panel_max_p, 0, 0);
		input_set_abs_params(input_dev, ABS_TILT_X,
					-GOODIX_PEN_MAX_TILT, GOODIX_PEN_MAX_TILT, 0, 0);
		input_set_abs_params(input_dev, ABS_TILT_Y,
					-GOODIX_PEN_MAX_TILT, GOODIX_PEN_MAX_TILT, 0, 0);
	}

	r = input_register_device(input_dev);
	if (r < 0) {
		ts_err("Unable to register input device");
		input_free_device(input_dev);
		return r;
	}

	return 0;
}

static void goodix_ts_input_dev_remove(struct goodix_ts_core *core_data)
{
	if (!core_data->input_dev)
		return;
	input_unregister_device(core_data->input_dev);
	input_free_device(core_data->input_dev);
	core_data->input_dev = NULL;
}

/**
 * goodix_ts_esd_work - check hardware status and recovery
 *  the hardware if needed.
 */
static void goodix_ts_esd_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct goodix_ts_esd *ts_esd = container_of(dwork,
			struct goodix_ts_esd, esd_work);
	struct goodix_ts_core *core_data =
		container_of(ts_esd, struct goodix_ts_core, ts_esd);
	const struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret = 0;

	if (ts_esd->irq_status)
		goto exit;

	if (!atomic_read(&ts_esd->esd_on))
		return;

	if (!hw_ops->esd_check)
		return;

	ret = hw_ops->esd_check(core_data);
	if (ret) {
		ts_err("esd check failed");
		goodix_ts_power_off(core_data);
		usleep_range(5000, 5100);
		goodix_ts_power_on(core_data);
	}

exit:
	if (atomic_read(&ts_esd->esd_on))
		schedule_delayed_work(&ts_esd->esd_work, 2 * HZ);
}

/**
 * goodix_ts_esd_on - turn on esd protection
 */
void goodix_ts_esd_on(struct goodix_ts_core *core_data)
{
	struct goodix_ic_info_misc *misc = &core_data->ic_info.misc;
	struct goodix_ts_esd *ts_esd = &core_data->ts_esd;

	if (!misc->esd_addr)
		return;

	if (atomic_read(&ts_esd->esd_on))
		return;

	atomic_set(&ts_esd->esd_on, 1);
	if (!schedule_delayed_work(&ts_esd->esd_work, 2 * HZ))
		ts_err("esd work already in workqueue");

	ts_debug("esd on");
}

/**
 * goodix_ts_esd_off - turn off esd protection
 */
void goodix_ts_esd_off(struct goodix_ts_core *core_data)
{
	struct goodix_ts_esd *ts_esd = &core_data->ts_esd;
	int ret;

	if (!atomic_read(&ts_esd->esd_on))
		return;

	atomic_set(&ts_esd->esd_on, 0);
	ret = cancel_delayed_work_sync(&ts_esd->esd_work);
	ts_debug("Esd off, esd work state %d", ret);
}
/**
 * goodix_ts_esd_init - initialize esd protection
 */
int goodix_ts_esd_init(struct goodix_ts_core *core_data)
{
	struct goodix_ic_info_misc *misc = &core_data->ic_info.misc;
	struct goodix_ts_esd *ts_esd = &core_data->ts_esd;

	if (!core_data->hw_ops->esd_check || !misc->esd_addr) {
		ts_err("missing key info for esd check");
		return 0;
	}

	INIT_DELAYED_WORK(&ts_esd->esd_work, goodix_ts_esd_work);
	ts_esd->ts_core = core_data;
	atomic_set(&ts_esd->esd_on, 0);

	return 0;
}

static void goodix_ts_release_connects(struct goodix_ts_core *core_data)
{
	struct input_dev *input_dev = core_data->input_dev;
	int i;

	mutex_lock(&input_dev->mutex);

	for (i = 0; i < GOODIX_MAX_TOUCH; i++) {
		input_mt_slot(input_dev, i);
		input_mt_report_slot_state(input_dev,
				MT_TOOL_FINGER,
				false);
	}
	input_report_key(input_dev, BTN_TOUCH, 0);
	input_mt_sync_frame(input_dev);
	input_sync(input_dev);

	mutex_unlock(&input_dev->mutex);
}

/**
 * goodix_ts_suspend - Touchscreen suspend function
 * Called by PM/FB/EARLYSUSPEN module to put the device to  sleep
 */
static int goodix_ts_suspend(struct goodix_ts_core *core_data)
{
	struct goodix_ext_module *ext_module, *next;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret;

	if (core_data->init_stage < CORE_INIT_STAGE2 ||
			atomic_read(&core_data->suspended))
		return 0;

	ts_debug("Suspend start");

	goodix_ts_esd_off(core_data);
	hw_ops->irq_enable(core_data, false);

	/* inform external module */
	mutex_lock(&goodix_modules.mutex);
	if (!list_empty(&goodix_modules.head)) {
		list_for_each_entry_safe(ext_module, next,
					 &goodix_modules.head, list) {
			if (!ext_module->funcs->suspend)
				continue;

			ret = ext_module->funcs->suspend(core_data, ext_module);
			if (ret == EVT_CANCEL) {
				mutex_unlock(&goodix_modules.mutex);
				ts_debug("Canceled by module:%s",
					 ext_module->name);
				goto out;
			}
		}
	}
	mutex_unlock(&goodix_modules.mutex);

	ret = goodix_ts_power_off(core_data);
	if (ret)
		ts_err("failed power off");

out:
	atomic_set(&core_data->suspended, 1);
	goodix_ts_release_connects(core_data);
	ts_debug("Suspend end");
	return 0;
}

/**
 * goodix_ts_resume - Touchscreen resume function
 * Called by PM/FB/EARLYSUSPEN module to wakeup device
 */
static int goodix_ts_resume(struct goodix_ts_core *core_data)
{
	struct goodix_ext_module *ext_module, *next;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret;

	if (core_data->init_stage < CORE_INIT_STAGE2 ||
			!atomic_read(&core_data->suspended))
		return 0;

	ts_debug("Resume start");

	mutex_lock(&goodix_modules.mutex);
	if (!list_empty(&goodix_modules.head)) {
		list_for_each_entry_safe(ext_module, next,
					&goodix_modules.head, list) {
			if (!ext_module->funcs->resume)
				continue;

			ret = ext_module->funcs->resume(core_data, ext_module);
			if (ret == EVT_CANCEL) {
				mutex_unlock(&goodix_modules.mutex);
				ts_debug("Canceled by module:%s",
					 ext_module->name);
				goto out;
			}
		}
	}
	mutex_unlock(&goodix_modules.mutex);

	ret = goodix_ts_power_on(core_data);
	if (ret)
		ts_err("failed power on");

out:
	atomic_set(&core_data->suspended, 0);
	hw_ops->irq_enable(core_data, true);
	goodix_ts_esd_on(core_data);
	ts_debug("Resume end");
	return 0;
}

#if IS_ENABLED(CONFIG_DRM)
static int goodix_ts_drm_notifier_callback(struct notifier_block *self,
					   unsigned long event, void *data)
{
	struct drm_panel_notifier *evdata = data;
	int *blank = NULL;
	struct goodix_ts_core *core_data =
		container_of(self, struct goodix_ts_core, drm_notifier);

	if (!evdata)
		return 0;

	if (!(event == DRM_PANEL_EARLY_EVENT_BLANK ||
	      event == DRM_PANEL_EVENT_BLANK)) {
		ts_debug("event(%lu) do not need process\n", event);
		return 0;
	}

	blank = evdata->data;
	if (!blank)
		return 0;

	ts_debug("DRM event:%lu,blank:%d", event, *blank);
	if (event == DRM_PANEL_EVENT_BLANK && *blank == DRM_PANEL_BLANK_UNBLANK)
		goodix_ts_resume(core_data);
	else if (event == DRM_PANEL_EARLY_EVENT_BLANK && *blank == DRM_PANEL_BLANK_POWERDOWN)
		goodix_ts_suspend(core_data);

	return 0;
}
#endif

#if IS_ENABLED(CONFIG_FB)
/**
 * goodix_ts_fb_notifier_callback - Framebuffer notifier callback
 * Called by kernel during framebuffer blanck/unblank phrase
 */
int goodix_ts_fb_notifier_callback(struct notifier_block *self,
	unsigned long event, void *data)
{
	struct goodix_ts_core *core_data =
		container_of(self, struct goodix_ts_core, fb_notifier);
	struct fb_event *fb_event = data;

	if (fb_event && fb_event->data && core_data) {
		if (event == FB_EARLY_EVENT_BLANK) {
			/* before fb blank */
		} else if (event == FB_EVENT_BLANK) {
			int *blank = fb_event->data;

			if (*blank == FB_BLANK_UNBLANK)
				goodix_ts_resume(core_data);
			else if (*blank == FB_BLANK_POWERDOWN)
				goodix_ts_suspend(core_data);
		}
	}

	return 0;
}
#endif

#if IS_ENABLED(CONFIG_PM)
/**
 * goodix_ts_pm_suspend - PM suspend function
 * Called by kernel during system suspend phrase
 */
int goodix_ts_pm_suspend(struct device *dev)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);

	return goodix_ts_suspend(core_data);
}
/**
 * goodix_ts_pm_resume - PM resume function
 * Called by kernel during system wakeup
 */
int goodix_ts_pm_resume(struct device *dev)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);

	return goodix_ts_resume(core_data);
}
#endif

/* try send the config specified with type */
static int goodix_send_ic_config(struct goodix_ts_core *core_data, int type)
{
	u32 config_id;
	struct goodix_ic_config *cfg;

	if (type >= GOODIX_MAX_CONFIG_GROUP) {
		ts_err("unsupproted config type %d", type);
		return -EINVAL;
	}

	cfg = core_data->ic_configs[type];
	if (!cfg || cfg->len <= 0) {
		ts_info("no valid normal config found");
		return -EINVAL;
	}

	config_id = goodix_get_file_config_id(cfg->data);
	if (core_data->ic_info.version.config_id == config_id) {
		ts_info("config id is equal 0x%x, skiped", config_id);
		return 0;
	}

	ts_info("try send config, id=0x%x", config_id);
	return core_data->hw_ops->send_config(core_data, cfg->data, cfg->len);
}

/**
 * goodix_later_init_thread - init IC fw and config
 * @data: point to goodix_ts_core
 *
 * This function respond for get fw version and try upgrade fw and config.
 * Note: when init encounter error, need release all resource allocated here.
 */
static int goodix_later_init_thread(void *data)
{
	struct goodix_ts_core *core_data = data;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	struct goodix_fw_version fw_version;
	int ret, i;
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_FW_UPDATE
	int update_flag = UPDATE_MODE_BLOCK | UPDATE_MODE_SRC_REQUEST;

	/* step 1: read version */
	ret = core_data->hw_ops->read_version(core_data, &fw_version);
	if (ret < 0) {
		ts_err("failed to get version info, try to upgrade");
		update_flag |= UPDATE_MODE_FORCE;
	} else {
		/* step 2: get config data from config bin */
		ret = goodix_get_config_proc(core_data, fw_version.sensor_id);
		if (ret)
			ts_info("no valid ic config found");
		else
			ts_info("success get valid ic config");
	}

	/* step 3: init fw struct add try do fw upgrade */
	ret = goodix_fw_update_init(core_data);
	if (ret) {
		ts_err("failed init fw update module");
		goto err_out;
	}

	ts_info("update flag: 0x%X", update_flag);
	ret = goodix_do_fw_update(core_data->ic_configs[CONFIG_TYPE_NORMAL],
				  update_flag);
	if (ret)
		ts_err("failed do fw update");
#endif

	/* step 4: get fw version and ic_info
	 * at this step we believe that the ic is in normal mode,
	 * if the version info is invalid there must have some
	 * problem we cann't cover so exit init directly.
	 */
	ret = hw_ops->read_version(core_data, &fw_version);
	if (ret) {
		ts_err("invalid fw version, abort");
		goto uninit_fw;
	}

	ret = hw_ops->get_ic_info(core_data, &core_data->ic_info);
	if (ret) {
		ts_err("invalid ic info, abort");
		goto uninit_fw;
	}

#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_FW_UPDATE
	/* the recomend way to update ic config is throuth ISP,
	 * if not we will send config with interactive mode
	 */
	goodix_send_ic_config(core_data, CONFIG_TYPE_NORMAL);
#endif

	/* alloc/config/register input device */
	ret = goodix_ts_input_dev_config(core_data);
	if (ret < 0) {
		ts_err("failed set input device");
		goto uninit_fw;
	}

	/* request irq line */
	ret = goodix_ts_irq_setup(core_data);
	if (ret < 0) {
		ts_info("failed set irq");
		goto exit;
	}
	ts_info("success register irq");

#if IS_ENABLED(CONFIG_FB)
	core_data->fb_notifier.notifier_call = goodix_ts_fb_notifier_callback;
	if (fb_register_client(&core_data->fb_notifier))
		ts_err("Failed to register fb notifier client:%d", ret);
#endif
#if IS_ENABLED(CONFIG_DRM)
	core_data->drm_notifier.notifier_call = goodix_ts_drm_notifier_callback;
	if (active_panel && drm_panel_notifier_register(
				    active_panel, &core_data->drm_notifier) < 0)
		ts_info("register notifier failed!\n");
 #endif

	/* create sysfs files */
	goodix_ts_sysfs_init(core_data);

	/* create procfs files */
	goodix_ts_procfs_init(core_data);

	/* esd protector */
	goodix_ts_esd_init(core_data);

	/* gesture init */
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_GESTURE
	gesture_module_init();
#endif

	/* inspect init */
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_INSPECT
	inspect_module_init();
#endif

	core_data->init_stage = CORE_INIT_STAGE2;

	hw_ops->irq_enable(core_data, true);
	goodix_ts_suspend(core_data);

	return 0;

exit:
	goodix_ts_input_dev_remove(core_data);
uninit_fw:
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_FW_UPDATE
	goodix_fw_update_uninit();
err_out:
#endif
	ts_err("stage2 init failed");
	core_data->init_stage = CORE_INIT_FAIL;
	for (i = 0; i < GOODIX_MAX_CONFIG_GROUP; i++) {
		kfree(core_data->ic_configs[i]);
		core_data->ic_configs[i] = NULL;
	}
	return ret;
}

static int goodix_start_later_init(struct goodix_ts_core *ts_core)
{
	struct task_struct *init_thrd;
	/* create and run update thread */
	init_thrd = kthread_run(goodix_later_init_thread,
				ts_core, "goodix_init_thread");
	if (IS_ERR_OR_NULL(init_thrd)) {
		ts_err("Failed to create update thread:%ld",
			PTR_ERR(init_thrd));
		return -EFAULT;
	}
	return 0;
}

#if defined(CONFIG_DRM)
static int goodix_check_dt(struct device_node *np)
{
	int i;
	int count;
	struct device_node *node;
	struct drm_panel *panel;

	count = of_count_phandle_with_args(np, "panel", NULL);
	if (count <= 0)
		return 0;

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "panel", i);
		panel = of_drm_find_panel(node);
		of_node_put(node);
		if (!IS_ERR(panel)) {
			active_panel = panel;
			return 0;
		}
	}

	return PTR_ERR(panel);
}
#endif

/**
 * goodix_ts_probe - called by kernel when Goodix touch
 *  platform driver is added.
 */
int goodix_ts_probe(struct device *dev,
		    struct goodix_bus_interface *bus_interface)
{
	struct goodix_ts_core *core_data = NULL;
	int ret;
	struct device_node *node;

	if (!bus_interface) {
		ts_err("Invalid touch device");
		core_module_prob_sate = CORE_MODULE_PROB_FAILED;
		return -ENODEV;
	}
	node = bus_interface->dev->of_node;

#if defined(CONFIG_DRM)
	ret = goodix_check_dt(node);
	if (ret == -EPROBE_DEFER)
		return ret;
#endif

	core_data = devm_kzalloc(dev,
			sizeof(struct goodix_ts_core), GFP_KERNEL);
	if (!core_data) {
		core_module_prob_sate = CORE_MODULE_PROB_FAILED;
		return -ENOMEM;
	}

	if (IS_ENABLED(CONFIG_OF) && bus_interface->dev->of_node) {
		/* parse devicetree property */
		ret = goodix_parse_dt(node, &core_data->board_data);
		if (ret) {
			ts_err("failed parse device info form dts, %d", ret);
			return -EINVAL;
		}
	} else {
		ts_err("no valid device tree node found");
		return -ENODEV;
	}

	core_data->hw_ops = goodix_get_hw_ops();
	if (!core_data->hw_ops) {
		ts_err("hw ops is NULL");
		core_module_prob_sate = CORE_MODULE_PROB_FAILED;
		return -EINVAL;
	}
	goodix_core_module_init();
	/* touch core layer is a platform driver */
	core_data->bus = bus_interface;
	dev_set_drvdata(dev, core_data);

	/* get GPIO resource */
	ret = goodix_ts_gpio_setup(core_data);
	if (ret) {
		ts_err("failed init gpio");
		goto err_out;
	}

	ret = goodix_ts_power_init(core_data);
	if (ret) {
		ts_err("failed init power");
		goto err_out;
	}

	ret = goodix_ts_power_on(core_data);
	if (ret) {
		ts_err("failed power on");
		goto err_out;
	}

	/* debug node init */
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_TOOLS
	goodix_tools_init();
#endif

	core_data->init_stage = CORE_INIT_STAGE1;
	goodix_modules.core_data = core_data;
	core_module_prob_sate = CORE_MODULE_PROB_SUCCESS;

	/* Try start a thread to get config-bin info */
	goodix_start_later_init(core_data);

	ts_info("goodix_ts_core probe success");
	return 0;

err_out:
	core_data->init_stage = CORE_INIT_FAIL;
	core_module_prob_sate = CORE_MODULE_PROB_FAILED;
	ts_err("goodix_ts_core failed, ret:%d", ret);
	return ret;
}

int goodix_ts_remove(struct device *dev)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;

#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_TOOLS
	goodix_tools_exit();
#endif

	if (core_data->init_stage >= CORE_INIT_STAGE2) {
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_GESTURE
		gesture_module_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_INSPECT
		inspect_module_exit();
#endif
		hw_ops->irq_enable(core_data, false);
#if IS_ENABLED(CONFIG_DRM)
		if (active_panel)
			drm_panel_notifier_unregister(active_panel,
						      &core_data->drm_notifier);
#endif
#if IS_ENABLED(CONFIG_FB)
		fb_unregister_client(&core_data->fb_notifier);
#endif
		core_module_prob_sate = CORE_MODULE_REMOVED;
		if (atomic_read(&core_data->ts_esd.esd_on))
			goodix_ts_esd_off(core_data);

#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_FW_UPDATE
		goodix_fw_update_uninit();
#endif
		goodix_ts_input_dev_remove(core_data);
		goodix_ts_sysfs_exit(core_data);
		goodix_ts_procfs_exit(core_data);
		goodix_ts_power_off(core_data);
	}

	return 0;
}