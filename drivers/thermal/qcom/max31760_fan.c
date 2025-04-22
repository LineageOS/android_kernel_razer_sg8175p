// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/thermal.h>
#ifdef CONFIG_MACH_RAZER_NICOLE
#include <linux/time.h>
#include <linux/timer.h>
#endif

#define MAX31760_CTRL_REG1		0x00
#define MAX31760_CTRL_REG2		0x01
#define MAX31760_CTRL_REG3		0x02
#define MAX31760_DUTY_CYCLE_CTRL_REG	0x50
#ifdef CONFIG_MACH_RAZER_NICOLE
#define MAX31760_DUTY_CYCLE_STAT_REG	0x51
#define MAX31760_TC1H			0x52
#define MAX31760_TC1L			0x53
#endif

#ifdef CONFIG_MACH_RAZER_NICOLE
#define VDD_MAX_UV	3300000
#define VDD_MIN_UV	3300000
#define VDD_MAX_FAN	2900000
#define VDD_MIN_FAN	2800000
#else
#define VDD_MAX_UV	3100000
#define VDD_MIN_UV	3000000
#endif
#define VDD_LOAD_UA	300000
#define VCCA_MAX_UV	1800000
#define VCCA_MIN_UV	1800000
#define VCCA_LOAD_UA	600000
#define FAN_SPEED_LEVEL0	0
#ifdef CONFIG_MACH_RAZER_NICOLE
#define FAN_SPEED_LEVEL7	7
#define FAN_SPEED_MAX		(FAN_SPEED_LEVEL7 + 1)
#define FAN_SPEED_DUTY_MAX	100
#define FAN_SPEED_DUTY_ARRY_MAX	(FAN_SPEED_DUTY_MAX + 1)
#define FAN_SPEED_RPM_MAX	18000
#define FAN_SPEED_ERROR		3600
#define FAN_SPEED_RPM_MIN	0

#define FAN_SPEED_RPM_ERROR_LEVEL_0	50
#define FAN_SPEED_RPM_ERROR_LEVEL_1	150
#define FAN_SPEED_RPM_ERROR_LEVEL_2	350
#define FAN_SPEED_RPM_ERROR_LEVEL_3	680
#define FAN_SPEED_RPM_SAMPLE_TIMES	1
#else
#define FAN_SPEED_LEVEL4	4
#define FAN_SPEED_MAX		5
#endif

struct max31760_data {
	struct device *dev;
	struct i2c_client *i2c_client;
	struct thermal_cooling_device *cdev;
	struct mutex update_lock;
	struct regulator *vdd_reg;
	struct regulator *vcca_reg;
#ifdef CONFIG_MACH_RAZER_NICOLE
	struct regulator *vdda_reg;
	u32 dpr_en_gpio;
	u32 usb_charge_en_gpio;
#else
	u32 max31760_en_gpio;
#endif
	unsigned int cur_state;
	atomic_t in_suspend;
};

#ifdef CONFIG_MACH_RAZER_NICOLE
struct max31760_data *Max31760_pdata = NULL;
static int max31760_speed_map[FAN_SPEED_MAX] = {0x00, 0x4e, 0x71, 0x97, 0xb2, 0xcc, 0xe8, 0xFF};
#else
static int max31760_speed_map[FAN_SPEED_MAX] = {0x00, 0x30, 0x85, 0xCF, 0xFF};
#endif

static int max31760_write_byte(struct max31760_data *pdata, u8 reg, u8 val)
{
	int ret = 0;
	struct i2c_client *client = pdata->i2c_client;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		dev_err(pdata->dev, "failed write reg %#x failure, ret:%d\n", reg, ret);
		return ret;
	}

	dev_dbg(pdata->dev, "successfully write reg %#x=%#x\n", reg, val);
	return 0;
}

#ifndef CONFIG_MACH_RAZER_NICOLE
static void max31760_enable_gpio(struct max31760_data *pdata, int on)
{
	gpio_direction_output(pdata->max31760_en_gpio, on);
	dev_dbg(pdata->dev, "gpio:%d set to %d\n", pdata->max31760_en_gpio, on);
	usleep_range(20000, 20100);
}
#endif

static void max31760_speed_control(struct max31760_data *pdata, unsigned long level)
{
	max31760_write_byte(pdata, MAX31760_DUTY_CYCLE_CTRL_REG, max31760_speed_map[level]);
}

#ifdef CONFIG_MACH_RAZER_NICOLE
static int max31760_read_reg(unsigned char reg)
{
	struct max31760_data *pdata = Max31760_pdata;
	struct i2c_client *client = pdata->i2c_client;
	int value = -1;

	value = i2c_smbus_read_byte_data(client, reg);
	if (value < 0) {
		dev_err(pdata->dev, "failed read reg %x: %x\n", reg, value);
	}

	return value;
}

static int get_fan_current_speed_rpm(void)
{
	int tc1h = 0, tc1l = 0;
	int value = 0;
	struct max31760_data *pdata = Max31760_pdata;
	struct i2c_client *client = pdata->i2c_client;

	tc1h = i2c_smbus_read_byte_data(client, MAX31760_TC1H);
	if (tc1h < 0) {
		dev_err(pdata->dev, "failed read tc1h %d %#x\n", tc1h, tc1h);
		value = -1;
	}

	tc1l = i2c_smbus_read_byte_data(client, MAX31760_TC1L);
	if (tc1l < 0) {
		dev_err(pdata->dev, "failed read tc1l %d %#x\n", tc1l, tc1l);
		value = -2;
	}

	value = 60 * 100000 / (tc1h * 256 + tc1l) / 2;

	return value;
}

static ssize_t fan_speed_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	int level;

	if (sscanf(buf, "%d", &level) != 1)
		return count;
	if (level < 0 || level > 255) {
		printk("max31760_fan_speed_set_by_duty parameter error, level=%d, must in 0 to 100\n",
				level);
		return count;
	}

	mutex_lock(&Max31760_pdata->update_lock);
        max31760_write_byte(Max31760_pdata, MAX31760_DUTY_CYCLE_CTRL_REG,
			(u8)level);
	mutex_unlock(&Max31760_pdata->update_lock);

	return count;
}

static ssize_t fan_speed_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", (int)get_fan_current_speed_rpm());
}

static ssize_t fan_duty_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n",
			max31760_read_reg(MAX31760_DUTY_CYCLE_STAT_REG));
}

static ssize_t usb_charge_en_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int status;

	if (sscanf(buf, "%d", &status) != 1)
		return count;

	if (status <= 0) {
		status = 0;
	} else if (status > 0) {
		status = 1;
	}

	mutex_lock(&Max31760_pdata->update_lock);
	if (gpio_is_valid(Max31760_pdata->usb_charge_en_gpio)) {
		gpio_direction_output(Max31760_pdata->usb_charge_en_gpio,
				status);
	}
	mutex_unlock(&Max31760_pdata->update_lock);

	return count;
}

static ssize_t usb_charge_en_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int status = -1;

	if (gpio_is_valid(Max31760_pdata->usb_charge_en_gpio)) {
		status = gpio_get_value(Max31760_pdata->usb_charge_en_gpio);
	}

	return sprintf(buf, "%d\n",status);
}

static ssize_t dpr_en_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int status;

	if (sscanf(buf, "%d", &status) != 1)
		return count;

	if (status <= 0) {
		status = 0;
	} else if (status > 0) {
		status = 1;
	}

	mutex_lock(&Max31760_pdata->update_lock);
	if (gpio_is_valid(Max31760_pdata->dpr_en_gpio)) {
		gpio_direction_output(Max31760_pdata->dpr_en_gpio, status);
	}
	mutex_unlock(&Max31760_pdata->update_lock);

	return count;
}

static ssize_t dpr_en_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	int status = -1;

	if (gpio_is_valid(Max31760_pdata->dpr_en_gpio)) {
		status = gpio_get_value(Max31760_pdata->dpr_en_gpio);
	}

	return sprintf(buf, "%d\n",status);
}

static DEVICE_ATTR(fan_speed, 0664, fan_speed_show, fan_speed_store);
static DEVICE_ATTR(usb_charge_en, 0664, usb_charge_en_show, usb_charge_en_store);
static DEVICE_ATTR(dpr_en, 0664, dpr_en_show, dpr_en_store);
static DEVICE_ATTR_RO(fan_duty);
#endif

static void max31760_set_cur_state_common(struct max31760_data *pdata,
				unsigned long state)
{
#ifdef CONFIG_MACH_RAZER_NICOLE
	if (state > FAN_SPEED_LEVEL7)
		state = FAN_SPEED_LEVEL7;
#else
	if (state > FAN_SPEED_LEVEL4)
		state = FAN_SPEED_LEVEL4;
#endif
	if (state < FAN_SPEED_LEVEL0)
		state = FAN_SPEED_LEVEL0;

	if (!atomic_read(&pdata->in_suspend))
		max31760_speed_control(pdata, state);
	pdata->cur_state = state;
}

static int max31760_get_max_state(struct thermal_cooling_device *cdev,
				unsigned long *state)
{
#ifdef CONFIG_MACH_RAZER_NICOLE
	*state = FAN_SPEED_LEVEL7;
#else
	*state = FAN_SPEED_LEVEL4;
#endif
	return 0;
}

static int max31760_get_cur_state(struct thermal_cooling_device *cdev,
				unsigned long *state)
{
	struct max31760_data *data = cdev->devdata;

	mutex_lock(&data->update_lock);
	*state = data->cur_state;
	mutex_unlock(&data->update_lock);

	return 0;
}

static int max31760_set_cur_state(struct thermal_cooling_device *cdev,
				unsigned long state)
{
	struct max31760_data *data = cdev->devdata;

	mutex_lock(&data->update_lock);
	max31760_set_cur_state_common(data, state);
	mutex_unlock(&data->update_lock);

	return 0;
}

static struct thermal_cooling_device_ops max31760_cooling_ops = {
	.get_max_state = max31760_get_max_state,
	.get_cur_state = max31760_get_cur_state,
	.set_cur_state = max31760_set_cur_state,
};

static int max31760_register_cdev(struct max31760_data *pdata)
{
	int ret = 0;
	char cdev_name[THERMAL_NAME_LENGTH] = "";

	snprintf(cdev_name, THERMAL_NAME_LENGTH, "fan-max31760");

	pdata->cdev = thermal_of_cooling_device_register(pdata->dev->of_node, cdev_name,
						pdata, &max31760_cooling_ops);
	if (IS_ERR(pdata->cdev)) {
		ret = PTR_ERR(pdata->cdev);
		dev_err(pdata->dev, "Cooling register failed for %s, ret:%d\n", cdev_name, ret);
		pdata->cdev = NULL;
		return ret;
	}

	dev_dbg(pdata->dev, "Cooling register success for %s\n", cdev_name);
	return 0;
}

static void max31760_hw_init(struct max31760_data *pdata)
{
	max31760_write_byte(pdata, MAX31760_CTRL_REG1, 0x19);
	max31760_write_byte(pdata, MAX31760_CTRL_REG2, 0x11);
	max31760_write_byte(pdata, MAX31760_CTRL_REG3, 0x31);
	mutex_lock(&pdata->update_lock);
	max31760_speed_control(pdata, FAN_SPEED_LEVEL0);
	pdata->cur_state = FAN_SPEED_LEVEL0;
	mutex_unlock(&pdata->update_lock);

	atomic_set(&pdata->in_suspend, 0);
}

static int max31760_parse_dt(struct max31760_data *pdata)
{
	int ret = 0;
	struct device_node *node = pdata->dev->of_node;

	if (!node) {
		pr_err("device tree info missing\n");
		return -EINVAL;
	}

#ifdef CONFIG_MACH_RAZER_NICOLE
	pdata->usb_charge_en_gpio = of_get_named_gpio(node,
			"maxim,usb_charge_contrl", 0);
	if (!gpio_is_valid(pdata->usb_charge_en_gpio)) {
		dev_err(pdata->dev, "usb_charge_en_gpio not specified\n");
		return -EINVAL;
	}

	ret = gpio_request(pdata->usb_charge_en_gpio, "usb_charge_en_gpio");
	if (ret) {
		pr_err("usb_charge_en_gpio request failed, ret:%d\n", ret);
		return -EINVAL;
	}

	gpio_direction_output(pdata->usb_charge_en_gpio, 0);

	pdata->dpr_en_gpio = of_get_named_gpio(node, "maxim,dpr_contrl", 0);
	if (!gpio_is_valid(pdata->dpr_en_gpio)) {
		dev_err(pdata->dev, "dpr_en_gpio not specified\n");
		return -EINVAL;
	}

	ret = gpio_request(pdata->dpr_en_gpio, "dpr_en_gpio");
	if (ret) {
		pr_err("dpr_en_gpio request failed, ret:%d\n", ret);
		return -EINVAL;
	}

	gpio_direction_output(pdata->dpr_en_gpio, 1);

#else
	pdata->max31760_en_gpio = of_get_named_gpio(node, "maxim,fan_en_gpio", 0);
	if (!gpio_is_valid(pdata->max31760_en_gpio)) {
		dev_err(pdata->dev, "max31760 enable gpio not specified\n");
		return -EINVAL;
	}

	ret = gpio_request(pdata->max31760_en_gpio, "max31760_en_gpio");
	if (ret) {
		pr_err("max31760 enable gpio request failed, ret:%d\n", ret);
		goto error;
	}

	max31760_enable_gpio(pdata, 1);

	return ret;

error:
	gpio_free(pdata->max31760_en_gpio);
#endif
	return ret;
}

static int max31760_enable_vregs(struct max31760_data *pdata)
{
	int ret = 0;

	pdata->vdd_reg = devm_regulator_get(pdata->dev, "maxim,vdd");
	if (IS_ERR(pdata->vdd_reg)) {
		ret = PTR_ERR(pdata->vdd_reg);
		dev_err(pdata->dev, "couldn't get vdd_reg regulator, ret:%d\n", ret);
		pdata->vdd_reg = NULL;
		return ret;
	}

	regulator_set_voltage(pdata->vdd_reg, VDD_MIN_UV, VDD_MAX_UV);
	regulator_set_load(pdata->vdd_reg, VDD_LOAD_UA);
	ret = regulator_enable(pdata->vdd_reg);
	if (ret < 0) {
		dev_err(pdata->dev, "vdd_reg regulator failed, ret:%d\n", ret);
		regulator_set_voltage(pdata->vdd_reg, 0, VDD_MAX_UV);
		regulator_set_load(pdata->vdd_reg, 0);
		return -EINVAL;
	}

	pdata->vcca_reg = devm_regulator_get(pdata->dev, "maxim,vcca");
	if (IS_ERR(pdata->vcca_reg)) {
		ret = PTR_ERR(pdata->vcca_reg);
		dev_err(pdata->dev, "couldn't get vcca_reg regulator, ret:%d\n", ret);
		pdata->vcca_reg = NULL;
		return ret;
	}

	regulator_set_voltage(pdata->vcca_reg, VCCA_MIN_UV, VCCA_MAX_UV);
	regulator_set_load(pdata->vcca_reg, VCCA_LOAD_UA);
	ret = regulator_enable(pdata->vcca_reg);
	if (ret < 0) {
		dev_err(pdata->dev, "vcca_reg regulator failed, ret:%d\n", ret);
		regulator_set_voltage(pdata->vcca_reg, 0, VCCA_MAX_UV);
		regulator_set_load(pdata->vcca_reg, 0);
		return -EINVAL;
	}

#ifdef CONFIG_MACH_RAZER_NICOLE
	pdata->vdda_reg = devm_regulator_get(pdata->dev, "maxim,vdda");
	if (IS_ERR(pdata->vdda_reg)) {
		ret = PTR_ERR(pdata->vdda_reg);
		dev_err(pdata->dev, "couldn't get vdda_reg regulator, ret:%d\n", ret);
		pdata->vdda_reg = NULL;
		return ret;
	}

	regulator_set_voltage(pdata->vdda_reg, VDD_MIN_FAN, VDD_MAX_FAN);
	regulator_set_load(pdata->vdda_reg, VDD_LOAD_UA);
	ret = regulator_enable(pdata->vdda_reg);
	if (ret < 0) {
		dev_err(pdata->dev, "vdda_reg regulator failed, ret:%d\n", ret);
		regulator_set_voltage(pdata->vdda_reg, 0, VDD_MAX_FAN);
		regulator_set_load(pdata->vdda_reg, 0);
		return -EINVAL;
	}
#endif

	return 0;
}

static int max31760_remove(struct i2c_client *client)
{
	struct max31760_data *pdata = i2c_get_clientdata(client);

	if (!pdata)
		return 0;

	thermal_cooling_device_unregister(pdata->cdev);
	regulator_disable(pdata->vdd_reg);
	regulator_disable(pdata->vcca_reg);
#ifdef CONFIG_MACH_RAZER_NICOLE
	regulator_disable(pdata->vdda_reg);
#else
	max31760_enable_gpio(pdata, 0);
	gpio_free(pdata->max31760_en_gpio);
#endif

	return 0;
}

static int max31760_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	struct max31760_data *pdata;

	if (!client || !client->dev.of_node) {
		pr_err("max31760 invalid input\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "device doesn't support I2C\n");
		return -ENODEV;
	}

	pdata = devm_kzalloc(&client->dev, sizeof(struct max31760_data), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->dev = &client->dev;
	pdata->i2c_client = client;
	i2c_set_clientdata(client, pdata);
	dev_set_drvdata(&client->dev, pdata);
	mutex_init(&pdata->update_lock);

	ret = max31760_parse_dt(pdata);
	if (ret) {
		dev_err(pdata->dev, "failed to parse device tree, ret:%d\n", ret);
		goto fail_parse_dt;
	}

	ret = max31760_enable_vregs(pdata);
	if (ret) {
		dev_err(pdata->dev, "failed to enable regulators, ret:%d\n", ret);
		goto fail_enable_vregs;
	}

	max31760_hw_init(pdata);
	ret = max31760_register_cdev(pdata);
	if (ret) {
		dev_err(pdata->dev, "failed to register cooling device, ret:%d\n", ret);
		goto fail_register_cdev;
	}

#ifdef CONFIG_MACH_RAZER_NICOLE
	Max31760_pdata = pdata;
	if (sysfs_create_file(&(pdata->cdev->device.kobj), &dev_attr_fan_speed.attr)) {
		dev_err(pdata->dev, "failed to create note fan_speed, ret:%d\n", ret);
	}

	if (sysfs_create_file(&(pdata->cdev->device.kobj), &dev_attr_fan_duty.attr)) {
		dev_err(pdata->dev, "failed to create note fan_duty, ret:%d\n", ret);
	}

	if (sysfs_create_file(&(pdata->cdev->device.kobj), &dev_attr_usb_charge_en.attr)) {
		dev_err(pdata->dev, "failed to create note usb_charge_en, ret:%d\n", ret);
	}

	if (sysfs_create_file(&(pdata->cdev->device.kobj), &dev_attr_dpr_en.attr)) {
		dev_err(pdata->dev, "failed to create note dpr_en, ret:%d\n", ret);
	}
#endif

	return ret;

fail_register_cdev:
	max31760_remove(client);
	return ret;
fail_enable_vregs:
#ifndef CONFIG_MACH_RAZER_NICOLE
	max31760_enable_gpio(pdata, 0);
	gpio_free(pdata->max31760_en_gpio);
#endif
fail_parse_dt:
	i2c_set_clientdata(client, NULL);
	dev_set_drvdata(&client->dev, NULL);
	return ret;
}

static void max31760_shutdown(struct i2c_client *client)
{
	max31760_remove(client);
}

static int max31760_suspend(struct device *dev)
{
	struct max31760_data *pdata = dev_get_drvdata(dev);

	dev_dbg(dev, "enter suspend now\n");
	if (pdata) {
		atomic_set(&pdata->in_suspend, 1);
		mutex_lock(&pdata->update_lock);
#ifdef CONFIG_MACH_RAZER_NICOLE
		pdata->cur_state = FAN_SPEED_LEVEL0;
#endif
		max31760_speed_control(pdata, FAN_SPEED_LEVEL0);
#ifndef CONFIG_MACH_RAZER_NICOLE
		max31760_enable_gpio(pdata, 0);
#else
		regulator_disable(pdata->vdda_reg);
#endif
		regulator_disable(pdata->vdd_reg);
		mutex_unlock(&pdata->update_lock);
	}

	return 0;
}

static int max31760_resume(struct device *dev)
{
	struct max31760_data *pdata = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "enter resume now\n");
	if (pdata) {
		atomic_set(&pdata->in_suspend, 0);
		mutex_lock(&pdata->update_lock);
#ifndef CONFIG_MACH_RAZER_NICOLE
		max31760_enable_gpio(pdata, 1);
#else
		ret = regulator_enable(pdata->vdda_reg);
		if (ret < 0)
			dev_err(pdata->dev, "vdda_reg regulator failed, ret:%d\n", ret);
#endif

		ret = regulator_enable(pdata->vdd_reg);
		if (ret < 0)
			dev_err(pdata->dev, "vdd_reg regulator failed, ret:%d\n", ret);

		max31760_write_byte(pdata, MAX31760_CTRL_REG1, 0x19);
		max31760_write_byte(pdata, MAX31760_CTRL_REG2, 0x11);
		max31760_write_byte(pdata, MAX31760_CTRL_REG3, 0x31);
		max31760_set_cur_state_common(pdata, pdata->cur_state);
		mutex_unlock(&pdata->update_lock);
	}

	return 0;
}

static const struct of_device_id max31760_id_table[] = {
	{ .compatible = "maxim,max31760",},
	{ },
};

static const struct i2c_device_id max31760_i2c_table[] = {
	{ "max31760", 0 },
	{ },
};

static SIMPLE_DEV_PM_OPS(max31760_pm_ops, max31760_suspend, max31760_resume);

static struct i2c_driver max31760_i2c_driver = {
	.probe = max31760_probe,
	.remove = max31760_remove,
	.shutdown = max31760_shutdown,
	.driver = {
		.name = "max31760",
		.of_match_table = max31760_id_table,
		.pm = &max31760_pm_ops,
	},
	.id_table = max31760_i2c_table,
};

module_i2c_driver(max31760_i2c_driver);
MODULE_DEVICE_TABLE(i2c, max31760_i2c_table);
MODULE_DESCRIPTION("Maxim 31760 Fan Controller");
MODULE_LICENSE("GPL v2");
