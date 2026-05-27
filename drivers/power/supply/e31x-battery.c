// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 National Instruments Corp
 * Author: Virendra Kakade <virendra.kakade@ni.com>
 *
 * Ettus Research E31x battery driver
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#include <linux/mfd/e31x-pmu.h>
#include <linux/of.h>

#define E31X_PMU_BATTERY_VOLTAGE_MASK		GENMASK(23, 8)
#define E31X_PMU_BATTERY_VOLTAGE_SHIFT		8
#define E31X_PMU_BATTERY_TEMP_ALERT_MASK       GENMASK(7,6)
#define E31X_PMU_BATTERY_TEMP_ALERT_SHIFT      6
#define E31X_PMU_BATTERY_ONLINE_MASK           BIT(5)
#define E31X_PMU_BATTERY_ONLINE_SHIFT          5
#define E31X_PMU_BATTERY_HEALTH_MASK           GENMASK(4,2)
#define E31X_PMU_BATTERY_HEALTH_SHIFT          2
#define E31X_PMU_BATTERY_STATUS_MASK           GENMASK(1,0)
#define E31X_PMU_BATTERY_STATUS_SHIFT          0

#define E31X_PMU_GAUGE_TEMP_MASK               GENMASK(31,16)
#define E31X_PMU_GAUGE_TEMP_SHIFT              16
#define E31X_PMU_GAUGE_CHARGE_MASK             GENMASK(15,0)
#define E31X_PMU_GAUGE_CHARGE_SHIFT            0

#define E31X_PMU_GAUGE_VOLTAGE_MASK            GENMASK(15, 0)
#define E31X_PMU_GAUGE_VOLTAGE_SHIFT           0

#define E31X_PMU_GAUGE_CHARGE_LAST_FULL_MASK   GENMASK(15, 0)
#define E31X_PMU_GAUGE_CHARGE_LAST_FULL_SHIFT  0

#define E31X_BATTERY_CHARGE_DESIGN_FULL (3200000)
#define E31X_PMU_VSENSE                        (6000)

struct e31x_battery_dev {
       struct regmap *regmap;
       struct power_supply *supply;
};

static int e31x_battery_get_status(struct e31x_battery_dev *bat,
                                  union power_supply_propval *val)
{
       u32 value;
       int err;

       err = regmap_read(bat->regmap, E31X_PMU_REG_CHARGER, &value);
       if (err)
               return err;

       value = E31X_PMU_GET_FIELD(CHARGER_ONLINE, value);

       /* if charger is offline, we're discharging, period */
       if (!value) {
               val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
               return 0;
       }

       err = regmap_read(bat->regmap, E31X_PMU_REG_BATTERY, &value);
       if (err)
               return err;

       value &= E31X_PMU_BATTERY_STATUS_MASK;

       switch (value) {
       case 0x0:
               val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
               break;
       case 0x1:
               val->intval = POWER_SUPPLY_STATUS_CHARGING;
               break;
       case 0x2:
               val->intval = POWER_SUPPLY_STATUS_FULL;
               break;
       case 0x3:
               val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
               break;
       default:
               return -EIO;
       };

       return 0;
}

static int e31x_battery_get_health(struct e31x_battery_dev *bat,
                                  union power_supply_propval *val)
{
       u32 value;
       int err;

       err = regmap_read(bat->regmap, E31X_PMU_REG_BATTERY, &value);
       if (err)
               return err;

       value = E31X_PMU_GET_FIELD(BATTERY_HEALTH, value);

       switch (value) {
       case 0x00:
               val->intval = POWER_SUPPLY_HEALTH_GOOD;
               break;
       case 0x01:
               val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
               break;
       case 0x02:
               val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
               break;
       case 0x03:
               val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
               break;
       case 0x04:
               val->intval = POWER_SUPPLY_HEALTH_COLD;
               break;
       default:
               val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
               break;
       }

       return 0;
}

static int e31x_battery_get_online(struct e31x_battery_dev *bat,
                                  union power_supply_propval *val)
{
       u32 value;
       int err;

       err = regmap_read(bat->regmap, E31X_PMU_REG_BATTERY, &value);
       if (err)
               return err;

       val->intval = !!(value & E31X_PMU_BATTERY_ONLINE_MASK);

       return 0;
}

static int e31x_battery_get_voltage_now(struct e31x_battery_dev *bat,
                                       union power_supply_propval *val)
{
       u32 value;
       int err;

       err = regmap_read(bat->regmap, E31X_PMU_REG_BATTERY, &value);

       if (err)
               return err;

       value = E31X_PMU_GET_FIELD(BATTERY_VOLTAGE, value);

       val->intval = 10000 * (value * E31X_PMU_VSENSE / GENMASK(15, 0));

       return 0;
}

#define E31X_PMU_BIN_TO_UAH(x) ((x) * 53)
#define E31X_PMU_UAH_TO_BIN(x) ((x) / 53)

static int e31x_battery_get_charge_now(struct e31x_battery_dev *bat,
                                       union power_supply_propval *val)
{
       u32 value;
       int err;

       err = regmap_read(bat->regmap, E31X_PMU_REG_GAUGE, &value);
       if (err)
               return err;

       value = E31X_PMU_GET_FIELD(GAUGE_CHARGE, value);
       val->intval = E31X_PMU_BIN_TO_UAH(value);

       return 0;
}

static int e31x_battery_set_charge_now(struct e31x_battery_dev *bat,
				       const union power_supply_propval *val)
{
	u32 data;
	int err;
	u16 charge;

	err = regmap_read(bat->regmap, E31X_PMU_REG_GAUGE, &data);
	if (err)
		return err;

	charge = E31X_PMU_UAH_TO_BIN(val->intval);

	data = (data & (~E31X_PMU_GAUGE_CHARGE_MASK)) |
			(charge << E31X_PMU_GAUGE_CHARGE_SHIFT);

	err = regmap_write(bat->regmap, E31X_PMU_REG_GAUGE, data);
	if (err)
		return err;

	return 0;
}

static int e31x_battery_get_temp(struct e31x_battery_dev *bat,
                                union power_supply_propval *val)
{
       u32 value;
       int err;

       err = regmap_read(bat->regmap, E31X_PMU_REG_GAUGE, &value);
       if (err)
               return err;

       value = E31X_PMU_GET_FIELD(GAUGE_TEMP, value);
       val->intval = 10 * (((600 * value) / 0xffff) - 273);

       return 0;
}

static int e31x_battery_get_charge_full(struct e31x_battery_dev *bat,
                                       union power_supply_propval *val)
{
       u32 value;
       int err;

       err = regmap_read(bat->regmap, E31X_PMU_REG_LAST, &value);
       if (err)
               return err;

       value = E31X_PMU_GET_FIELD(GAUGE_CHARGE_LAST_FULL, value);
       val->intval = E31X_PMU_BIN_TO_UAH(value);

       return 0;
}

static const int e31x_pmu_temp_values[] = {
       600, 800, 1000, 1200
};

static int e31x_battery_get_temp_alert_max(struct e31x_battery_dev *bat,
                                          union power_supply_propval *val)
{
       u32 value;
       int err;
       int i;

       err = regmap_read(bat->regmap, E31X_PMU_REG_BATTERY, &value);
       if (err)
               return err;

       value = E31X_PMU_GET_FIELD(BATTERY_TEMP_ALERT, value);
       for (i = 1; i < ARRAY_SIZE(e31x_pmu_temp_values); i++)
               if (e31x_pmu_temp_values[i] > value)
                       break;

       val->intval = e31x_pmu_temp_values[i - 1];

       return 0;
}

static int e31x_battery_get_prop(struct power_supply *psy,
                                enum power_supply_property psp,
                                union power_supply_propval *val)
{
       struct e31x_battery_dev *battery = power_supply_get_drvdata(psy);

       switch(psp) {
       case POWER_SUPPLY_PROP_STATUS:
               return e31x_battery_get_status(battery, val);
       case POWER_SUPPLY_PROP_HEALTH:
               return e31x_battery_get_health(battery, val);
       case POWER_SUPPLY_PROP_ONLINE:
               return e31x_battery_get_online(battery, val);
       case POWER_SUPPLY_PROP_TECHNOLOGY:
               val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
               break;
       case POWER_SUPPLY_PROP_TEMP:
               return e31x_battery_get_temp(battery, val);
       case POWER_SUPPLY_PROP_VOLTAGE_NOW:
               return e31x_battery_get_voltage_now(battery, val);
       case POWER_SUPPLY_PROP_CHARGE_NOW:
               return e31x_battery_get_charge_now(battery, val);
       case POWER_SUPPLY_PROP_CHARGE_FULL:
               return e31x_battery_get_charge_full(battery, val);
       case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
               val->intval = E31X_BATTERY_CHARGE_DESIGN_FULL;
               return 0;
       case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
               return e31x_battery_get_temp_alert_max(battery, val);
       default:
               break;
       };

       return 0;
}


static int e31x_battery_set_prop(struct power_supply *psy,
                                enum power_supply_property psp,
                                const union power_supply_propval *val)
{
	int ret;
	struct e31x_battery_dev *battery = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		ret = e31x_battery_set_charge_now(battery, val);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static enum power_supply_property e31x_battery_props[] = {
       POWER_SUPPLY_PROP_STATUS,
       POWER_SUPPLY_PROP_HEALTH,
       POWER_SUPPLY_PROP_ONLINE,
       POWER_SUPPLY_PROP_TECHNOLOGY,
       POWER_SUPPLY_PROP_TEMP,
       POWER_SUPPLY_PROP_VOLTAGE_NOW,
       POWER_SUPPLY_PROP_CHARGE_NOW,
       POWER_SUPPLY_PROP_CHARGE_FULL,
       POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
       POWER_SUPPLY_PROP_TEMP_ALERT_MAX,
};

static int e31x_battery_property_is_writeable(struct power_supply *psy,
					      enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		ret = 1;
		break;
	default:
		ret = 0;
	}

	return ret;
}

static const struct power_supply_desc e31x_battery_desc = {
       .name = "e31x-battery",
       .type = POWER_SUPPLY_TYPE_BATTERY,
       .properties = e31x_battery_props,
       .num_properties = ARRAY_SIZE(e31x_battery_props),
       .property_is_writeable = e31x_battery_property_is_writeable,
       .get_property = e31x_battery_get_prop,
       .set_property = e31x_battery_set_prop,
};

static const struct of_device_id e31x_battery_id[] = {
       { .compatible = "ni,e31x-battery" },
       { },
};

static int e31x_battery_probe(struct platform_device *pdev)
{
       struct power_supply_config psy_cfg = {};
       struct e31x_battery_dev *battery;

       if (!of_device_is_available(pdev->dev.of_node))
               return -ENODEV;

       battery = devm_kzalloc(&pdev->dev, sizeof(*battery), GFP_KERNEL);
       if (!battery)
               return -ENOMEM;

       battery->regmap = syscon_regmap_lookup_by_phandle(pdev->dev.parent->of_node, "regmap");

       psy_cfg.of_node = pdev->dev.of_node;
       psy_cfg.drv_data = battery;

       battery->supply = devm_power_supply_register(&pdev->dev,
                                                    &e31x_battery_desc,
                                                    &psy_cfg);
       if (IS_ERR(battery->supply))
               return PTR_ERR(battery->supply);
       return 0;
}

static struct platform_driver e31x_battery_driver = {
       .driver = {
               .name = "e31x-battery",
               .of_match_table = e31x_battery_id,
       },
       .probe = e31x_battery_probe,
};
module_platform_driver(e31x_battery_driver);

MODULE_AUTHOR("Virendra Kakade <virendra.kakade@ni.com>");
MODULE_DESCRIPTION("E31x battery driver");
MODULE_LICENSE("GPL");
