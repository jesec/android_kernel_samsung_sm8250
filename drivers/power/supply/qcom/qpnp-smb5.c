// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/log2.h>
#include <linux/qpnp/qpnp-revid.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/iio/consumer.h>
#include <linux/pmic-voter.h>
#include "smb5-reg.h"
#include "smb5-lib.h"
#include "schgm-flash.h"
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/22, sjc Add for charging */
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/rtc.h>
#include <linux/proc_fs.h>
#include <linux/iio/consumer.h>
#include <linux/kthread.h>

//#include <soc/oppo/boot_mode.h>
//#include <soc/oppo/device_info.h>
#include <soc/oppo/oppo_project.h>

#include "../../oppo/oppo_charger.h"
#include "../../oppo/oppo_gauge.h"
#include "../../oppo/oppo_vooc.h"
#include "../../oppo/oppo_short.h"
#include "../../oppo/charger_ic/oppo_short_ic.h"

#include "../../oppo/oppo_adapter.h"
#include "../../oppo/charger_ic/oppo_bq25882.h"
#include "../../oppo/gauge_ic/oppo_bq27541.h"
#include "../../oppo/charger_ic/op_charge.h"

static struct task_struct *oppo_usbtemp_kthread;
DECLARE_WAIT_QUEUE_HEAD(oppo_usbtemp_wq);
extern struct oppo_chg_chip *g_oppo_chip;
extern 	bool fg_oppo_set_input_current;
bool oppo_ccdetect_check_is_gpio(struct oppo_chg_chip *chip);
int oppo_ccdetect_gpio_init(struct oppo_chg_chip *chip);
void oppo_ccdetect_irq_init(struct oppo_chg_chip *chip);
void oppo_ccdetect_disable(void);
void oppo_ccdetect_enable(void);
int oppo_ccdetect_get_power_role(void);
int qpnp_get_prop_charger_voltage_now(void);
bool oppo_get_otg_switch_status(void);
int oppo_ccdetect_support_check(void);

#define OPPO_SUPPORT_CCDETECT_IN_FTM_MODE	2
#define OPPO_SUPPORT_CCDETECT_NOT_FTM_MODE	1

extern void oppo_set_otg_switch_status_default(bool value); //add for single charge file (move to qpnp-smb5 after merge) to static
int oppo_tbatt_power_off_task_init(struct oppo_chg_chip *chip);
#endif

static struct smb_params smb5_pmi632_params = {
	.fcc			= {
		.name   = "fast charge current",
		.reg    = CHGR_FAST_CHARGE_CURRENT_CFG_REG,
		.min_u  = 0,
		.max_u  = 3000000,
		.step_u = 50000,
	},
	.fv			= {
		.name   = "float voltage",
		.reg    = CHGR_FLOAT_VOLTAGE_CFG_REG,
		.min_u  = 3600000,
		.max_u  = 4800000,
		.step_u = 10000,
	},
	.usb_icl		= {
		.name   = "usb input current limit",
		.reg    = USBIN_CURRENT_LIMIT_CFG_REG,
		.min_u  = 0,
		.max_u  = 3000000,
		.step_u = 50000,
	},
	.icl_max_stat		= {
		.name   = "dcdc icl max status",
		.reg    = ICL_MAX_STATUS_REG,
		.min_u  = 0,
		.max_u  = 3000000,
		.step_u = 50000,
	},
	.icl_stat		= {
		.name   = "input current limit status",
		.reg    = ICL_STATUS_REG,
		.min_u  = 0,
		.max_u  = 3000000,
		.step_u = 50000,
	},
	.otg_cl			= {
		.name	= "usb otg current limit",
		.reg	= DCDC_OTG_CURRENT_LIMIT_CFG_REG,
		.min_u	= 500000,
		.max_u	= 1000000,
		.step_u	= 250000,
	},
	.jeita_cc_comp_hot	= {
		.name	= "jeita fcc reduction",
		.reg	= JEITA_CCCOMP_CFG_HOT_REG,
		.min_u	= 0,
		.max_u	= 1575000,
		.step_u	= 25000,
	},
	.jeita_cc_comp_cold	= {
		.name	= "jeita fcc reduction",
		.reg	= JEITA_CCCOMP_CFG_COLD_REG,
		.min_u	= 0,
		.max_u	= 1575000,
		.step_u	= 25000,
	},
	.freq_switcher		= {
		.name	= "switching frequency",
		.reg	= DCDC_FSW_SEL_REG,
		.min_u	= 600,
		.max_u	= 1200,
		.step_u	= 400,
		.set_proc = smblib_set_chg_freq,
	},
	.aicl_5v_threshold		= {
		.name   = "AICL 5V threshold",
		.reg    = USBIN_5V_AICL_THRESHOLD_REG,
		.min_u  = 4000,
		.max_u  = 4700,
		.step_u = 100,
	},
	.aicl_cont_threshold		= {
		.name   = "AICL CONT threshold",
		.reg    = USBIN_CONT_AICL_THRESHOLD_REG,
		.min_u  = 4000,
		.max_u  = 8800,
		.step_u = 100,
		.get_proc = smblib_get_aicl_cont_threshold,
		.set_proc = smblib_set_aicl_cont_threshold,
	},
};

static struct smb_params smb5_pm8150b_params = {
	.fcc			= {
		.name   = "fast charge current",
		.reg    = CHGR_FAST_CHARGE_CURRENT_CFG_REG,
		.min_u  = 0,
		.max_u  = 8000000,
		.step_u = 50000,
	},
	.fv			= {
		.name   = "float voltage",
		.reg    = CHGR_FLOAT_VOLTAGE_CFG_REG,
		.min_u  = 3600000,
		.max_u  = 4790000,
		.step_u = 10000,
	},
	.usb_icl		= {
		.name   = "usb input current limit",
		.reg    = USBIN_CURRENT_LIMIT_CFG_REG,
		.min_u  = 0,
		.max_u  = 5000000,
		.step_u = 50000,
	},
	.icl_max_stat		= {
		.name   = "dcdc icl max status",
		.reg    = ICL_MAX_STATUS_REG,
		.min_u  = 0,
		.max_u  = 5000000,
		.step_u = 50000,
	},
	.icl_stat		= {
		.name   = "aicl icl status",
		.reg    = AICL_ICL_STATUS_REG,
		.min_u  = 0,
		.max_u  = 5000000,
		.step_u = 50000,
	},
	.otg_cl			= {
		.name	= "usb otg current limit",
		.reg	= DCDC_OTG_CURRENT_LIMIT_CFG_REG,
		.min_u	= 500000,
		.max_u	= 3000000,
		.step_u	= 500000,
	},
	.dc_icl		= {
		.name   = "DC input current limit",
		.reg    = DCDC_CFG_REF_MAX_PSNS_REG,
		.min_u  = 0,
		.max_u  = DCIN_ICL_MAX_UA,
		.step_u = 50000,
	},
	.jeita_cc_comp_hot	= {
		.name	= "jeita fcc reduction",
		.reg	= JEITA_CCCOMP_CFG_HOT_REG,
		.min_u	= 0,
		.max_u	= 8000000,
		.step_u	= 25000,
		.set_proc = NULL,
	},
	.jeita_cc_comp_cold	= {
		.name	= "jeita fcc reduction",
		.reg	= JEITA_CCCOMP_CFG_COLD_REG,
		.min_u	= 0,
		.max_u	= 8000000,
		.step_u	= 25000,
		.set_proc = NULL,
	},
	.freq_switcher		= {
		.name	= "switching frequency",
		.reg	= DCDC_FSW_SEL_REG,
		.min_u	= 600,
		.max_u	= 1200,
		.step_u	= 400,
		.set_proc = smblib_set_chg_freq,
	},
	.aicl_5v_threshold		= {
		.name   = "AICL 5V threshold",
		.reg    = USBIN_5V_AICL_THRESHOLD_REG,
		.min_u  = 4000,
		.max_u  = 4700,
		.step_u = 100,
	},
	.aicl_cont_threshold		= {
		.name   = "AICL CONT threshold",
		.reg    = USBIN_CONT_AICL_THRESHOLD_REG,
		.min_u  = 4000,
		.max_u  = 11800,
		.step_u = 100,
		.get_proc = smblib_get_aicl_cont_threshold,
		.set_proc = smblib_set_aicl_cont_threshold,
	},
};
#ifndef VENDOR_EDIT
struct smb_dt_props {
	int			usb_icl_ua;
	struct device_node	*revid_dev_node;
	enum float_options	float_option;
	int			chg_inhibit_thr_mv;
	bool			no_battery;
	bool			hvdcp_disable;
	bool			hvdcp_autonomous;
	bool			adc_based_aicl;
	int			sec_charger_config;
	int			auto_recharge_soc;
	int			auto_recharge_vbat_mv;
	int			wd_bark_time;
	int			wd_snarl_time_cfg;
	int			batt_profile_fcc_ua;
	int			batt_profile_fv_uv;
	int			term_current_src;
	int			term_current_thresh_hi_ma;
	int			term_current_thresh_lo_ma;
	int			disable_suspend_on_collapse;
};

struct smb5 {
	struct smb_charger	chg;
	struct dentry		*dfs_root;
	struct smb_dt_props	dt;
};
#endif
#ifdef VENDOR_EDIT

#ifdef VENDOR_EDIT
/* Cong.Dai@BSP.TP.Init, 2018/04/08, Add for notify touchpanel status */
bool __attribute__((weak)) boot_with_console(void) {return false;}
#endif

static int oppo_chg_2uart_pinctrl_init(struct oppo_chg_chip *chip)
{
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return -EINVAL;
	}

	if (boot_with_console() == true ) {
		return 0;
	}

	chg = &chip->pmic_spmi.smb5_chip->chg;

	chg->chg_2uart_pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chg->chg_2uart_pinctrl)) {
		chg_err("get 2uart chg_2uart_pinctrl fail\n");
		return -EINVAL;
	}

	chg->chg_2uart_default = pinctrl_lookup_state(chg->chg_2uart_pinctrl, "chg_qupv3_se2_2uart_default");
	if (IS_ERR_OR_NULL(chg->chg_2uart_default)) {
		chg_err("get chg_2uart_default fail\n");
		return -EINVAL;
	}

	chg->chg_2uart_sleep = pinctrl_lookup_state(chg->chg_2uart_pinctrl, "chg_qupv3_se2_2uart_sleep");
	if (IS_ERR_OR_NULL(chg->chg_2uart_sleep)) {
		chg_err("get chg_2uart_sleep fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chg->chg_2uart_pinctrl, chg->chg_2uart_default);

	return 0;
}

static int oppo_chg_set_2uart_pinctrl_chgID(struct oppo_chg_chip *chip)
{
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return -EINVAL;
	}

	if (boot_with_console() == true) {
		return 0;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (IS_ERR_OR_NULL(chg->chg_2uart_pinctrl) || IS_ERR_OR_NULL(chg->chg_2uart_sleep)) {
		chg_err("get 2uart chg_2uart_pinctrl fail\n");
		return -EINVAL;
	}
	mutex_lock(&chg->pinctrl_mutex);
	pinctrl_select_state(chg->chg_2uart_pinctrl, chg->chg_2uart_sleep);
	mutex_unlock(&chg->pinctrl_mutex);
	return 0;
}

static int oppo_chg_set_2uart_pinctrl_default(struct oppo_chg_chip *chip)
{
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return -EINVAL;
	}

	if (boot_with_console() == true) {
		return 0;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (IS_ERR_OR_NULL(chg->chg_2uart_pinctrl) || IS_ERR_OR_NULL(chg->chg_2uart_default)) {
		chg_err("get 2uart chg_2uart_pinctrl fail\n");
		return -EINVAL;
	}
	mutex_lock(&chg->pinctrl_mutex);
	pinctrl_select_state(chg->chg_2uart_pinctrl, chg->chg_2uart_default);	
	mutex_unlock(&chg->pinctrl_mutex);
	return 0;
}

int smbchg_get_chargerid_volt(void)
{
	int rc, chargerid_volt = 0;
	struct oppo_chg_chip *chip = g_oppo_chip;
	struct smb_charger *chg = NULL;

    if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return 0;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (IS_ERR_OR_NULL(chg->iio.chgid_v_chan)) {
		printk(KERN_ERR "[OPPO_CHG][%s]: chg->iio.chgid_v_chan  is  NULL !\n", __func__);
		return 0;
	}

	oppo_chg_set_2uart_pinctrl_chgID(chip);
       msleep(10);
       
	rc = iio_read_channel_processed(chg->iio.chgid_v_chan, &chargerid_volt);
	if (rc < 0) {
		chg_err("[OPPO_CHG][%s]: iio_read_channel_processed  get error rc=%d\n", __func__, rc);
		return 0;
	}

	chargerid_volt = chargerid_volt / 1000;
	chg_err("chargerid_volt: %d\n", chargerid_volt);

   	oppo_chg_set_2uart_pinctrl_default(chip);
    
	return chargerid_volt;
}


static int smbchg_chargerid_switch_gpio_init(struct oppo_chg_chip *chip)
{
	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
		chg_err("get normalchg_gpio.pinctrl fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.chargerid_switch_active =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "chargerid_switch_active");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_active)) {
		chg_err("get chargerid_switch_active fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.chargerid_switch_sleep =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "chargerid_switch_sleep");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_sleep)) {
		chg_err("get chargerid_switch_sleep fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.chargerid_switch_default =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "chargerid_switch_default");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_default)) {
		chg_err("get chargerid_switch_default fail\n");
		return -EINVAL;
	}

	if (chip->normalchg_gpio.chargerid_switch_gpio > 0) {
		gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 0);
	}
	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.chargerid_switch_default);

	return 0;
}

void smbchg_set_chargerid_switch_val(int value)
{
	struct oppo_chg_chip *chip = g_oppo_chip;
	
	if (chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
		chg_err("chargerid_switch_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)
		|| IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_active)
		|| IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_sleep)
		|| IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_default)) {
		chg_err("pinctrl null, return\n");
		return;
	}

	if (oppo_vooc_get_adapter_update_real_status() == ADAPTER_FW_NEED_UPDATE
		|| oppo_vooc_get_btb_temp_over() == true) {
		chg_err("adapter update or btb_temp_over, return\n");
		return;
	}
#if 0
	if (chip->pmic_spmi.not_support_1200ma && !value && !is_usb_present(chip)) {
	/* BugID 879716 : Solve some situatuion ChargerID is not 0 mV when usb is not present */
	// wenbin.liu@BSP.CHG.Basic, 2016/11/14
		chip->chargerid_volt = 0;
		chip->chargerid_volt_got = false;
	}
#endif
	mutex_lock(&chip->pmic_spmi.smb5_chip->chg.pinctrl_mutex);

	if (value) {
		gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 1);
		pinctrl_select_state(chip->normalchg_gpio.pinctrl,
				chip->normalchg_gpio.chargerid_switch_default);
	} else {
		gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 0);
		pinctrl_select_state(chip->normalchg_gpio.pinctrl,
				chip->normalchg_gpio.chargerid_switch_default);
	}

	mutex_unlock(&chip->pmic_spmi.smb5_chip->chg.pinctrl_mutex);
	chg_err("set value:%d, gpio_val:%d\n",
		value, gpio_get_value(chip->normalchg_gpio.chargerid_switch_gpio));
}

int smbchg_get_chargerid_switch_val(void)
{
	struct oppo_chg_chip *chip = g_oppo_chip;
	
	if (chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
		chg_err("chargerid_switch_gpio not exist, return\n");
		return -1;
	}

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)
		|| IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_active)
		|| IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_sleep)) {
		chg_err("pinctrl null, return\n");
		return -1;
	}

	return gpio_get_value(chip->normalchg_gpio.chargerid_switch_gpio);
}
#endif
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/03/02, sjc Add for using gpio as shipmode stm6620 */
static int oppo_ship_gpio_init(struct oppo_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb2_chg not ready!\n", __func__);
		return -EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
		chg_err("get normalchg_gpio.pinctrl fail\n");
	}

	chip->normalchg_gpio.ship_active =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "ship_active");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ship_active)) {
		chg_err("get ship_active fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.ship_sleep =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "ship_sleep");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ship_sleep)) {
		chg_err("get ship_sleep fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_sleep);

	return 0;
}

static bool oppo_ship_check_is_gpio(struct oppo_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb2_chg not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.ship_gpio))
		return true;

	return false;
}

#define PWM_COUNT	5
static void smbchg_enter_shipmode(struct oppo_chg_chip *chip)
{
	int i = 0;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb2_chg not ready!\n", __func__);
		return;
	}

	if (oppo_ship_check_is_gpio(chip) == true) {
		chg_debug("select gpio control\n");

		mutex_lock(&chip->pmic_spmi.smb5_chip->chg.pinctrl_mutex);
		if (!IS_ERR_OR_NULL(chip->normalchg_gpio.ship_sleep)) {
			pinctrl_select_state(chip->normalchg_gpio.pinctrl,
				chip->normalchg_gpio.ship_sleep);
		}
		for (i = 0; i < PWM_COUNT; i++) {
			//gpio_direction_output(chip->normalchg_gpio.ship_gpio, 1);
			pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_active);
			mdelay(3);
			//gpio_direction_output(chip->normalchg_gpio.ship_gpio, 0);
			pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_sleep);
			mdelay(3);
		}

		mutex_unlock(&chip->pmic_spmi.smb5_chip->chg.pinctrl_mutex);
		chg_debug("power off after 15s\n");
	}
}
#endif /* VENDOR_EDIT */
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/03/02, sjc Add for HW shortc */
static int oppo_shortc_gpio_init(struct oppo_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return -EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
		chg_err("get normalchg_gpio.pinctrl fail\n");
	}
	chip->normalchg_gpio.shortc_active =
		pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "shortc_active");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.shortc_active)) {
		chg_err("get shortc_active fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.shortc_active);


	return 0;
}

static bool oppo_shortc_check_is_gpio(struct oppo_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.shortc_gpio))
		return true;

	return false;
}

static int oppo_shipmode_id_gpio_init(struct oppo_chg_chip *chip)
{
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return -EINVAL;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	chg->shipmode_id_pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chg->shipmode_id_pinctrl)) {
		chg_err("get shipmode_id_pinctrl fail\n");
	}		

	chg->shipmode_id_active =
		pinctrl_lookup_state(chg->shipmode_id_pinctrl, "shipmode_id_active");
	if (IS_ERR_OR_NULL(chg->shipmode_id_active)) {
		chg_err("get shipmode_id_active fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chg->shipmode_id_pinctrl, chg->shipmode_id_active);

	return 0;
}


static bool oppo_shipmode_id_check_is_gpio(struct oppo_chg_chip *chip)
{
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return -EINVAL;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (gpio_is_valid(chg->shipmode_id_gpio)) {
		printk(KERN_ERR "[OPPO_CHG][%s]: tongfeng test shipmode_id_gpio true!\n", __func__);
		return true;
	}

	return false;
}

#ifdef CONFIG_OPPO_SHORT_HW_CHECK
static bool oppo_chg_get_shortc_hw_gpio_status(void)
{
	bool shortc_hw_status = 1;
	struct oppo_chg_chip *chip = g_oppo_chip;
	
	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return shortc_hw_status;
	}

	if (oppo_shortc_check_is_gpio(chip) == true) {
		shortc_hw_status = !!(gpio_get_value(chip->normalchg_gpio.shortc_gpio));
	}
	return shortc_hw_status;
}
#else
static bool oppo_chg_get_shortc_hw_gpio_status(void)
{
	bool shortc_hw_status = 1;

	return shortc_hw_status;
}
#endif /* CONFIG_OPPO_SHORT_HW_CHECK */
#endif /* VENDOR_EDIT */
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/01/30, sjc Add for using gpio as CC detect */

int oppo_usbtemp_adc_gpio_init(struct oppo_chg_chip *chip)
{
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return -EINVAL;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	chg->usbtemp_gpio1_adc_pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chg->usbtemp_gpio1_adc_pinctrl)) {
		chg_err("get usbtemp_gpio1_adc_pinctrl fail\n");
		return -EINVAL;
	}

	chg->usbtemp_gpio1_default = pinctrl_lookup_state(chg->usbtemp_gpio1_adc_pinctrl, "gpio1_adc_default");
	if (IS_ERR_OR_NULL(chg->usbtemp_gpio1_default)) {
		chg_err("get usbtemp_gpio1_default fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chg->usbtemp_gpio1_adc_pinctrl, chg->usbtemp_gpio1_default);

	return 0;
}


int oppo_ccdetect_gpio_init(struct oppo_chg_chip *chip)
{
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return -EINVAL;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	chg->ccdetect_pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chg->ccdetect_pinctrl)) {
		chg_err("get ccdetect ccdetect_pinctrl fail\n");
		return -EINVAL;
	}

	chg->ccdetect_active = pinctrl_lookup_state(chg->ccdetect_pinctrl, "ccdetect_active");
	if (IS_ERR_OR_NULL(chg->ccdetect_active)) {
		chg_err("get ccdetect_active fail\n");
		return -EINVAL;
	}

	chg->ccdetect_sleep = pinctrl_lookup_state(chg->ccdetect_pinctrl, "ccdetect_sleep");
	if (IS_ERR_OR_NULL(chg->ccdetect_sleep)) {
		chg_err("get ccdetect_sleep fail\n");
		return -EINVAL;
	}

	if (chg->ccdetect_gpio > 0) {
		gpio_direction_input(chg->ccdetect_gpio);
	}

	pinctrl_select_state(chg->ccdetect_pinctrl, chg->ccdetect_active);

	return 0;
}

void oppo_ccdetect_irq_init(struct oppo_chg_chip *chip)
{
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	chg->ccdetect_irq = gpio_to_irq(chg->ccdetect_gpio);
    printk(KERN_ERR "[OPPO_CHG][%s]: chg->ccdetect_irq[%d]!\n", __func__, chg->ccdetect_irq);
}

void oppo_ccdetect_enable(void)
{
	int rc;
	u8 stat;
	struct smb_charger *chg = NULL;
	struct oppo_chg_chip *chip = g_oppo_chip;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb2_chg not ready!\n", __func__);
		return;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (oppo_ccdetect_check_is_gpio(chip) != true)
		return;

	rc = smblib_read(chg, TYPE_C_MODE_CFG_REG, &stat);
	if (rc < 0) {
		printk(KERN_ERR "[OPPO_CHG][%s]: 111 Couldn't read 0x1368 rc=%d\n", __func__, rc);
	} else {
		printk(KERN_ERR "[OPPO_CHG][%s]:111 reg0x1368[0x%x], bit[2:0]=0(DRP)\n", __func__, stat);
	}

	/* set DRP mode */
	rc = smblib_masked_write(chg, TYPE_C_MODE_CFG_REG,
			TYPEC_POWER_ROLE_CMD_MASK, 0x0);//bit[2:0]=0
	if (rc < 0) {
		printk(KERN_ERR "[OPPO_CHG][%s]: Couldn't clear 0x1368[0] rc=%d\n", __func__, rc);
	}

	rc = smblib_read(chg, TYPE_C_MODE_CFG_REG, &stat);
	if (rc < 0) {
		printk(KERN_ERR "[OPPO_CHG][%s]: Couldn't read 0x1368 rc=%d\n", __func__, rc);
	} else {
		printk(KERN_ERR "[OPPO_CHG][%s]: reg0x1368[0x%x], bit[2:0]=0(DRP)\n", __func__, stat);
	}
}

void oppo_ccdetect_disable(void)
{    
	int rc;
	u8 stat;
	struct smb_charger *chg = NULL;
	struct oppo_chg_chip *chip = g_oppo_chip;

	//return;
	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb2_chg not ready!\n", __func__);
		return;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (oppo_ccdetect_check_is_gpio(chip) != true)
		return;

#ifdef VENDOR_EDIT  /* zhangkun@BSP.CHG.Basic, 2019/02/28, Add for disable CC detect*/
	/* set sink mode only */
	rc = smblib_masked_write(chg, TYPE_C_MODE_CFG_REG,
			TYPEC_POWER_ROLE_CMD_MASK | TYPEC_TRY_MODE_MASK, EN_SNK_ONLY_BIT);//bit[4:0]=0x02
#endif
	if (rc < 0) {
		printk(KERN_ERR "[OPPO_CHG][%s]: Couldn't set 0x1368[2] rc=%d\n", __func__, rc);
	}

	rc = smblib_read(chg, TYPE_C_MODE_CFG_REG, &stat);
	if (rc < 0) {
		printk(KERN_ERR "[OPPO_CHG][%s]: Couldn't read 0x1368 rc=%d\n", __func__, rc);
	} else {
		printk(KERN_ERR "[OPPO_CHG][%s]: reg0x1368[0x%x], bit[2:0]=4(UFP)\n", __func__, stat);
	}
}

int oppo_ccdetect_get_power_role(void)
{
	int rc;
	struct smb_charger *chg = NULL;
	union power_supply_propval val = {0,};

	if (!g_oppo_chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return POWER_SUPPLY_TYPEC_PR_NONE;
	}
	chg = &g_oppo_chip->pmic_spmi.smb5_chip->chg;

	rc = smblib_get_prop_typec_power_role(chg, &val);
	if (rc < 0) {
		printk(KERN_ERR "[OPPO_CHG][%s]: Couldn't get typec power role, rc=%d\n", __func__, rc);
		return POWER_SUPPLY_TYPEC_PR_DUAL;
	}
	return val.intval;
}

bool oppo_ccdetect_check_is_gpio(struct oppo_chg_chip *chip)
{
	struct smb_charger *chg = NULL;
	int boot_mode = get_boot_mode();

    if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return false;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	/* HW engineer requirement */
	if (boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN
			|| boot_mode == MSM_BOOT_MODE__FACTORY)
		return false;

	if (gpio_is_valid(chg->ccdetect_gpio))
		return true;

	return false;
}
int oppo_ccdetect_support_check(void)
{
	struct smb_charger *chg = NULL;
	struct oppo_chg_chip *chip = g_oppo_chip;
	int boot_mode = get_boot_mode();

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: g_oppo_chip not ready!\n", __func__);
		return OPPO_NOT_SUPPORT_CCDETECT;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;
	if (boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN
			|| boot_mode == MSM_BOOT_MODE__FACTORY) {
			return OPPO_SUPPORT_CCDETECT_IN_FTM_MODE;
	}
	if (gpio_is_valid(chg->ccdetect_gpio))
		return OPPO_SUPPORT_CCDETECT_NOT_FTM_MODE;

	return OPPO_NOT_SUPPORT_CCDETECT;
}
EXPORT_SYMBOL(oppo_ccdetect_support_check);
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/01/30, sjc Add for using gpio as CC detect */
#define CCDETECT_DELAY_MS	50
irqreturn_t oppo_ccdetect_change_handler(int irq, void *data)
{
	struct oppo_chg_chip *chip = data;
	struct smb_charger *chg = &chip->pmic_spmi.smb5_chip->chg;

	cancel_delayed_work_sync(&chg->ccdetect_work);
	vote(chg->awake_votable, CCDETECT_VOTER, true, 0);
	//smblib_dbg(chg, PR_INTERRUPT, "Scheduling ccdetect work\n");
    printk(KERN_ERR "[OPPO_CHG][%s]: Scheduling ccdetect work!\n", __func__);
	schedule_delayed_work(&chg->ccdetect_work,
			msecs_to_jiffies(CCDETECT_DELAY_MS));
	return IRQ_HANDLED;
}
#endif /* VENDOR_EDIT */

static void oppo_ccdetect_irq_register(struct oppo_chg_chip *chip)
{
	int ret = 0;
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	ret = devm_request_threaded_irq(chip->dev, chg->ccdetect_irq,
			NULL, oppo_ccdetect_change_handler, IRQF_TRIGGER_FALLING
			| IRQF_TRIGGER_RISING | IRQF_ONESHOT, "ccdetect-change", chip);
	if (ret < 0) {
		chg_err("Unable to request ccdetect-change irq: %d\n", ret);
	}
    printk(KERN_ERR "%s: !!!!! irq register\n", __FUNCTION__);

	ret = enable_irq_wake(chg->ccdetect_irq);
	if (ret != 0) {
		chg_err("enable_irq_wake: ccdetect_irq failed %d\n", ret);
	}
}
#endif /* VENDOR_EDIT */
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/05/25, sjc Add for usbtemp */
static bool oppo_usbtemp_check_is_gpio(struct oppo_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.dischg_gpio))
		return true;

	return false;
}

bool oppo_usbtemp_check_is_support(void)
{
	if(oppo_usbtemp_check_is_gpio(g_oppo_chip) == true)
		return true;
	
	chg_err("dischg return false\n");

	return false;
}

#define USBTEMP_DEFAULT_VOLT_VALUE_MV 950

#define USB_40C	40
#define USB_50C	50
#define USB_55C	53
#define USB_57C	57
#define USB_100C	100
#define VBUS_VOLT_THRESHOLD	400
#define USB_VBUS_SHORT_DISABLE_VOLT		509
#define USB_VBUS_SHORT_ENABLE_VOLT		392
#define MIN_MONITOR_INTERVAL	50//50ms
#define MAX_MONITOR_INTERVAL	50//50ms
#define RETRY_CNT_DELAY         5 //ms
#define VBUS_MONITOR_INTERVAL	3000//3s
#define HIGH_TEMP_SHORT_CHECK_TIMEOUT 1000 /*ms*/
static  int usbtemp_error_r = 0;

static int usbtemp_debug = 0;
module_param_named(usbtemp_debug, usbtemp_debug, int, 0600);

static void oppo_get_usbtemp_volt(struct oppo_chg_chip *chip)
{
	int rc, usbtemp_volt = 0;
	struct smb_charger *chg = NULL;

    if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return ;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (IS_ERR_OR_NULL(chg->iio.usbtemp_v_chan_r)) {
		printk(KERN_ERR "[OPPO_CHG][%s]: chg->iio.usbtemp_v_chan_r  is  NULL !\n", __func__);
		return ;
	}

	rc = iio_read_channel_processed(chg->iio.usbtemp_v_chan_r, &usbtemp_volt);
	if (rc < 0) {
		chg_err("[OPPO_CHG][%s]: usbtemp_v_chan_r  get error\n", __func__);
		return ;
	}

	chip->usbtemp_volt_r = usbtemp_volt / 1000;
	if (chip->usbtemp_volt_r > USBTEMP_DEFAULT_VOLT_VALUE_MV) {
		chip->usbtemp_volt_r = USBTEMP_DEFAULT_VOLT_VALUE_MV;
	}

	if (IS_ERR_OR_NULL(chg->iio.usbtemp_v_chan_l)) {
        chip->usbtemp_volt_l = chip->usbtemp_volt_r;
        if(usbtemp_debug == 1){
		printk(KERN_ERR "[OPPO_CHG][%s]: chg->iio.usbtemp_v_chan_l  is  NULL !\n", __func__);
		chg_err("chip->usbtemp_volt_r: %d,chip->usbtemp_volt_l: %d\n", chip->usbtemp_volt_r,chip->usbtemp_volt_l);
        }
        usbtemp_error_r = 1;
		return ;
	}

	rc = iio_read_channel_processed(chg->iio.usbtemp_v_chan_l, &usbtemp_volt);
	if (rc < 0) {
        if(usbtemp_debug == 1)
		chg_err("[OPPO_CHG][%s]: usbtemp_v_chan_l  get error\n", __func__);
        chip->usbtemp_volt_l = chip->usbtemp_volt_r;
        usbtemp_error_r = 1;
		return ;
	}

	chip->usbtemp_volt_l = usbtemp_volt / 1000;
	if (chip->usbtemp_volt_l > USBTEMP_DEFAULT_VOLT_VALUE_MV) {
		chip->usbtemp_volt_l = USBTEMP_DEFAULT_VOLT_VALUE_MV;
	}
	if(usbtemp_debug == 1)
		chg_err("chip->usbtemp_volt_r: %d,chip->usbtemp_volt_l: %d\n", chip->usbtemp_volt_r,chip->usbtemp_volt_l);

	return ;
}

static int oppo_dischg_gpio_init(struct oppo_chg_chip *chip)
{
	if (!chip) {
		chg_err("chip NULL\n");
		return EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
		chg_err("get dischg_pinctrl fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.dischg_enable = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "dischg_enable");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable)) {
		chg_err("get dischg_enable fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.dischg_disable = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "dischg_disable");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_disable)) {
		chg_err("get dischg_disable fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_disable);

	return 0;
}

static int usb_status = 0;
void oppo_set_usb_status(int status)
{
	usb_status = usb_status | status;
}

void oppo_clear_usb_status(int status)
{
	usb_status = usb_status & (~status);
}

int oppo_get_usb_status(void)
{
	return usb_status;
}

#if 0
static int oppo_get_usb_status(void)
{
	return usb_status;
}
#endif

//#define USB_50C_VOLT	467//450
//#define USB_55C_VOLT	400//384
//#define USB_60C_VOLT	342//327
//#define VBUS_VOLT_THRESHOLD	400
//#define MIN_MONITOR_INTERVAL	50//50ms
//#define MAX_MONITOR_INTERVAL	200//200ms
//#define VBUS_MONITOR_INTERVAL	3000//3s

static void  get_usb_temp(struct oppo_chg_chip *chg)
{
	int i = 0;

	for (i = ARRAY_SIZE(con_volt_7250) - 1; i >= 0; i--) {
		if (con_volt_7250[i] >= chg->usbtemp_volt_r)
			break;
		else if (i == 0)
			break;
	}
    chg->usb_temp_r = con_temp_7250[i]+1;

	if (usbtemp_error_r != 0) {
        chg->usb_temp_l = chg->usb_temp_r;
        if(usbtemp_debug == 1){
		printk(KERN_ERR "[OPPO_CHG][%s]: chg->usb_temp_l  is  error !\n", __func__);
		chg_err("chg->usbtemp_volt_r:%d,chg->usbtemp_volt_l:%d\n",chg->usb_temp_r, chg->usb_temp_l);
        }
		return ;
	}

	for (i = ARRAY_SIZE(con_volt_7250) - 1; i >= 0; i--) {
		if (con_volt_7250[i] >= chg->usbtemp_volt_l)
			break;
		else if (i == 0)
			break;
	}
	chg->usb_temp_l = con_temp_7250[i]+1;

	if(usbtemp_debug == 1)
		chg_err("chg->usb_temp_r:%d,chg->usb_temp_l:%d\n",chg->usb_temp_r, chg->usb_temp_l);

	return ;
}

static bool oppo_usbtemp_condition(void)
{
	int rc;
	bool vbus_rising;
	u8 stat;
	struct smb_charger *chg = NULL;

	if (!g_oppo_chip) {
		chg_err("fail to init oppo_chip\n");
		return false;
	}

	chg = &g_oppo_chip->pmic_spmi.smb5_chip->chg;

	if (chg->typec_mode >= POWER_SUPPLY_TYPEC_SINK && chg->typec_mode <= POWER_SUPPLY_TYPEC_POWERED_CABLE_ONLY)
		return true;

	rc = smblib_read(chg, USBIN_BASE + INT_RT_STS_OFFSET, &stat);
	if (rc < 0) {
		chg_err("fail to read pmic register, rc = %d\n", rc);
		return false;
	}

	vbus_rising = (bool)(stat & USBIN_PLUGIN_RT_STS_BIT);
	if (vbus_rising == true)
		return true;

	return false;
}

static int oppo_usbtemp_monitor_main(void *data)
{
	int delay = 0;
	//int usbtemp_volt = 0;
	int vbus_volt = 0;
	static bool dischg_flag = false;
	static int total_count = 0;
	int retry_cnt = 3, i = 0;
	int count_r = 1, count_l = 1;
	static int last_usb_temp_r = 25;
	static int current_temp_r = 25;
	static int last_usb_temp_l = 25;
	static int current_temp_l = 25;
	static int count = 0;
	int rc = 0;
	int level = 1;
	int print_temp = 0;

	struct smb_charger *chg = NULL;
	struct oppo_chg_chip *chip = g_oppo_chip;

	chg = &chip->pmic_spmi.smb5_chip->chg;

	while (!kthread_should_stop()) {
		if (oppo_get_chg_powersave() == true) {
			delay = 1000 * 50;//30S
			goto check_again;
		}
		oppo_get_usbtemp_volt(chip);
		get_usb_temp(chip);
		#ifdef VENDOR_EDIT/* OuYangBaiLi@BSP.CHG.Basic,charging_bsp 2019/11/07,Add for charging_bsp */
		if(chip->usb_temp_l > chip->usb_temp_r){
			if(print_temp != chip->usb_temp_l)
				chg_err("usb_tem_change usb_temp_r:%d,usb_temp_l:%d\n",chip->usb_temp_r, chip->usb_temp_l);
			print_temp = chip->usb_temp_l;
		}else {
			if(print_temp != chip->usb_temp_r)
				chg_err("usb_tem_change usb_temp_r:%d,usb_temp_l:%d\n",chip->usb_temp_r, chip->usb_temp_l);
			print_temp = chip->usb_temp_r;
		}
		//if(print_temp != chip->usb_temp_r)
			//chg_err("usb_tem_change usb_temp_r:%d,usb_temp_l:%d\n",chip->usb_temp_r, chip->usb_temp_l);
		print_temp = chip->usb_temp_r;
		#endif /* VENDOR_EDIT */
		if ((chip->usb_temp_r < USB_50C)&&(chip->usb_temp_l < USB_50C))//get vbus when usbtemp < 50C
			vbus_volt = qpnp_get_prop_charger_voltage_now();
		else
			vbus_volt = 0;

		if ((chip->usb_temp_r < USB_40C)&&(chip->usb_temp_l < USB_40C)) {
			delay = MAX_MONITOR_INTERVAL;
			total_count = 10;
		} else {
			delay = MIN_MONITOR_INTERVAL;
			total_count = 30;
		}

		if (((chip->usb_temp_r < USB_50C)&&(chip->usb_temp_l < USB_50C)) && vbus_volt < VBUS_VOLT_THRESHOLD)
			delay = VBUS_MONITOR_INTERVAL;
		if (oppo_usbtemp_condition() == false) {
			if (dischg_flag == true && oppo_ccdetect_check_is_gpio(chip) == true) {
				level = gpio_get_value(chg->ccdetect_gpio);
				if (!IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_disable) && level == 1) {
					dischg_flag = false;
					chg_err("dischg disable...[%d]\n", chip->usbtemp_volt);
					oppo_clear_usb_status(USB_TEMP_HIGH);
					mutex_lock(&chg->pinctrl_mutex);
					pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_disable);
					mutex_unlock(&chg->pinctrl_mutex);
				}
			}
			count = 0;
			wait_event_interruptible(oppo_usbtemp_wq, (oppo_usbtemp_condition() == true));
		}else{
			if ((USB_57C <= chip->usb_temp_r && chip->usb_temp_r < USB_100C)
					|| (USB_57C <= chip->usb_temp_l && chip->usb_temp_l < USB_100C)) {
				if (dischg_flag == false) {
					for (i = 1; i < retry_cnt; i++) {
						mdelay(RETRY_CNT_DELAY);
						oppo_get_usbtemp_volt(chip);
						get_usb_temp(chip);
						if (chip->usb_temp_r >= USB_57C)
							count_r++;
						if (chip->usb_temp_l >= USB_57C)
							count_l++;
					}
					if (count_r >= retry_cnt || count_l >= retry_cnt) {
						if (!IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable) && !(get_project() == 19101 && strcmp("T0",get_PCB_Version()) == 0)) {
							dischg_flag = true;
							chg_err("dischg enable1...[%d]\n", chip->usbtemp_volt);
#ifndef CONFIG_HIGH_TEMP_VERSION
							rc = smblib_masked_write(chg, TYPE_C_MODE_CFG_REG,
									TYPEC_POWER_ROLE_CMD_MASK | TYPEC_TRY_MODE_MASK, EN_SNK_ONLY_BIT);//bit[2:0]=0x4
							if (rc < 0) {
								chg_err("[OPPO_CHG][%s]: Couldn't set 0x1544[2] rc=%d\n", __func__, rc);
							}
							oppo_set_usb_status(USB_TEMP_HIGH);
							if (oppo_vooc_get_fastchg_started() == true) {
								oppo_chg_set_chargerid_switch_val(0);
								oppo_vooc_switch_mode(NORMAL_CHARGER_MODE);
								oppo_vooc_reset_mcu();
							}
							chip->chg_ops->charger_suspend();
							usleep_range(20000,20000);
#endif
							mutex_lock(&chg->pinctrl_mutex);
#ifdef CONFIG_HIGH_TEMP_VERSION
							chg_err(" CONFIG_HIGH_TEMP_VERSION enable here,do not set vbus down \n");
							rc = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_disable);
#else
							chg_err(" CONFIG_HIGH_TEMP_VERSION disabled \n");
							rc = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_enable);
#endif
							mutex_unlock(&chg->pinctrl_mutex);
						}
					}
					count_r = 1;
					count_l = 1;
				}
				count = 0;
			} else if (((chip->usb_temp_r - chip->temperature/10) > 10 && chip->usb_temp_r < USB_100C) 
					|| ((chip->usb_temp_l - chip->temperature/10) > 10 && chip->usb_temp_l < USB_100C)){
				if (dischg_flag == false) {
					if (count <= total_count) {
						if (count == 0) {
							last_usb_temp_r = chip->usb_temp_r;
							last_usb_temp_l = chip->usb_temp_l;
						} else {
							current_temp_r = chip->usb_temp_r;
							current_temp_l = chip->usb_temp_l;
						} 
						if ((current_temp_r - last_usb_temp_r) >= 3 || (current_temp_l - last_usb_temp_l) >= 3) {
							for (i = 1; i < retry_cnt; i++) {
								mdelay(RETRY_CNT_DELAY);
								oppo_get_usbtemp_volt(chip);
								get_usb_temp(chip);
								if ((chip->usb_temp_r - last_usb_temp_r) >= 3)
									count_r++;
								if ((chip->usb_temp_l - last_usb_temp_l) >= 3)
									count_l++;
							}
							current_temp_r = chip->usb_temp_r;
							current_temp_l = chip->usb_temp_l;
							if (count_r >= retry_cnt || count_l >= retry_cnt) {
								if (!IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable)&& !(get_project() == 19101 && strcmp("T0",get_PCB_Version()) == 0)) {
									dischg_flag = true;
									chg_err("dischg enable1...[%d]\n", chip->usbtemp_volt);
#ifndef CONFIG_HIGH_TEMP_VERSION
									rc = smblib_masked_write(chg, TYPE_C_MODE_CFG_REG,
											TYPEC_POWER_ROLE_CMD_MASK | TYPEC_TRY_MODE_MASK, EN_SNK_ONLY_BIT);//bit[2:0]=0x4
									if (rc < 0) {
										chg_err("[OPPO_CHG][%s]: Couldn't set 0x1544[2] rc=%d\n", __func__, rc);
									}
									oppo_set_usb_status(USB_TEMP_HIGH);
									if (oppo_vooc_get_fastchg_started() == true) {
										oppo_chg_set_chargerid_switch_val(0);
										oppo_vooc_switch_mode(NORMAL_CHARGER_MODE);
										oppo_vooc_reset_mcu();
									}
									chip->chg_ops->charger_suspend();
									usleep_range(20000,20000);
#endif
									mutex_lock(&chg->pinctrl_mutex);
#ifdef CONFIG_HIGH_TEMP_VERSION
									chg_err(" CONFIG_HIGH_TEMP_VERSION enable here,do not set vbus down \n");
									rc = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_disable);
#else
									chg_err(" CONFIG_HIGH_TEMP_VERSION disabled \n");
									rc = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_enable);
#endif
									mutex_unlock(&chg->pinctrl_mutex);
								}
							}
							count_r = 1;
							count_l = 1;
						}
						count++;
						if (count > total_count) {
							count = 0;
						}
					}
				}
				//msleep(delay);
			}
check_again:
			msleep(delay);
		}
		//chg_err("==================usbtemp_volt[%d], chip->usb_temp[%d]\n", chip->usbtemp_volt,chip->usb_temp);
	}


	return 0;
}

static void oppo_usbtemp_thread_init(void)
{
	oppo_usbtemp_kthread =
			kthread_run(oppo_usbtemp_monitor_main, 0, "usbtemp_kthread");
	if (IS_ERR(oppo_usbtemp_kthread)) {
		chg_err("failed to cread oppo_usbtemp_kthread\n");
	}
}
void oppo_wake_up_usbtemp_thread(void)
{
	if (oppo_usbtemp_check_is_support() == true){
		wake_up_interruptible(&oppo_usbtemp_wq);
	}
}

static int oppo_chg_parse_custom_dt(struct oppo_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = chip->dev->of_node;
	struct smb_charger *chg = &chip->pmic_spmi.smb5_chip->chg;
	if (!node) {
			pr_err("device tree node missing\n");
			return -EINVAL;
	}
#ifdef VENDOR_EDIT
	/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/22, sjc Add for charging*/
	if (g_oppo_chip) {
		g_oppo_chip->normalchg_gpio.chargerid_switch_gpio =
				of_get_named_gpio(node, "qcom,chargerid_switch-gpio", 0);
		if (g_oppo_chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
			chg_err("Couldn't read chargerid_switch-gpio rc = %d, chargerid_switch_gpio:%d\n",
					rc, g_oppo_chip->normalchg_gpio.chargerid_switch_gpio);
		} else {
			if (gpio_is_valid(g_oppo_chip->normalchg_gpio.chargerid_switch_gpio)) {
				rc = gpio_request(g_oppo_chip->normalchg_gpio.chargerid_switch_gpio, "charging-switch1-gpio");
				if (rc) {
					chg_err("unable to request chargerid_switch_gpio:%d\n", g_oppo_chip->normalchg_gpio.chargerid_switch_gpio);
				} else {
					smbchg_chargerid_switch_gpio_init(g_oppo_chip);
				}
			}
			chg_err("chargerid_switch_gpio:%d\n", g_oppo_chip->normalchg_gpio.chargerid_switch_gpio);
		}
	}
#endif /*VENDOR_EDIT*/
#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2018/05/08,  Add for using gpio as USB vbus short */
	if (g_oppo_chip) {
		g_oppo_chip->normalchg_gpio.dischg_gpio = of_get_named_gpio(node, "qcom,dischg-gpio", 0);
		if (g_oppo_chip->normalchg_gpio.dischg_gpio <= 0) {
			chg_err("Couldn't read qcom,dischg-gpio rc=%d, qcom,dischg-gpio:%d\n",
				rc, g_oppo_chip->normalchg_gpio.dischg_gpio);
		} else {
			if (oppo_usbtemp_check_is_support() == true) {
				if (gpio_is_valid(g_oppo_chip->normalchg_gpio.dischg_gpio)) {
					rc = gpio_request(g_oppo_chip->normalchg_gpio.dischg_gpio, "dischg-gpio");
					if (rc) {
						chg_err("unable to request dischg-gpio:%d\n", g_oppo_chip->normalchg_gpio.dischg_gpio);
					} else {
						oppo_dischg_gpio_init(g_oppo_chip);
					}
				}
			}
			chg_err("dischg-gpio:%d\n", g_oppo_chip->normalchg_gpio.dischg_gpio);
		}
	}
#endif /*VENDOR_EDIT*/
	
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/03/02, sjc Add for using gpio as shipmode stm6620 */
	if (g_oppo_chip) {
		g_oppo_chip->normalchg_gpio.ship_gpio =
				of_get_named_gpio(node, "qcom,ship-gpio", 0);
		if (g_oppo_chip->normalchg_gpio.ship_gpio <= 0) {
			chg_err("Couldn't read qcom,ship-gpio rc = %d, qcom,ship-gpio:%d\n",
					rc, g_oppo_chip->normalchg_gpio.ship_gpio);
		} else {
			if (oppo_ship_check_is_gpio(g_oppo_chip) == true) {
				rc = gpio_request(g_oppo_chip->normalchg_gpio.ship_gpio, "ship-gpio");
				if (rc) {
					chg_err("unable to request ship-gpio:%d\n",
							g_oppo_chip->normalchg_gpio.ship_gpio);
				} else {
					oppo_ship_gpio_init(g_oppo_chip);
					if (rc)
						chg_err("unable to init ship-gpio:%d\n", g_oppo_chip->normalchg_gpio.ship_gpio);
				}
			}
			chg_err("ship-gpio:%d\n", g_oppo_chip->normalchg_gpio.ship_gpio);
		}
	}
#endif /*VENDOR_EDIT*/
	
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/03/02, sjc Add for HW shortc */
	if (g_oppo_chip) {
		g_oppo_chip->normalchg_gpio.shortc_gpio =
				of_get_named_gpio(node, "qcom,shortc-gpio", 0);
		if (g_oppo_chip->normalchg_gpio.shortc_gpio <= 0) {
			chg_err("Couldn't read qcom,shortc-gpio rc = %d, qcom,shortc-gpio:%d\n",
					rc, g_oppo_chip->normalchg_gpio.shortc_gpio);
		} else {
			if (oppo_shortc_check_is_gpio(g_oppo_chip) == true) {
				rc = gpio_request(g_oppo_chip->normalchg_gpio.shortc_gpio, "shortc-gpio");
				if (rc) {
					chg_err("unable to request shortc-gpio:%d\n",
							g_oppo_chip->normalchg_gpio.shortc_gpio);
				} else {
					oppo_shortc_gpio_init(g_oppo_chip);
					if (rc)
						chg_err("unable to init ship-gpio:%d\n", g_oppo_chip->normalchg_gpio.ship_gpio);
				}
			}
			chg_err("shortc-gpio:%d\n", g_oppo_chip->normalchg_gpio.shortc_gpio);
		}
	}
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/03/02, sjc Add for HW shortc */
	if (g_oppo_chip) {
		chg->shipmode_id_gpio =
				of_get_named_gpio(node, "qcom,shipmode-id-gpio", 0);
		if (chg->shipmode_id_gpio <= 0) {
			chg_err("Couldn't read qcom,shipmode-id-gpio rc = %d, qcom,shipmode-id-gpio:%d\n",
					rc, chg->shipmode_id_gpio);
		} else {
			if (oppo_shipmode_id_check_is_gpio(g_oppo_chip) == true) {
				rc = gpio_request(chg->shipmode_id_gpio, "qcom,shipmode-id-gpio");
				if (rc) {
					chg_err("unable to request qcom,shipmode-id-gpio:%d\n",
							chg->shipmode_id_gpio);
				} else {
					oppo_shipmode_id_gpio_init(g_oppo_chip);
					if (rc)
						chg_err("unable to init qcom,shipmode-id-gpio:%d\n", chg->shipmode_id_gpio);
				}
			}
			chg_err("qcom,shipmode-id-gpio:%d\n", chg->shipmode_id_gpio);
		}
	}
#endif /* VENDOR_EDIT */


#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/01/30, sjc Add for using gpio as CC detect */
    if (chip) {
        chg->ccdetect_gpio = of_get_named_gpio(node, "qcom,ccdetect-gpio", 0);
        if (chg->ccdetect_gpio <= 0) {
            chg_err("Couldn't read qcom,ccdetect-gpio rc=%d, qcom,ccdetect-gpio:%d\n",
                    rc, chg->ccdetect_gpio);
        } else {
            if (oppo_ccdetect_check_is_gpio(chip) == true) {
                rc = gpio_request(chg->ccdetect_gpio, "ccdetect-gpio");
                if (rc) {
                    chg_err("unable to request ccdetect-gpio:%d\n", chg->ccdetect_gpio);
                } else {
                    rc = oppo_ccdetect_gpio_init(chip);
                    if (rc)
                        chg_err("unable to init ccdetect-gpio:%d\n", chg->ccdetect_gpio);
                    else
                        oppo_ccdetect_irq_init(chip);
                }
            }
            chg_err("ccdetect-gpio:%d\n", chg->ccdetect_gpio);
        }
    }
#endif /*VENDOR_EDIT*/

    oppo_usbtemp_adc_gpio_init(chip);

	return rc;

}

#endif /*VENDOR_EDIT*/
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/03/15, sjc Add for OTG debug */
static int __debug_mask = PR_MISC | PR_OTG | PR_INTERRUPT | PR_REGISTER;
#else
static int __debug_mask;
#endif
static ssize_t pd_disabled_show(struct device *dev, struct device_attribute
				*attr, char *buf)
{
	struct smb5 *chip = dev_get_drvdata(dev);
	struct smb_charger *chg = &chip->chg;

	return snprintf(buf, PAGE_SIZE, "%d\n", chg->pd_disabled);
}

static ssize_t pd_disabled_store(struct device *dev, struct device_attribute
				 *attr, const char *buf, size_t count)
{
	int val;
	struct smb5 *chip = dev_get_drvdata(dev);
	struct smb_charger *chg = &chip->chg;

	if (kstrtos32(buf, 0, &val))
		return -EINVAL;

	chg->pd_disabled = val;

	return count;
}
static DEVICE_ATTR_RW(pd_disabled);

static ssize_t weak_chg_icl_ua_show(struct device *dev, struct device_attribute
				    *attr, char *buf)
{
	struct smb5 *chip = dev_get_drvdata(dev);
	struct smb_charger *chg = &chip->chg;

	return snprintf(buf, PAGE_SIZE, "%d\n", chg->weak_chg_icl_ua);
}

static ssize_t weak_chg_icl_ua_store(struct device *dev, struct device_attribute
				 *attr, const char *buf, size_t count)
{
	int val;
	struct smb5 *chip = dev_get_drvdata(dev);
	struct smb_charger *chg = &chip->chg;

	if (kstrtos32(buf, 0, &val))
		return -EINVAL;

	chg->weak_chg_icl_ua = val;

	return count;
}
static DEVICE_ATTR_RW(weak_chg_icl_ua);

static struct attribute *smb5_attrs[] = {
	&dev_attr_pd_disabled.attr,
	&dev_attr_weak_chg_icl_ua.attr,
	NULL,
};
ATTRIBUTE_GROUPS(smb5);

enum {
	BAT_THERM = 0,
	MISC_THERM,
	CONN_THERM,
	SMB_THERM,
};

static const struct clamp_config clamp_levels[] = {
	{ {0x11C6, 0x11F9, 0x13F1}, {0x60, 0x2E, 0x90} },
	{ {0x11C6, 0x11F9, 0x13F1}, {0x60, 0x2B, 0x9C} },
};

#define PMI632_MAX_ICL_UA	3000000
#define PM6150_MAX_FCC_UA	3000000
static int smb5_chg_config_init(struct smb5 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct pmic_revid_data *pmic_rev_id;
	struct device_node *revid_dev_node, *node = chg->dev->of_node;
	int rc = 0;

	revid_dev_node = of_parse_phandle(node, "qcom,pmic-revid", 0);
	if (!revid_dev_node) {
		pr_err("Missing qcom,pmic-revid property\n");
		return -EINVAL;
	}

	pmic_rev_id = get_revid_data(revid_dev_node);
	if (IS_ERR_OR_NULL(pmic_rev_id)) {
		/*
		 * the revid peripheral must be registered, any failure
		 * here only indicates that the rev-id module has not
		 * probed yet.
		 */
		rc =  -EPROBE_DEFER;
		goto out;
	}

	switch (pmic_rev_id->pmic_subtype) {
	case PM8150B_SUBTYPE:
		chip->chg.smb_version = PM8150B_SUBTYPE;
		chg->param = smb5_pm8150b_params;
		chg->name = "pm8150b_charger";
#ifdef VENDOR_EDIT
		/*zhijie.li@BSP.CHG.Basic, 2019/1/25, lzj add for aicl*/
        	chip->chg.wa_flags |= BOOST_BACK_WA;
#endif
		chg->wa_flags |= CHG_TERMINATION_WA;
		break;
	case PM7250B_SUBTYPE:
		chip->chg.smb_version = PM7250B_SUBTYPE;
		chg->param = smb5_pm8150b_params;
		chg->name = "pm7250b_charger";
		chg->wa_flags |= CHG_TERMINATION_WA;
		chg->uusb_moisture_protection_capable = true;
		break;
	case PM6150_SUBTYPE:
		chip->chg.smb_version = PM6150_SUBTYPE;
		chg->param = smb5_pm8150b_params;
		chg->name = "pm6150_charger";
		chg->wa_flags |= SW_THERM_REGULATION_WA | CHG_TERMINATION_WA;
		if (pmic_rev_id->rev4 >= 2)
			chg->uusb_moisture_protection_capable = true;
		chg->main_fcc_max = PM6150_MAX_FCC_UA;
		break;
	case PMI632_SUBTYPE:
		chip->chg.smb_version = PMI632_SUBTYPE;
		chg->wa_flags |= WEAK_ADAPTER_WA | USBIN_OV_WA
				| CHG_TERMINATION_WA | USBIN_ADC_WA;
		chg->param = smb5_pmi632_params;
		chg->use_extcon = true;
		chg->name = "pmi632_charger";
		/* PMI632 does not support PD */
		chg->pd_not_supported = true;
		chg->lpd_disabled = true;
		if (pmic_rev_id->rev4 >= 2)
			chg->uusb_moisture_protection_enabled = true;
		chg->hw_max_icl_ua =
			(chip->dt.usb_icl_ua > 0) ? chip->dt.usb_icl_ua
						: PMI632_MAX_ICL_UA;
		break;
	default:
		pr_err("PMIC subtype %d not supported\n",
				pmic_rev_id->pmic_subtype);
		rc = -EINVAL;
		goto out;
	}

	chg->chg_freq.freq_5V			= 600;
	chg->chg_freq.freq_6V_8V		= 800;
	chg->chg_freq.freq_9V			= 1050;
	chg->chg_freq.freq_12V                  = 1200;
	chg->chg_freq.freq_removal		= 1050;
	chg->chg_freq.freq_below_otg_threshold	= 800;
	chg->chg_freq.freq_above_otg_threshold	= 800;

	if (of_property_read_bool(node, "qcom,disable-sw-thermal-regulation"))
		chg->wa_flags &= ~SW_THERM_REGULATION_WA;

	if (of_property_read_bool(node, "qcom,disable-fcc-restriction"))
		chg->main_fcc_max = -EINVAL;

out:
	of_node_put(revid_dev_node);
	return rc;
}

#define PULL_NO_PULL	0
#define PULL_30K	30
#define PULL_100K	100
#define PULL_400K	400
static int get_valid_pullup(int pull_up)
{
	/* pull up can only be 0/30K/100K/400K) */
	switch (pull_up) {
	case PULL_NO_PULL:
		return INTERNAL_PULL_NO_PULL;
	case PULL_30K:
		return INTERNAL_PULL_30K_PULL;
	case PULL_100K:
		return INTERNAL_PULL_100K_PULL;
	case PULL_400K:
		return INTERNAL_PULL_400K_PULL;
	default:
		return INTERNAL_PULL_100K_PULL;
	}
}

#define INTERNAL_PULL_UP_MASK	0x3
static int smb5_configure_internal_pull(struct smb_charger *chg, int type,
					int pull)
{
	int rc;
	int shift = type * 2;
	u8 mask = INTERNAL_PULL_UP_MASK << shift;
	u8 val = pull << shift;

	rc = smblib_masked_write(chg, BATIF_ADC_INTERNAL_PULL_UP_REG,
				mask, val);
	if (rc < 0)
		dev_err(chg->dev,
			"Couldn't configure ADC pull-up reg rc=%d\n", rc);

	return rc;
}

#ifndef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/05/27, sjc Modify for OTG current limit (V3.1)  */
#define MICRO_1P5A		1500000
#else
#define MICRO_1P5A		1000000
#endif
#define MICRO_P1A			100000
#define MICRO_1PA			1000000
#ifndef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2018/12/22,  Modify for OTG current limit (V3.1)  */
#define MICRO_3PA			3000000
#else
#define MICRO_3PA			1500000
#endif
#define OTG_DEFAULT_DEGLITCH_TIME_MS	50
#define DEFAULT_WD_BARK_TIME		64
#define DEFAULT_WD_SNARL_TIME_8S	0x07
static int smb5_parse_dt_misc(struct smb5 *chip, struct device_node *node)
{
	int rc = 0, byte_len;
	struct smb_charger *chg = &chip->chg;

	of_property_read_u32(node, "qcom,sec-charger-config",
					&chip->dt.sec_charger_config);
	chg->sec_cp_present =
		chip->dt.sec_charger_config == POWER_SUPPLY_CHARGER_SEC_CP ||
		chip->dt.sec_charger_config == POWER_SUPPLY_CHARGER_SEC_CP_PL;

	chg->sec_pl_present =
		chip->dt.sec_charger_config == POWER_SUPPLY_CHARGER_SEC_PL ||
		chip->dt.sec_charger_config == POWER_SUPPLY_CHARGER_SEC_CP_PL;

	chg->step_chg_enabled = of_property_read_bool(node,
				"qcom,step-charging-enable");

	chg->typec_legacy_use_rp_icl = of_property_read_bool(node,
				"qcom,typec-legacy-rp-icl");

	chg->sw_jeita_enabled = of_property_read_bool(node,
				"qcom,sw-jeita-enable");

	chg->pd_not_supported = chg->pd_not_supported ||
			of_property_read_bool(node, "qcom,usb-pd-disable");

	chg->lpd_disabled = of_property_read_bool(node, "qcom,lpd-disable");

	rc = of_property_read_u32(node, "qcom,wd-bark-time-secs",
					&chip->dt.wd_bark_time);
	if (rc < 0 || chip->dt.wd_bark_time < MIN_WD_BARK_TIME)
		chip->dt.wd_bark_time = DEFAULT_WD_BARK_TIME;

	rc = of_property_read_u32(node, "qcom,wd-snarl-time-config",
					&chip->dt.wd_snarl_time_cfg);
	if (rc < 0)
		chip->dt.wd_snarl_time_cfg = DEFAULT_WD_SNARL_TIME_8S;

	chip->dt.no_battery = of_property_read_bool(node,
						"qcom,batteryless-platform");

	if (of_find_property(node, "qcom,thermal-mitigation", &byte_len)) {
		chg->thermal_mitigation = devm_kzalloc(chg->dev, byte_len,
			GFP_KERNEL);

		if (chg->thermal_mitigation == NULL)
			return -ENOMEM;

		chg->thermal_levels = byte_len / sizeof(u32);
		rc = of_property_read_u32_array(node,
				"qcom,thermal-mitigation",
				chg->thermal_mitigation,
				chg->thermal_levels);
		if (rc < 0) {
			dev_err(chg->dev,
				"Couldn't read threm limits rc = %d\n", rc);
			return rc;
		}
	}

	rc = of_property_read_u32(node, "qcom,charger-temp-max",
			&chg->charger_temp_max);
	if (rc < 0)
		chg->charger_temp_max = -EINVAL;

	rc = of_property_read_u32(node, "qcom,smb-temp-max",
			&chg->smb_temp_max);
	if (rc < 0)
		chg->smb_temp_max = -EINVAL;

	rc = of_property_read_u32(node, "qcom,float-option",
						&chip->dt.float_option);
	if (!rc && (chip->dt.float_option < 0 || chip->dt.float_option > 4)) {
		pr_err("qcom,float-option is out of range [0, 4]\n");
		return -EINVAL;
	}

	chip->dt.hvdcp_disable = of_property_read_bool(node,
						"qcom,hvdcp-disable");
	chg->hvdcp_disable = chip->dt.hvdcp_disable;

	chip->dt.hvdcp_autonomous = of_property_read_bool(node,
						"qcom,hvdcp-autonomous-enable");

	chip->dt.auto_recharge_soc = -EINVAL;
	rc = of_property_read_u32(node, "qcom,auto-recharge-soc",
				&chip->dt.auto_recharge_soc);
	if (!rc && (chip->dt.auto_recharge_soc < 0 ||
			chip->dt.auto_recharge_soc > 100)) {
		pr_err("qcom,auto-recharge-soc is incorrect\n");
		return -EINVAL;
	}
	chg->auto_recharge_soc = chip->dt.auto_recharge_soc;

	chg->suspend_input_on_debug_batt = of_property_read_bool(node,
					"qcom,suspend-input-on-debug-batt");

	rc = of_property_read_u32(node, "qcom,otg-deglitch-time-ms",
					&chg->otg_delay_ms);
	if (rc < 0)
		chg->otg_delay_ms = OTG_DEFAULT_DEGLITCH_TIME_MS;

	chg->fcc_stepper_enable = of_property_read_bool(node,
					"qcom,fcc-stepping-enable");

	if (chg->uusb_moisture_protection_capable)
		chg->uusb_moisture_protection_enabled =
			of_property_read_bool(node,
					"qcom,uusb-moisture-protection-enable");

	chg->hw_die_temp_mitigation = of_property_read_bool(node,
					"qcom,hw-die-temp-mitigation");

	chg->hw_connector_mitigation = of_property_read_bool(node,
					"qcom,hw-connector-mitigation");

	chg->hw_skin_temp_mitigation = of_property_read_bool(node,
					"qcom,hw-skin-temp-mitigation");

	chg->en_skin_therm_mitigation = of_property_read_bool(node,
					"qcom,en-skin-therm-mitigation");

	chg->connector_pull_up = -EINVAL;
	of_property_read_u32(node, "qcom,connector-internal-pull-kohm",
					&chg->connector_pull_up);

	chip->dt.disable_suspend_on_collapse = of_property_read_bool(node,
					"qcom,disable-suspend-on-collapse");
	chg->smb_pull_up = -EINVAL;
	of_property_read_u32(node, "qcom,smb-internal-pull-kohm",
					&chg->smb_pull_up);

	chip->dt.adc_based_aicl = of_property_read_bool(node,
					"qcom,adc-based-aicl");

	return 0;
}

static int smb5_parse_dt_adc_channels(struct smb_charger *chg)
{
	int rc = 0;

	rc = smblib_get_iio_channel(chg, "mid_voltage", &chg->iio.mid_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "usb_in_voltage",
					&chg->iio.usbin_v_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "chg_temp", &chg->iio.temp_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "usb_in_current",
					&chg->iio.usbin_i_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "sbux_res", &chg->iio.sbux_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "vph_voltage", &chg->iio.vph_v_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "die_temp", &chg->iio.die_temp_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "conn_temp",
					&chg->iio.connector_temp_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "skin_temp", &chg->iio.skin_temp_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "smb_temp", &chg->iio.smb_temp_chan);
	if (rc < 0)
		return rc;

	return 0;
}

static int smb5_parse_dt_currents(struct smb5 *chip, struct device_node *node)
{
	int rc = 0, tmp;
	struct smb_charger *chg = &chip->chg;

	rc = of_property_read_u32(node,
			"qcom,fcc-max-ua", &chip->dt.batt_profile_fcc_ua);
	if (rc < 0)
		chip->dt.batt_profile_fcc_ua = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,usb-icl-ua", &chip->dt.usb_icl_ua);
	if (rc < 0)
		chip->dt.usb_icl_ua = -EINVAL;
#ifndef VENDOR_EDIT
        /* Jianchao.Shi@BSP.CHG.Basic, 2017/04/28, sjc Modify for charging */
	chg->dcp_icl_ua = chip->dt.usb_icl_ua;
#else
	chg->dcp_icl_ua = -EINVAL;
#endif

	rc = of_property_read_u32(node,
				"qcom,otg-cl-ua", &chg->otg_cl_ua);
	if (rc < 0)
		chg->otg_cl_ua = (chip->chg.smb_version == PMI632_SUBTYPE) ?
							MICRO_1PA : MICRO_3PA;
	pr_err("of_property_read_u32 otg current otg_cl_ua=%d\n", chg->otg_cl_ua);
	rc = of_property_read_u32(node, "qcom,chg-term-src",
			&chip->dt.term_current_src);
	if (rc < 0)
		chip->dt.term_current_src = ITERM_SRC_UNSPECIFIED;

	if (chip->dt.term_current_src == ITERM_SRC_ADC)
		rc = of_property_read_u32(node, "qcom,chg-term-base-current-ma",
				&chip->dt.term_current_thresh_lo_ma);

	rc = of_property_read_u32(node, "qcom,chg-term-current-ma",
			&chip->dt.term_current_thresh_hi_ma);

	chg->wls_icl_ua = DCIN_ICL_MAX_UA;
	rc = of_property_read_u32(node, "qcom,wls-current-max-ua",
			&tmp);
	if (!rc && tmp < DCIN_ICL_MAX_UA)
		chg->wls_icl_ua = tmp;

	return 0;
}

static int smb5_parse_dt_voltages(struct smb5 *chip, struct device_node *node)
{
	int rc = 0;

	rc = of_property_read_u32(node,
				"qcom,fv-max-uv", &chip->dt.batt_profile_fv_uv);
	if (rc < 0)
		chip->dt.batt_profile_fv_uv = -EINVAL;

	rc = of_property_read_u32(node, "qcom,chg-inhibit-threshold-mv",
				&chip->dt.chg_inhibit_thr_mv);
	if (!rc && (chip->dt.chg_inhibit_thr_mv < 0 ||
				chip->dt.chg_inhibit_thr_mv > 300)) {
		pr_err("qcom,chg-inhibit-threshold-mv is incorrect\n");
		return -EINVAL;
	}

	chip->dt.auto_recharge_vbat_mv = -EINVAL;
	rc = of_property_read_u32(node, "qcom,auto-recharge-vbat-mv",
				&chip->dt.auto_recharge_vbat_mv);
	if (!rc && (chip->dt.auto_recharge_vbat_mv < 0)) {
		pr_err("qcom,auto-recharge-vbat-mv is incorrect\n");
		return -EINVAL;
	}

	return 0;
}

static int smb5_parse_dt(struct smb5 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct device_node *node = chg->dev->of_node;
	int rc = 0;

	if (!node) {
		pr_err("device tree node missing\n");
		return -EINVAL;
	}

	rc = smb5_parse_dt_voltages(chip, node);
	if (rc < 0)
		return rc;

	rc = smb5_parse_dt_currents(chip, node);
	if (rc < 0)
		return rc;

	rc = smb5_parse_dt_adc_channels(chg);
	if (rc < 0)
		return rc;

	rc = smb5_parse_dt_misc(chip, node);
	if (rc < 0)
		return rc;

	return 0;
}

static int smb5_set_prop_comp_clamp_level(struct smb_charger *chg,
			     const union power_supply_propval *val)
{
	int rc = 0, i;
	struct clamp_config clamp_config;
	enum comp_clamp_levels level;

	level = val->intval;
	if (level >= MAX_CLAMP_LEVEL) {
		pr_err("Invalid comp clamp level=%d\n", val->intval);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(clamp_config.reg); i++) {
		rc = smblib_write(chg, clamp_levels[level].reg[i],
			     clamp_levels[level].val[i]);
		if (rc < 0)
			dev_err(chg->dev,
				"Failed to configure comp clamp settings for reg=0x%04x rc=%d\n",
				   clamp_levels[level].reg[i], rc);
	}

	chg->comp_clamp_level = val->intval;

	return rc;
}

/************************
 * USB PSY REGISTRATION *
 ************************/
static enum power_supply_property smb5_usb_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_PD_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_TYPEC_MODE,
	POWER_SUPPLY_PROP_TYPEC_POWER_ROLE,
	POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION,
	POWER_SUPPLY_PROP_LOW_POWER,
	POWER_SUPPLY_PROP_PD_ACTIVE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
	POWER_SUPPLY_PROP_INPUT_CURRENT_NOW,
	POWER_SUPPLY_PROP_BOOST_CURRENT,
	POWER_SUPPLY_PROP_PE_START,
	POWER_SUPPLY_PROP_CTM_CURRENT_MAX,
	POWER_SUPPLY_PROP_HW_CURRENT_MAX,
	POWER_SUPPLY_PROP_REAL_TYPE,
	POWER_SUPPLY_PROP_PD_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_PD_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_CONNECTOR_TYPE,
	POWER_SUPPLY_PROP_CONNECTOR_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_LIMIT,
	POWER_SUPPLY_PROP_SMB_EN_MODE,
	POWER_SUPPLY_PROP_SMB_EN_REASON,
	POWER_SUPPLY_PROP_ADAPTER_CC_MODE,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_MOISTURE_DETECTED,
	POWER_SUPPLY_PROP_HVDCP_OPTI_ALLOWED,
	POWER_SUPPLY_PROP_QC_OPTI_DISABLE,
	POWER_SUPPLY_PROP_VOLTAGE_VPH,
	POWER_SUPPLY_PROP_THERM_ICL_LIMIT,
	POWER_SUPPLY_PROP_SKIN_HEALTH,
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/02/18, sjc Add for OTG sw */
	POWER_SUPPLY_PROP_OTG_SWITCH,
	POWER_SUPPLY_PROP_OTG_ONLINE,
	POWER_SUPPLY_PROP_FAST_CHG_TYPE,
#endif

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/06/23, sjc Add for usb status */
	POWER_SUPPLY_PROP_USB_STATUS,
#endif
};
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/04/09, sjc Add for OTG sw */
/**************************************************************
 * bit[0]=0: NO standard typec device/cable connected(ccdetect gpio in high level)
 * bit[0]=1: standard typec device/cable connected(ccdetect gpio in low level)
 * bit[1]=0: NO OTG typec device/cable connected
 * bit[1]=1: OTG typec device/cable connected
 **************************************************************/
#define DISCONNECT						0
#define STANDARD_TYPEC_DEV_CONNECT	BIT(0)
#define OTG_DEV_CONNECT				BIT(1)

bool oppo_get_otg_switch_status(void)
{
	struct oppo_chg_chip *chip = g_oppo_chip;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb2_chg not ready!\n", __func__);
		return false;
	}

	return chip->otg_switch;
}

static bool oppo_get_otg_online_status_default(void)
{
	int rc;
	union power_supply_propval val;
	struct smb_charger *chg = NULL;
	if (!g_oppo_chip) {
		chg_err("fail to init oppo_chip\n");
		return false;
	}

	chg = &g_oppo_chip->pmic_spmi.smb5_chip->chg;

	rc = power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_TYPEC_MODE, &val);
	if (rc < 0) {
		chg_err("fail to get property\n");
		return false;
	}

	if (val.intval == POWER_SUPPLY_TYPEC_SINK)
		g_oppo_chip->otg_online = true;
	else
		g_oppo_chip->otg_online = false;
	return g_oppo_chip->otg_online;
}

static int oppo_get_otg_online_status(void)
{
	union power_supply_propval val;
	int ret;
	int online = 0;
	int level = 0;
	int typec_otg = 0;
	static int pre_level = 1;
	static int pre_typec_otg = 0;
	struct smb_charger *chg = NULL;
	struct oppo_chg_chip *chip = g_oppo_chip;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return false;
	}

	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (oppo_ccdetect_check_is_gpio(chip) == true) {
		level = gpio_get_value(chg->ccdetect_gpio);
		if (level != gpio_get_value(chg->ccdetect_gpio)) {
			printk(KERN_ERR "[OPPO_CHG][%s]: ccdetect_gpio is unstable, try again...\n", __func__);
			usleep_range(5000, 5100);
			level = gpio_get_value(chg->ccdetect_gpio);
		}
	} else {
		return oppo_get_otg_online_status_default();
	}
	online = (level == 1) ? DISCONNECT : STANDARD_TYPEC_DEV_CONNECT;

	ret = power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_TYPEC_MODE, &val);
	if (ret) {
		printk(KERN_ERR "%s: Unable to read USB TYPEC_MODE\n", __func__);
		val.intval = 0;
	}
	if (val.intval >= POWER_SUPPLY_TYPEC_SINK
			&& val.intval <= POWER_SUPPLY_TYPEC_POWERED_CABLE_ONLY) {
		typec_otg = 1;
	} else {
		typec_otg = 0;
	}
	online = online | ((typec_otg == 1) ? OTG_DEV_CONNECT : DISCONNECT);

	if ((pre_level ^ level) || (pre_typec_otg ^ typec_otg)) {
		pre_level = level;
		pre_typec_otg = typec_otg;
		printk(KERN_ERR "[OPPO_CHG][%s]: gpio[%s], c-otg[%d], otg_online[%d]\n",
				__func__, level ? "H" : "L", typec_otg, online);
	}

	chip->otg_online = online;
	return online;
}

static void oppo_set_otg_switch_status(bool value)
{
	int level = 0;
	struct smb_charger *chg = NULL;
	struct oppo_chg_chip *chip = g_oppo_chip;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	/*boot-up with newman OTG connected, android will set persist.sys.oppo.otg_support, so...*/
	if (oppo_ccdetect_check_is_gpio(chip) == true) {
		level = gpio_get_value(chg->ccdetect_gpio);
		if (level != 1) {
			printk(KERN_ERR "[OPPO_CHG][%s]: gpio[%s], should set, return\n", __func__, level ? "H" : "L");
			return;
		}
	}else {
    		oppo_set_otg_switch_status_default(value);
        	return;
	}

	chip->otg_switch = !!value;
	if (value) {
		oppo_ccdetect_enable();
	} else {
		oppo_ccdetect_disable();
	}
	printk(KERN_ERR "[OPPO_CHG][%s]: otg_switch=%d, otg_online=%d\n",
			__func__, chip->otg_switch, chip->otg_online);
}
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/04/26, sjc Add for charging */
static bool use_present_status = false;
#endif

static int smb5_usb_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;
	val->intval = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		rc = smblib_get_prop_usb_present(chg, val);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
#ifdef VENDOR_EDIT
		/* Jianchao.Shi@BSP.CHG.Basic, 2017/04/26, sjc Modify for charging */
		if (use_present_status)
			rc = smblib_get_prop_usb_present(chg, val);
		else
			rc = smblib_get_prop_usb_online(chg, val);
#else
		rc = smblib_get_usb_online(chg, val);
#endif
#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2019-08-19  for c to c */
        if (chg->real_charger_type == POWER_SUPPLY_TYPE_USB_PD) {
            val->intval = 1;
            break;
        }
#endif

		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		rc = smblib_get_prop_usb_voltage_max_design(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = smblib_get_prop_usb_voltage_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_LIMIT:
		if (chg->usbin_forced_max_uv)
			val->intval = chg->usbin_forced_max_uv;
		else
			smblib_get_prop_usb_voltage_max_design(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		rc = smblib_get_prop_usb_voltage_now(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_CURRENT_MAX:
		val->intval = get_client_vote(chg->usb_icl_votable, PD_VOTER);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_get_prop_input_current_settled(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_USB_PD;
		break;
	case POWER_SUPPLY_PROP_REAL_TYPE:
		val->intval = chg->real_charger_type;
		break;
	case POWER_SUPPLY_PROP_TYPEC_MODE:
		rc = smblib_get_usb_prop_typec_mode(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPEC_POWER_ROLE:
		rc = smblib_get_prop_typec_power_role(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION:
		rc = smblib_get_prop_typec_cc_orientation(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPEC_SRC_RP:
		rc = smblib_get_prop_typec_select_rp(chg, val);
		break;
	case POWER_SUPPLY_PROP_LOW_POWER:
		rc = smblib_get_prop_low_power(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_ACTIVE:
		val->intval = chg->pd_active;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
		rc = smblib_get_prop_input_current_settled(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_NOW:
		rc = smblib_get_prop_usb_current_now(chg, val);
		break;
	case POWER_SUPPLY_PROP_BOOST_CURRENT:
		val->intval = chg->boost_current_ua;
		break;
	case POWER_SUPPLY_PROP_PD_IN_HARD_RESET:
		rc = smblib_get_prop_pd_in_hard_reset(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_USB_SUSPEND_SUPPORTED:
		val->intval = chg->system_suspend_supported;
		break;
	case POWER_SUPPLY_PROP_PE_START:
		rc = smblib_get_pe_start(chg, val);
		break;
	case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
		val->intval = get_client_vote(chg->usb_icl_votable, CTM_VOTER);
		break;
	case POWER_SUPPLY_PROP_HW_CURRENT_MAX:
		rc = smblib_get_charge_current(chg, &val->intval);
		break;
	case POWER_SUPPLY_PROP_PR_SWAP:
		rc = smblib_get_prop_pr_swap_in_progress(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MAX:
		val->intval = chg->voltage_max_uv;
		break;
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MIN:
		val->intval = chg->voltage_min_uv;
		break;
	case POWER_SUPPLY_PROP_SDP_CURRENT_MAX:
		val->intval = get_client_vote(chg->usb_icl_votable,
					      USB_PSY_VOTER);
		break;
	case POWER_SUPPLY_PROP_CONNECTOR_TYPE:
		val->intval = chg->connector_type;
		break;
	case POWER_SUPPLY_PROP_CONNECTOR_HEALTH:
		val->intval = smblib_get_prop_connector_health(chg);
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		rc = smblib_get_prop_scope(chg, val);
		break;
	case POWER_SUPPLY_PROP_SMB_EN_MODE:
		mutex_lock(&chg->smb_lock);
		val->intval = chg->sec_chg_selected;
		mutex_unlock(&chg->smb_lock);
		break;
	case POWER_SUPPLY_PROP_SMB_EN_REASON:
		val->intval = chg->cp_reason;
		break;
	case POWER_SUPPLY_PROP_MOISTURE_DETECTED:
		val->intval = chg->moisture_present;
		break;
	case POWER_SUPPLY_PROP_HVDCP_OPTI_ALLOWED:
		val->intval = !chg->flash_active;
		break;
	case POWER_SUPPLY_PROP_QC_OPTI_DISABLE:
		if (chg->hw_die_temp_mitigation)
			val->intval = POWER_SUPPLY_QC_THERMAL_BALANCE_DISABLE
					| POWER_SUPPLY_QC_INOV_THERMAL_DISABLE;
		if (chg->hw_connector_mitigation)
			val->intval |= POWER_SUPPLY_QC_CTM_DISABLE;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_VPH:
		rc = smblib_get_prop_vph_voltage_now(chg, val);
		break;
	case POWER_SUPPLY_PROP_THERM_ICL_LIMIT:
		val->intval = get_client_vote(chg->usb_icl_votable,
					THERMAL_THROTTLE_VOTER);
		break;
	case POWER_SUPPLY_PROP_ADAPTER_CC_MODE:
		val->intval = chg->adapter_cc_mode;
		break;
	case POWER_SUPPLY_PROP_SKIN_HEALTH:
		val->intval = smblib_get_skin_temp_status(chg);
		break;
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/02/18, sjc Add for OTG sw */
	case POWER_SUPPLY_PROP_OTG_SWITCH:
		val->intval = oppo_get_otg_switch_status();
		break;
	case POWER_SUPPLY_PROP_OTG_ONLINE:
		val->intval = oppo_get_otg_online_status();
		break;
	case POWER_SUPPLY_PROP_FAST_CHG_TYPE:
		if(((oppo_vooc_get_fastchg_started() == true) ||
			(oppo_vooc_get_fastchg_dummy_started() == true))&&
			(g_oppo_chip->vbatt_num == 1)&&(oppo_vooc_get_fast_chg_type() == FASTCHG_CHARGER_TYPE_UNKOWN)){
			val->intval = CHARGER_SUBTYPE_FASTCHG_VOOC;
		}else{
			val->intval = oppo_vooc_get_fast_chg_type();
			if (val->intval == 0 && g_oppo_chip && g_oppo_chip->chg_ops->get_charger_subtype) {
				val->intval = g_oppo_chip->chg_ops->get_charger_subtype();
			}
		}
		break;
#endif /*VENDOR_EDIT*/
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/06/23, sjc Add for usb status */
	case POWER_SUPPLY_PROP_USB_STATUS:
		val->intval = oppo_get_usb_status();
		break;
#endif /*VENDOR_EDIT*/
	default:
		pr_err("get prop %d is not supported in usb\n", psp);
		rc = -EINVAL;
		break;
	}

	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

#define MIN_THERMAL_VOTE_UA	500000
static int smb5_usb_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int icl, rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PD_CURRENT_MAX:
		rc = smblib_set_prop_pd_current_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPEC_POWER_ROLE:
		rc = smblib_set_prop_typec_power_role(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPEC_SRC_RP:
		rc = smblib_set_prop_typec_select_rp(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_ACTIVE:
		rc = smblib_set_prop_pd_active(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_IN_HARD_RESET:
		rc = smblib_set_prop_pd_in_hard_reset(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_USB_SUSPEND_SUPPORTED:
		chg->system_suspend_supported = val->intval;
		break;
	case POWER_SUPPLY_PROP_BOOST_CURRENT:
		rc = smblib_set_prop_boost_current(chg, val);
		break;
	case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
		rc = vote(chg->usb_icl_votable, CTM_VOTER,
						val->intval >= 0, val->intval);
		break;
	case POWER_SUPPLY_PROP_PR_SWAP:
		rc = smblib_set_prop_pr_swap_in_progress(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MAX:
		rc = smblib_set_prop_pd_voltage_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MIN:
		rc = smblib_set_prop_pd_voltage_min(chg, val);
		break;
	case POWER_SUPPLY_PROP_SDP_CURRENT_MAX:
		rc = smblib_set_prop_sdp_current_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_CONNECTOR_HEALTH:
		chg->connector_health = val->intval;
		power_supply_changed(chg->usb_psy);
		break;
	case POWER_SUPPLY_PROP_THERM_ICL_LIMIT:
		if (!is_client_vote_enabled(chg->usb_icl_votable,
						THERMAL_THROTTLE_VOTER)) {
			chg->init_thermal_ua = get_effective_result(
							chg->usb_icl_votable);
			icl = chg->init_thermal_ua + val->intval;
		} else {
			icl = get_client_vote(chg->usb_icl_votable,
					THERMAL_THROTTLE_VOTER) + val->intval;
		}

		if (icl >= MIN_THERMAL_VOTE_UA)
			rc = vote(chg->usb_icl_votable, THERMAL_THROTTLE_VOTER,
				(icl != chg->init_thermal_ua) ? true : false,
				icl);
		else
			rc = -EINVAL;
		break;
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/02/18, sjc Add for OTG sw */
	case POWER_SUPPLY_PROP_OTG_SWITCH:
		oppo_set_otg_switch_status(!!val->intval);
		break;
#endif
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_LIMIT:
		smblib_set_prop_usb_voltage_max_limit(chg, val);
		break;
	case POWER_SUPPLY_PROP_ADAPTER_CC_MODE:
		chg->adapter_cc_mode = val->intval;
		break;
	default:
		pr_err("set prop %d is not supported\n", psp);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int smb5_usb_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CONNECTOR_HEALTH:
	case POWER_SUPPLY_PROP_THERM_ICL_LIMIT:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_LIMIT:
	case POWER_SUPPLY_PROP_ADAPTER_CC_MODE:
		return 1;
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/02/18, sjc Add for OTG sw */
	case POWER_SUPPLY_PROP_OTG_SWITCH:
		return 1;
#endif
	default:
		break;
	}

	return 0;
}

#ifndef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2018/12/21,  Modify for charging */
static const struct power_supply_desc usb_psy_desc = {
#else
static struct power_supply_desc usb_psy_desc = {
#endif
	.name = "usb",
#ifndef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/03/28, sjc Modify for charging */
	.type = POWER_SUPPLY_TYPE_USB_PD,
#else
	.type = POWER_SUPPLY_TYPE_UNKNOWN,
#endif
	.properties = smb5_usb_props,
	.num_properties = ARRAY_SIZE(smb5_usb_props),
	.get_property = smb5_usb_get_prop,
	.set_property = smb5_usb_set_prop,
	.property_is_writeable = smb5_usb_prop_is_writeable,
};

static int smb5_init_usb_psy(struct smb5 *chip)
{
	struct power_supply_config usb_cfg = {};
	struct smb_charger *chg = &chip->chg;

	usb_cfg.drv_data = chip;
	usb_cfg.of_node = chg->dev->of_node;
	chg->usb_psy = devm_power_supply_register(chg->dev,
						  &usb_psy_desc,
						  &usb_cfg);
	if (IS_ERR(chg->usb_psy)) {
		pr_err("Couldn't register USB power supply\n");
		return PTR_ERR(chg->usb_psy);
	}

	return 0;
}
#ifdef VENDOR_EDIT
/* Tongfeng.Huang@BSP.CHG.Basic, 2018/12/21,  Add for update usb type*/
void oppo_set_smb5_usb_props_type(enum power_supply_type type)
{
	chg_err("old type[%d], new type[%d]\n", usb_psy_desc.type, type);
	usb_psy_desc.type = type;
	return;
}
#endif

/********************************
 * USB PC_PORT PSY REGISTRATION *
 ********************************/
static enum power_supply_property smb5_usb_port_props[] = {
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static int smb5_usb_port_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_USB;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/04/26, sjc Modify for charging */
		if (use_present_status)
			rc = smblib_get_prop_usb_present(chg, val);
		else
			rc = smblib_get_prop_usb_online(chg, val);
#else
		rc = smblib_get_prop_usb_online(chg, val);
#endif
#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2019-08-19  for c to c */
        if (chg->real_charger_type == POWER_SUPPLY_TYPE_USB_PD) {
            val->intval = 1;
            break;
        }
#endif

		if (!val->intval)
			break;

		if (((chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT) ||
		   (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB))
			&& (chg->real_charger_type == POWER_SUPPLY_TYPE_USB))
			val->intval = 1;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = 5000000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_get_prop_input_current_settled(chg, val);
		break;
	default:
		pr_err_ratelimited("Get prop %d is not supported in pc_port\n",
				psp);
		return -EINVAL;
	}

	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int smb5_usb_port_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	int rc = 0;

	switch (psp) {
	default:
		pr_err_ratelimited("Set prop %d is not supported in pc_port\n",
				psp);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static const struct power_supply_desc usb_port_psy_desc = {
	.name		= "pc_port",
	.type		= POWER_SUPPLY_TYPE_USB,
	.properties	= smb5_usb_port_props,
	.num_properties	= ARRAY_SIZE(smb5_usb_port_props),
	.get_property	= smb5_usb_port_get_prop,
	.set_property	= smb5_usb_port_set_prop,
};

static int smb5_init_usb_port_psy(struct smb5 *chip)
{
	struct power_supply_config usb_port_cfg = {};
	struct smb_charger *chg = &chip->chg;

	usb_port_cfg.drv_data = chip;
	usb_port_cfg.of_node = chg->dev->of_node;
	chg->usb_port_psy = devm_power_supply_register(chg->dev,
						  &usb_port_psy_desc,
						  &usb_port_cfg);
	if (IS_ERR(chg->usb_port_psy)) {
		pr_err("Couldn't register USB pc_port power supply\n");
		return PTR_ERR(chg->usb_port_psy);
	}

	return 0;
}

/*****************************
 * USB MAIN PSY REGISTRATION *
 *****************************/

static enum power_supply_property smb5_usb_main_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_SETTLED,
	POWER_SUPPLY_PROP_FCC_DELTA,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_FLASH_ACTIVE,
	POWER_SUPPLY_PROP_FLASH_TRIGGER,
	POWER_SUPPLY_PROP_TOGGLE_STAT,
	POWER_SUPPLY_PROP_MAIN_FCC_MAX,
	POWER_SUPPLY_PROP_IRQ_STATUS,
	POWER_SUPPLY_PROP_FORCE_MAIN_FCC,
	POWER_SUPPLY_PROP_FORCE_MAIN_ICL,
	POWER_SUPPLY_PROP_COMP_CLAMP_LEVEL,
	POWER_SUPPLY_PROP_HEALTH,
};

static int smb5_usb_main_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = smblib_get_charge_param(chg, &chg->param.fv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		rc = smblib_get_charge_param(chg, &chg->param.fcc,
							&val->intval);
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_MAIN;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
		rc = smblib_get_prop_input_current_settled(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_SETTLED:
		rc = smblib_get_prop_input_voltage_settled(chg, val);
		break;
	case POWER_SUPPLY_PROP_FCC_DELTA:
		rc = smblib_get_prop_fcc_delta(chg, val);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_get_icl_current(chg, &val->intval);
		break;
	case POWER_SUPPLY_PROP_FLASH_ACTIVE:
		val->intval = chg->flash_active;
		break;
	case POWER_SUPPLY_PROP_FLASH_TRIGGER:
		rc = schgm_flash_get_vreg_ok(chg, &val->intval);
		break;
	case POWER_SUPPLY_PROP_TOGGLE_STAT:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_MAIN_FCC_MAX:
		val->intval = chg->main_fcc_max;
		break;
	case POWER_SUPPLY_PROP_IRQ_STATUS:
		rc = smblib_get_irq_status(chg, val);
		break;
	case POWER_SUPPLY_PROP_FORCE_MAIN_FCC:
		rc = smblib_get_charge_param(chg, &chg->param.fcc,
							&val->intval);
		break;
	case POWER_SUPPLY_PROP_FORCE_MAIN_ICL:
		rc = smblib_get_charge_param(chg, &chg->param.usb_icl,
							&val->intval);
		break;
	case POWER_SUPPLY_PROP_COMP_CLAMP_LEVEL:
		val->intval = chg->comp_clamp_level;
		break;
	/* Use this property to report SMB health */
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = smblib_get_prop_smb_health(chg);
		break;
	default:
		pr_debug("get prop %d is not supported in usb-main\n", psp);
		rc = -EINVAL;
		break;
	}
	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int smb5_usb_main_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	union power_supply_propval pval = {0, };
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = smblib_set_charge_param(chg, &chg->param.fv, val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		rc = smblib_set_charge_param(chg, &chg->param.fcc, val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_set_icl_current(chg, val->intval);
		break;
	case POWER_SUPPLY_PROP_FLASH_ACTIVE:
		if ((chg->smb_version == PMI632_SUBTYPE)
				&& (chg->flash_active != val->intval)) {
			chg->flash_active = val->intval;

			rc = smblib_get_prop_usb_present(chg, &pval);
			if (rc < 0)
				pr_err("Failed to get USB preset status rc=%d\n",
						rc);
			if (pval.intval) {
				rc = smblib_force_vbus_voltage(chg,
					chg->flash_active ? FORCE_5V_BIT
								: IDLE_BIT);
				if (rc < 0)
					pr_err("Failed to force 5V\n");
				else
					chg->pulse_cnt = 0;
			} else {
				/* USB absent & flash not-active - vote 100mA */
				vote(chg->usb_icl_votable, SW_ICL_MAX_VOTER,
							true, SDP_100_MA);
			}

			pr_debug("flash active VBUS 5V restriction %s\n",
				chg->flash_active ? "applied" : "removed");

			/* Update userspace */
			if (chg->batt_psy)
				power_supply_changed(chg->batt_psy);
		}
		break;
	case POWER_SUPPLY_PROP_TOGGLE_STAT:
		rc = smblib_toggle_smb_en(chg, val->intval);
		break;
	case POWER_SUPPLY_PROP_MAIN_FCC_MAX:
		chg->main_fcc_max = val->intval;
		rerun_election(chg->fcc_votable);
		break;
	case POWER_SUPPLY_PROP_FORCE_MAIN_FCC:
		vote_override(chg->fcc_main_votable, CC_MODE_VOTER,
				(val->intval < 0) ? false : true, val->intval);
		/* Main FCC updated re-calculate FCC */
		rerun_election(chg->fcc_votable);
		break;
	case POWER_SUPPLY_PROP_FORCE_MAIN_ICL:
		vote_override(chg->usb_icl_votable, CC_MODE_VOTER,
				(val->intval < 0) ? false : true, val->intval);
		break;
	case POWER_SUPPLY_PROP_COMP_CLAMP_LEVEL:
		rc = smb5_set_prop_comp_clamp_level(chg, val);
		break;
	default:
		pr_err("set prop %d is not supported\n", psp);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int smb5_usb_main_prop_is_writeable(struct power_supply *psy,
				enum power_supply_property psp)
{
	int rc;

	switch (psp) {
	case POWER_SUPPLY_PROP_TOGGLE_STAT:
	case POWER_SUPPLY_PROP_MAIN_FCC_MAX:
	case POWER_SUPPLY_PROP_FORCE_MAIN_FCC:
	case POWER_SUPPLY_PROP_FORCE_MAIN_ICL:
	case POWER_SUPPLY_PROP_COMP_CLAMP_LEVEL:
		rc = 1;
		break;
	default:
		rc = 0;
		break;
	}

	return rc;
}

static const struct power_supply_desc usb_main_psy_desc = {
	.name		= "main",
	.type		= POWER_SUPPLY_TYPE_MAIN,
	.properties	= smb5_usb_main_props,
	.num_properties	= ARRAY_SIZE(smb5_usb_main_props),
	.get_property	= smb5_usb_main_get_prop,
	.set_property	= smb5_usb_main_set_prop,
	.property_is_writeable = smb5_usb_main_prop_is_writeable,
};

static int smb5_init_usb_main_psy(struct smb5 *chip)
{
	struct power_supply_config usb_main_cfg = {};
	struct smb_charger *chg = &chip->chg;

	usb_main_cfg.drv_data = chip;
	usb_main_cfg.of_node = chg->dev->of_node;
	chg->usb_main_psy = devm_power_supply_register(chg->dev,
						  &usb_main_psy_desc,
						  &usb_main_cfg);
	if (IS_ERR(chg->usb_main_psy)) {
		pr_err("Couldn't register USB main power supply\n");
		return PTR_ERR(chg->usb_main_psy);
	}

	return 0;
}

/*************************
 * DC PSY REGISTRATION   *
 *************************/
#ifndef VENDOR_EDIT
	/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Delete for charging*/
static enum power_supply_property smb5_dc_props[] = {
	POWER_SUPPLY_PROP_INPUT_SUSPEND,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION,
	POWER_SUPPLY_PROP_REAL_TYPE,
	POWER_SUPPLY_PROP_DC_RESET,
};

static int smb5_dc_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		val->intval = get_effective_result(chg->dc_suspend_votable);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		rc = smblib_get_prop_dc_present(chg, val);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		rc = smblib_get_prop_dc_online(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		rc = smblib_get_prop_dc_voltage_now(chg, val);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_get_prop_dc_current_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = smblib_get_prop_dc_voltage_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_REAL_TYPE:
		val->intval = POWER_SUPPLY_TYPE_WIRELESS;
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION:
		rc = smblib_get_prop_voltage_wls_output(chg, val);
		break;
	case POWER_SUPPLY_PROP_DC_RESET:
		val->intval = 0;
		break;
	default:
		return -EINVAL;
	}
	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}
	return 0;
}

static int smb5_dc_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		rc = vote(chg->dc_suspend_votable, WBC_VOTER,
				(bool)val->intval, 0);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_set_prop_dc_current_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION:
		rc = smblib_set_prop_voltage_wls_output(chg, val);
		break;
	case POWER_SUPPLY_PROP_DC_RESET:
		rc = smblib_set_prop_dc_reset(chg);
		break;
	default:
		return -EINVAL;
	}

	return rc;
}

static int smb5_dc_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION:
		return 1;
	default:
		break;
	}

	return 0;
}

static const struct power_supply_desc dc_psy_desc = {
	.name = "dc",
	.type = POWER_SUPPLY_TYPE_WIRELESS,
	.properties = smb5_dc_props,
	.num_properties = ARRAY_SIZE(smb5_dc_props),
	.get_property = smb5_dc_get_prop,
	.set_property = smb5_dc_set_prop,
	.property_is_writeable = smb5_dc_prop_is_writeable,
};

static int smb5_init_dc_psy(struct smb5 *chip)
{
	struct power_supply_config dc_cfg = {};
	struct smb_charger *chg = &chip->chg;

	dc_cfg.drv_data = chip;
	dc_cfg.of_node = chg->dev->of_node;
	chg->dc_psy = devm_power_supply_register(chg->dev,
						  &dc_psy_desc,
						  &dc_cfg);
	if (IS_ERR(chg->dc_psy)) {
		pr_err("Couldn't register USB power supply\n");
		return PTR_ERR(chg->dc_psy);
	}

	return 0;
}
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/03/07, sjc Add for charging*/
/*************************
 * AC PSY REGISTRATION *
 *************************/
 static enum power_supply_property ac_props[] = {
/*oppo own ac props*/
        POWER_SUPPLY_PROP_ONLINE,
};

static int smb5_ac_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	int rc = 0;

    rc = oppo_ac_get_property(psy, psp, val);

	return rc;
}

static const struct power_supply_desc ac_psy_desc = {
	.name = "ac",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = ac_props,
	.num_properties = ARRAY_SIZE(ac_props),
	.get_property = smb5_ac_get_property,
};

static int smb5_init_ac_psy(struct smb5 *chip)
{
	struct power_supply_config ac_cfg = {};
	struct smb_charger *chg = &chip->chg;

	ac_cfg.drv_data = chip;
	ac_cfg.of_node = chg->dev->of_node;
	chg->ac_psy = devm_power_supply_register(chg->dev,
						  &ac_psy_desc,
						  &ac_cfg);
	if (IS_ERR(chg->ac_psy)) {
		pr_err("Couldn't register AC power supply\n");
		return PTR_ERR(chg->ac_psy);
	}

	return 0;
}
#endif /* VENDOR_EDIT */

/*************************
 * BATT PSY REGISTRATION *
 *************************/
static enum power_supply_property smb5_batt_props[] = {
	POWER_SUPPLY_PROP_INPUT_SUSPEND,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CHARGER_TEMP,
	POWER_SUPPLY_PROP_CHARGER_TEMP_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_QNOVO,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_QNOVO,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_SW_JEITA_ENABLED,
	POWER_SUPPLY_PROP_CHARGE_DONE,
	POWER_SUPPLY_PROP_PARALLEL_DISABLE,
	POWER_SUPPLY_PROP_SET_SHIP_MODE,
	POWER_SUPPLY_PROP_DIE_HEALTH,
	POWER_SUPPLY_PROP_RERUN_AICL,
	POWER_SUPPLY_PROP_DP_DM,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_RECHARGE_SOC,
	POWER_SUPPLY_PROP_CHARGE_FULL,
#ifdef CONFIG_OPPO_CHIP_SOC_NODE
	POWER_SUPPLY_PROP_CHIP_SOC,
#endif
	POWER_SUPPLY_PROP_FORCE_RECHARGE,
	POWER_SUPPLY_PROP_FCC_STEPPER_ENABLE,
#ifdef VENDOR_EDIT
    /*oppo own battery props*/
        POWER_SUPPLY_PROP_VOLTAGE_MIN,
    
        POWER_SUPPLY_PROP_CHARGE_NOW,
        POWER_SUPPLY_PROP_AUTHENTICATE,
        POWER_SUPPLY_PROP_CHARGE_TIMEOUT,
        POWER_SUPPLY_PROP_CHARGE_TECHNOLOGY,
        POWER_SUPPLY_PROP_FAST_CHARGE,
        POWER_SUPPLY_PROP_MMI_CHARGING_ENABLE,
        POWER_SUPPLY_PROP_BATTERY_FCC,
        POWER_SUPPLY_PROP_BATTERY_SOH,
        POWER_SUPPLY_PROP_BATTERY_CC,
        POWER_SUPPLY_PROP_BATTERY_RM,
        POWER_SUPPLY_PROP_BATTERY_NOTIFY_CODE,
#ifdef CONFIG_OPPO_SMART_CHARGER_SUPPORT
        POWER_SUPPLY_PROP_COOL_DOWN,
#endif
        POWER_SUPPLY_PROP_ADAPTER_FW_UPDATE,
        POWER_SUPPLY_PROP_VOOCCHG_ING,
    
    /*CTS*/
        POWER_SUPPLY_PROP_CURRENT_MAX,
    
#ifdef CONFIG_OPPO_CHECK_CHARGERID_VOLT
        POWER_SUPPLY_PROP_CHARGERID_VOLT,
#endif
#ifdef CONFIG_OPPO_SHIP_MODE_SUPPORT
        POWER_SUPPLY_PROP_SHIP_MODE,
#endif
#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
#ifdef CONFIG_OPPO_SHORT_USERSPACE
        POWER_SUPPLY_PROP_SHORT_C_LIMIT_CHG,
        POWER_SUPPLY_PROP_SHORT_C_LIMIT_RECHG,
        POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
        POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
#else
        POWER_SUPPLY_PROP_SHORT_C_BATT_UPDATE_CHANGE,
        POWER_SUPPLY_PROP_SHORT_C_BATT_IN_IDLE,
        POWER_SUPPLY_PROP_SHORT_C_BATT_CV_STATUS,
#endif//CONFIG_OPPO_SHORT_USERSPACE
#endif
#ifdef CONFIG_OPPO_SHORT_HW_CHECK
        POWER_SUPPLY_PROP_SHORT_C_HW_FEATURE,
        POWER_SUPPLY_PROP_SHORT_C_HW_STATUS,
#endif
#ifdef CONFIG_OPPO_SHORT_IC_CHECK
        POWER_SUPPLY_PROP_SHORT_C_IC_OTP_STATUS,
        POWER_SUPPLY_PROP_SHORT_C_IC_VOLT_THRESH,
        POWER_SUPPLY_PROP_SHORT_C_IC_OTP_VALUE,
#endif
#endif//VENDOR_EDIT
};

static int smb5_batt_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb_charger *chg = power_supply_get_drvdata(psy);
	int rc = 0;

	switch (psp) {
#ifndef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2018/11/07,  Delete for charging */
	case POWER_SUPPLY_PROP_STATUS:
		rc = smblib_get_prop_batt_status(chg, val);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		rc = smblib_get_prop_batt_health(chg, val);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		rc = smblib_get_prop_batt_present(chg, val);
		break;
#endif
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		rc = smblib_get_prop_input_suspend(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		rc = smblib_get_prop_batt_charge_type(chg, val);
		break;
#ifndef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2018/11/07,  Delete for charging */
	case POWER_SUPPLY_PROP_CAPACITY:
		rc = smblib_get_prop_batt_capacity(chg, val);
		break;
#endif
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		rc = smblib_get_prop_system_temp_level(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		rc = smblib_get_prop_system_temp_level_max(chg, val);
		break;
#ifndef VENDOR_EDIT
    /* Jianchao.Shi@BSP.CHG.Basic, 2017/03/07, sjc Modify for charging */
    /* CHARGER_TEMP and CHARGER_TEMP_MAX is dependent on FG and only for HVDCP */
	case POWER_SUPPLY_PROP_CHARGER_TEMP:
		rc = smblib_get_prop_charger_temp(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGER_TEMP_MAX:
		val->intval = chg->charger_temp_max;
		break;
#else
        case POWER_SUPPLY_PROP_CHARGER_TEMP:
        case POWER_SUPPLY_PROP_CHARGER_TEMP_MAX:
            val->intval = -1;
            break;
#endif
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED:
		rc = smblib_get_prop_input_current_limited(chg, val);
		break;
	case POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED:
		val->intval = chg->step_chg_enabled;
		break;
	case POWER_SUPPLY_PROP_SW_JEITA_ENABLED:
		val->intval = chg->sw_jeita_enabled;
		break;
#ifndef VENDOR_EDIT
		/* tongfeng.Huang@BSP.CHG.Basic, 2018/11/07,  Delete for charging */
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		rc = smblib_get_prop_from_bms(chg,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, val);
		break;
#endif
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = get_client_vote(chg->fv_votable,
				BATT_PROFILE_VOTER);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_QNOVO:
		val->intval = get_client_vote_locked(chg->fv_votable,
				QNOVO_VOTER);
		break;
#ifndef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2018/11/07,  Delete for charging */
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		rc = smblib_get_batt_current_now(chg, val);
		break;
#endif
	case POWER_SUPPLY_PROP_CURRENT_QNOVO:
		val->intval = get_client_vote_locked(chg->fcc_votable,
				QNOVO_VOTER);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = get_client_vote(chg->fcc_votable,
					      BATT_PROFILE_VOTER);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		val->intval = get_effective_result(chg->fcc_votable);
		break;
#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2018/11/07,  Delete for charging */
#ifndef CONFIG_OPPO_SHORT_C_BATT_CHECK
#ifndef CONFIG_OPPO_SHORT_USERSPACE
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		rc = smblib_get_prop_batt_iterm(chg, val);
		break;
#endif
#endif
#endif
#ifndef VENDOR_EDIT
	/* tongfeng.Huang@BSP.CHG.Basic, 2018/11/07,  Delete for charging */
	case POWER_SUPPLY_PROP_TEMP:
		rc = smblib_get_prop_from_bms(chg, POWER_SUPPLY_PROP_TEMP, val);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
#endif
	case POWER_SUPPLY_PROP_CHARGE_DONE:
		rc = smblib_get_prop_batt_charge_done(chg, val);
		break;
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
		val->intval = get_client_vote(chg->pl_disable_votable,
					      USER_VOTER);
		break;
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		/* Not in ship mode as long as device is active */
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_DIE_HEALTH:
		rc = smblib_get_die_health(chg, val);
		break;
	case POWER_SUPPLY_PROP_DP_DM:
		val->intval = chg->pulse_cnt;
		break;
	case POWER_SUPPLY_PROP_RERUN_AICL:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		rc = smblib_get_prop_from_bms(chg,
				POWER_SUPPLY_PROP_CHARGE_COUNTER, val);
#ifdef VENDOR_EDIT
/* tongfeng.Huang@BSP.CHG.Basic, 2019/02/02,   Add for cts */
		if (g_oppo_chip) {
			val->intval = g_oppo_chip->ui_soc * g_oppo_chip->batt_capacity_mah * 1000 / 100;
		}
#endif
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		rc = smblib_get_prop_from_bms(chg,
				POWER_SUPPLY_PROP_CYCLE_COUNT, val);
		break;
	case POWER_SUPPLY_PROP_RECHARGE_SOC:
		val->intval = chg->auto_recharge_soc;
		break;
	case POWER_SUPPLY_PROP_CHARGE_QNOVO_ENABLE:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		rc = smblib_get_prop_from_bms(chg,
				POWER_SUPPLY_PROP_CHARGE_FULL, val);
		break;
	case POWER_SUPPLY_PROP_FORCE_RECHARGE:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_FCC_STEPPER_ENABLE:
		val->intval = chg->fcc_stepper_enable;
		break;
#ifdef VENDOR_EDIT
    /* Jianchao.Shi@PSW.BSP.CHG.Basic, 2018/04/19, sjc Add for CTS */
        case POWER_SUPPLY_PROP_CURRENT_MAX:
            rc = smblib_get_prop_input_current_settled(chg, val);
            break;
#endif
	default:
//#ifdef VENDOR_EDIT
    /*oppo own battery props*/
            rc = oppo_battery_get_property(psy, psp, val);
//#else
		//pr_err("batt power supply prop %d not supported\n", psp);
		///return -EINVAL;
//endif
	}

	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int smb5_batt_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *val)
{
	int rc = 0;
	struct smb_charger *chg = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_STATUS:
		rc = smblib_set_prop_batt_status(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		rc = smblib_set_prop_input_suspend(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		rc = smblib_set_prop_system_temp_level(chg, val);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		rc = smblib_set_prop_batt_capacity(chg, val);
		break;
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
		vote(chg->pl_disable_votable, USER_VOTER, (bool)val->intval, 0);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		chg->batt_profile_fv_uv = val->intval;
		vote(chg->fv_votable, BATT_PROFILE_VOTER, true, val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_QNOVO:
		if (val->intval == -EINVAL) {
			vote(chg->fv_votable, BATT_PROFILE_VOTER, true,
					chg->batt_profile_fv_uv);
			vote(chg->fv_votable, QNOVO_VOTER, false, 0);
		} else {
			vote(chg->fv_votable, QNOVO_VOTER, true, val->intval);
			vote(chg->fv_votable, BATT_PROFILE_VOTER, false, 0);
		}
		break;
	case POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED:
		chg->step_chg_enabled = !!val->intval;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		chg->batt_profile_fcc_ua = val->intval;
		vote(chg->fcc_votable, BATT_PROFILE_VOTER, true, val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_QNOVO:
		vote(chg->pl_disable_votable, PL_QNOVO_VOTER,
			val->intval != -EINVAL && val->intval < 2000000, 0);
		if (val->intval == -EINVAL) {
			vote(chg->fcc_votable, BATT_PROFILE_VOTER,
					true, chg->batt_profile_fcc_ua);
			vote(chg->fcc_votable, QNOVO_VOTER, false, 0);
		} else {
			vote(chg->fcc_votable, QNOVO_VOTER, true, val->intval);
			vote(chg->fcc_votable, BATT_PROFILE_VOTER, false, 0);
		}
		break;
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		/* Not in ship mode as long as the device is active */
		if (!val->intval)
			break;
		if (chg->pl.psy)
			power_supply_set_property(chg->pl.psy,
				POWER_SUPPLY_PROP_SET_SHIP_MODE, val);
		rc = smblib_set_prop_ship_mode(chg, val);
		break;
	case POWER_SUPPLY_PROP_RERUN_AICL:
		rc = smblib_run_aicl(chg, RERUN_AICL);
		break;
	case POWER_SUPPLY_PROP_DP_DM:
		if (!chg->flash_active)
			rc = smblib_dp_dm(chg, val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED:
		rc = smblib_set_prop_input_current_limited(chg, val);
		break;
	case POWER_SUPPLY_PROP_DIE_HEALTH:
		chg->die_health = val->intval;
		power_supply_changed(chg->batt_psy);
		break;
	case POWER_SUPPLY_PROP_RECHARGE_SOC:
		rc = smblib_set_prop_rechg_soc_thresh(chg, val);
		break;
	case POWER_SUPPLY_PROP_FORCE_RECHARGE:
			/* toggle charging to force recharge */
			vote(chg->chg_disable_votable, FORCE_RECHARGE_VOTER,
					true, 0);
			/* charge disable delay */
			msleep(50);
			vote(chg->chg_disable_votable, FORCE_RECHARGE_VOTER,
					false, 0);
		break;
	case POWER_SUPPLY_PROP_FCC_STEPPER_ENABLE:
		chg->fcc_stepper_enable = val->intval;
		break;
	default:
#ifdef VENDOR_EDIT
    /*oppo own battery props*/
    rc = oppo_battery_set_property(psy, prop, val);
#else
		rc = -EINVAL;
#endif
	}

	return rc;
}

static int smb5_batt_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
#ifdef VENDOR_EDIT
        /*oppo own battery props*/
    int rc = 0;
#endif
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
	case POWER_SUPPLY_PROP_DP_DM:
	case POWER_SUPPLY_PROP_RERUN_AICL:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED:
	case POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_DIE_HEALTH:
		return 1;
	default:
#ifdef VENDOR_EDIT
    /*oppo own battery props*/
            rc = oppo_battery_property_is_writeable(psy, psp);
            return rc;
#endif
		break;
	}

	return 0;
}

static const struct power_supply_desc batt_psy_desc = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = smb5_batt_props,
	.num_properties = ARRAY_SIZE(smb5_batt_props),
	.get_property = smb5_batt_get_prop,
	.set_property = smb5_batt_set_prop,
	.property_is_writeable = smb5_batt_prop_is_writeable,
};

static int smb5_init_batt_psy(struct smb5 *chip)
{
	struct power_supply_config batt_cfg = {};
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	batt_cfg.drv_data = chg;
	batt_cfg.of_node = chg->dev->of_node;
	chg->batt_psy = devm_power_supply_register(chg->dev,
					   &batt_psy_desc,
					   &batt_cfg);
	if (IS_ERR(chg->batt_psy)) {
		pr_err("Couldn't register battery power supply\n");
		return PTR_ERR(chg->batt_psy);
	}

	return rc;
}
#ifdef VENDOR_EDIT
/*oppo own battery props*/
static int oppo_power_supply_init(struct smb5 *chip)
{
    int rc = 0;

    rc = smb5_init_ac_psy(chip);
    if (rc < 0) {
        pr_err("Couldn't initialize ac psy rc=%d\n", rc);
        return rc;
    }

//kong
    rc = smb5_init_batt_psy(chip);
    if (rc < 0) {
        pr_err("Couldn't initialize batt psy rc=%d\n", rc);
        return rc;
    }

//kong
    rc = smb5_init_usb_psy(chip);
    if (rc < 0) {
        pr_err("Couldn't initialize usb psy rc=%d\n", rc);
       return rc;
    }

    return rc;
}
#endif /* VENDOR_EDIT */
/******************************
 * VBUS REGULATOR REGISTRATION *
 ******************************/

static struct regulator_ops smb5_vbus_reg_ops = {
	.enable = smblib_vbus_regulator_enable,
	.disable = smblib_vbus_regulator_disable,
	.is_enabled = smblib_vbus_regulator_is_enabled,
};

static int smb5_init_vbus_regulator(struct smb5 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct regulator_config cfg = {};
	int rc = 0;

	chg->vbus_vreg = devm_kzalloc(chg->dev, sizeof(*chg->vbus_vreg),
				      GFP_KERNEL);
	if (!chg->vbus_vreg)
		return -ENOMEM;

	cfg.dev = chg->dev;
	cfg.driver_data = chip;

	chg->vbus_vreg->rdesc.owner = THIS_MODULE;
	chg->vbus_vreg->rdesc.type = REGULATOR_VOLTAGE;
	chg->vbus_vreg->rdesc.ops = &smb5_vbus_reg_ops;
	chg->vbus_vreg->rdesc.of_match = "qcom,smb5-vbus";
	chg->vbus_vreg->rdesc.name = "qcom,smb5-vbus";

	chg->vbus_vreg->rdev = devm_regulator_register(chg->dev,
						&chg->vbus_vreg->rdesc, &cfg);
	if (IS_ERR(chg->vbus_vreg->rdev)) {
		rc = PTR_ERR(chg->vbus_vreg->rdev);
		chg->vbus_vreg->rdev = NULL;
		if (rc != -EPROBE_DEFER)
			pr_err("Couldn't register VBUS regulator rc=%d\n", rc);
	}

	return rc;
}

/******************************
 * VCONN REGULATOR REGISTRATION *
 ******************************/

static struct regulator_ops smb5_vconn_reg_ops = {
	.enable = smblib_vconn_regulator_enable,
	.disable = smblib_vconn_regulator_disable,
	.is_enabled = smblib_vconn_regulator_is_enabled,
};

static int smb5_init_vconn_regulator(struct smb5 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct regulator_config cfg = {};
	int rc = 0;

	if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB)
		return 0;

	chg->vconn_vreg = devm_kzalloc(chg->dev, sizeof(*chg->vconn_vreg),
				      GFP_KERNEL);
	if (!chg->vconn_vreg)
		return -ENOMEM;

	cfg.dev = chg->dev;
	cfg.driver_data = chip;

	chg->vconn_vreg->rdesc.owner = THIS_MODULE;
	chg->vconn_vreg->rdesc.type = REGULATOR_VOLTAGE;
	chg->vconn_vreg->rdesc.ops = &smb5_vconn_reg_ops;
	chg->vconn_vreg->rdesc.of_match = "qcom,smb5-vconn";
	chg->vconn_vreg->rdesc.name = "qcom,smb5-vconn";

	chg->vconn_vreg->rdev = devm_regulator_register(chg->dev,
						&chg->vconn_vreg->rdesc, &cfg);
	if (IS_ERR(chg->vconn_vreg->rdev)) {
		rc = PTR_ERR(chg->vconn_vreg->rdev);
		chg->vconn_vreg->rdev = NULL;
		if (rc != -EPROBE_DEFER)
			pr_err("Couldn't register VCONN regulator rc=%d\n", rc);
	}

	return rc;
}

/***************************
 * HARDWARE INITIALIZATION *
 ***************************/
static int smb5_configure_typec(struct smb_charger *chg)
{
	union power_supply_propval pval = {0, };
	int rc;
	u8 val = 0;

	rc = smblib_read(chg, LEGACY_CABLE_STATUS_REG, &val);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't read Legacy status rc=%d\n", rc);
		return rc;
	}

	/*
	 * Across reboot, standard typeC cables get detected as legacy cables
	 * due to VBUS attachment prior to CC attach/dettach. To handle this,
	 * "early_usb_attach" flag is used, which assumes that across reboot,
	 * the cable connected can be standard typeC. However, its jurisdiction
	 * is limited to PD capable designs only. Hence, for non-PD type designs
	 * reset legacy cable detection by disabling/enabling typeC mode.
	 */
	if (chg->pd_not_supported && (val & TYPEC_LEGACY_CABLE_STATUS_BIT)) {
		pval.intval = POWER_SUPPLY_TYPEC_PR_NONE;
		smblib_set_prop_typec_power_role(chg, &pval);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't disable TYPEC rc=%d\n", rc);
			return rc;
		}

		/* delay before enabling typeC */
		msleep(50);

		pval.intval = POWER_SUPPLY_TYPEC_PR_DUAL;
		smblib_set_prop_typec_power_role(chg, &pval);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't enable TYPEC rc=%d\n", rc);
			return rc;
		}
	}

	smblib_apsd_enable(chg, true);

	rc = smblib_masked_write(chg, TYPE_C_CFG_REG,
				BC1P2_START_ON_CC_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "failed to write TYPE_C_CFG_REG rc=%d\n",
				rc);

		return rc;
	}

	/* Use simple write to clear interrupts */
	rc = smblib_write(chg, TYPE_C_INTERRUPT_EN_CFG_1_REG, 0);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure Type-C interrupts rc=%d\n", rc);
		return rc;
	}

	val = chg->lpd_disabled ? 0 : TYPEC_WATER_DETECTION_INT_EN_BIT;
	/* Use simple write to enable only required interrupts */
	rc = smblib_write(chg, TYPE_C_INTERRUPT_EN_CFG_2_REG,
				TYPEC_SRC_BATT_HPWR_INT_EN_BIT | val);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure Type-C interrupts rc=%d\n", rc);
		return rc;
	}

	/* enable try.snk and clear force sink for DRP mode */
#ifndef VENDOR_EDIT
	/* kun.zhang@BSP.CHG.Basic, 2019/10/28,  Add for charging */
	rc = smblib_masked_write(chg, TYPE_C_MODE_CFG_REG,
				EN_TRY_SNK_BIT | EN_SNK_ONLY_BIT,
				EN_TRY_SNK_BIT);
#else
rc = smblib_masked_write(chg, TYPE_C_MODE_CFG_REG,
			TYPEC_POWER_ROLE_CMD_MASK | TYPEC_TRY_MODE_MASK,
			EN_SNK_ONLY_BIT);
#endif
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure TYPE_C_MODE_CFG_REG rc=%d\n",
				rc);
		return rc;
	}
	chg->typec_try_mode |= EN_TRY_SNK_BIT;

	/* For PD capable targets configure VCONN for software control */
	if (!chg->pd_not_supported) {
		rc = smblib_masked_write(chg, TYPE_C_VCONN_CONTROL_REG,
				 VCONN_EN_SRC_BIT | VCONN_EN_VALUE_BIT,
				 VCONN_EN_SRC_BIT);
		if (rc < 0) {
			dev_err(chg->dev,
				"Couldn't configure VCONN for SW control rc=%d\n",
				rc);
			return rc;
		}
	}

	/* Enable detection of unoriented debug accessory in source mode */
	rc = smblib_masked_write(chg, DEBUG_ACCESS_SRC_CFG_REG,
				 EN_UNORIENTED_DEBUG_ACCESS_SRC_BIT,
				 EN_UNORIENTED_DEBUG_ACCESS_SRC_BIT);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure TYPE_C_DEBUG_ACCESS_SRC_CFG_REG rc=%d\n",
				rc);
		return rc;
	}

	if (chg->smb_version != PMI632_SUBTYPE) {
		rc = smblib_masked_write(chg, USBIN_LOAD_CFG_REG,
				USBIN_IN_COLLAPSE_GF_SEL_MASK |
				USBIN_AICL_STEP_TIMING_SEL_MASK,
				0);
		if (rc < 0) {
			dev_err(chg->dev,
				"Couldn't set USBIN_LOAD_CFG_REG rc=%d\n", rc);
			return rc;
		}
	}

	/* Set CC threshold to 1.6 V in source mode */
	rc = smblib_masked_write(chg, TYPE_C_EXIT_STATE_CFG_REG,
				SEL_SRC_UPPER_REF_BIT, SEL_SRC_UPPER_REF_BIT);
	if (rc < 0)
		dev_err(chg->dev,
			"Couldn't configure CC threshold voltage rc=%d\n", rc);
#ifdef VENDOR_EDIT
        /* zhangkun.PSW.BSP.CHG  2019-04-27  increase OTG_CURRENT_LIMIT to recognize 500G Seagate disk */
                //smblib_write(chg, 0x1152, 0x02);
	rc = smblib_masked_write(chg, DCDC_OTG_CURRENT_LIMIT_CFG_REG,
		DCDC_OTG_CURRENT_LIMIT_1000MA_BIT, DCDC_OTG_CURRENT_LIMIT_1000MA_BIT);
	if (rc < 0)
		dev_err(chg->dev,
			"Couldn't DCDC_OTG_CURRENT_LIMIT_CFG_REG rc=%d\n", rc);
#endif

#ifdef VENDOR_EDIT
    /* zhangkun.PSW.BSP.CHG  2019-04-27  reduce DCD time */
	rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG, DCD_TIMEOUT_SEL_BIT, 0);
	if (rc < 0)
		dev_err(chg->dev,
			"Couldn't DCDC_OTG_CURRENT_LIMIT_CFG_REG rc=%d\n", rc);
#endif

	return rc;
}

static int smb5_configure_micro_usb(struct smb_charger *chg)
{
	int rc;

	/* For micro USB connector, use extcon by default */
	chg->use_extcon = true;
	chg->pd_not_supported = true;

	rc = smblib_masked_write(chg, TYPE_C_INTERRUPT_EN_CFG_2_REG,
					MICRO_USB_STATE_CHANGE_INT_EN_BIT,
					MICRO_USB_STATE_CHANGE_INT_EN_BIT);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure Type-C interrupts rc=%d\n", rc);
		return rc;
	}

	if (chg->uusb_moisture_protection_enabled) {
		/* Enable moisture detection interrupt */
		rc = smblib_masked_write(chg, TYPE_C_INTERRUPT_EN_CFG_2_REG,
				TYPEC_WATER_DETECTION_INT_EN_BIT,
				TYPEC_WATER_DETECTION_INT_EN_BIT);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't enable moisture detection interrupt rc=%d\n",
				rc);
			return rc;
		}

		/* Enable uUSB factory mode */
		rc = smblib_masked_write(chg, TYPEC_U_USB_CFG_REG,
					EN_MICRO_USB_FACTORY_MODE_BIT,
					EN_MICRO_USB_FACTORY_MODE_BIT);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't enable uUSB factory mode c=%d\n",
				rc);
			return rc;
		}

		/* Disable periodic monitoring of CC_ID pin */
		rc = smblib_write(chg, ((chg->smb_version == PMI632_SUBTYPE) ?
			PMI632_TYPEC_U_USB_WATER_PROTECTION_CFG_REG :
			TYPEC_U_USB_WATER_PROTECTION_CFG_REG), 0);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't disable periodic monitoring of CC_ID rc=%d\n",
				rc);
			return rc;
		}
	}

	/* Enable HVDCP detection and authentication */
	if (!chg->hvdcp_disable)
		smblib_hvdcp_detect_enable(chg, true);

	return rc;
}

#define RAW_ITERM(iterm_ma, max_range)				\
		div_s64((int64_t)iterm_ma * ADC_CHG_ITERM_MASK, max_range)
static int smb5_configure_iterm_thresholds_adc(struct smb5 *chip)
{
	u8 *buf;
	int rc = 0;
	s16 raw_hi_thresh, raw_lo_thresh, max_limit_ma;
	struct smb_charger *chg = &chip->chg;

	if (chip->chg.smb_version == PMI632_SUBTYPE)
		max_limit_ma = ITERM_LIMITS_PMI632_MA;
	else
		max_limit_ma = ITERM_LIMITS_PM8150B_MA;

	if (chip->dt.term_current_thresh_hi_ma < (-1 * max_limit_ma)
		|| chip->dt.term_current_thresh_hi_ma > max_limit_ma
		|| chip->dt.term_current_thresh_lo_ma < (-1 * max_limit_ma)
		|| chip->dt.term_current_thresh_lo_ma > max_limit_ma) {
		dev_err(chg->dev, "ITERM threshold out of range rc=%d\n", rc);
		return -EINVAL;
	}

	/*
	 * Conversion:
	 *	raw (A) = (term_current * ADC_CHG_ITERM_MASK) / max_limit_ma
	 * Note: raw needs to be converted to big-endian format.
	 */

	if (chip->dt.term_current_thresh_hi_ma) {
		raw_hi_thresh = RAW_ITERM(chip->dt.term_current_thresh_hi_ma,
					max_limit_ma);
		raw_hi_thresh = sign_extend32(raw_hi_thresh, 15);
		buf = (u8 *)&raw_hi_thresh;
		raw_hi_thresh = buf[1] | (buf[0] << 8);

		rc = smblib_batch_write(chg, CHGR_ADC_ITERM_UP_THD_MSB_REG,
				(u8 *)&raw_hi_thresh, 2);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure ITERM threshold HIGH rc=%d\n",
					rc);
			return rc;
		}
	}

	if (chip->dt.term_current_thresh_lo_ma) {
		raw_lo_thresh = RAW_ITERM(chip->dt.term_current_thresh_lo_ma,
					max_limit_ma);
		raw_lo_thresh = sign_extend32(raw_lo_thresh, 15);
		buf = (u8 *)&raw_lo_thresh;
		raw_lo_thresh = buf[1] | (buf[0] << 8);

		rc = smblib_batch_write(chg, CHGR_ADC_ITERM_LO_THD_MSB_REG,
				(u8 *)&raw_lo_thresh, 2);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure ITERM threshold LOW rc=%d\n",
					rc);
			return rc;
		}
	}

	return rc;
}

static int smb5_configure_iterm_thresholds(struct smb5 *chip)
{
	int rc = 0;
	struct smb_charger *chg = &chip->chg;

	switch (chip->dt.term_current_src) {
	case ITERM_SRC_ADC:
		if (chip->chg.smb_version == PM8150B_SUBTYPE) {
			rc = smblib_masked_write(chg, CHGR_ADC_TERM_CFG_REG,
					TERM_BASED_ON_SYNC_CONV_OR_SAMPLE_CNT,
					TERM_BASED_ON_SAMPLE_CNT);
			if (rc < 0) {
				dev_err(chg->dev, "Couldn't configure ADC_ITERM_CFG rc=%d\n",
						rc);
				return rc;
			}
		}
		rc = smb5_configure_iterm_thresholds_adc(chip);
		break;
	default:
		break;
	}

	return rc;
}

static int smb5_configure_mitigation(struct smb_charger *chg)
{
	int rc;
	u8 chan = 0, src_cfg = 0;

	if (!chg->hw_die_temp_mitigation && !chg->hw_connector_mitigation &&
			!chg->hw_skin_temp_mitigation) {
		src_cfg = THERMREG_SW_ICL_ADJUST_BIT;
	} else {
		if (chg->hw_die_temp_mitigation) {
			chan = DIE_TEMP_CHANNEL_EN_BIT;
			src_cfg = THERMREG_DIE_ADC_SRC_EN_BIT
				| THERMREG_DIE_CMP_SRC_EN_BIT;
		}

		if (chg->hw_connector_mitigation) {
			chan |= CONN_THM_CHANNEL_EN_BIT;
			src_cfg |= THERMREG_CONNECTOR_ADC_SRC_EN_BIT;
		}

		if (chg->hw_skin_temp_mitigation) {
			chan |= MISC_THM_CHANNEL_EN_BIT;
			src_cfg |= THERMREG_SKIN_ADC_SRC_EN_BIT;
		}

		rc = smblib_masked_write(chg, BATIF_ADC_CHANNEL_EN_REG,
			CONN_THM_CHANNEL_EN_BIT | DIE_TEMP_CHANNEL_EN_BIT |
			MISC_THM_CHANNEL_EN_BIT, chan);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't enable ADC channel rc=%d\n",
				rc);
			return rc;
		}
	}

	rc = smblib_masked_write(chg, MISC_THERMREG_SRC_CFG_REG,
		THERMREG_SW_ICL_ADJUST_BIT | THERMREG_DIE_ADC_SRC_EN_BIT |
		THERMREG_DIE_CMP_SRC_EN_BIT | THERMREG_SKIN_ADC_SRC_EN_BIT |
		SKIN_ADC_CFG_BIT | THERMREG_CONNECTOR_ADC_SRC_EN_BIT, src_cfg);
	if (rc < 0) {
		dev_err(chg->dev,
				"Couldn't configure THERM_SRC reg rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int smb5_init_dc_peripheral(struct smb_charger *chg)
{
	int rc = 0;

	/* PMI632 does not have DC peripheral */
	if (chg->smb_version == PMI632_SUBTYPE)
		return 0;

	/* Set DCIN ICL to 100 mA */
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl, DCIN_ICL_MIN_UA);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set dc_icl rc=%d\n", rc);
		return rc;
	}

	/* Disable DC Input missing poller function */
	rc = smblib_masked_write(chg, DCIN_LOAD_CFG_REG,
					INPUT_MISS_POLL_EN_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't disable DC Input missing poller rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int smb5_configure_recharging(struct smb5 *chip)
{
	int rc = 0;
	struct smb_charger *chg = &chip->chg;
	union power_supply_propval pval;
	/* Configure VBATT-based or automatic recharging */

	rc = smblib_masked_write(chg, CHGR_CFG2_REG, RECHG_MASK,
				(chip->dt.auto_recharge_vbat_mv != -EINVAL) ?
				VBAT_BASED_RECHG_BIT : 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure VBAT-rechg CHG_CFG2_REG rc=%d\n",
			rc);
		return rc;
	}

	/* program the auto-recharge VBAT threshold */
	if (chip->dt.auto_recharge_vbat_mv != -EINVAL) {
		u32 temp = VBAT_TO_VRAW_ADC(chip->dt.auto_recharge_vbat_mv);

		temp = ((temp & 0xFF00) >> 8) | ((temp & 0xFF) << 8);
		rc = smblib_batch_write(chg,
			CHGR_ADC_RECHARGE_THRESHOLD_MSB_REG, (u8 *)&temp, 2);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure ADC_RECHARGE_THRESHOLD REG rc=%d\n",
				rc);
			return rc;
		}
		/* Program the sample count for VBAT based recharge to 3 */
		rc = smblib_masked_write(chg, CHGR_NO_SAMPLE_TERM_RCHG_CFG_REG,
					NO_OF_SAMPLE_FOR_RCHG,
					2 << NO_OF_SAMPLE_FOR_RCHG_SHIFT);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure CHGR_NO_SAMPLE_FOR_TERM_RCHG_CFG rc=%d\n",
				rc);
			return rc;
		}
	}

	rc = smblib_masked_write(chg, CHGR_CFG2_REG, RECHG_MASK,
				(chip->dt.auto_recharge_soc != -EINVAL) ?
				SOC_BASED_RECHG_BIT : VBAT_BASED_RECHG_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure SOC-rechg CHG_CFG2_REG rc=%d\n",
			rc);
		return rc;
	}

	/* program the auto-recharge threshold */
	if (chip->dt.auto_recharge_soc != -EINVAL) {
		pval.intval = chip->dt.auto_recharge_soc;
		rc = smblib_set_prop_rechg_soc_thresh(chg, &pval);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure CHG_RCHG_SOC_REG rc=%d\n",
					rc);
			return rc;
		}

		/* Program the sample count for SOC based recharge to 1 */
		rc = smblib_masked_write(chg, CHGR_NO_SAMPLE_TERM_RCHG_CFG_REG,
						NO_OF_SAMPLE_FOR_RCHG, 0);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure CHGR_NO_SAMPLE_FOR_TERM_RCHG_CFG rc=%d\n",
				rc);
			return rc;
		}
	}

	return 0;
}

static int smb5_configure_float_charger(struct smb5 *chip)
{
	int rc = 0;
	u8 val = 0;
	struct smb_charger *chg = &chip->chg;

	/* configure float charger options */
	switch (chip->dt.float_option) {
	case FLOAT_SDP:
		val = FORCE_FLOAT_SDP_CFG_BIT;
		break;
	case DISABLE_CHARGING:
		val = FLOAT_DIS_CHGING_CFG_BIT;
		break;
	case SUSPEND_INPUT:
		val = SUSPEND_FLOAT_CFG_BIT;
		break;
	case FLOAT_DCP:
	default:
		val = 0;
		break;
	}

	chg->float_cfg = val;
	/* Update float charger setting and set DCD timeout 300ms */
	rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
				FLOAT_OPTIONS_MASK | DCD_TIMEOUT_SEL_BIT, val);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't change float charger setting rc=%d\n",
			rc);
		return rc;
	}

	return 0;
}

static int smb5_init_connector_type(struct smb_charger *chg)
{
	int rc, type = 0;
	u8 val = 0;

	/*
	 * PMI632 can have the connector type defined by a dedicated register
	 * PMI632_TYPEC_MICRO_USB_MODE_REG or by a common TYPEC_U_USB_CFG_REG.
	 */
	if (chg->smb_version == PMI632_SUBTYPE) {
		rc = smblib_read(chg, PMI632_TYPEC_MICRO_USB_MODE_REG, &val);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't read USB mode rc=%d\n", rc);
			return rc;
		}
		type = !!(val & MICRO_USB_MODE_ONLY_BIT);
	}

	/*
	 * If PMI632_TYPEC_MICRO_USB_MODE_REG is not set and for all non-PMI632
	 * check the connector type using TYPEC_U_USB_CFG_REG.
	 */
	if (!type) {
		rc = smblib_read(chg, TYPEC_U_USB_CFG_REG, &val);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't read U_USB config rc=%d\n",
					rc);
			return rc;
		}

		type = !!(val & EN_MICRO_USB_MODE_BIT);
	}

	pr_debug("Connector type=%s\n", type ? "Micro USB" : "TypeC");

	if (type) {
		chg->connector_type = POWER_SUPPLY_CONNECTOR_MICRO_USB;
		rc = smb5_configure_micro_usb(chg);
	} else {
		chg->connector_type = POWER_SUPPLY_CONNECTOR_TYPEC;
		rc = smb5_configure_typec(chg);
	}
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure TypeC/micro-USB mode rc=%d\n", rc);
		return rc;
	}

	/*
	 * PMI632 based hw init:
	 * - Rerun APSD to ensure proper charger detection if device
	 *   boots with charger connected.
	 * - Initialize flash module for PMI632
	 */
	if (chg->smb_version == PMI632_SUBTYPE) {
		schgm_flash_init(chg);
		smblib_rerun_apsd_if_required(chg);
	}

	return 0;

}

static int smb5_init_hw(struct smb5 *chip)
{
	struct smb_charger *chg = &chip->chg;
	int rc;
#ifndef VENDOR_EDIT /* Jianchao.Shi@BSP.CHG.Basic, 2017/04/06, sjc Modify for charging */
	u8 val = 0, mask = 0;
#else
	u8 val = 0;
#endif

	if (chip->dt.no_battery)
		chg->fake_capacity = 50;

	if (chip->dt.batt_profile_fcc_ua < 0)
		smblib_get_charge_param(chg, &chg->param.fcc,
				&chg->batt_profile_fcc_ua);

	if (chip->dt.batt_profile_fv_uv < 0)
		smblib_get_charge_param(chg, &chg->param.fv,
				&chg->batt_profile_fv_uv);

	smblib_get_charge_param(chg, &chg->param.usb_icl,
				&chg->default_icl_ua);
	smblib_get_charge_param(chg, &chg->param.aicl_5v_threshold,
				&chg->default_aicl_5v_threshold_mv);
	chg->aicl_5v_threshold_mv = chg->default_aicl_5v_threshold_mv;
	smblib_get_charge_param(chg, &chg->param.aicl_cont_threshold,
				&chg->default_aicl_cont_threshold_mv);
	chg->aicl_cont_threshold_mv = chg->default_aicl_cont_threshold_mv;

	if (chg->charger_temp_max == -EINVAL) {
		rc = smblib_get_thermal_threshold(chg,
					DIE_REG_H_THRESHOLD_MSB_REG,
					&chg->charger_temp_max);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't get charger_temp_max rc=%d\n",
					rc);
			return rc;
		}
	}

	/*
	 * If SW thermal regulation WA is active then all the HW temperature
	 * comparators need to be disabled to prevent HW thermal regulation,
	 * apart from DIE_TEMP analog comparator for SHDN regulation.
	 */
	if (chg->wa_flags & SW_THERM_REGULATION_WA) {
		rc = smblib_write(chg, MISC_THERMREG_SRC_CFG_REG,
					THERMREG_SW_ICL_ADJUST_BIT
					| THERMREG_DIE_CMP_SRC_EN_BIT);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't disable HW thermal regulation rc=%d\n",
				rc);
			return rc;
		}
	} else {
		/* configure temperature mitigation */
		rc = smb5_configure_mitigation(chg);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure mitigation rc=%d\n",
					rc);
			return rc;
		}
	}

	/* Set HVDCP autonomous mode per DT option */
	smblib_hvdcp_hw_inov_enable(chg, chip->dt.hvdcp_autonomous);

	/* Disable HVDCP and authentication algorithm if specified in DT */
	if (chg->hvdcp_disable)
		smblib_hvdcp_detect_enable(chg, false);

	rc = smb5_init_connector_type(chg);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure connector type rc=%d\n",
				rc);
		return rc;
	}

	/* Use ICL results from HW */
	rc = smblib_icl_override(chg, HW_AUTO_MODE);
	if (rc < 0) {
		pr_err("Couldn't disable ICL override rc=%d\n", rc);
		return rc;
	}

	/* set OTG current limit */
	pr_err("smblib_set_charge_param otg current otg_cl_ua=%d\n", chg->otg_cl_ua);
	rc = smblib_set_charge_param(chg, &chg->param.otg_cl, chg->otg_cl_ua);
	if (rc < 0) {
		pr_err("Couldn't set otg current limit rc=%d\n", rc);
		return rc;
	}

	/* vote 0mA on usb_icl for non battery platforms */
	vote(chg->usb_icl_votable,
		DEFAULT_VOTER, chip->dt.no_battery, 0);
	vote(chg->dc_suspend_votable,
		DEFAULT_VOTER, chip->dt.no_battery, 0);
	vote(chg->fcc_votable, HW_LIMIT_VOTER,
		chip->dt.batt_profile_fcc_ua > 0, chip->dt.batt_profile_fcc_ua);
	vote(chg->fv_votable, HW_LIMIT_VOTER,
		chip->dt.batt_profile_fv_uv > 0, chip->dt.batt_profile_fv_uv);
	vote(chg->fcc_votable,
		BATT_PROFILE_VOTER, chg->batt_profile_fcc_ua > 0,
		chg->batt_profile_fcc_ua);
	vote(chg->fv_votable,
		BATT_PROFILE_VOTER, chg->batt_profile_fv_uv > 0,
		chg->batt_profile_fv_uv);

	/* Some h/w limit maximum supported ICL */
	vote(chg->usb_icl_votable, HW_LIMIT_VOTER,
			chg->hw_max_icl_ua > 0, chg->hw_max_icl_ua);

	/* Initialize DC peripheral configurations */
	rc = smb5_init_dc_peripheral(chg);
	if (rc < 0)
		return rc;

	/*
	 * AICL configuration: enable aicl and aicl rerun and based on DT
	 * configuration enable/disable ADB based AICL and Suspend on collapse.
	 */
#ifdef VENDOR_EDIT
        /* Jianchao.Shi@BSP.CHG.Basic, 2017/04/06, sjc Modify for charging */
            rc = smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG,
                    SUSPEND_ON_COLLAPSE_USBIN_BIT | USBIN_AICL_START_AT_MAX_BIT
                        | USBIN_AICL_ADC_EN_BIT | USBIN_AICL_PERIODIC_RERUN_EN_BIT, USBIN_AICL_PERIODIC_RERUN_EN_BIT);
            if (rc < 0) {
                dev_err(chg->dev, "Couldn't configure AICL rc=%d\n", rc);
                return rc;
            }
#else
	mask = USBIN_AICL_PERIODIC_RERUN_EN_BIT | USBIN_AICL_ADC_EN_BIT
			| USBIN_AICL_EN_BIT | SUSPEND_ON_COLLAPSE_USBIN_BIT;
	val = USBIN_AICL_PERIODIC_RERUN_EN_BIT | USBIN_AICL_EN_BIT;
	if (!chip->dt.disable_suspend_on_collapse)
		val |= SUSPEND_ON_COLLAPSE_USBIN_BIT;
	if (chip->dt.adc_based_aicl)
		val |= USBIN_AICL_ADC_EN_BIT;

	rc = smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG,
			mask, val);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't config AICL rc=%d\n", rc);
		return rc;
	}
#endif

	rc = smblib_write(chg, AICL_RERUN_TIME_CFG_REG,
				AICL_RERUN_TIME_12S_VAL);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure AICL rerun interval rc=%d\n", rc);
		return rc;
	}

	/* enable the charging path */
	rc = vote(chg->chg_disable_votable, DEFAULT_VOTER, false, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't enable charging rc=%d\n", rc);
		return rc;
	}

	/* configure VBUS for software control */
	rc = smblib_masked_write(chg, DCDC_OTG_CFG_REG, OTG_EN_SRC_CFG_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure VBUS for SW control rc=%d\n", rc);
		return rc;
	}

	val = (ilog2(chip->dt.wd_bark_time / 16) << BARK_WDOG_TIMEOUT_SHIFT)
			& BARK_WDOG_TIMEOUT_MASK;
	val |= (BITE_WDOG_TIMEOUT_8S | BITE_WDOG_DISABLE_CHARGING_CFG_BIT);
	val |= (chip->dt.wd_snarl_time_cfg << SNARL_WDOG_TIMEOUT_SHIFT)
			& SNARL_WDOG_TIMEOUT_MASK;

	rc = smblib_masked_write(chg, SNARL_BARK_BITE_WD_CFG_REG,
			BITE_WDOG_DISABLE_CHARGING_CFG_BIT |
			SNARL_WDOG_TIMEOUT_MASK | BARK_WDOG_TIMEOUT_MASK |
			BITE_WDOG_TIMEOUT_MASK,
			val);
	if (rc < 0) {
		pr_err("Couldn't configue WD config rc=%d\n", rc);
		return rc;
	}

	/* enable WD BARK and enable it on plugin */
	val = WDOG_TIMER_EN_ON_PLUGIN_BIT | BARK_WDOG_INT_EN_BIT;
	rc = smblib_masked_write(chg, WD_CFG_REG,
			WATCHDOG_TRIGGER_AFP_EN_BIT |
			WDOG_TIMER_EN_ON_PLUGIN_BIT |
			BARK_WDOG_INT_EN_BIT, val);
	if (rc < 0) {
		pr_err("Couldn't configue WD config rc=%d\n", rc);
		return rc;
	}

	/* set termination current threshold values */
	rc = smb5_configure_iterm_thresholds(chip);
	if (rc < 0) {
		pr_err("Couldn't configure ITERM thresholds rc=%d\n",
				rc);
		return rc;
	}
#ifdef VENDOR_EDIT
        /* Jianchao.Shi@BSP.CHG.Basic, 2017/03/29, sjc Add for TYPE_C_CHANGE_IRQ storm(and counter current) */
            smblib_masked_write(chg, 0x1380, 0x03, 0x3);
#ifndef VENDOR_EDIT
         /*zhijie.li@BSP.CHG.Basic, 2019/1/9, lzj add for USBIN_IN_COLLAPSE*/
            smblib_masked_write(chg, 0x1365, 0x03, 0x3);
#endif
#endif
#ifdef VENDOR_EDIT
        /* Jianchao.Shi@BSP.CHG.Basic, 2017/06/28, sjc Add for reducing DCD timeout */
            smblib_masked_write(chg, 0x1363, 0x20, 0);
#endif
        
#ifdef VENDOR_EDIT
        /* Jianchao.Shi@BSP.CHG.Basic, 2017/06/29, sjc Add for reducing chg delay after unsuspend */
            smblib_masked_write(chg, 0x1052, 0x02, 0);
            smblib_masked_write(chg, 0x1053, 0x40, 0);
#endif
        
#ifdef VENDOR_EDIT
        /* Jianchao.Shi@BSP.CHG.Basic, 2018/01/23, sjc Add for disable thermal cfg */
            smblib_masked_write(chg, 0x1670, 0xff, 0);
#endif /*VENDOR_EDIT*/
    
#ifdef VENDOR_EDIT
    /* zhangkun@BSP.CHG.Basic, 2019/01/26, add for close usb debug mode */
    rc = smblib_masked_write(chg, DEBUG_ACCESS_SNK_CFG_REG,
                0xff, 0x7);
    if (rc < 0)
        pr_err("Couldn't enable at bootup rc=%d\n", rc);
#endif
    
    
#ifdef VENDOR_EDIT
        /* tongfeng.Huang@BSP.CHG.Basic, 2018/07/05, sjc Add for set hw aicl to 4.4V */
        fg_oppo_set_input_current = false;
#endif /*VENDOR_EDIT*/

	rc = smb5_configure_float_charger(chip);
	if (rc < 0)
		return rc;

	switch (chip->dt.chg_inhibit_thr_mv) {
	case 50:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				INHIBIT_ANALOG_VFLT_MINUS_50MV);
		break;
	case 100:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				INHIBIT_ANALOG_VFLT_MINUS_100MV);
		break;
	case 200:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				INHIBIT_ANALOG_VFLT_MINUS_200MV);
		break;
	case 300:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				INHIBIT_ANALOG_VFLT_MINUS_300MV);
		break;
	case 0:
		rc = smblib_masked_write(chg, CHGR_CFG2_REG,
				CHARGER_INHIBIT_BIT, 0);
	default:
		break;
	}

	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure charge inhibit threshold rc=%d\n",
			rc);
		return rc;
	}

	rc = smblib_write(chg, CHGR_FAST_CHARGE_SAFETY_TIMER_CFG_REG,
					FAST_CHARGE_SAFETY_TIMER_768_MIN);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set CHGR_FAST_CHARGE_SAFETY_TIMER_CFG_REG rc=%d\n",
			rc);
		return rc;
	}

	rc = smb5_configure_recharging(chip);
	if (rc < 0)
		return rc;

	rc = smblib_disable_hw_jeita(chg, true);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set hw jeita rc=%d\n", rc);
		return rc;
	}

	rc = smblib_masked_write(chg, DCDC_ENG_SDCDC_CFG5_REG,
			ENG_SDCDC_BAT_HPWR_MASK, BOOST_MODE_THRESH_3P6_V);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure DCDC_ENG_SDCDC_CFG5 rc=%d\n",
				rc);
		return rc;
	}

	if (chg->connector_pull_up != -EINVAL) {
		rc = smb5_configure_internal_pull(chg, CONN_THERM,
				get_valid_pullup(chg->connector_pull_up));
		if (rc < 0) {
			dev_err(chg->dev,
				"Couldn't configure CONN_THERM pull-up rc=%d\n",
				rc);
			return rc;
		}
	}

	if (chg->smb_pull_up != -EINVAL) {
		rc = smb5_configure_internal_pull(chg, SMB_THERM,
				get_valid_pullup(chg->smb_pull_up));
		if (rc < 0) {
			dev_err(chg->dev,
				"Couldn't configure SMB pull-up rc=%d\n",
				rc);
			return rc;
		}
	}

	return rc;
}

static int smb5_post_init(struct smb5 *chip)
{
	struct smb_charger *chg = &chip->chg;
	union power_supply_propval pval;
	int rc;
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/01/30, sjc Add for using gpio as CC detect */
    int level = 0;
#endif
	/*
	 * In case the usb path is suspended, we would have missed disabling
	 * the icl change interrupt because the interrupt could have been
	 * not requested
	 */
	rerun_election(chg->usb_icl_votable);
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/01/30, sjc Add for using gpio as CC detect */
    if (g_oppo_chip && oppo_ccdetect_check_is_gpio(g_oppo_chip) == true) {
        level = gpio_get_value(chg->ccdetect_gpio);
        usleep_range(2000, 2100);
        if (level != gpio_get_value(chg->ccdetect_gpio)) {
            printk(KERN_ERR "[OPPO_CHG][%s]: ccdetect_gpio is unstable, try again...\n", __func__);
            usleep_range(10000, 11000);
            level = gpio_get_value(chg->ccdetect_gpio);
        }
        if (level <= 0) {
            oppo_ccdetect_enable();
        }
    } else {
		/* configure power role for dual-role */
		pval.intval = POWER_SUPPLY_TYPEC_PR_DUAL;
		rc = smblib_set_prop_typec_power_role(chg, &pval);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure DRP role rc=%d\n",
					rc);
			return rc;
		}

    }
    //oppo_ccdetect_enable();
#else
	/* configure power role for dual-role */
	pval.intval = POWER_SUPPLY_TYPEC_PR_DUAL;
	rc = smblib_set_prop_typec_power_role(chg, &pval);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure DRP role rc=%d\n",
				rc);
		return rc;
	}
#endif
	rerun_election(chg->temp_change_irq_disable_votable);

	return 0;
}

/****************************
 * DETERMINE INITIAL STATUS *
 ****************************/

static int smb5_determine_initial_status(struct smb5 *chip)
{
	struct smb_irq_data irq_data = {chip, "determine-initial-status"};
	struct smb_charger *chg = &chip->chg;
	union power_supply_propval val;
	int rc;

	rc = smblib_get_prop_usb_present(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get usb present rc=%d\n", rc);
		return rc;
	}
	chg->early_usb_attach = val.intval;

	if (chg->bms_psy)
		smblib_suspend_on_debug_battery(chg);

	usb_plugin_irq_handler(0, &irq_data);
	dc_plugin_irq_handler(0, &irq_data);
	typec_attach_detach_irq_handler(0, &irq_data);
	typec_state_change_irq_handler(0, &irq_data);
	usb_source_change_irq_handler(0, &irq_data);
	chg_state_change_irq_handler(0, &irq_data);
	icl_change_irq_handler(0, &irq_data);
	batt_temp_changed_irq_handler(0, &irq_data);
	wdog_bark_irq_handler(0, &irq_data);
	typec_or_rid_detection_change_irq_handler(0, &irq_data);
	wdog_snarl_irq_handler(0, &irq_data);
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/07/13, sjc Add for fake typec */
    if (chg->early_usb_attach)
        schedule_delayed_work(&chg->typec_disable_cmd_work, msecs_to_jiffies(1000));
#endif

	return 0;
}

/**************************
 * INTERRUPT REGISTRATION *
 **************************/

static struct smb_irq_info smb5_irqs[] = {
	/* CHARGER IRQs */
	[CHGR_ERROR_IRQ] = {
		.name		= "chgr-error",
		.handler	= default_irq_handler,
	},
	[CHG_STATE_CHANGE_IRQ] = {
		.name		= "chg-state-change",
		.handler	= chg_state_change_irq_handler,
		.wake		= true,
	},
	[STEP_CHG_STATE_CHANGE_IRQ] = {
		.name		= "step-chg-state-change",
	},
	[STEP_CHG_SOC_UPDATE_FAIL_IRQ] = {
		.name		= "step-chg-soc-update-fail",
	},
	[STEP_CHG_SOC_UPDATE_REQ_IRQ] = {
		.name		= "step-chg-soc-update-req",
	},
	[FG_FVCAL_QUALIFIED_IRQ] = {
		.name		= "fg-fvcal-qualified",
	},
	[VPH_ALARM_IRQ] = {
		.name		= "vph-alarm",
	},
	[VPH_DROP_PRECHG_IRQ] = {
		.name		= "vph-drop-prechg",
	},
	/* DCDC IRQs */
	[OTG_FAIL_IRQ] = {
		.name		= "otg-fail",
		.handler	= default_irq_handler,
	},
	[OTG_OC_DISABLE_SW_IRQ] = {
		.name		= "otg-oc-disable-sw",
	},
	[OTG_OC_HICCUP_IRQ] = {
		.name		= "otg-oc-hiccup",
	},
	[BSM_ACTIVE_IRQ] = {
		.name		= "bsm-active",
	},
	[HIGH_DUTY_CYCLE_IRQ] = {
		.name		= "high-duty-cycle",
		.handler	= high_duty_cycle_irq_handler,
		.wake		= true,
	},
	[INPUT_CURRENT_LIMITING_IRQ] = {
		.name		= "input-current-limiting",
		.handler	= default_irq_handler,
	},
	[CONCURRENT_MODE_DISABLE_IRQ] = {
		.name		= "concurrent-mode-disable",
	},
	[SWITCHER_POWER_OK_IRQ] = {
		.name		= "switcher-power-ok",
		.handler	= switcher_power_ok_irq_handler,
#ifdef VENDOR_EDIT
		/*zhijie.li@BSP.CHG.Basic, 2019/1/26, lzj add for aicl*/
		.storm_data = {true, 1000, 3},
#endif
	},
	/* BATTERY IRQs */
	[BAT_TEMP_IRQ] = {
		.name		= "bat-temp",
		.handler	= batt_temp_changed_irq_handler,
		.wake		= true,
	},
	[ALL_CHNL_CONV_DONE_IRQ] = {
		.name		= "all-chnl-conv-done",
	},
	[BAT_OV_IRQ] = {
		.name		= "bat-ov",
		.handler	= batt_psy_changed_irq_handler,
	},
	[BAT_LOW_IRQ] = {
		.name		= "bat-low",
		.handler	= batt_psy_changed_irq_handler,
	},
	[BAT_THERM_OR_ID_MISSING_IRQ] = {
		.name		= "bat-therm-or-id-missing",
		.handler	= batt_psy_changed_irq_handler,
	},
	[BAT_TERMINAL_MISSING_IRQ] = {
		.name		= "bat-terminal-missing",
		.handler	= batt_psy_changed_irq_handler,
	},
	[BUCK_OC_IRQ] = {
		.name		= "buck-oc",
	},
	[VPH_OV_IRQ] = {
		.name		= "vph-ov",
	},
	/* USB INPUT IRQs */
	[USBIN_COLLAPSE_IRQ] = {
		.name		= "usbin-collapse",
		.handler	= default_irq_handler,
	},
	[USBIN_VASHDN_IRQ] = {
		.name		= "usbin-vashdn",
		.handler	= default_irq_handler,
	},
	[USBIN_UV_IRQ] = {
		.name		= "usbin-uv",
		.handler	= usbin_uv_irq_handler,
		.wake		= true,
		.storm_data	= {true, 3000, 5},
	},
	[USBIN_OV_IRQ] = {
		.name		= "usbin-ov",
		.handler	= usbin_ov_irq_handler,
	},
	[USBIN_PLUGIN_IRQ] = {
		.name		= "usbin-plugin",
		.handler	= usb_plugin_irq_handler,
		.wake           = true,
	},
	[USBIN_REVI_CHANGE_IRQ] = {
		.name		= "usbin-revi-change",
	},
	[USBIN_SRC_CHANGE_IRQ] = {
		.name		= "usbin-src-change",
		.handler	= usb_source_change_irq_handler,
		.wake           = true,
	},
	[USBIN_ICL_CHANGE_IRQ] = {
		.name		= "usbin-icl-change",
		.handler	= icl_change_irq_handler,
		.wake           = true,
	},
	/* DC INPUT IRQs */
	[DCIN_VASHDN_IRQ] = {
		.name		= "dcin-vashdn",
	},
	[DCIN_UV_IRQ] = {
		.name		= "dcin-uv",
		.handler	= dcin_uv_irq_handler,
		.wake		= true,
	},
	[DCIN_OV_IRQ] = {
		.name		= "dcin-ov",
		.handler	= default_irq_handler,
	},
	[DCIN_PLUGIN_IRQ] = {
		.name		= "dcin-plugin",
		.handler	= dc_plugin_irq_handler,
		.wake           = true,
	},
	[DCIN_REVI_IRQ] = {
		.name		= "dcin-revi",
	},
	[DCIN_PON_IRQ] = {
		.name		= "dcin-pon",
		.handler	= default_irq_handler,
	},
	[DCIN_EN_IRQ] = {
		.name		= "dcin-en",
		.handler	= default_irq_handler,
	},
	/* TYPEC IRQs */
	[TYPEC_OR_RID_DETECTION_CHANGE_IRQ] = {
		.name		= "typec-or-rid-detect-change",
		.handler	= typec_or_rid_detection_change_irq_handler,
		.wake           = true,
	},
	[TYPEC_VPD_DETECT_IRQ] = {
		.name		= "typec-vpd-detect",
	},
	[TYPEC_CC_STATE_CHANGE_IRQ] = {
		.name		= "typec-cc-state-change",
		.handler	= typec_state_change_irq_handler,
		.wake           = true,
	},
	[TYPEC_VCONN_OC_IRQ] = {
		.name		= "typec-vconn-oc",
		.handler	= default_irq_handler,
	},
	[TYPEC_VBUS_CHANGE_IRQ] = {
		.name		= "typec-vbus-change",
	},
	[TYPEC_ATTACH_DETACH_IRQ] = {
		.name		= "typec-attach-detach",
		.handler	= typec_attach_detach_irq_handler,
		.wake		= true,
	},
	[TYPEC_LEGACY_CABLE_DETECT_IRQ] = {
		.name		= "typec-legacy-cable-detect",
		.handler	= default_irq_handler,
	},
	[TYPEC_TRY_SNK_SRC_DETECT_IRQ] = {
		.name		= "typec-try-snk-src-detect",
	},
	/* MISCELLANEOUS IRQs */
	[WDOG_SNARL_IRQ] = {
		.name		= "wdog-snarl",
		.handler	= wdog_snarl_irq_handler,
		.wake		= true,
	},
	[WDOG_BARK_IRQ] = {
		.name		= "wdog-bark",
		.handler	= wdog_bark_irq_handler,
		.wake		= true,
	},
	[AICL_FAIL_IRQ] = {
		.name		= "aicl-fail",
	},
	[AICL_DONE_IRQ] = {
		.name		= "aicl-done",
		.handler	= default_irq_handler,
	},
	[SMB_EN_IRQ] = {
		.name		= "smb-en",
		.handler	= smb_en_irq_handler,
	},
	[IMP_TRIGGER_IRQ] = {
		.name		= "imp-trigger",
	},
	/*
	 * triggered when DIE or SKIN or CONNECTOR temperature across
	 * either of the _REG_L, _REG_H, _RST, or _SHDN thresholds
	 */
	[TEMP_CHANGE_IRQ] = {
		.name		= "temp-change",
		.handler	= temp_change_irq_handler,
		.wake		= true,
	},
	[TEMP_CHANGE_SMB_IRQ] = {
		.name		= "temp-change-smb",
	},
	/* FLASH */
	[VREG_OK_IRQ] = {
		.name		= "vreg-ok",
	},
	[ILIM_S2_IRQ] = {
		.name		= "ilim2-s2",
		.handler	= schgm_flash_ilim2_irq_handler,
	},
	[ILIM_S1_IRQ] = {
		.name		= "ilim1-s1",
	},
	[VOUT_DOWN_IRQ] = {
		.name		= "vout-down",
	},
	[VOUT_UP_IRQ] = {
		.name		= "vout-up",
	},
	[FLASH_STATE_CHANGE_IRQ] = {
		.name		= "flash-state-change",
		.handler	= schgm_flash_state_change_irq_handler,
	},
	[TORCH_REQ_IRQ] = {
		.name		= "torch-req",
	},
	[FLASH_EN_IRQ] = {
		.name		= "flash-en",
	},
	/* SDAM */
	[SDAM_STS_IRQ] = {
		.name		= "sdam-sts",
		.handler	= sdam_sts_change_irq_handler,
	},
};

static int smb5_get_irq_index_byname(const char *irq_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb5_irqs); i++) {
		if (strcmp(smb5_irqs[i].name, irq_name) == 0)
			return i;
	}

	return -ENOENT;
}

static int smb5_request_interrupt(struct smb5 *chip,
				struct device_node *node, const char *irq_name)
{
	struct smb_charger *chg = &chip->chg;
	int rc, irq, irq_index;
	struct smb_irq_data *irq_data;

	irq = of_irq_get_byname(node, irq_name);
	if (irq < 0) {
		pr_err("Couldn't get irq %s byname\n", irq_name);
		return irq;
	}

	irq_index = smb5_get_irq_index_byname(irq_name);
	if (irq_index < 0) {
		pr_err("%s is not a defined irq\n", irq_name);
		return irq_index;
	}

	if (!smb5_irqs[irq_index].handler)
		return 0;

	irq_data = devm_kzalloc(chg->dev, sizeof(*irq_data), GFP_KERNEL);
	if (!irq_data)
		return -ENOMEM;

	irq_data->parent_data = chip;
	irq_data->name = irq_name;
	irq_data->storm_data = smb5_irqs[irq_index].storm_data;
	mutex_init(&irq_data->storm_data.storm_lock);

	smb5_irqs[irq_index].enabled = true;
	rc = devm_request_threaded_irq(chg->dev, irq, NULL,
					smb5_irqs[irq_index].handler,
					IRQF_ONESHOT, irq_name, irq_data);
	if (rc < 0) {
		pr_err("Couldn't request irq %d\n", irq);
		return rc;
	}

	smb5_irqs[irq_index].irq = irq;
	smb5_irqs[irq_index].irq_data = irq_data;
	if (smb5_irqs[irq_index].wake)
		enable_irq_wake(irq);

	return rc;
}

static int smb5_request_interrupts(struct smb5 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct device_node *node = chg->dev->of_node;
	struct device_node *child;
	int rc = 0;
	const char *name;
	struct property *prop;

	for_each_available_child_of_node(node, child) {
		of_property_for_each_string(child, "interrupt-names",
					    prop, name) {
			rc = smb5_request_interrupt(chip, child, name);
			if (rc < 0)
				return rc;
		}
	}

	vote(chg->limited_irq_disable_votable, CHARGER_TYPE_VOTER, true, 0);
	vote(chg->hdc_irq_disable_votable, CHARGER_TYPE_VOTER, true, 0);

	return rc;
}

static void smb5_free_interrupts(struct smb_charger *chg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb5_irqs); i++) {
		if (smb5_irqs[i].irq > 0) {
			if (smb5_irqs[i].wake)
				disable_irq_wake(smb5_irqs[i].irq);

			devm_free_irq(chg->dev, smb5_irqs[i].irq,
						smb5_irqs[i].irq_data);
		}
	}
}

static void smb5_disable_interrupts(struct smb_charger *chg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb5_irqs); i++) {
		if (smb5_irqs[i].irq > 0)
			disable_irq(smb5_irqs[i].irq);
	}
}

#if defined(CONFIG_DEBUG_FS)

static int force_batt_psy_update_write(void *data, u64 val)
{
	struct smb_charger *chg = data;

	power_supply_changed(chg->batt_psy);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(force_batt_psy_update_ops, NULL,
			force_batt_psy_update_write, "0x%02llx\n");

static int force_usb_psy_update_write(void *data, u64 val)
{
	struct smb_charger *chg = data;

	power_supply_changed(chg->usb_psy);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(force_usb_psy_update_ops, NULL,
			force_usb_psy_update_write, "0x%02llx\n");

static int force_dc_psy_update_write(void *data, u64 val)
{
	struct smb_charger *chg = data;
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/05/09, sjc Add for charging */
	if (chg->dc_psy)
#endif
	power_supply_changed(chg->dc_psy);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(force_dc_psy_update_ops, NULL,
			force_dc_psy_update_write, "0x%02llx\n");

static void smb5_create_debugfs(struct smb5 *chip)
{
	struct dentry *file;

	chip->dfs_root = debugfs_create_dir("charger", NULL);
	if (IS_ERR_OR_NULL(chip->dfs_root)) {
		pr_err("Couldn't create charger debugfs rc=%ld\n",
			(long)chip->dfs_root);
		return;
	}

	file = debugfs_create_file("force_batt_psy_update", 0600,
			    chip->dfs_root, chip, &force_batt_psy_update_ops);
	if (IS_ERR_OR_NULL(file))
		pr_err("Couldn't create force_batt_psy_update file rc=%ld\n",
			(long)file);

	file = debugfs_create_file("force_usb_psy_update", 0600,
			    chip->dfs_root, chip, &force_usb_psy_update_ops);
	if (IS_ERR_OR_NULL(file))
		pr_err("Couldn't create force_usb_psy_update file rc=%ld\n",
			(long)file);

	file = debugfs_create_file("force_dc_psy_update", 0600,
			    chip->dfs_root, chip, &force_dc_psy_update_ops);
	if (IS_ERR_OR_NULL(file))
		pr_err("Couldn't create force_dc_psy_update file rc=%ld\n",
			(long)file);

	file = debugfs_create_u32("debug_mask", 0600, chip->dfs_root,
			&__debug_mask);
	if (IS_ERR_OR_NULL(file))
		pr_err("Couldn't create debug_mask file rc=%ld\n", (long)file);
}

#else

static void smb5_create_debugfs(struct smb5 *chip)
{}

#endif
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/05/22, sjc Add for dump registers */
static bool d_reg_mask = false;
static ssize_t dump_registers_mask_write(struct file *file, const char __user *buff, size_t count, loff_t *ppos)
{
	char mask[16];

	if (copy_from_user(&mask, buff, count)) {
		printk(KERN_ERR "dump_registers_mask_write error.\n");
		return -EFAULT;
	}

	if (strncmp(mask, "dump808", 7) == 0) {
		d_reg_mask = true;
		printk(KERN_ERR "dump registers mask enable.\n");
	} else {
		d_reg_mask = false;
		return -EFAULT;
	}

	return count;
}

static const struct file_operations dump_registers_mask_fops = {
	.write = dump_registers_mask_write,
	.llseek = noop_llseek,
};

static void init_proc_dump_registers_mask(void)
{
	if (!proc_create("d_reg_mask", S_IWUSR | S_IWGRP | S_IWOTH, NULL, &dump_registers_mask_fops)) {
		printk(KERN_ERR "proc_create dump_registers_mask_fops fail\n");
	}
}
#endif /* VENDOR_EDIT */
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
//static int get_boot_mode(void);
static int smbchg_usb_suspend_disable(void);
static int smbchg_usb_suspend_enable(void);
static int smbchg_charging_enble(void);
bool oppo_chg_is_usb_present(void);
int qpnp_get_prop_charger_voltage_now(void);

static void dump_regs(void)
{
	int i;
	int j;
	int rc;
	u8 stat;
	int base[] = {0x1000, 0x1100, 0x1200, 0x1300, 0x1400, 0x1600, 0x1800, 0x1900};
	struct smb_charger *chg = NULL;

	if (!g_oppo_chip || !d_reg_mask || oppo_get_chg_powersave() == true)
		return;

	chg = &g_oppo_chip->pmic_spmi.smb5_chip->chg;
	if (!chg)
		return;

	pr_err("================= %s: begin ======================\n", __func__);

	for (j = 0; j < 8; j++) {
		for (i = 0; i < 255; i++) {
			rc = smblib_read(chg, base[j] + i, &stat);
			if (rc < 0) {
				pr_err("Couldn't read %x rc=%d\n", base[j] + i, rc);
			} else {
				pr_err("%x : %x\n", base[j] + i, stat);
			}
		}

		msleep(1000);
	}

	pr_err("================= %s: end ======================\n", __func__);

	d_reg_mask = false;
}

static int smbchg_kick_wdt(void)
{
	return 0;
}

static int oppo_chg_hw_init(void)
{
	int boot_mode = get_boot_mode();

	if (boot_mode != MSM_BOOT_MODE__RF && boot_mode != MSM_BOOT_MODE__WLAN) {
		smbchg_usb_suspend_disable();
	} else {
		smbchg_usb_suspend_enable();
	}
	smbchg_charging_enble();

	return 0;
}

static int smbchg_set_fastchg_current_raw(int current_ma)
{
	int rc = 0;
	struct oppo_chg_chip *chip = g_oppo_chip;

	rc = vote(chip->pmic_spmi.smb5_chip->chg.fcc_votable, DEFAULT_VOTER,
			true, current_ma * 1000);
	if (rc < 0)
		chg_err("Couldn't vote fcc_votable[%d], rc=%d\n", current_ma, rc);

	return rc;
}

static void smbchg_set_aicl_point(int vol)
{
	//DO Nothing
}

static void smbchg_aicl_enable(bool enable)
{
	int rc = 0;
	u8 aicl_op;
	struct oppo_chg_chip *chip = g_oppo_chip;

	rc = smblib_masked_write(&chip->pmic_spmi.smb5_chip->chg, USBIN_AICL_OPTIONS_CFG_REG,
			USBIN_AICL_EN_BIT, enable ? USBIN_AICL_EN_BIT : 0);
	if (rc < 0)
		chg_err("Couldn't write USBIN_AICL_OPTIONS_CFG_REG rc=%d\n", rc);
	rc = smblib_read(&chip->pmic_spmi.smb5_chip->chg, 0x1380, &aicl_op);
	if (!rc)
		chg_err("AICL_OPTIONS 0x1380 = 0x%02x\n", aicl_op); //dump 0x1380
}

static void smbchg_usbin_collapse_irq_enable(bool enable)
{
	static bool collapse_en = true;
	struct oppo_chg_chip *chip = g_oppo_chip;

	if (enable && !collapse_en){
		enable_irq(chip->pmic_spmi.smb5_chip->chg.irq_info[USBIN_COLLAPSE_IRQ].irq);
	}else if (!enable && collapse_en){
		disable_irq(chip->pmic_spmi.smb5_chip->chg.irq_info[USBIN_COLLAPSE_IRQ].irq);
	}
	collapse_en = enable;
}

static void smbchg_rerun_aicl(void)
{
	smbchg_aicl_enable(false);
	/* Add a delay so that AICL successfully clears */
	msleep(50);
	smbchg_aicl_enable(true);
}

static bool  oppo_chg_is_normal_mode(void)
{
	int boot_mode = get_boot_mode();

	if (boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN)
		return false;
	return true;
}

static bool oppo_chg_is_suspend_status(void)
{
	int rc = 0;
	u8 stat;
	struct smb_charger *chg = NULL;

	if (!g_oppo_chip)
		return false;

	chg = &g_oppo_chip->pmic_spmi.smb5_chip->chg;

	rc = smblib_read(chg, POWER_PATH_STATUS_REG, &stat);
	if (rc < 0) {
		printk(KERN_ERR "oppo_chg_is_suspend_status: Couldn't read POWER_PATH_STATUS rc=%d\n", rc);
		return false;
	}

	return (bool)(stat & USBIN_SUSPEND_STS_BIT);
}

static void oppo_chg_clear_suspend(void)
{
	int rc;
	int boot_mode = get_boot_mode();
	struct smb_charger *chg = NULL;

	if (!g_oppo_chip)
		return;
	if (boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN)
		return;

	chg = &g_oppo_chip->pmic_spmi.smb5_chip->chg;

	rc = smblib_masked_write(chg, USBIN_CMD_IL_REG, USBIN_SUSPEND_BIT, 1);
	if (rc < 0) {
		printk(KERN_ERR "oppo_chg_monitor_work: Couldn't set USBIN_SUSPEND_BIT rc=%d\n", rc);
	}
	msleep(50);
	rc = smblib_masked_write(chg, USBIN_CMD_IL_REG, USBIN_SUSPEND_BIT, 0);
	if (rc < 0) {
		printk(KERN_ERR "oppo_chg_monitor_work: Couldn't clear USBIN_SUSPEND_BIT rc=%d\n", rc);
	}
}

static void oppo_chg_check_clear_suspend(void)
{
	use_present_status = true;
	oppo_chg_clear_suspend();
	use_present_status = false;
}

static int usb_icl[] = {
	300, 500, 900, 1200, 1350, 1500, 1750, 2000, 3000,
};

#define USBIN_25MA	25000
static int oppo_chg_set_input_current(int current_ma)
{
       int rc = 0, i = 0;
	int chg_vol = 0;
	int aicl_point = 0;
	u8 stat = 0;
	int pre_current = 0;
	struct oppo_chg_chip *chip = g_oppo_chip;

	if (chip->mmi_chg == 0) {
		/*for charger cycle test*/
		chg_debug( "mmi_chg, return\n");
		return rc;
	}
	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER, false, 0);
	}

	if (chip->pmic_spmi.smb5_chip->chg.pre_current_ma == current_ma)
		return rc;
	else {
		pre_current = chip->pmic_spmi.smb5_chip->chg.pre_current_ma;
		chip->pmic_spmi.smb5_chip->chg.pre_current_ma = current_ma;
	}
    fg_oppo_set_input_current = true;

	chg_debug( "usb input max current limit=%d setting %02x, pre_current[%d]\n", current_ma, i, pre_current);

	if (chip->batt_volt > 4100 )
		aicl_point = 4550;
	else
		aicl_point = 4500;

	smbchg_aicl_enable(false);
	smbchg_usbin_collapse_irq_enable(false);

	if (current_ma < 500) {
		i = 0;
		goto aicl_end;
	}

	i = 1; /* 500 */
	rc = vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
	msleep(90);

	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		chg_debug( "use 500 here\n");
		goto aicl_boost_back;
	}

	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		chg_debug( "use 500 here\n");
		goto aicl_boost_back;
	}
	if (chg_vol < aicl_point) {
		chg_debug( "use 500 here\n");
		goto aicl_end;
	} else if (current_ma < 900)
		goto aicl_end;

	i = 2; /* 900 */
	rc = vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
	msleep(90);

	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		i = i - 1;
		goto aicl_boost_back;
	}
	if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
		i = i - 1;
		goto aicl_suspend;
	}

	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		i = i - 1;
		goto aicl_boost_back;
	}
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma < 1200)
		goto aicl_end;

	i = 3; /* 1200 */
	rc = vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
	msleep(90);

	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		i = i - 1;
		goto aicl_boost_back;
	}
	if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
		i = i - 1;
		goto aicl_suspend;
	}

	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		i = i - 1;
		goto aicl_boost_back;
	}
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	}

	i = 4; /* 1350 */
	rc = vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
	msleep(130);

	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		i = i - 2;
		goto aicl_boost_back;
	}
	if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
		i = i - 2;
		goto aicl_suspend;
	}

	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		i = i - 2;
		goto aicl_boost_back;
	}
	if (chg_vol < aicl_point) {
		i = i - 2; 
		goto aicl_pre_step;
	} 
	
	i = 5; /* 1500 */
	rc = vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
	msleep(120);

	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		i = i - 3;
		goto aicl_boost_back;
	}
	if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
		i = i - 3;
		goto aicl_suspend;
	}

	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		i = i - 3;
		goto aicl_boost_back;
	}
	if (chg_vol < aicl_point) {
		i = i - 3; //We DO NOT use 1.2A here
		goto aicl_pre_step;
	} else if (current_ma < 1500) {
		i = i - 2; //We use 1.2A here
		goto aicl_end;
	} else if (current_ma < 2000)
		goto aicl_end;

	i = 6; /* 1750 */
	rc = vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
	msleep(120);

	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		i = i - 3;
		goto aicl_boost_back;
	}
	if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
		i = i - 3;
		goto aicl_suspend;
	}

	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		i = i - 3;
		goto aicl_boost_back;
	}
	if (chg_vol < aicl_point) {
		i = i - 3; //1.2
		goto aicl_pre_step;
	}

	i = 7; /* 2000 */
	rc = vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
	msleep(90);

	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		i = i - 2;
		goto aicl_boost_back;
	}
	if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
		i = i - 2;
		goto aicl_suspend;
	}

	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		i = i - 2;
		goto aicl_boost_back;
	}
	if (chg_vol < aicl_point) {
		i =  i - 2;//1.5
		goto aicl_pre_step;
	} else if (current_ma < 3000)
		goto aicl_end;

	i = 8; /* 3000 */
	rc = vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
	msleep(90);

	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		i = i - 1;
		goto aicl_boost_back;
	}
	if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
		i = i - 1;
		goto aicl_suspend;
	}

	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma >= 3000)
		goto aicl_end;

aicl_pre_step:
	rc = vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
	chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, i, usb_icl[i], aicl_point);
	smbchg_rerun_aicl();
	smbchg_usbin_collapse_irq_enable(true);
	goto aicl_return;
aicl_end:
	rc = vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
	chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, i, usb_icl[i], aicl_point);
	smbchg_rerun_aicl();
	smbchg_usbin_collapse_irq_enable(true);
	goto aicl_return;
aicl_boost_back:
	rc = vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
	chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_boost_back\n", chg_vol, i, usb_icl[i], aicl_point);
	if (chip->pmic_spmi.smb5_chip->chg.wa_flags & BOOST_BACK_WA)
		vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER, false, 0);
	smbchg_rerun_aicl();
	smbchg_usbin_collapse_irq_enable(true);
	goto aicl_return;
aicl_suspend:
	rc = vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
	chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_suspend\n", chg_vol, i, usb_icl[i], aicl_point);
	oppo_chg_check_clear_suspend();
	smbchg_rerun_aicl();
	smbchg_usbin_collapse_irq_enable(true);
	goto aicl_return;
aicl_return:
	/*FORCE icl 500mA for AUDIO_ADAPTER combo cable*/
	if (chip->pmic_spmi.smb5_chip->chg.typec_mode == POWER_SUPPLY_TYPEC_SINK_AUDIO_ADAPTER) {
		chg_debug( "AUDIO ADAPTER MODE\n");
		rc = smblib_read(&chip->pmic_spmi.smb5_chip->chg, USBIN_LOAD_CFG_REG, &stat);
		if (rc < 0) {
			chg_debug( "read USBIN_LOAD_CFG_REG, failed rc=%d\n", rc);
		}
		if ((bool)(stat& ICL_OVERRIDE_AFTER_APSD_BIT)) {
			rc = smblib_write(&chip->pmic_spmi.smb5_chip->chg, USBIN_CURRENT_LIMIT_CFG_REG, 0x14);
			if (rc < 0) {
				chg_debug( "Couldn't write USBIN_CURRENT_LIMIT_CFG_REG rc=%d\n", rc);
			} else {
				chg_debug( "FORCE icl 500\n");
			}
		}
	}
	return rc;
}

static int smbchg_float_voltage_set(int vfloat_mv)
{
	int rc = 0;
	struct oppo_chg_chip *chip = g_oppo_chip;

	rc = vote(chip->pmic_spmi.smb5_chip->chg.fv_votable, BATT_PROFILE_VOTER/*DEFAULT_VOTER*/,
			true, vfloat_mv * 1000);
	if (rc < 0)
		chg_err("Couldn't vote fv_votable[%d], rc=%d\n", vfloat_mv, rc);

	return rc;
}

static int smbchg_term_current_set(int term_current)
{
	int rc = 0;
	u8 val_raw = 0;
	struct oppo_chg_chip *chip = g_oppo_chip;

	if (term_current < 0 || term_current > 750)
		term_current = 150;

	val_raw = term_current / 50;
	rc = smblib_masked_write(&chip->pmic_spmi.smb5_chip->chg, TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG,
			TCCC_CHARGE_CURRENT_TERMINATION_SETTING_MASK, val_raw);
	if (rc < 0)
		chg_err("Couldn't write TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG rc=%d\n", rc);

	return rc;
}

static int smbchg_charging_enble(void)
{
	int rc = 0;
	struct oppo_chg_chip *chip = g_oppo_chip;

	rc = vote(chip->pmic_spmi.smb5_chip->chg.chg_disable_votable, DEFAULT_VOTER,
			false, 0);
	if (rc < 0)
		chg_err("Couldn't enable charging, rc=%d\n", rc);

	return rc;
}

static int smbchg_charging_disble(void)
{
	int rc = 0;
	struct oppo_chg_chip *chip = g_oppo_chip;

	rc = vote(chip->pmic_spmi.smb5_chip->chg.chg_disable_votable, DEFAULT_VOTER,
			true, 0);
	if (rc < 0)
		chg_err("Couldn't disable charging, rc=%d\n", rc);

	chip->pmic_spmi.smb5_chip->chg.pre_current_ma = -1;

	return rc;
}

static int smbchg_get_charge_enable(void)
{
	int rc = 0;
	u8 temp = 0;
	struct oppo_chg_chip *chip = g_oppo_chip;

	rc = smblib_read(&chip->pmic_spmi.smb5_chip->chg, CHARGING_ENABLE_CMD_REG, &temp);
	if (rc < 0) {
		chg_err("Couldn't read CHARGING_ENABLE_CMD_REG rc=%d\n", rc);
		return 0;
	}
	rc = temp & CHARGING_ENABLE_CMD_BIT;

	return rc;
}

static int smbchg_usb_suspend_enable(void)
{
	int rc = 0;
	struct oppo_chg_chip *chip = g_oppo_chip;

	rc = smblib_set_usb_suspend(&chip->pmic_spmi.smb5_chip->chg, true);
	if (rc < 0)
		chg_err("Couldn't write enable to USBIN_SUSPEND_BIT rc=%d\n", rc);

	chip->pmic_spmi.smb5_chip->chg.pre_current_ma = -1;

	return rc;
}

static int smbchg_usb_suspend_disable(void)
{
	int rc = 0;
	int boot_mode = get_boot_mode();
	struct oppo_chg_chip *chip = g_oppo_chip;

	if (boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN) {
		chg_err("RF/WLAN, suspending...\n");
		rc = smblib_set_usb_suspend(&chip->pmic_spmi.smb5_chip->chg, true);
		if (rc < 0)
			chg_err("Couldn't write enable to USBIN_SUSPEND_BIT rc=%d\n", rc);
		return rc;
	}

	rc = smblib_set_usb_suspend(&chip->pmic_spmi.smb5_chip->chg, false);
	if (rc < 0)
		chg_err("Couldn't write disable to USBIN_SUSPEND_BIT rc=%d\n", rc);

	return rc;
}

static int smbchg_set_rechg_vol(int rechg_vol)
{
	return 0;
}

static int smbchg_reset_charger(void)
{
	return 0;
}

static int smbchg_read_full(void)
{
	int rc = 0;
	u8 stat = 0;
	struct oppo_chg_chip *chip = g_oppo_chip;

	if (!oppo_chg_is_usb_present())
		return 0;

	rc = smblib_read(&chip->pmic_spmi.smb5_chip->chg, BATTERY_CHARGER_STATUS_1_REG, &stat);
	if (rc < 0) {
		chg_err("Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n", rc);
		return 0;
	}
	stat = stat & BATTERY_CHARGER_STATUS_MASK;
#ifndef VENDOR_EDIT
/* Kun.Zhang@BSP.CHG.Basic, 2019/10/15,  Add for CHG */
	if (stat == TERMINATE_CHARGE || stat == INHIBIT_CHARGE)
		return 1;
	return 0;
#else
	return 0;
#endif
}

static int smbchg_otg_enable(void)
{
	return 0;
}

static int smbchg_otg_disable(void)
{
	return 0;
}

static int oppo_set_chging_term_disable(void)
{
	return 0;
}

static bool qcom_check_charger_resume(void)
{
	return true;
}

bool smbchg_need_to_check_ibatt(void)
{
	return false;
}

static int smbchg_get_chg_current_step(void)
{
	return 25;
}
extern int opchg_get_charger_type(void);
int qpnp_get_prop_charger_voltage_now(void)
{
	int val = 0, rc = 0;
	union power_supply_propval pval = {0, };
	struct smb_charger *chg = NULL;
	struct oppo_chg_chip *chip = g_oppo_chip;
	
	if (!chip)
		return 0;

	//if (!oppo_chg_is_usb_present())
	//	return 0;
	chg = &chip->pmic_spmi.smb5_chip->chg;
	rc = smblib_get_prop_usb_present(chg, &pval);
	if (rc < 0) {
		chg_err("Couldn't get usb presence status rc=%d\n", rc);
		return -ENODATA;
	}

	/* usb not present */
	if (!pval.intval) {
		val = 0;
		return 0;
	}
	if (chg->smb_version != PM8150B_SUBTYPE) {
		if (!chg->iio.usbin_v_chan || PTR_ERR(chg->iio.usbin_v_chan) == -EPROBE_DEFER)
			chg->iio.usbin_v_chan = iio_channel_get(chg->dev, "usbin_v");

		if (IS_ERR(chg->iio.usbin_v_chan))
			return PTR_ERR(chg->iio.usbin_v_chan);

		iio_read_channel_processed(chg->iio.usbin_v_chan, &val);
	} else {
		if (!chg->iio.mid_chan || PTR_ERR(chg->iio.mid_chan) == -EPROBE_DEFER)
			chg->iio.mid_chan = iio_channel_get(chg->dev, "mid_voltage");

		if (IS_ERR(chg->iio.mid_chan))
			return PTR_ERR(chg->iio.mid_chan);

		iio_read_channel_processed(chg->iio.mid_chan, &val);

		if ((val / 1000) < chip->batt_volt) {
			if (oppo_vooc_get_fastchg_started() == true) {
				val = (chip->batt_volt + 300) * 1000 ;
			}
		}
	}

	if (val < 2000 * 1000)
		chg->pre_current_ma = -1;
	return val / 1000;
}

bool oppo_chg_is_usb_present(void)
{
	int rc = 0;
	u8 stat = 0;
	bool vbus_rising = false;
	struct oppo_chg_chip *chip = g_oppo_chip;
	
	if (!chip)
		return false;

#ifdef VENDOR_EDIT//Fanhong.Kong@ProDrv.CHG,add 2018/06/02 for SVOOC OTG
	if ((chip->pmic_spmi.smb5_chip->chg.typec_mode == POWER_SUPPLY_TYPEC_SINK
		|| chip->pmic_spmi.smb5_chip->chg.typec_mode == POWER_SUPPLY_TYPEC_SINK_POWERED_CABLE)
		&& chip->vbatt_num == 2 ) {
		chg_err("chg->typec_mode = SINK,oppo_chg_is_usb_present return false!\n");
		rc = false;
		return rc ;
	}
#endif/*VENDOR_EDIT*/

	rc = smblib_read(&chip->pmic_spmi.smb5_chip->chg, USBIN_BASE + INT_RT_STS_OFFSET, &stat);
	if (rc < 0) {
		chg_err("Couldn't read USB_INT_RT_STS, rc=%d\n", rc);
		return false;
	}
	vbus_rising = (bool)(stat & USBIN_PLUGIN_RT_STS_BIT);

	if (vbus_rising == false && oppo_vooc_get_fastchg_started() == true) {
		if (qpnp_get_prop_charger_voltage_now() > 2000) {
			chg_err("USBIN_PLUGIN_RT_STS_BIT low but fastchg started true and chg vol > 2V\n");
			vbus_rising = true;
		}
	}
#ifdef VENDOR_EDIT//Fanhong.Kong@ProDrv.CHG,add 2018/04/20 for SVOOC
	if (vbus_rising == false && (oppo_vooc_get_fastchg_started() == true && (chip->vbatt_num == 2))) {
			chg_err("USBIN_PLUGIN_RT_STS_BIT low but fastchg started true and SVOOC\n");
			vbus_rising = true;
	}
#endif/*VENDOR_EDIT*/

	if (vbus_rising == false)
		chip->pmic_spmi.smb5_chip->chg.pre_current_ma = -1;

	return vbus_rising;
}


int qpnp_get_battery_voltage(void)
{
	return 3800;//Not use anymore
}
#if 0
static int get_boot_mode(void)
{
	return 0;
}
#endif
int smbchg_get_boot_reason(void)
{
	return 0;
}

int oppo_chg_get_shutdown_soc(void)
{
	return 0;
}

int oppo_chg_backup_soc(int backup_soc)
{
	return 0;
}

static int smbchg_get_aicl_level_ma(void)
{
	return 0;
}

static int smbchg_force_tlim_en(bool enable)
{
	return 0;
}

static int smbchg_system_temp_level_set(int lvl_sel)
{
	return 0;
}

static int smbchg_set_prop_flash_active(enum skip_reason reason, bool disable)
{
	return 0;
}

static int smbchg_dp_dm(int val)
{
	return 0;
}

static int smbchg_calc_max_flash_current(void)
{
	return 0;
}

static int oppo_chg_get_fv(struct oppo_chg_chip *chip)
{
	int flv = chip->limits.temp_normal_vfloat_mv;
	int batt_temp = chip->temperature;

	if (batt_temp > chip->limits.hot_bat_decidegc) {//53C
		//default
	} else if (batt_temp >= chip->limits.warm_bat_decidegc) {//45C
		flv = chip->limits.temp_warm_vfloat_mv;
	} else if (batt_temp >= chip->limits.normal_bat_decidegc) {//16C
		flv = chip->limits.temp_normal_vfloat_mv;
	} else if (batt_temp >= chip->limits.little_cool_bat_decidegc) {//12C
		flv = chip->limits.temp_little_cool_vfloat_mv;
	} else if (batt_temp >= chip->limits.cool_bat_decidegc) {//5C
		flv = chip->limits.temp_cool_vfloat_mv;
	} else if (batt_temp >= chip->limits.little_cold_bat_decidegc) {//0C
		flv = chip->limits.temp_little_cold_vfloat_mv;
	} else if (batt_temp >= chip->limits.cold_bat_decidegc) {//-3C
		flv = chip->limits.temp_cold_vfloat_mv;
	} else {
		//default
	}

	return flv;
}

static int oppo_chg_get_charging_current(struct oppo_chg_chip *chip)
{
	int charging_current = 0;
	int batt_temp = chip->temperature;

	if (batt_temp > chip->limits.hot_bat_decidegc) {//53C
		charging_current = 0;
	} else if (batt_temp >= chip->limits.warm_bat_decidegc) {//45C
		charging_current = chip->limits.temp_warm_fastchg_current_ma;
	} else if (batt_temp >= chip->limits.normal_bat_decidegc) {//16C
		charging_current = chip->limits.temp_normal_fastchg_current_ma;
	} else if (batt_temp >= chip->limits.little_cool_bat_decidegc) {//12C
		charging_current = chip->limits.temp_little_cool_fastchg_current_ma;
	} else if (batt_temp >= chip->limits.cool_bat_decidegc) {//5C
		if (chip->batt_volt > 4180)
			charging_current = chip->limits.temp_cool_fastchg_current_ma_low;
		else
			charging_current = chip->limits.temp_cool_fastchg_current_ma_high;
	} else if (batt_temp >= chip->limits.little_cold_bat_decidegc) {//0C
		charging_current = chip->limits.temp_little_cold_fastchg_current_ma;
	} else if (batt_temp >= chip->limits.cold_bat_decidegc) {//-3C
		charging_current = chip->limits.temp_cold_fastchg_current_ma;
	} else {
		charging_current = 0;
	}

	return charging_current;
}

#ifdef CONFIG_OPPO_RTC_DET_SUPPORT
static int rtc_reset_check(void)
{
	struct rtc_time tm;
	struct rtc_device *rtc;
	int rc = 0;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return 0;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		pr_err("Error reading rtc device (%s) : %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		pr_err("Invalid RTC time (%s): %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	if ((tm.tm_year == 70) && (tm.tm_mon == 0) && (tm.tm_mday <= 1)) {
		chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  @@@ wday: %d, yday: %d, isdst: %d\n",
			tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
			tm.tm_wday, tm.tm_yday, tm.tm_isdst);
		rtc_class_close(rtc);
		return 1;
	}

	chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  ###  wday: %d, yday: %d, isdst: %d\n",
		tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
		tm.tm_wday, tm.tm_yday, tm.tm_isdst);

close_time:
	rtc_class_close(rtc);
	return 0;
}
#endif /* CONFIG_OPPO_RTC_DET_SUPPORT */

#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
/* This function is getting the dynamic aicl result/input limited in mA.
 * If charger was suspended, it must return 0(mA).
 * It meets the requirements in SDM660 platform.
 */
static int oppo_chg_get_dyna_aicl_result(void)
{
	struct power_supply *usb_psy = NULL;
	union power_supply_propval pval = {0, };

	usb_psy = power_supply_get_by_name("usb");
	if (usb_psy) {
		power_supply_get_property(usb_psy,
				POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
				&pval);
		return pval.intval / 1000;
	}

	return 1000;
}
#endif /* CONFIG_OPPO_SHORT_C_BATT_CHECK */

static int get_current_time(unsigned long *now_tm_sec)
{
	struct rtc_time tm;
	struct rtc_device *rtc;
	int rc;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return -EINVAL;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		pr_err("Error reading rtc device (%s) : %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		pr_err("Invalid RTC time (%s): %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}
	rtc_tm_to_time(&tm, now_tm_sec);

close_time:
	rtc_class_close(rtc);
	return rc;
}

static unsigned long suspend_tm_sec = 0;
static int smb5_pm_resume(struct device *dev)
{
	int rc = 0;
	unsigned long resume_tm_sec = 0;
	unsigned long sleep_time = 0;

	if (!g_oppo_chip)
		return 0;

	rc = get_current_time(&resume_tm_sec);
	if (rc || suspend_tm_sec == -1) {
		chg_err("RTC read failed\n");
		sleep_time = 0;
	} else {
		sleep_time = resume_tm_sec - suspend_tm_sec;
	}

	if (sleep_time < 0) {
		sleep_time = 0;
	}

	oppo_chg_soc_update_when_resume(sleep_time);

	return 0;
}

static int smb5_pm_suspend(struct device *dev)
{
	if (!g_oppo_chip)
		return 0;

	if (get_current_time(&suspend_tm_sec)) {
		chg_err("RTC read failed\n");
		suspend_tm_sec = -1;
	}

	return 0;
}

static const struct dev_pm_ops smb5_pm_ops = {
	.resume		= smb5_pm_resume,
	.suspend		= smb5_pm_suspend,
};

struct oppo_chg_operations  smb5_chg_ops = {
	.dump_registers = dump_regs,
	.kick_wdt = smbchg_kick_wdt,
	.hardware_init = oppo_chg_hw_init,
	.charging_current_write_fast = smbchg_set_fastchg_current_raw,
	.set_aicl_point = smbchg_set_aicl_point,
	.input_current_write = oppo_chg_set_input_current,
	.float_voltage_write = smbchg_float_voltage_set,
	.term_current_set = smbchg_term_current_set,
	.charging_enable = smbchg_charging_enble,
	.charging_disable = smbchg_charging_disble,
	.get_charging_enable = smbchg_get_charge_enable,
	.charger_suspend = smbchg_usb_suspend_enable,
	.charger_unsuspend = smbchg_usb_suspend_disable,
	.set_rechg_vol = smbchg_set_rechg_vol,
	.reset_charger = smbchg_reset_charger,
	.read_full = smbchg_read_full,
	.otg_enable = smbchg_otg_enable,
	.otg_disable = smbchg_otg_disable,
	.set_charging_term_disable = oppo_set_chging_term_disable,
	.check_charger_resume = qcom_check_charger_resume,
	.get_chargerid_volt = smbchg_get_chargerid_volt,
	.set_chargerid_switch_val = smbchg_set_chargerid_switch_val,
	.get_chargerid_switch_val = smbchg_get_chargerid_switch_val,
	.need_to_check_ibatt = smbchg_need_to_check_ibatt,
	.get_chg_current_step = smbchg_get_chg_current_step,
#ifdef CONFIG_OPPO_CHARGER_MTK
	.get_charger_type = mt_power_supply_type_check,
	.get_charger_volt = battery_meter_get_charger_voltage,
	.check_chrdet_status = pmic_chrdet_status,
	.get_instant_vbatt = battery_meter_get_battery_voltage,
	.get_boot_mode = get_boot_mode,
	.get_boot_reason = get_boot_reason,
#ifdef CONFIG_MTK_HAFG_20
	.get_rtc_soc = get_rtc_spare_oppo_fg_value,
	.set_rtc_soc = set_rtc_spare_oppo_fg_value,
#else
	.get_rtc_soc = get_rtc_spare_fg_value,
	.set_rtc_soc = set_rtc_spare_fg_value,
#endif	/* CONFIG_MTK_HAFG_20 */
	.set_power_off = mt_power_off,
	.usb_connect = mt_usb_connect,
	.usb_disconnect = mt_usb_disconnect,
#else
	.get_charger_type = opchg_get_charger_type,
	.get_charger_volt = qpnp_get_prop_charger_voltage_now,
	.check_chrdet_status = oppo_chg_is_usb_present,
	.get_instant_vbatt = qpnp_get_battery_voltage,
	.get_boot_mode = get_boot_mode,
	.get_boot_reason = smbchg_get_boot_reason,
	.get_rtc_soc = oppo_chg_get_shutdown_soc,
	.set_rtc_soc = oppo_chg_backup_soc,
	.get_aicl_ma = smbchg_get_aicl_level_ma,
	.rerun_aicl = smbchg_rerun_aicl,
	.tlim_en = smbchg_force_tlim_en,
	.set_system_temp_level = smbchg_system_temp_level_set,
	.otg_pulse_skip_disable = smbchg_set_prop_flash_active,
	.set_dp_dm = smbchg_dp_dm,
	.calc_flash_current = smbchg_calc_max_flash_current,
#endif	/* CONFIG_OPPO_CHARGER_MTK */
#ifdef CONFIG_OPPO_RTC_DET_SUPPORT
	.check_rtc_reset = rtc_reset_check,
#endif
#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
	.get_dyna_aicl_result = oppo_chg_get_dyna_aicl_result,
#endif
	.get_shortc_hw_gpio_status = oppo_chg_get_shortc_hw_gpio_status,
	.get_charger_subtype = oppo_chg_get_charger_subtype,
	.oppo_chg_pd_setup = oppo_chg_set_pd_config,
	.oppo_chg_get_pd_type = oppo_sm7250_get_pd_type,
};
#endif /* VENDOR_EDIT */

static int smb5_show_charger_status(struct smb5 *chip)
{
	struct smb_charger *chg = &chip->chg;
	union power_supply_propval val;
	int usb_present, batt_present, batt_health, batt_charge_type;
	int rc;

	rc = smblib_get_prop_usb_present(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get usb present rc=%d\n", rc);
		return rc;
	}
	usb_present = val.intval;

	rc = smblib_get_prop_batt_present(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get batt present rc=%d\n", rc);
		return rc;
	}
	batt_present = val.intval;

#ifndef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/04/27, sjc Modify for charging */
	rc = smblib_get_prop_batt_health(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get batt health rc=%d\n", rc);
		val.intval = POWER_SUPPLY_HEALTH_UNKNOWN;
	}
	batt_health = val.intval;
#else
    if(g_oppo_chip)
    	batt_health = oppo_chg_get_prop_batt_health(g_oppo_chip);
#endif

	rc = smblib_get_prop_batt_charge_type(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get batt charge type rc=%d\n", rc);
		return rc;
	}
	batt_charge_type = val.intval;

	pr_info("SMB5 status - usb:present=%d type=%d batt:present = %d health = %d charge = %d\n",
		usb_present, chg->real_charger_type,
		batt_present, batt_health, batt_charge_type);
	return rc;
}

static int smb5_probe(struct platform_device *pdev)
{
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
	struct oppo_chg_chip *oppo_chip;
	struct power_supply *main_psy = NULL;
	union power_supply_propval pval = {0, };
#endif
	struct smb5 *chip;
	struct smb_charger *chg;
	int rc = 0;
#ifdef VENDOR_EDIT//Fanhong.Kong@ProDrv.CHG,add 2018/04/20 for SVOOC
	chg_err("into smb5_probe----\n");

	oppo_chip = devm_kzalloc(&pdev->dev, sizeof(*oppo_chip), GFP_KERNEL);
	if (!oppo_chip)
		return -ENOMEM;
	oppo_chip->dev = &pdev->dev;

	rc = oppo_chg_parse_svooc_dt(oppo_chip);

	if (oppo_chip->vbatt_num == 1) {
		if (oppo_gauge_check_chip_is_null()) {
			chg_err("gauge chip null, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}
		oppo_chip->chg_ops = &smb5_chg_ops;
	} else {
		if (oppo_gauge_ic_chip_is_null() || oppo_vooc_check_chip_is_null()
				|| oppo_charger_ic_chip_is_null() || oppo_adapter_check_chip_is_null()) {
			chg_err("[oppo_chg_init] vooc || gauge || chg not ready, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}
		oppo_chip->chg_ops = (oppo_get_chg_ops());
	}

	g_oppo_chip = oppo_chip;
	chg_debug("smb5_Probe Start----\n");
#endif

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
	oppo_chip->pmic_spmi.smb5_chip = chip;

#endif
	chg = &chip->chg;
	chg->dev = &pdev->dev;
	chg->debug_mask = &__debug_mask;
	chg->pd_disabled = 0;
	chg->weak_chg_icl_ua = 500000;
	chg->mode = PARALLEL_MASTER;
	chg->irq_info = smb5_irqs;
	chg->die_health = -EINVAL;
	chg->connector_health = -EINVAL;
	chg->otg_present = false;
	chg->main_fcc_max = -EINVAL;
	mutex_init(&chg->adc_lock);

	chg->regmap = dev_get_regmap(chg->dev->parent, NULL);
	if (!chg->regmap) {
		pr_err("parent regmap is missing\n");
		return -EINVAL;
	}

	rc = smb5_chg_config_init(chip);
	if (rc < 0) {
		if (rc != -EPROBE_DEFER)
			pr_err("Couldn't setup chg_config rc=%d\n", rc);
		return rc;
	}
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/08/10, sjc Add for charging */
	mutex_init(&chg->pinctrl_mutex);
	chg->pre_current_ma = -1;
#endif
#ifdef VENDOR_EDIT
	/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/22, sjc Add for charging*/
	rc = of_property_match_string(chg->dev->of_node, "io-channel-names", "chgID_voltage_adc");
	if (rc >= 0) {
		chg->iio.chgid_v_chan = iio_channel_get(chg->dev,
				"chgID_voltage_adc");
		if (IS_ERR(chg->iio.chgid_v_chan)) {
			rc = PTR_ERR(chg->iio.chgid_v_chan);
			if (rc != -EPROBE_DEFER)
				dev_err(chg->dev, "chgid_v_chan  get  error, %ld\n",	rc);
			chg->iio.chgid_v_chan = NULL;
			return rc;
		}
		pr_err("[OPPO_CHG] test chg->iio.chgid_v_chan \n");
	}
#endif
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/05/25, sjc Add for usbtemp */
    rc = smblib_get_iio_channel(chg, "usb_temp_l",
					&chg->iio.usbtemp_v_chan_l);

    rc = smblib_get_iio_channel(chg, "usb_temp_r",
					&chg->iio.usbtemp_v_chan_r);
#endif

	rc = smb5_parse_dt(chip);
	if (rc < 0) {
		pr_err("Couldn't parse device tree rc=%d\n", rc);
		return rc;
	}

	if (alarmtimer_get_rtcdev())
		alarm_init(&chg->lpd_recheck_timer, ALARM_REALTIME,
				smblib_lpd_recheck_timer);
	else
		return -EPROBE_DEFER;

	rc = smblib_init(chg);
	if (rc < 0) {
		pr_err("Smblib_init failed rc=%d\n", rc);
		return rc;
	}

	/* set driver data before resources request it */
	platform_set_drvdata(pdev, chip);

	/* extcon registration */
	chg->extcon = devm_extcon_dev_allocate(chg->dev, smblib_extcon_cable);
	if (IS_ERR(chg->extcon)) {
		rc = PTR_ERR(chg->extcon);
		dev_err(chg->dev, "failed to allocate extcon device rc=%d\n",
				rc);
		goto cleanup;
	}

	rc = devm_extcon_dev_register(chg->dev, chg->extcon);
	if (rc < 0) {
		dev_err(chg->dev, "failed to register extcon device rc=%d\n",
				rc);
		goto cleanup;
	}

	/* Support reporting polarity and speed via properties */
	rc = extcon_set_property_capability(chg->extcon,
			EXTCON_USB, EXTCON_PROP_USB_TYPEC_POLARITY);
	rc |= extcon_set_property_capability(chg->extcon,
			EXTCON_USB, EXTCON_PROP_USB_SS);
	rc |= extcon_set_property_capability(chg->extcon,
			EXTCON_USB_HOST, EXTCON_PROP_USB_TYPEC_POLARITY);
	rc |= extcon_set_property_capability(chg->extcon,
			EXTCON_USB_HOST, EXTCON_PROP_USB_SS);
	if (rc < 0) {
		dev_err(chg->dev,
			"failed to configure extcon capabilities\n");
		goto cleanup;
	}

	rc = smb5_init_hw(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize hardware rc=%d\n", rc);
		goto cleanup;
	}

	/*
	 * VBUS regulator enablement/disablement for host mode is handled
	 * by USB-PD driver only. For micro-USB and non-PD typeC designs,
	 * the VBUS regulator is enabled/disabled by the smb driver itself
	 * before sending extcon notifications.
	 * Hence, register vbus and vconn regulators for PD supported designs
	 * only.
	 */
	if (!chg->pd_not_supported) {
		rc = smb5_init_vbus_regulator(chip);
		if (rc < 0) {
			pr_err("Couldn't initialize vbus regulator rc=%d\n",
				rc);
			goto cleanup;
		}

		rc = smb5_init_vconn_regulator(chip);
		if (rc < 0) {
			pr_err("Couldn't initialize vconn regulator rc=%d\n",
				rc);
			goto cleanup;
		}
	}

	switch (chg->smb_version) {
	case PM8150B_SUBTYPE:
	case PM6150_SUBTYPE:
	case PM7250B_SUBTYPE:
#ifndef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Delete for charging*/
		rc = smb5_init_dc_psy(chip);
		if (rc < 0) {
			pr_err("Couldn't initialize dc psy rc=%d\n", rc);
			goto cleanup;
		}
#endif
		break;
	default:
		break;
	}

//kong
	rc = oppo_power_supply_init(chip);
	if (rc < 0) {
			pr_err("Couldn't initialize usb main psy rc=%d\n", rc);
			goto cleanup;
	}
	
	rc = smb5_init_usb_main_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize usb main psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb5_init_usb_port_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize usb pc_port psy rc=%d\n", rc);
		goto cleanup;
	}
	#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/04/11, sjc Add for charging*/
	if (oppo_chg_is_usb_present()) {
		rc = smblib_masked_write(chg, CHARGING_ENABLE_CMD_REG,
				CHARGING_ENABLE_CMD_BIT, 0);
		if (rc < 0)
			pr_err("Couldn't disable at bootup rc=%d\n", rc);
		msleep(100);
		rc = smblib_masked_write(chg, CHARGING_ENABLE_CMD_REG,
				CHARGING_ENABLE_CMD_BIT, CHARGING_ENABLE_CMD_BIT);
		if (rc < 0)
			pr_err("Couldn't enable at bootup rc=%d\n", rc);
	}
#endif
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
	oppo_chg_parse_custom_dt(oppo_chip);
	oppo_chg_parse_charger_dt(oppo_chip);
    	oppo_chg_2uart_pinctrl_init(oppo_chip);
	oppo_chg_init(oppo_chip);
	main_psy = power_supply_get_by_name("main");
	if (main_psy) {
		pval.intval = 1000 * oppo_chg_get_fv(oppo_chip);
		power_supply_set_property(main_psy,
				POWER_SUPPLY_PROP_VOLTAGE_MAX,
				&pval);
		pval.intval = 1000 * oppo_chg_get_charging_current(oppo_chip);
		power_supply_set_property(main_psy,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
				&pval);
	}
#endif
#ifndef VENDOR_EDIT
	rc = smb5_init_usb_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize usb psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb5_init_usb_main_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize usb main psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb5_init_usb_port_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize usb pc_port psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb5_init_batt_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize batt psy rc=%d\n", rc);
		goto cleanup;
	}
#endif
	rc = smb5_determine_initial_status(chip);
	if (rc < 0) {
		pr_err("Couldn't determine initial status rc=%d\n",
			rc);
		goto cleanup;
	}

	rc = smb5_request_interrupts(chip);
	if (rc < 0) {
		pr_err("Couldn't request interrupts rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb5_post_init(chip);
	if (rc < 0) {
		pr_err("Failed in post init rc=%d\n", rc);
		goto free_irq;
	}
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/01/30, sjc Add for using gpio as CC detect */
    if (oppo_ccdetect_check_is_gpio(oppo_chip) == true)
        oppo_ccdetect_irq_register(oppo_chip);
#endif
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
//	g_oppo_chip->authenticate = oppo_gauge_get_batt_authenticate();
//	if(!g_oppo_chip->authenticate)
//		smbchg_charging_disble();
	oppo_chg_wake_update_work();
	oppo_tbatt_power_off_task_init(oppo_chip);
#endif
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/05/22, sjc Add for dump register */
	init_proc_dump_registers_mask();
#endif

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/05/25, sjc Add for usbtemp */
	if (oppo_usbtemp_check_is_support() == true)
		oppo_usbtemp_thread_init();
#endif
	smb5_create_debugfs(chip);

	rc = sysfs_create_groups(&chg->dev->kobj, smb5_groups);
	if (rc < 0) {
		pr_err("Couldn't create sysfs files rc=%d\n", rc);
		goto free_irq;
	}

	rc = smb5_show_charger_status(chip);
	if (rc < 0) {
		pr_err("Failed in getting charger status rc=%d\n", rc);
		goto free_irq;
	}

	device_init_wakeup(chg->dev, true);

	pr_info("QPNP SMB5 probed successfully\n");

	return rc;

free_irq:
	smb5_free_interrupts(chg);
cleanup:
	smblib_deinit(chg);
	platform_set_drvdata(pdev, NULL);

	return rc;
}

static int smb5_remove(struct platform_device *pdev)
{
	struct smb5 *chip = platform_get_drvdata(pdev);
	struct smb_charger *chg = &chip->chg;

	/* force enable APSD */
	smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG,
				BC1P2_SRC_DETECT_BIT, BC1P2_SRC_DETECT_BIT);

	smb5_free_interrupts(chg);
	smblib_deinit(chg);
	sysfs_remove_groups(&chg->dev->kobj, smb5_groups);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static void smb5_shutdown(struct platform_device *pdev)
{
	struct smb5 *chip = platform_get_drvdata(pdev);
	struct smb_charger *chg = &chip->chg;

#ifdef VENDOR_EDIT
	/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/22, sjc Add for charging*/
	if (g_oppo_chip) {
		oppo_vooc_reset_mcu();
		smbchg_set_chargerid_switch_val(0);
		oppo_vooc_switch_mode(NORMAL_CHARGER_MODE);
		msleep(30);
	}
#endif

	/* disable all interrupts */
	smb5_disable_interrupts(chg);

	/* configure power role for UFP */
	if (chg->connector_type == POWER_SUPPLY_CONNECTOR_TYPEC)
		smblib_masked_write(chg, TYPE_C_MODE_CFG_REG,
				TYPEC_POWER_ROLE_CMD_MASK, EN_SNK_ONLY_BIT);

	/* force enable and rerun APSD */
	smblib_apsd_enable(chg, true);
	smblib_hvdcp_exit_config(chg);

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/03/02, sjc Add for using gpio as shipmode stm6620 */

	if (g_oppo_chip && g_oppo_chip->enable_shipmode) {
		msleep(1000);
		smbchg_enter_shipmode(g_oppo_chip);
	}
#endif /* VENDOR_EDIT */
}

static const struct of_device_id match_table[] = {
	{ .compatible = "qcom,qpnp-smb5", },
	{ },
};

static struct platform_driver smb5_driver = {
	.driver		= {
		.name		= "qcom,qpnp-smb5",
		.of_match_table	= match_table,
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/25, sjc Add for charging */
		.pm		= &smb5_pm_ops,
#endif
	},
	.probe		= smb5_probe,
	.remove		= smb5_remove,
	.shutdown	= smb5_shutdown,
};
module_platform_driver(smb5_driver);

MODULE_DESCRIPTION("QPNP SMB5 Charger Driver");
MODULE_LICENSE("GPL v2");
