/*
 * Driver quectel 5G module RM510Q.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

struct quectel_module_ctrl_platform_data {
	int full_card_power_off_gpio;
	int module_reset_gpio;
	const char *name;
};

struct quectel_module_ctrl_drvdata {
	const struct quectel_module_ctrl_platform_data *pdata;
};

static int hq_gpio_configure(struct quectel_module_ctrl_drvdata *data, bool on)
{
	int err = 0;

	if (on) {
		if (gpio_is_valid(data->pdata->module_reset_gpio)) {
			err = gpio_request(data->pdata->module_reset_gpio,
					   "module_reset_gpio");
			if (err) {
				pr_err("module_reset_gpio request failed\n");
				goto err_module_reset_gpio_req;
			}

			err = gpio_direction_output(
				data->pdata->module_reset_gpio, 1);
			if (err) {
				pr_err("set_direction for module_reset_gpio failed\n");
				goto err_module_reset_gpio_dir;
			}
		}

		if (gpio_is_valid(data->pdata->full_card_power_off_gpio)) {
			err = gpio_request(
				data->pdata->full_card_power_off_gpio,
				"full_card_power_off_gpio");
			if (err) {
				pr_err("full_card_power_off_gpio gpio request failed\n");
				goto err_module_reset_gpio_dir;
			}
			err = gpio_direction_output(
				data->pdata->full_card_power_off_gpio, 1);
			if (err) {
				pr_err("set_direction for full_card_power_off_gpio failed\n");
				goto err_full_card_power_off_gpio_dir;
			}
		}

		msleep(20);

		gpio_set_value_cansleep(data->pdata->module_reset_gpio, 0);

		return 0;
	} else {
		if (gpio_is_valid(data->pdata->module_reset_gpio))
			gpio_free(data->pdata->module_reset_gpio);

		if (gpio_is_valid(data->pdata->full_card_power_off_gpio)) {
			err = gpio_direction_output(
				data->pdata->full_card_power_off_gpio, 0);
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
	struct device_node *node = dev->of_node;
	struct quectel_module_ctrl_platform_data *pdata;

	if (!node)
		return ERR_PTR(-ENODEV);

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	of_property_read_string(node, "label", &pdata->name);
	pdata->full_card_power_off_gpio =
		of_get_named_gpio(node, "full-card-power-off-gpio", 0);
	pdata->module_reset_gpio =
		of_get_named_gpio(node, "module-reset-gpio", 0);

	return pdata;
}

static const struct of_device_id quectel_module_ctrl_of_match[] = {
	{ .compatible = "quectel-module-ctrl", },
	{ },
};
MODULE_DEVICE_TABLE(of, quectel_module_ctrl_of_match);
#endif

static int quectel_module_ctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct quectel_module_ctrl_platform_data *pdata = dev_get_platdata(dev);
	struct quectel_module_ctrl_drvdata *ddata;
	int error;

	if (!pdata) {
		pdata = quectel_module_ctrl_get_devtree_pdata(dev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	}

	ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	ddata->pdata = pdata;
	platform_set_drvdata(pdev, ddata);

	error = hq_gpio_configure(ddata, 1);
	if (error)
		return error;

	return 0;
}

static int quectel_module_ctrl_remove(struct platform_device *pdev)
{
	struct quectel_module_ctrl_drvdata *ddata = platform_get_drvdata(pdev);

	hq_gpio_configure(ddata, 0);

	return 0;
}

static SIMPLE_DEV_PM_OPS(quectel_module_ctrl_pm_ops, NULL, NULL);

static struct platform_driver quectel_module_ctrl_device_driver = {
	.probe = quectel_module_ctrl_probe,
	.remove = quectel_module_ctrl_remove,
	.driver = {
		.name = "quectel-module-ctrl",
		.pm = &quectel_module_ctrl_pm_ops,
		.of_match_table = of_match_ptr(quectel_module_ctrl_of_match),
	}
};

static int __init quectel_module_ctrl_init(void)
{
	return platform_driver_register(&quectel_module_ctrl_device_driver);
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
