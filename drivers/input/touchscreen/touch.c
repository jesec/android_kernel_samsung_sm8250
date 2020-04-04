/***************************************************
 * File:touch.c
 * VENDOR_EDIT
 * Copyright (c)  2008- 2030  Oppo Mobile communication Corp.ltd.
 * Description:
 *             tp dev
 * Version:1.0:
 * Date created:2016/09/02
 * Author: hao.wang@Bsp.Driver
 * TAG: BSP.TP.Init
*/

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include "oppo_touchscreen/tp_devices.h"
#include "oppo_touchscreen/touchpanel_common.h"
#include <soc/oppo/oppo_project.h>
#include <soc/oppo/device_info.h>
#include "touch.h"

#define MAX_LIMIT_DATA_LENGTH         100

extern char *saved_command_line;
int g_tp_dev_vendor = TP_UNKNOWN;
int g_tp_prj_id = 0;

/*if can not compile success, please update vendor/oppo_touchsreen*/
struct tp_dev_name tp_dev_names[] = {
    {TP_OFILM, "OFILM"},
    {TP_BIEL, "BIEL"},
    {TP_TRULY, "TRULY"},
    {TP_BOE, "BOE"},
    {TP_G2Y, "G2Y"},
    {TP_TPK, "TPK"},
    {TP_JDI, "JDI"},
    {TP_TIANMA, "TIANMA"},
    {TP_SAMSUNG, "SAMSUNG"},
    {TP_DSJM, "DSJM"},
    {TP_BOE_B8, "BOEB8"},
    {TP_UNKNOWN, "UNKNOWN"},
};

//int g_tp_dev_vendor = TP_UNKNOWN;
typedef enum {
    TP_INDEX_NULL,
    himax_83112a,
    ili9881_auo,
    ili9881_tm,
    nt36525b_boe,
    nt36525b_hlt,
    nt36672c,
    ili9881_inx
} TP_USED_INDEX;
TP_USED_INDEX tp_used_index  = TP_INDEX_NULL;


#define GET_TP_DEV_NAME(tp_type) ((tp_dev_names[tp_type].type == (tp_type))?tp_dev_names[tp_type].name:"UNMATCH")

bool __init tp_judge_ic_match(char *tp_ic_name)
{


	pr_err("[TP] tp_ic_name = %s \n", tp_ic_name);
	pr_err("[TP] boot_command_line = %s \n", saved_command_line);

	switch(get_project()) {
		case 19101:
			if (strstr(tp_ic_name, "synaptics-s3908") && (strstr(saved_command_line, "mdss_dsi_oppo19101boe_nt37800_1080_2400_cmd")
				|| strstr(saved_command_line, "mdss_dsi_oppo19101boe_nt37800_1080_2340_cmd"))) {
				g_tp_dev_vendor = TP_BOE;
				return true;
			}
			if (strstr(tp_ic_name, "synaptics-s3706") && (strstr(saved_command_line, "mdss_dsi_oppo19125samsung_s6e3fc2x01_1080_2340_cmd")
				|| strstr(saved_command_line, "mdss_dsi_oppo19125samsung_ams641rw01_1080_2340_cmd"))) {
				g_tp_dev_vendor = TP_SAMSUNG;
				return true;
			}
			if (strstr(tp_ic_name, "sec-s6sy771") && (strstr(saved_command_line, "mdss_dsi_oppo19101samsung_sofx01f_a_1080_2400_cmd")
				|| strstr(saved_command_line, "mdss_dsi_oppo19101samsung_amb655uv01_1080_2400_cmd"))) {
				g_tp_dev_vendor = TP_SAMSUNG;
				return true;
			}
			pr_err("[TP] Driver does not match the project\n");
			break;
		case 19125:
		case 19127:
			if (strstr(tp_ic_name, "synaptics-s3706") && (strstr(saved_command_line, "mdss_dsi_oppo19125samsung_s6e3fc2x01_1080_2340_cmd")
				|| strstr(saved_command_line, "mdss_dsi_oppo19125samsung_ams641rw01_1080_2340_cmd")
				|| strstr(saved_command_line, "mdss_dsi_oppo19101samsung_sofx01f_a_1080_2400_cmd"))) {
				g_tp_dev_vendor = TP_SAMSUNG;
				return true;
			}
			pr_err("[TP] Driver does not match the project\n");
			break;
		case 19751:
		case 19752:
		case 19753:
		case 19700:
			if (strstr(tp_ic_name, "novatek,nf_nt36672c") && (strstr(saved_command_line, "mdss_dsi_oppo19751boe_nt36672c_1080_2400_90fps_vid"))) {
				g_tp_dev_vendor = TP_BOE;
				tp_used_index = nt36672c;
				return true;
			}else if(strstr(tp_ic_name, "novatek,nf_nt36672c") && (strstr(saved_command_line, "mdss_dsi_oppo19751jdi_nt36672c_1080_2400_90fps_vid"))){
				g_tp_dev_vendor = TP_JDI;
				tp_used_index = nt36672c;
				return true;
			}
			else if(strstr(tp_ic_name, "himax,hx83112a_nf") && (strstr(saved_command_line, "mdss_dsi_oppo19751dsjm_hx83112a_1080_2340_vid"))) {
				g_tp_dev_vendor = TP_DSJM;
				tp_used_index = himax_83112a;
				pr_err("[TP] Driver himax_83112a\n");
				return true;
			}
			pr_err("[TP] Driver does not match the project\n");
		default:
			pr_err("Invalid project\n");
			break;
	}
	pr_err("Lcd module not found\n");
	return false;

}

int tp_util_get_vendor(struct hw_resource *hw_res, struct panel_info *panel_data)
{
	char* vendor;

	panel_data->test_limit_name = kzalloc(MAX_LIMIT_DATA_LENGTH, GFP_KERNEL);
	if (panel_data->test_limit_name == NULL) {
		pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
	}

	panel_data->tp_type = g_tp_dev_vendor;

	if (panel_data->tp_type == TP_UNKNOWN) {
		pr_err("[TP]%s type is unknown\n", __func__);
		return 0;
	}
	vendor = GET_TP_DEV_NAME(panel_data->tp_type);
	strcpy(panel_data->manufacture_info.manufacture, vendor);
	snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH,
			"tp/%d/FW_%s_%s.img",
			get_project(), panel_data->chip_name, vendor);

	if (panel_data->test_limit_name) {
		snprintf(panel_data->test_limit_name, MAX_LIMIT_DATA_LENGTH,
			"tp/%d/LIMIT_%s_%s.img",
			get_project(), panel_data->chip_name, vendor);
	}
	if (tp_used_index == himax_83112a) {
		memcpy(panel_data->manufacture_info.version, "HX_DSJM", 7);
		panel_data->firmware_headfile.firmware_data = FW_18621_HX83112A_NF_DSJM;
		panel_data->firmware_headfile.firmware_size = sizeof(FW_18621_HX83112A_NF_DSJM);
	} else if ((tp_used_index == nt36672c) && (g_tp_dev_vendor == TP_BOE) ) {

		memcpy(panel_data->manufacture_info.version, "NVT_BOE", 7);
		panel_data->firmware_headfile.firmware_data = FW_17951_NT36525C_BOE;
		panel_data->firmware_headfile.firmware_size = sizeof(FW_17951_NT36525C_BOE);
	}else if((tp_used_index == nt36672c) && (g_tp_dev_vendor == TP_JDI)){

		memcpy(panel_data->manufacture_info.version, "NVT_JDI", 7);
		panel_data->firmware_headfile.firmware_data = FW_17951_NT36525C_JDI;
		panel_data->firmware_headfile.firmware_size = sizeof(FW_17951_NT36525C_JDI);
	}
	panel_data->manufacture_info.fw_path = panel_data->fw_name;

	pr_info("[TP]vendor:%s fw:%s limit:%s\n",vendor,panel_data->fw_name,
		panel_data->test_limit_name == NULL ? "NO Limit" : panel_data->test_limit_name);

	return 0;
}
int reconfig_power_control(struct touchpanel_data *ts)
{
    int ret = 0;

	if ((tp_used_index == nt36672c) && (g_tp_dev_vendor == TP_JDI)) {
		ts->hw_res.TX_NUM = 16;
		ts->hw_res.RX_NUM = 36;
		pr_info("[TP] Use JDI TX,RX=[%d],[%d]\n", ts->hw_res.TX_NUM, ts->hw_res.RX_NUM);
	}else{
		pr_info("[TP] Use BOE TX,RX=[%d],[%d]\n", ts->hw_res.TX_NUM, ts->hw_res.RX_NUM);
	}
    return ret;
}

