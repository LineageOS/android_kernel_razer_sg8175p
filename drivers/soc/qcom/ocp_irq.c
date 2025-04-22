/* 
 * otg ocp driver
 * Author: i.thundersoft.com
 */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/irq.h>

#include <linux/gpio.h>
#include <linux/of_gpio.h>

struct ocp {
	struct work_struct ocp_work;
	struct device *dev;
	int ocp_control_gpio;
	int ocp_irq_gpio;
	int ocp_ctr_gpio;
	int ocp_irq;
	int ocp_irq_n;
};

struct ocp *ocp_info;

static void det_ocp_close_ic(void)
{
	if (ocp_info->ocp_ctr_gpio > 0) {
		gpio_direction_output(ocp_info->ocp_ctr_gpio, 0);
	} else {
		dev_err(ocp_info->dev, "no ocp_ctr_gpio defined\n");
	}
}

static irqreturn_t ocp_ocp_threadirq_func(int irq, void *data)
{
	det_ocp_close_ic();

	return IRQ_HANDLED;
}

static int ocp_ocp_irq_setup(struct ocp *ocp_info)
{
	int ret;

	ocp_info->ocp_irq_n = gpio_to_irq(ocp_info->ocp_irq);
	if (ocp_info->ocp_irq_n < 0) {
		dev_err(ocp_info->dev, "failed to get ocp irq num %d",
			ocp_info->ocp_irq_n);
		return -EINVAL;
	}

	ret = devm_request_threaded_irq(ocp_info->dev, ocp_info->ocp_irq_n,
					NULL, ocp_ocp_threadirq_func,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					"ocp_irq", ocp_info);
	if (ret < 0)
		dev_err(ocp_info->dev, "Failed to requeset ocp threaded irq:%d",
			ret);

	return ret;
}

static int ocp_ocp_gpio_setup(struct ocp *ocp_info)
{
	if (!gpio_is_valid(ocp_info->ocp_control_gpio)) {
		dev_err(ocp_info->dev, "%s: ocp control gpio not specified\n",
			__func__);
		return -1;
	} else {
		ocp_info->ocp_ctr_gpio = ocp_info->ocp_control_gpio;
		if ((gpio_request(ocp_info->ocp_ctr_gpio, "ocp_ctr_gpio"))) {
			dev_err(ocp_info->dev,
				"%s ocp control gpio request failed\n",
				__func__);
			return -1;
		}
	}

	if (!gpio_is_valid(ocp_info->ocp_irq_gpio)) {
		dev_err(ocp_info->dev, "%s: ocp irq gpio not specified\n",
			__func__);
		return -1;
	} else {
		ocp_info->ocp_irq = ocp_info->ocp_irq_gpio;
		if ((gpio_request(ocp_info->ocp_irq, "ocp_irq"))) {
			dev_err(ocp_info->dev,
				"%s ocp irq gpio request failed\n", __func__);
			return -1;
		}
		gpio_direction_input(ocp_info->ocp_irq);
	}

	return 0;
}

static int ocp_get_properties(struct ocp *ocp_info)
{
	struct device *dev = ocp_info->dev;
	struct device_node *child_node = dev->of_node;
	int ret;

	if (!child_node) {
		dev_err(ocp_info->dev,
			"No DT child node found for connected ocp.\n");
		return -1;
	} else {
		ret = of_get_named_gpio(child_node, "ocp,control-gpio", 0);
		if (ret < 0) {
			dev_err(ocp_info->dev,
				"invalid opc,control-gpio in dt: %d", ret);
			return ret;
		}
		ocp_info->ocp_control_gpio = ret;

		ret = of_get_named_gpio(child_node, "ocp,irq-gpio", 0);
		if (ret < 0) {
			dev_err(ocp_info->dev, "invalid ocp irq-gpio in dt: %d",
				ret);
			return ret;
		}
		ocp_info->ocp_irq_gpio = ret;
	}

	return ret;
}

static int ocp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	ocp_info = devm_kzalloc(dev, sizeof(*ocp_info), GFP_KERNEL);
	if (!ocp_info)
		return -ENOMEM;

	ocp_info->dev = dev;

	ret = ocp_get_properties(ocp_info);
	if (ret < 0) {
		dev_err(ocp_info->dev, "failed getting dt properties: %d", ret);
		return ret;
	}

	ret = ocp_ocp_gpio_setup(ocp_info);
	if (ret < 0) {
		dev_err(ocp_info->dev, "failed setting up gpio: %d", ret);
		return ret;
	}

	ret = ocp_ocp_irq_setup(ocp_info);
	if (ret < 0) {
		dev_err(ocp_info->dev, "failed setting up irq: %d", ret);
		return ret;
	}

	return ret;
}

static int ocp_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id of_ocp_match[] = {
	{ .compatible = "qcom,ocp-msm" },
	{},
};
MODULE_DEVICE_TABLE(of, of_ocp_match);

static struct platform_driver ocp_driver = {
	.probe		= ocp_probe,
	.remove		= ocp_remove,
	.driver		= {
		.name	= "ocp",
		.of_match_table	= of_match_ptr(of_ocp_match),
	},
};

module_platform_driver(ocp_driver);

MODULE_ALIAS("platform:ocp");
MODULE_AUTHOR("thundersoft");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ocp Driver");
