// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for ON Semiconductor FAN53870 PMIC (7-channel LDO)
 *
 * Used on Xiaomi sm8550 devices (e.g. Redmi K70 / POCO F6 Pro, "vermeer")
 * as the camera power management IC.
 *
 * Register map and voltage tables follow the vendor driver for the same part
 * (Xiaomi's drivers/regulator/wl2868c.c), verified on the board:
 *   WL2868C_SLAVE_ADDR = 0x5E (8-bit)  -> 7-bit 0x2f on the camera i2c hub
 *   ENABLE register = 0x0e, one bit per LDO (bit 0 = LDO1 .. bit 6 = LDO7)
 *   VSET registers  = 0x03 + (LDO - 1), written as a full byte
 *   LDO1-2 (DVDD class): 496mV  + n*8mV
 *   LDO3-7 (AVDD class): 1504mV + n*8mV
 * e.g. LDO1 at 1.2V -> 0x58 and LDO3..7 at 2.8V -> 0xa2, which is exactly
 * what the chip reports on the reference board.
 *
 * The vendor devicetree calls this part "onsemi,fan53870" but gives it the
 * wrong i2c address (0x35); the silicon answers at 0x2f.
 *
 * The regulators are described by child nodes below a "regulators" node
 * (of_match = "LDO1".."LDO7" + regulators_node), so the regulator core
 * attaches a device node to each one and consumers can use phandles such as
 *   avdd-supply = <&fan53870_l3>;
 * Without that, of_regulator_get() returns -EPROBE_DEFER forever.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/linear_range.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#define FAN53870_REG_ENABLE	0x0e
#define FAN53870_VSEL0		0x03

#define FAN53870_LDO1_2_MIN_UV		496000
#define FAN53870_LDO1_2_STEP_UV		8000
#define FAN53870_LDO1_2_MAX_UV		1504000

#define FAN53870_LDO3_7_MIN_UV		1504000
#define FAN53870_LDO3_7_STEP_UV		8000
#define FAN53870_LDO3_7_MAX_UV		3400000

struct fan53870_regulator {
	struct regulator_desc desc;
	unsigned int enable_bit;
};

static int fan53870_regulator_set_voltage_sel(struct regulator_dev *rdev,
					      unsigned int sel)
{
	return regmap_update_bits(rdev->regmap, rdev->desc->vsel_reg,
				  0xff, sel);
}

static int fan53870_regulator_get_voltage_sel(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, rdev->desc->vsel_reg, &val);
	if (ret)
		return ret;

	return val & 0xff;
}

static int fan53870_regulator_enable(struct regulator_dev *rdev)
{
	struct fan53870_regulator *fan = rdev_get_drvdata(rdev);
	unsigned int mask = BIT(fan->enable_bit);

	return regmap_update_bits(rdev->regmap, FAN53870_REG_ENABLE,
				  mask, mask);
}

static int fan53870_regulator_disable(struct regulator_dev *rdev)
{
	struct fan53870_regulator *fan = rdev_get_drvdata(rdev);
	unsigned int mask = BIT(fan->enable_bit);

	return regmap_update_bits(rdev->regmap, FAN53870_REG_ENABLE, mask, 0);
}

static int fan53870_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct fan53870_regulator *fan = rdev_get_drvdata(rdev);
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, FAN53870_REG_ENABLE, &val);
	if (ret)
		return ret;

	return !!(val & BIT(fan->enable_bit));
}

static const struct regulator_ops fan53870_regulator_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.set_voltage_sel	= fan53870_regulator_set_voltage_sel,
	.get_voltage_sel	= fan53870_regulator_get_voltage_sel,
	.enable			= fan53870_regulator_enable,
	.disable		= fan53870_regulator_disable,
	.is_enabled		= fan53870_regulator_is_enabled,
};

static const struct linear_range fan53870_ldo1_2_ranges[] = {
	REGULATOR_LINEAR_RANGE(FAN53870_LDO1_2_MIN_UV,
			       0, 0xff, FAN53870_LDO1_2_STEP_UV),
};

static const struct linear_range fan53870_ldo3_7_ranges[] = {
	REGULATOR_LINEAR_RANGE(FAN53870_LDO3_7_MIN_UV,
			       0, 0xff, FAN53870_LDO3_7_STEP_UV),
};

static const struct fan53870_regulator fan53870_regulators[] = {
	[0] = {
		.desc = {
			.name = "fan53870-l1",
			.of_match = "LDO1",
			.regulators_node = "regulators",
			.supply_name = "vin",
			.id = 0,
			.type = REGULATOR_VOLTAGE,
			.ops = &fan53870_regulator_ops,
			.n_voltages = 0x100,
			.linear_ranges = fan53870_ldo1_2_ranges,
			.n_linear_ranges = ARRAY_SIZE(fan53870_ldo1_2_ranges),
			.vsel_reg = FAN53870_VSEL0,
			.vsel_mask = 0xff,
			.owner = THIS_MODULE,
		},
		.enable_bit = 0,
	},
	[1] = {
		.desc = {
			.name = "fan53870-l2",
			.of_match = "LDO2",
			.regulators_node = "regulators",
			.supply_name = "vin",
			.id = 1,
			.type = REGULATOR_VOLTAGE,
			.ops = &fan53870_regulator_ops,
			.n_voltages = 0x100,
			.linear_ranges = fan53870_ldo1_2_ranges,
			.n_linear_ranges = ARRAY_SIZE(fan53870_ldo1_2_ranges),
			.vsel_reg = FAN53870_VSEL0 + 1,
			.vsel_mask = 0xff,
			.owner = THIS_MODULE,
		},
		.enable_bit = 1,
	},
	[2] = {
		.desc = {
			.name = "fan53870-l3",
			.of_match = "LDO3",
			.regulators_node = "regulators",
			.supply_name = "vin",
			.id = 2,
			.type = REGULATOR_VOLTAGE,
			.ops = &fan53870_regulator_ops,
			.n_voltages = 0x100,
			.linear_ranges = fan53870_ldo3_7_ranges,
			.n_linear_ranges = ARRAY_SIZE(fan53870_ldo3_7_ranges),
			.vsel_reg = FAN53870_VSEL0 + 2,
			.vsel_mask = 0xff,
			.owner = THIS_MODULE,
		},
		.enable_bit = 2,
	},
	[3] = {
		.desc = {
			.name = "fan53870-l4",
			.of_match = "LDO4",
			.regulators_node = "regulators",
			.supply_name = "vin",
			.id = 3,
			.type = REGULATOR_VOLTAGE,
			.ops = &fan53870_regulator_ops,
			.n_voltages = 0x100,
			.linear_ranges = fan53870_ldo3_7_ranges,
			.n_linear_ranges = ARRAY_SIZE(fan53870_ldo3_7_ranges),
			.vsel_reg = FAN53870_VSEL0 + 3,
			.vsel_mask = 0xff,
			.owner = THIS_MODULE,
		},
		.enable_bit = 3,
	},
	[4] = {
		.desc = {
			.name = "fan53870-l5",
			.of_match = "LDO5",
			.regulators_node = "regulators",
			.supply_name = "vin",
			.id = 4,
			.type = REGULATOR_VOLTAGE,
			.ops = &fan53870_regulator_ops,
			.n_voltages = 0x100,
			.linear_ranges = fan53870_ldo3_7_ranges,
			.n_linear_ranges = ARRAY_SIZE(fan53870_ldo3_7_ranges),
			.vsel_reg = FAN53870_VSEL0 + 4,
			.vsel_mask = 0xff,
			.owner = THIS_MODULE,
		},
		.enable_bit = 4,
	},
	[5] = {
		.desc = {
			.name = "fan53870-l6",
			.of_match = "LDO6",
			.regulators_node = "regulators",
			.supply_name = "vin",
			.id = 5,
			.type = REGULATOR_VOLTAGE,
			.ops = &fan53870_regulator_ops,
			.n_voltages = 0x100,
			.linear_ranges = fan53870_ldo3_7_ranges,
			.n_linear_ranges = ARRAY_SIZE(fan53870_ldo3_7_ranges),
			.vsel_reg = FAN53870_VSEL0 + 5,
			.vsel_mask = 0xff,
			.owner = THIS_MODULE,
		},
		.enable_bit = 5,
	},
	[6] = {
		.desc = {
			.name = "fan53870-l7",
			.of_match = "LDO7",
			.regulators_node = "regulators",
			.supply_name = "vin",
			.id = 6,
			.type = REGULATOR_VOLTAGE,
			.ops = &fan53870_regulator_ops,
			.n_voltages = 0x100,
			.linear_ranges = fan53870_ldo3_7_ranges,
			.n_linear_ranges = ARRAY_SIZE(fan53870_ldo3_7_ranges),
			.vsel_reg = FAN53870_VSEL0 + 6,
			.vsel_mask = 0xff,
			.owner = THIS_MODULE,
		},
		.enable_bit = 6,
	},
};

static const struct regmap_config fan53870_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x0e,
};

static int fan53870_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct regulator_config config = { };
	struct regmap *regmap;
	int i;

	regmap = devm_regmap_init_i2c(client, &fan53870_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "failed to init regmap\n");

	for (i = 0; i < ARRAY_SIZE(fan53870_regulators); i++) {
		struct regulator_dev *rdev;

		config.dev = dev;
		config.regmap = regmap;
		config.driver_data = (void *)&fan53870_regulators[i];

		rdev = devm_regulator_register(dev, &fan53870_regulators[i].desc,
					      &config);
		if (IS_ERR(rdev))
			return dev_err_probe(dev, PTR_ERR(rdev),
					     "failed to register %s\n",
					     fan53870_regulators[i].desc.name);

		/*
		 * Without a DT node the regulator cannot be referenced by a
		 * phandle, and a consumer asking for it gets -EPROBE_DEFER
		 * forever instead of an error.  Say so loudly.
		 */
		if (!rdev->dev.of_node)
			dev_warn(dev, "%s has no DT node (regulators/LDO%d missing)\n",
				 fan53870_regulators[i].desc.name, i + 1);
	}

	return 0;
}

static const struct of_device_id fan53870_of_match[] = {
	{ .compatible = "onnn,fan53870" },
	{ .compatible = "onsemi,fan53870" },	/* vendor spelling, transitional */
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fan53870_of_match);

static const struct i2c_device_id fan53870_i2c_id[] = {
	{ "fan53870", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, fan53870_i2c_id);

static struct i2c_driver fan53870_driver = {
	.driver = {
		.name = "fan53870",
		.of_match_table = fan53870_of_match,
	},
	.probe = fan53870_i2c_probe,
	.id_table = fan53870_i2c_id,
};
module_i2c_driver(fan53870_driver);

MODULE_DESCRIPTION("ON Semiconductor FAN53870 regulator driver");
MODULE_LICENSE("GPL");
