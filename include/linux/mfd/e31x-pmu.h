// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 National Instruments Corp
 * Author: Virendra Kakade <virendra.kakade@ni.com>
 *
 * Ettus Research E31x PMU constants
 */

#ifndef MFD_E31X_PMU_H
#define MFD_E31X_PMU_H

#include <linux/bitops.h>

#define E31X_PMU_REG_MISC      0x04
#define E31X_PMU_REG_BATTERY   0x08
#define E31X_PMU_REG_CHARGER   0x0c
#define E31X_PMU_REG_GAUGE     0x10
#define E31X_PMU_REG_STATUS    0x14
#define E31X_PMU_REG_LAST      0x18
#define E31X_PMU_REG_EEPROM    0x1c

#define E31X_PMU_GET_FIELD(name, reg) \
       (((reg) & E31X_PMU_## name ##_MASK) >> \
        E31X_PMU_## name ##_SHIFT)

/* the eeprom register */
static const u32 E31X_PMU_EEPROM_AUTOBOOT_MASK = BIT(0);
static const u32 E31X_PMU_EEPROM_AUTOBOOT_SHIFT = 0;
static const u32 E31X_PMU_EEPROM_DB_POWER_MASK = BIT(1);
static const u32 E31X_PMU_EEPROM_DB_POWER_SHIFT = 1;

#define E31X_PMU_CHARGER_ONLINE_MASK           BIT(2)
#define E31X_PMU_CHARGER_ONLINE_SHIFT          2

#endif /* MFD_E31X_PMU_H */
