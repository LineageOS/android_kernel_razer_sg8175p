// SPDX-License-Identifier: GPL-2.0-only

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>

int thundersoft_straps_get_strap_value(struct device *dev, const char *gpioname)
{
	int gpio_count, i, value = 0;
	struct gpio_desc *desc;
	char appended_name[128];

	strlcpy(appended_name, gpioname, sizeof(appended_name));
	strlcat(appended_name, "-gpios", sizeof(appended_name));

	gpio_count = of_gpio_named_count(dev->of_node, appended_name);
	if (gpio_count < 1) {
		dev_err(dev, "Failed to find any GPIOs for strap: %s\n",
				gpioname);
		return -EINVAL;
	}

	dev_dbg(dev, "Reading strap '%s' (%d bits)\n", gpioname, gpio_count);

	for (i = gpio_count - 1; i >= 0; i--) {
		desc = devm_gpiod_get_index(dev, gpioname, i, GPIOD_IN);
		if (IS_ERR(desc)) {
			dev_err(dev, "Failed to get GPIO %s index %d\n",
					gpioname, i);
			return PTR_ERR(desc);
		}

		value <<= 1;
		value |= gpiod_get_value_cansleep(desc);

		dev_dbg(dev, "Strap %s bit %d value: %d (current total: %d)\n",
				gpioname, i, gpiod_get_value_cansleep(desc),
				value);

		devm_gpiod_put(dev, desc);
	}

	return value;
}

static int thundersoft_straps_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int sku, board_rev, command_line_len;
	const char *sku_str = NULL, *board_rev_str = NULL;
	char *new_command_line;

	dev_info(dev, "Initializing Thundersoft straps driver\n");

	sku = thundersoft_straps_get_strap_value(dev, "sku");
	board_rev = thundersoft_straps_get_strap_value(dev, "board_rev");

	if (sku >= 0) {
		sku_str = sku == 0 ? " androidboot.hardware.sku=5g" :
				     " androidboot.hardware.sku=wifi";
		dev_info(dev, "Detected SKU: %d (%s)\n", sku, sku_str);
	} else {
		dev_warn(dev, "Could not read SKU strap (err: %d)\n", sku);
	}

	switch (board_rev)
	{
	case 0: board_rev_str = " androidboot.hardware.board_rev=EVB"; break;
	case 1: board_rev_str = " androidboot.hardware.board_rev=T0"; break;
	case 2: board_rev_str = " androidboot.hardware.board_rev=EVT1"; break;
	case 3: board_rev_str = " androidboot.hardware.board_rev=EVT2"; break;
	case 4: board_rev_str = " androidboot.hardware.board_rev=DVT1"; break;
	case 5: board_rev_str = " androidboot.hardware.board_rev=DVT2"; break;
	case 6: board_rev_str = " androidboot.hardware.board_rev=DVT3"; break;
	// There was no change from PVT to MP
	case 7: board_rev_str = " androidboot.hardware.board_rev=MP"; break;
	default:
		dev_warn(dev, "Unknown board revision strap value: %d\n",
				board_rev);
		break;
	}

	if (!sku_str && !board_rev_str) {
		dev_err(dev, "Neither SKU nor Board Rev found. Aborting.\n");
		return -EINVAL;
	}

	command_line_len = strlen(saved_command_line) + 1;
	if (sku_str)
		command_line_len += strlen(sku_str);
	if (board_rev_str)
		command_line_len += strlen(board_rev_str);

	new_command_line = devm_kzalloc(dev, command_line_len, GFP_KERNEL);
	if (!new_command_line) {
		dev_err(dev, "Failed to allocate memory for new cmdline\n");
		return -ENOMEM;
	}

	strlcpy(new_command_line, saved_command_line, command_line_len);
	if (sku_str)
		strlcat(new_command_line, sku_str, command_line_len);
	if (board_rev_str)
		strlcat(new_command_line, board_rev_str, command_line_len);

	saved_command_line = new_command_line;

	dev_info(dev, "Updated boot cmdline successfully\n");
	dev_dbg(dev, "New cmdline: %s\n", saved_command_line);

	return 0;
}

static int thundersoft_straps_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id of_thundersoft_straps_match[] = {
	{ .compatible = "thundersoft,straps" },
	{},
};
MODULE_DEVICE_TABLE(of, of_thundersoft_straps_match);

static struct platform_driver thundersoft_straps_driver = {
	.probe		= thundersoft_straps_probe,
	.remove		= thundersoft_straps_remove,
	.driver		= {
		.name	= "thundersoft_straps",
		.of_match_table	= of_match_ptr(of_thundersoft_straps_match),
	},
};

module_platform_driver(thundersoft_straps_driver);

MODULE_AUTHOR("Balázs Triszka");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("cmdline sku and board revision append for Thundersoft gpio straps");
