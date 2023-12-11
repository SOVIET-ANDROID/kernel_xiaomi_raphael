/*
 * Copyright (c) 2013-2018,2020, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_BLUETOOTH_POWER_H
#define __LINUX_BLUETOOTH_POWER_H

/* log index information */
struct log_index {
	int init;
	int crash;
};

/*
 * voltage regulator information required for configuring the
 * bluetooth chipset
 */
struct bt_power_vreg_data {
	/* voltage regulator handle */
	struct regulator *reg;
	/* regulator name */
	const char *name;
	/* voltage levels to be set */
	unsigned int low_vol_level;
	unsigned int high_vol_level;
	/* current level to be set */
	unsigned int load_uA;
	/*
	 * is set voltage supported for this regulator?
	 * false => set voltage is not supported
	 * true  => set voltage is supported
	 *
	 * Some regulators (like gpio-regulators, LVS (low voltage swtiches)
	 * PMIC regulators) dont have the capability to call
	 * regulator_set_voltage or regulator_set_optimum_mode
	 * Use this variable to indicate if its a such regulator or not
	 */
	bool set_voltage_sup;
	/* is this regulator enabled? */
	bool is_enabled;
	/* Index for reg. w.r.t init & crash */
	struct log_index indx;
};

struct bt_power_clk_data {
	/* clock regulator handle */
	struct clk *clk;
	/* clock name */
	const char *name;
	/* is this clock enabled? */
	bool is_enabled;
};

/*
 * Platform data for the bluetooth power driver.
 */
struct bluetooth_power_platform_data {
	/* Bluetooth reset gpio */
	int bt_gpio_sys_rst;
	/* Bluetooth sw_ctrl gpio */
	int bt_gpio_sw_ctrl;
	/* Bluetooth 3p3 gpio */
	int bt_gpio_3p3_en;
	/* Bluetooth 1p3 gpio */
	int bt_gpio_1p3_en;
	struct device *slim_dev;
	/* VDDIO voltage regulator */
	struct bt_power_vreg_data *vreg_info;
	/* bluetooth reference clock */
	struct bt_power_clk_data *bt_chip_clk;
	/* Optional: Bluetooth power setup function */
	int (*bt_power_setup)(int);
};

int bt_register_slimdev(struct device *dev);
int get_chipset_version(void);

#define BT_CMD_SLIM_TEST         0xbfac
#define BT_CMD_PWR_CTRL          0xbfad
#define BT_CMD_CHIPSET_VERS      0xbfae
/* 0xbfaf --> reserved for kernel 5.4 */
#define BT_CMD_CHECK_SW_CTRL     0xbfb0
#define BT_CMD_GETVAL_POWER_SRCS 0xbfb1

/* Total number of power src for logging */
#define BT_POWER_SRC_SIZE        28
#endif /* __LINUX_BLUETOOTH_POWER_H */
