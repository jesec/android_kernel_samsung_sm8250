/************************************************************************************
** Copyright (C), 2008-2019, OPPO Mobile Comm Corp., Ltd
** VENDOR_EDIT
** File: oppo_sensor_devinfo.c
**
** Description:
**		Definitions for sensor devinfo.
**
** Version: 1.0
** Date created: 2019/09/14,17:02
** --------------------------- Revision History: ------------------------------------
* <version>		<date>		<author>		<desc>
**************************************************************************************/
#include "oppo_sensor_devinfo.h"
#include <soc/qcom/subsystem_restart.h>

struct sensor_info * g_chip = NULL;

static struct oppo_als_cali_data *gdata = NULL;

static struct devinfo _info[] = {
	{STK3A5X,"stk33502","SensorTek"},
	{TCS3701,"tcs3701","AMS"},
};
int __attribute__((weak)) register_devinfo(char *name, struct manufacture_info *info) { return 1;}

struct delayed_work sensor_work;
__attribute__((weak)) void oppo_device_dir_redirect(struct sensor_info * chip)
{
	pr_info("%s oppo_device_dir_redirect \n", __func__);
};

static void oppo_sensor_parse_dts(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct sensor_info * chip = platform_get_drvdata(pdev);
	int rc = 0;
	int value = 0;

	pr_info("start \n");

	rc = of_property_read_u32(node, "als-match-id", &value);
	if (rc) {
		chip->als_match_id = STK3A5X;
	} else {
		chip->als_match_id = value;
	}

	rc = of_property_read_u32(node, "als-type", &value);
	if (rc) {
		chip->als_type = NORMAL;
	} else {
		chip->als_type = value;
	}

	rc = of_property_read_u32(node, "ps-type", &value);
	if (rc) {
		chip->ps_type = NORMAL;
	} else {
		chip->ps_type = value;
	}

	rc = of_property_read_u32(node, "ps-cali-type", &value);
	if (rc) {
		chip->ps_cali_type = SOFTWARE_CAIL;
	} else {
		chip->ps_cali_type = value;
	}

	rc = of_property_read_u32(node, "is-unit-device", &value);
	if (rc) {
		chip->is_unit_device = true;
	} else {
		if ( value == 0) {
			chip->is_unit_device = false;
		} else {
			chip->is_unit_device = true;
		}
	}

	rc = of_property_read_u32(node, "is-als-dri", &value);
	if (rc) {
		chip->is_als_dri = false;
	} else {
		if ( value == 0) {
			chip->is_als_dri = false;
		} else {
			chip->is_als_dri = true;
		}
	}

	rc = of_property_read_u32(node, "irq-number", &value);
	if (rc) {
		pr_err("unable to acquire irq_number\n");
	} else {
		chip->irq_number = value;
	}

	rc = of_property_read_u32(node, "bus-number", &value);
	if (rc) {
		pr_err("unable to acquire bus_number\n");
	} else {
		chip->bus_number = value;
	}

	rc = of_property_read_u32(node, "ps-match-id", &value);
	if (rc) {
		pr_err("unable to acquire ps_resource\n");
	} else {
		chip->ps_match_id = value;
	}

	rc = of_property_read_u32(node, "als-factor", &value);
	if (rc) {
		chip->als_factor = 1000;
	} else {
		chip->als_factor = value;
	}
	rc = of_property_read_u32(node, "ak-msensor-dir", &value);
	if (rc) {
		chip->ak_msensor_dir = 0;
	} else {
		chip->ak_msensor_dir = value;
	}
	rc = of_property_read_u32(node, "mx-msensor-dir", &value);
	if (rc) {
		chip->mx_msensor_dir = 0;
	} else {
		chip->mx_msensor_dir = value;
	}
	rc = of_property_read_u32(node, "st-gsensor-dir", &value);
	if (rc) {
		chip->st_gsensor_dir = 0;
	} else {
		chip->st_gsensor_dir = value;
	}
	rc = of_property_read_u32(node, "bs-gsensor-dir", &value);
	if (rc) {
		chip->bs_gsensor_dir = 0;
	} else {
		chip->bs_gsensor_dir = value;
	}
	rc = of_property_read_u32(node, "als-row-coe", &value);
	if (rc) {
		gdata->row_coe = 1000;
	} else {
		gdata->row_coe = value;
	}
	oppo_device_dir_redirect(chip);

	chip->is_ps_initialed = 0;
	chip->is_als_initialed = 0;

	pr_info("%s [ %d%d %d %d] \n", __func__,
	chip->als_factor,
	chip->als_type,
	chip->ps_type,
	chip->ps_cali_type);

	pr_info("%s [ %d%d %d %d] \n", __func__,
	chip->ak_msensor_dir,
	chip->mx_msensor_dir,
	chip->st_gsensor_dir,
	chip->bs_gsensor_dir);

	pr_info("%s [%d %d %d %d %d %d] \n", __func__,
	chip->is_unit_device,
	chip->is_als_dri,
	chip->irq_number,
	chip->bus_number,
	chip->ps_match_id,
	chip->als_match_id);
}

static void sensor_dev_work(struct work_struct *work)
{
	int i = 0;
	int ret = 0;

	if (!g_chip) {
		pr_info("g_chip null\n");
		return;
	}

	pr_info("%s  match_id %d\n", __func__,g_chip->ps_match_id);
	//need check sensor inited
	for (i = 0; i < sizeof(_info)/sizeof(struct devinfo); i++) {
		if (_info[i].id == g_chip->ps_match_id) {
			do {
				ret = register_device_proc("Sensor_alsps", _info[i].ic_name, _info[i].vendor_name);
				pr_info("%s ret %d\n",__func__, ret);
				msleep(5000);
			} while(--retry_count && ret);
			break;
		}
	}
}

static ssize_t als_type_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (!g_chip){
		return -ENOMEM;
	}

	len = sprintf(page,"%d",g_chip->als_type);

	if(len > *off)
		len -= *off;
	else
		len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static struct file_operations als_type_fops = {
	.read = als_type_read_proc,
};

static ssize_t red_max_lux_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (!gdata){
		return -ENOMEM;
	}

	len = sprintf(page,"%d",gdata->red_max_lux);

	if(len > *off)
		len -= *off;
	else
		len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}
static ssize_t red_max_lux_write_proc(struct file *file, const char __user *buf,
		size_t count, loff_t *off)

{
	char page[256] = {0};
	unsigned int input = 0;

	if(!gdata){
		return -ENOMEM;
	}


	if (count > 256)
		count = 256;
	if(count > *off)
		count -= *off;
	else
		count = 0;

	if (copy_from_user(page, buf, count))
		return -EFAULT;
	*off += count;

	if (sscanf(page, "%u", &input) != 1) {
		count = -EINVAL;
		return count;
	}

	if(input != gdata->red_max_lux){
		gdata->red_max_lux= input;
	}

	return count;
}
static struct file_operations red_max_lux_fops = {
	.read = red_max_lux_read_proc,
	.write = red_max_lux_write_proc,
};

int oppo_subsystem_restart(const char *name);
static int (*oppo_restart_func)(const char *name);

static ssize_t ssr_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (!gdata){
		return -ENOMEM;
	}

	len = sprintf(page,"%d",gdata->ssr);

	if(len > *off)
		len -= *off;
	else
		len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t ssr_write_proc(struct file *file, const char __user *buf,
		size_t count, loff_t *off)

{
	char page[256] = {0};
	unsigned int input = 0;

	if(!gdata){
		return -ENOMEM;
	}


	if (count > 256)
		count = 256;
	if(count > *off)
		count -= *off;
	else
		count = 0;

	if (copy_from_user(page, buf, count))
		return -EFAULT;
	*off += count;

	if (sscanf(page, "%u", &input) != 1) {
		count = -EINVAL;
		return count;
	}

	if(input != gdata->ssr){
		gdata->ssr= input;
	}

	if (gdata->ssr == 1)
	{
		if (!oppo_restart_func)
			oppo_restart_func = symbol_request(oppo_subsystem_restart);

		if (oppo_restart_func) {
			pr_err("%s():force restart adsp...\n",  __func__);
			oppo_restart_func("adsp");
		} else {
			pr_err("%s(): oppo_restart_func symbol not found\n",  __func__);
		}
	}
	return count;
}

static struct file_operations ssr_fops = {
	.read = ssr_read_proc,
	.write = ssr_write_proc,
};

static ssize_t white_max_lux_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (!gdata){
		return -ENOMEM;
	}

	len = sprintf(page,"%d",gdata->white_max_lux);

	if(len > *off)
		len -= *off;
	else
		len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}
static ssize_t white_max_lux_write_proc(struct file *file, const char __user *buf,
		size_t count, loff_t *off)

{
	char page[256] = {0};
	unsigned int input = 0;

	if(!gdata){
		return -ENOMEM;
	}


	if (count > 256)
		count = 256;
	if(count > *off)
		count -= *off;
	else
		count = 0;

	if (copy_from_user(page, buf, count))
		return -EFAULT;
	*off += count;

	if (sscanf(page, "%u", &input) != 1) {
		count = -EINVAL;
		return count;
	}

	if(input != gdata->white_max_lux){
		gdata->white_max_lux= input;
	}

	return count;
}
static struct file_operations white_max_lux_fops = {
	.read = white_max_lux_read_proc,
	.write = white_max_lux_write_proc,
};
static ssize_t blue_max_lux_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (!gdata){
		return -ENOMEM;
	}

	len = sprintf(page,"%d",gdata->blue_max_lux);

	if(len > *off)
		len -= *off;
	else
		len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}
static ssize_t blue_max_lux_write_proc(struct file *file, const char __user *buf,
		size_t count, loff_t *off)

{
	char page[256] = {0};
	unsigned int input = 0;

	if(!gdata){
		return -ENOMEM;
	}


	if (count > 256)
		count = 256;
	if(count > *off)
		count -= *off;
	else
		count = 0;

	if (copy_from_user(page, buf, count))
		return -EFAULT;
	*off += count;

	if (sscanf(page, "%u", &input) != 1) {
		count = -EINVAL;
		return count;
	}

	if(input != gdata->blue_max_lux){
		gdata->blue_max_lux= input;
	}

	return count;
}
static struct file_operations blue_max_lux_fops = {
	.read = blue_max_lux_read_proc,
	.write = blue_max_lux_write_proc,
};
static ssize_t green_max_lux_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (!gdata){
		return -ENOMEM;
	}

	len = sprintf(page,"%d",gdata->green_max_lux);

	if(len > *off)
		len -= *off;
	else
		len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}
static ssize_t green_max_lux_write_proc(struct file *file, const char __user *buf,
		size_t count, loff_t *off)

{
	char page[256] = {0};
	unsigned int input = 0;

	if(!gdata){
		return -ENOMEM;
	}


	if (count > 256)
		count = 256;
	if(count > *off)
		count -= *off;
	else
		count = 0;

	if (copy_from_user(page, buf, count))
		return -EFAULT;
	*off += count;

	if (sscanf(page, "%u", &input) != 1) {
		count = -EINVAL;
		return count;
	}

	if(input != gdata->green_max_lux){
		gdata->green_max_lux= input;
	}

	return count;
}
static struct file_operations green_max_lux_fops = {
	.read = green_max_lux_read_proc,
	.write = green_max_lux_write_proc,
};
static ssize_t cali_coe_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (!gdata){
		return -ENOMEM;
	}

	len = sprintf(page,"%d",gdata->cali_coe);

	if(len > *off)
		len -= *off;
	else
		len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}
static ssize_t cali_coe_write_proc(struct file *file, const char __user *buf,
		size_t count, loff_t *off)

{
	char page[256] = {0};
	unsigned int input = 0;

	if(!gdata){
		return -ENOMEM;
	}


	if (count > 256)
		count = 256;
	if(count > *off)
		count -= *off;
	else
		count = 0;

	if (copy_from_user(page, buf, count))
		return -EFAULT;
	*off += count;

	if (sscanf(page, "%u", &input) != 1) {
		count = -EINVAL;
		return count;
	}

	if(input != gdata->cali_coe){
		gdata->cali_coe= input;
	}

	return count;
}
static struct file_operations cali_coe_fops = {
	.read = cali_coe_read_proc,
	.write = cali_coe_write_proc,
};
static ssize_t row_coe_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (!gdata){
		return -ENOMEM;
	}

	len = sprintf(page,"%d",gdata->row_coe);

	if(len > *off)
		len -= *off;
	else
		len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}
static ssize_t row_coe_write_proc(struct file *file, const char __user *buf,
		size_t count, loff_t *off)

{
	char page[256] = {0};
	unsigned int input = 0;

	if(!gdata){
		return -ENOMEM;
	}


	if (count > 256)
		count = 256;
	if(count > *off)
		count -= *off;
	else
		count = 0;

	if (copy_from_user(page, buf, count))
		return -EFAULT;
	*off += count;

	if (sscanf(page, "%u", &input) != 1) {
		count = -EINVAL;
		return count;
	}

	if(input != gdata->row_coe){
		gdata->row_coe= input;
	}

	return count;
}
static struct file_operations row_coe_fops = {
	.read = row_coe_read_proc,
	.write = row_coe_write_proc,
};

static int oppo_devinfo_probe(struct platform_device *pdev)
{
	struct sensor_info * chip = NULL;
	size_t smem_size = 0;
	void *smem_addr = NULL;
	int rc = 0;
	struct proc_dir_entry *pentry;
	struct oppo_als_cali_data *data = NULL;

	pr_info("%s call\n", __func__);

	smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY,
		SMEM_SENSOR,
		&smem_size);
	if (IS_ERR(smem_addr)) {
		pr_err("unable to acquire smem SMEM_SENSOR entry\n");
		return -ENOMEM;
	}

	chip = (struct sensor_info *)(smem_addr);
	if (chip == ERR_PTR(-EPROBE_DEFER)) {
		chip = NULL;
		pr_err("unable to acquire entry\n");
		return -ENOMEM;
	}

	if(gdata){
		printk("%s:just can be call one time\n",__func__);
		return 0;
	}
	data = kzalloc(sizeof(struct oppo_als_cali_data),GFP_KERNEL);
	if(data == NULL){
		rc = -ENOMEM;
		printk("%s:kzalloc fail %d\n",__func__,rc);
		return rc;
	}
	gdata = data;

	platform_set_drvdata(pdev,chip);

	oppo_sensor_parse_dts(pdev);

	INIT_DELAYED_WORK(&sensor_work, sensor_dev_work);
	g_chip = chip;

	schedule_delayed_work(&sensor_work, msecs_to_jiffies(SENSOR_DEVINFO_SYNC_TIME));
	pr_info("%s success\n", __func__);

	if (gdata->proc_oppo_als) {
		printk("proc_oppo_als has alread inited\n");
		return 0;
	}

	gdata->proc_oppo_als =  proc_mkdir("oppoAls", NULL);
	if(!gdata->proc_oppo_als) {
		pr_err("can't create proc_oppo_als proc\n");
		rc = -EFAULT;
		return rc;
	}

	pentry = proc_create("red_max_lux",0666, gdata->proc_oppo_als,
		&red_max_lux_fops);
	if(!pentry) {
		pr_err("create red_max_lux proc failed.\n");
		rc = -EFAULT;
		return rc;
	}

	pentry = proc_create("green_max_lux",0666, gdata->proc_oppo_als,
		&green_max_lux_fops);
	if(!pentry) {
		pr_err("create green_max_lux proc failed.\n");
		rc = -EFAULT;
		return rc;
	}

	pentry = proc_create("blue_max_lux",0666, gdata->proc_oppo_als,
		&blue_max_lux_fops);
	if(!pentry) {
		pr_err("create blue_max_lux proc failed.\n");
		rc = -EFAULT;
		return rc;
	}

	pentry = proc_create("white_max_lux",0666, gdata->proc_oppo_als,
		&white_max_lux_fops);
	if(!pentry) {
		pr_err("create white_max_lux proc failed.\n");
		rc = -EFAULT;
		return rc;
	}

	pentry = proc_create("cali_coe",0666, gdata->proc_oppo_als,
		&cali_coe_fops);
	if(!pentry) {
		pr_err("create cali_coe proc failed.\n");
		rc = -EFAULT;
		return rc;
	}

	pentry = proc_create("row_coe",0666, gdata->proc_oppo_als,
		&row_coe_fops);
	if(!pentry) {
		pr_err("create row_coe proc failed.\n");
		rc = -EFAULT;
		return rc;
	}

	pentry = proc_create("als_type",0666, gdata->proc_oppo_als,
		&als_type_fops);

	if(!pentry) {
		pr_err("create als_type_fops proc failed.\n");
		rc = -EFAULT;
		return rc;
	}

	pentry = proc_create("ssr",0666, gdata->proc_oppo_als,
		&ssr_fops);

	if(!pentry) {
		pr_err("create oppo ssr.\n");
		rc = -EFAULT;
		return rc;
	}

	return 0;
}

static int oppo_devinfo_remove(struct platform_device *pdev)
{
	if (gdata){
		kfree(gdata);
		gdata = NULL;
	}

	return 0;
}

static const struct of_device_id of_drv_match[] = {
	{ .compatible = "sensor-devinfo"},
	{},
};
MODULE_DEVICE_TABLE(of, of_motor_match);

static struct platform_driver _driver = {
	.probe		= oppo_devinfo_probe,
	.remove		= oppo_devinfo_remove,
	.driver		= {
		.name	= "sensor_devinfo",
		.of_match_table = of_drv_match,
	},
};

static int __init oppo_devinfo_init(void)
{
	pr_info("oppo_devinfo_init call\n");

	platform_driver_register(&_driver);
	return 0;
}

core_initcall(oppo_devinfo_init);

MODULE_DESCRIPTION("sensor devinfo");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Murphy@oppo.com");

