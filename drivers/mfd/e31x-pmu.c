// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 National Instruments Corp
 * Author: Virendra Kakade <virendra.kakade@ni.com>
 *
 * Ettus Research E31x PMU MFD driver
 */

#include <linux/err.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/mfd/core.h>
#include <linux/of_device.h>
#include <linux/mfd/e31x-pmu.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of.h>

#define E31X_PMU_MISC_IRQ_MASK         BIT(8)
#define E31X_PMU_MISC_IRQ_SHIFT                8
#define E31X_PMU_MISC_VERSION_MIN_MASK GENMASK(3, 0)
#define E31X_PMU_MISC_VERSION_MIN_SHIFT 0
#define E31X_PMU_MISC_VERSION_MAJ_MASK GENMASK(7, 4)
#define E31X_PMU_MISC_VERSION_MAJ_SHIFT 4

struct e31x_pmu {
       struct regmap *regmap;
};

static int e31x_pmu_check_version(struct platform_device *pdev, struct e31x_pmu *pmu)
{
       int timeout = 100;
       u32 misc, maj, min;
       int err;
       /* we need to wait a bit for firmware to populate the fields */
       while (timeout--) {
               err = regmap_read(pmu->regmap, E31X_PMU_REG_MISC, &misc);
               if (err)
                       return err;
               if (misc)
                       break;

               usleep_range(2500, 5000);
       }

       /* only firmware versions above 2.0 are supported */
       maj = E31X_PMU_GET_FIELD(MISC_VERSION_MAJ, misc);
       min = E31X_PMU_GET_FIELD(MISC_VERSION_MIN, misc);
       if (maj < 2) {
               dev_err(&pdev->dev, "Unsupported firmware version %u.%u\n", maj, min);
               return -ENOTSUPP;
       } else {
               dev_info(&pdev->dev, "Found firmware version %u.%u\n", maj, min);
       }

       return 0;
}

static ssize_t autoboot_store(struct device *dev,
              struct device_attribute *attr, const char *buf, size_t size)
{
       struct e31x_pmu *pmu = dev_get_drvdata(dev);
       u32 eeprom;
       unsigned long autoboot_bit, autoboot_input;
       int err;

       err = kstrtoul(buf, 10, &autoboot_input);
       if (err)
              return -EINVAL;

       /* either on or off ... */
       autoboot_bit = autoboot_input ? 0x1 : 0x0;

       err = regmap_read(pmu->regmap, E31X_PMU_REG_EEPROM, &eeprom);
       if (err)
              return err;

       eeprom &= ~E31X_PMU_EEPROM_AUTOBOOT_MASK;
       eeprom |= autoboot_bit << E31X_PMU_EEPROM_AUTOBOOT_SHIFT;

       err = regmap_write(pmu->regmap, E31X_PMU_REG_EEPROM, eeprom);
       if (err)
              return err;

       return size;
}

static ssize_t autoboot_show(struct device *dev,
              struct device_attribute *attr, char *buf)
{
       struct e31x_pmu *pmu = dev_get_drvdata(dev);
       u32 eeprom;
       int ret;

       ret = regmap_read(pmu->regmap, E31X_PMU_REG_EEPROM, &eeprom);
       if (ret)
              return ret;
       eeprom &= E31X_PMU_EEPROM_AUTOBOOT_MASK;
       eeprom >>= E31X_PMU_EEPROM_AUTOBOOT_SHIFT;

       ret = sprintf(buf, "%u\n", eeprom);

       return ret;
}

static DEVICE_ATTR_RW(autoboot);

static struct attribute *e31x_pmu_attrs[] = {
       &dev_attr_autoboot.attr,
       NULL
};

ATTRIBUTE_GROUPS(e31x_pmu);

static int e31x_pmu_probe(struct platform_device *pdev)
{
       int ret;
       struct e31x_pmu *pmu;
       pmu = devm_kzalloc(&pdev->dev, sizeof(*pmu), GFP_KERNEL);
       if (!pmu)
               return -ENOMEM;
       platform_set_drvdata(pdev, pmu);
       pmu->regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "regmap");

       if (IS_ERR(pmu->regmap))
               return PTR_ERR(pmu->regmap);
       if (e31x_pmu_check_version(pdev, pmu))
               return -ENOTSUPP;

       ret = sysfs_create_group(&pdev->dev.kobj, &e31x_pmu_group);
       if (ret) {
              dev_err(&pdev->dev, "sysfs creation failed\n");
              return ret;
       }

       return devm_of_platform_populate(&pdev->dev);
}

static void e31x_pmu_remove(struct platform_device *pdev)
{
       sysfs_remove_group(&pdev->dev.kobj, &e31x_pmu_group);
}

static const struct of_device_id e31x_pmu_id[] = {
       { .compatible = "ni,e31x-pmu" },
       {},
};

static struct platform_driver e31x_pmu_driver = {
       .driver = {
               .name = "e31x-pmu",
               .of_match_table = e31x_pmu_id,
               .groups = e31x_pmu_groups,
       },
       .probe = e31x_pmu_probe,
       .remove = e31x_pmu_remove,
};
module_platform_driver(e31x_pmu_driver);

MODULE_DESCRIPTION("E31x PMU driver");
MODULE_AUTHOR("Virendra Kakade <virendra.kakade@ni.com>");
MODULE_LICENSE("GPL");
