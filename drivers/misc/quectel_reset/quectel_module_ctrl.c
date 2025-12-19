/*
 * Driver quectel 5G module RM510Q.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>

#ifndef _HQ_5G_CTRL_H
#define _HQ_5G_CTRL_H

struct quectel_module_ctrl_platform_data {
	int full_card_power_off_gpio;
	int module_reset_gpio;
	const char *name;
	struct regulator *vreg;
};
#endif

struct quectel_module_ctrl_drvdata {
	const struct quectel_module_ctrl_platform_data *pdata;
	struct mutex rw_lock;
};

static void quectel_module_ctrl_set_gpio(int gpio, bool value)
{
	char *desc = NULL;

	desc = value? "high":"low";

	if (gpio_is_valid(gpio)) {
		gpio_direction_output(gpio, value);
		gpio_set_value_cansleep(gpio, value);
		pr_err("%s: gpio [%d] = [%s]\n", __func__, gpio, desc);
	}
}

static int quectel_module_ctrl_get_gpio(int gpio)
{
	char *desc = NULL;
	int state = 0;

	if (gpio_is_valid(gpio)) {
		state = gpio_get_value_cansleep(gpio);
		desc = state? "high":"low";
		pr_err("%s: gpio [%d] = [%s]\n", __func__, gpio, desc);
	}

	return state;
}

static ssize_t quectel_module_ctrl_full_power_gpio_show(struct device *dev, struct device_attribute *attr, char *buf )
{
	struct platform_device *pdev = to_platform_device(dev);
	struct quectel_module_ctrl_drvdata *ddata = platform_get_drvdata(pdev);
	int state = 0;

	state = quectel_module_ctrl_get_gpio(ddata->pdata->full_card_power_off_gpio);

	return scnprintf(buf, PAGE_SIZE, "%d\n", state);
}

static ssize_t quectel_module_ctrl_full_power_gpio_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct quectel_module_ctrl_drvdata *ddata = platform_get_drvdata(pdev);

	unsigned long val;
	int rc;
	bool state;

	if (size > 2)
		return -EINVAL;

	rc = kstrtoul(buf, 10, &val);
	if (rc != 0)
		return rc;

	state = val ? 1 : 0;

	quectel_module_ctrl_set_gpio(ddata->pdata->full_card_power_off_gpio, state);

	return size;
}

static DEVICE_ATTR(full_power_gpio, 0664,
		quectel_module_ctrl_full_power_gpio_show,
		quectel_module_ctrl_full_power_gpio_store);

static ssize_t quectel_module_ctrl_module_reset_show(struct device *dev, struct device_attribute *attr, char *buf )
{
	struct platform_device *pdev = to_platform_device(dev);
	struct quectel_module_ctrl_drvdata *ddata = platform_get_drvdata(pdev);
	int state = 0;

	state = quectel_module_ctrl_get_gpio(ddata->pdata->module_reset_gpio);

	return scnprintf(buf, PAGE_SIZE, "%d\n", state);
}

static ssize_t quectel_module_ctrl_module_reset_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct quectel_module_ctrl_drvdata *ddata = platform_get_drvdata(pdev);

	unsigned long val;
	int rc;
	bool state;

	if (size > 2)
		return -EINVAL;

	rc = kstrtoul(buf, 10, &val);
	if (rc != 0)
		return rc;

	state = val ? 1 : 0;

	quectel_module_ctrl_set_gpio(ddata->pdata->module_reset_gpio, state);

	return size;
}

static DEVICE_ATTR(module_reset_gpio, 0664,
		quectel_module_ctrl_module_reset_show,
		quectel_module_ctrl_module_reset_store);

static struct attribute *quectel_module_ctrl_attrs[] = {
	&dev_attr_full_power_gpio.attr,
	&dev_attr_module_reset_gpio.attr,
	NULL,
};

static struct attribute_group quectel_module_ctrl_attr_group = {
	.attrs = quectel_module_ctrl_attrs,
};

static int hq_gpio_configure(struct quectel_module_ctrl_drvdata *data, bool on)
{
	int err = 0;

	if (on) {
		if (gpio_is_valid(data->pdata->module_reset_gpio)) {
			err = gpio_request(data->pdata->module_reset_gpio,
						"module_reset_gpio");
			if (err) {
				pr_err("module_reset_gpio request failed");
				goto err_module_reset_gpio_req;
			}

			err = gpio_direction_output(data->pdata->module_reset_gpio, 1);
			if (err) {
				pr_err("set_direction for module_reset_gpio failed\n");
				goto err_module_reset_gpio_dir;
			}
		}

		if (gpio_is_valid(data->pdata->full_card_power_off_gpio)) {
			err = gpio_request(data->pdata->full_card_power_off_gpio,
						"full_card_power_off_gpio");
			if (err) {
				pr_err("full_card_power_off_gpio gpio request failed");
				goto err_module_reset_gpio_dir;
			}
			err = gpio_direction_output(data->pdata->full_card_power_off_gpio, 1);
			if (err) {
				pr_err("set_direction for full_card_power_off_gpio failed\n");
				goto err_full_card_power_off_gpio_dir;
			}
		}
		msleep(20);

		gpio_set_value_cansleep(data->pdata->module_reset_gpio, 0);

		return 0;
	}else {
		if (gpio_is_valid(data->pdata->module_reset_gpio))
			gpio_free(data->pdata->module_reset_gpio);

		if (gpio_is_valid(data->pdata->full_card_power_off_gpio)) {
			err = gpio_direction_output(data->pdata->full_card_power_off_gpio, 0);
			if (err) {
				pr_err("unable to set direction for gpio [%d]\n",
					data->pdata->full_card_power_off_gpio);
			}
			gpio_free(data->pdata->full_card_power_off_gpio);
		}
	}

	return 0;
err_full_card_power_off_gpio_dir:
	if (gpio_is_valid(data->pdata->full_card_power_off_gpio))
		gpio_free(data->pdata->full_card_power_off_gpio);
err_module_reset_gpio_dir:
	if (gpio_is_valid(data->pdata->module_reset_gpio))
		gpio_free(data->pdata->module_reset_gpio);
err_module_reset_gpio_req:
	return err;
}

#ifdef CONFIG_OF
/*
 * Translate node properties into platform_data
 */
static struct quectel_module_ctrl_platform_data *
quectel_module_ctrl_get_devtree_pdata(struct device *dev)
{
	struct device_node *node;
	struct quectel_module_ctrl_platform_data *pdata;

	node = dev->of_node;
	if (!node)
		return ERR_PTR(-ENODEV);

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	of_property_read_string(node, "label", &pdata->name);

	pdata->full_card_power_off_gpio = of_get_named_gpio(node, "full-card-power-off-gpio", 0);
	pdata->module_reset_gpio = of_get_named_gpio(node, "module-reset-gpio", 0);

	return pdata;
}

static const struct of_device_id quectel_module_ctrl_of_match[] = {
	{ .compatible = "quectel-module-ctrl", },
	{ },
};
MODULE_DEVICE_TABLE(of, quectel_module_ctrl_of_match);

#else

static inline struct quectel_module_ctrl_platform_data *
quectel_module_ctrl_get_devtree_pdata(struct device *dev)
{
	return ERR_PTR(-ENODEV);
}

#endif

static int quectel_module_ctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct quectel_module_ctrl_platform_data *pdata = dev_get_platdata(dev);
	struct quectel_module_ctrl_drvdata *ddata;
	int error;

	pr_info("%s\n", __func__);

	if (!pdata) {
		pdata = quectel_module_ctrl_get_devtree_pdata(dev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	}

	ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata) {
		dev_err(dev, "failed to allocate state\n");
		return -ENOMEM;
	}

	ddata->pdata = pdata;
	mutex_init(&ddata->rw_lock);

	platform_set_drvdata(pdev, ddata);

	/*
	pdata->vreg = devm_regulator_get(dev, "vreg");
	if (IS_ERR(pdata->vreg)) {
		error = PTR_ERR(pdata->vreg);
		dev_err(dev, "couldn't get vcca_reg regulator, ret:%d\n", error);
		pdata->vreg = NULL;
		return error;
	}

	error = regulator_enable(pdata->vreg);
	if (error < 0) {
		dev_err(dev, "vcca_reg regulator failed, ret:%d\n", error);
		//regulator_set_voltage(pdata->vcca_reg, 0, VCCA_MAX_UV);
		//regulator_set_load(pdata->vcca_reg, 0);
		return -EINVAL;
	}
	*/

	error = hq_gpio_configure(ddata, 1);

	error = sysfs_create_group(&pdev->dev.kobj, &quectel_module_ctrl_attr_group);
	if (error) {
		dev_err(dev, "Unable to export 5g module sysfs, error: %d\n",
			error);
		return error;
	}

	//device_init_wakeup(&pdev->dev, wakeup);

	pr_info("%s success\n", __func__);
	return 0;
}

static int quectel_module_ctrl_remove(struct platform_device *pdev)
{
	struct quectel_module_ctrl_drvdata *ddata = platform_get_drvdata(pdev);

	if (gpio_is_valid(ddata->pdata->module_reset_gpio)) {
		gpio_set_value(ddata->pdata->module_reset_gpio, 1);
		gpio_free(ddata->pdata->module_reset_gpio);
	}

	if (gpio_is_valid(ddata->pdata->full_card_power_off_gpio)) {
		gpio_set_value(ddata->pdata->full_card_power_off_gpio, 0);
		gpio_free(ddata->pdata->full_card_power_off_gpio);
	}

	sysfs_remove_group(&pdev->dev.kobj, &quectel_module_ctrl_attr_group);

	//device_init_wakeup(&pdev->dev, 0);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int quectel_module_ctrl_suspend(struct device *dev)
{
	//struct quectel_module_ctrl_drvdata *ddata = dev_get_drvdata(dev);

	return 0;
}

static int quectel_module_ctrl_resume(struct device *dev)
{
	//struct quectel_module_ctrl_drvdata *ddata = dev_get_drvdata(dev);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(quectel_module_ctrl_pm_ops, quectel_module_ctrl_suspend, quectel_module_ctrl_resume);

static struct platform_driver quectel_module_ctrl_device_driver = {
	.probe		= quectel_module_ctrl_probe,
	.remove		= quectel_module_ctrl_remove,
	.driver		= {
		.name	= "quectel-module-ctrl",
		.pm	= &quectel_module_ctrl_pm_ops,
		.of_match_table = of_match_ptr(quectel_module_ctrl_of_match),
	}
};

static int __init quectel_module_ctrl_init(void)
{
	int ret = 0;

	pr_err("%s: start\n", __func__);
	ret = platform_driver_register(&quectel_module_ctrl_device_driver);
	if (ret) {
		pr_err("failed to quectel module ctrl\n");
		goto err_driver;
	}

	pr_err("%s: finished\n", __func__);

err_driver:
	return ret;
}

static void __exit quectel_module_ctrl_exit(void)
{
	platform_driver_unregister(&quectel_module_ctrl_device_driver);
}

module_init(quectel_module_ctrl_init);
module_exit(quectel_module_ctrl_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("hq");
MODULE_DESCRIPTION("quectel 5g module driver");
