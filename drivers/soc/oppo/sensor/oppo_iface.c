/************************************************************************************
** Copyright (C), 2008-2017, OPPO Mobile Comm Corp., Ltd
** VENDOR_EDIT
** File: oppo_oppo_iface.c
**
** Description:
**		Definitions for oppo_iface.
**
** Version: 1.0
** Date created: 2018/01/14,20:27
** Author: Fei.Mo@PSW.BSP.Sensor
**
** --------------------------- Revision History: ------------------------------------
* <version>		<date>		<author>		<desc>
* Revision 1.0		2018/01/14	Fei.Mo@PSW.BSP.Sensor	Created
**************************************************************************************/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/pwm.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <soc/oppo/oppo_project.h>
#include <linux/proc_fs.h>

#include "oppo_iface.h"

static struct oppo_iface_chip *g_the_chip = NULL;

static void report_work_func(struct work_struct *work)
{
	struct oppo_iface_chip *chip = container_of(work, struct oppo_iface_chip, report_work);

	if (chip->is_need_to_report) {
		chip->is_need_to_report = false;
		kobject_uevent(&chip->dev->kobj,KOBJ_CHANGE);
	}

	chip->is_timeout = true;
}

static enum alarmtimer_restart light_level_timer_func(struct alarm *alrm, ktime_t now)
{
	if (!g_the_chip) {
		printk("g_the_chip null \n ");
		return ALARMTIMER_NORESTART;
	}
	schedule_work(&g_the_chip->report_work);

	return ALARMTIMER_NORESTART;
}

static ssize_t oppo_brightness_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	if (!g_the_chip) {
		printk("g_the_chip null\n");
		return snprintf(buf, PAGE_SIZE, "%d\n", 0);
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", g_the_chip->brightness);
}

static ssize_t oppo_brightness_store(struct device *pdev, struct device_attribute *attr,
			   const char *buff, size_t count)
{
	int brightness = -1;
	static int brightness_pre = -1;

	if (!g_the_chip) {
		printk("g_the_chip null\n");
		return count;
	}

	if (sscanf(buff, "%d", &brightness) == 1) {
		g_the_chip->brightness = brightness;
		if (brightness_pre != brightness) {
			brightness_pre = brightness;
			g_the_chip->is_need_to_report = true;
			if (g_the_chip->is_timeout) {
				g_the_chip->is_timeout = false;
				alarm_start_relative(&g_the_chip->timer,
					ktime_set(REPORT_TIME / 1000, (REPORT_TIME % 1000) * 1000000));
			}
		}
	}

	return count;
}

static ssize_t oppo_dc_actived_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	if (!g_the_chip) {
		printk("g_the_chip null\n");
		return snprintf(buf, PAGE_SIZE, "%d\n", 0);
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", g_the_chip->dc_actived);
}

static ssize_t oppo_dc_actived_store(struct device *pdev, struct device_attribute *attr,
			   const char *buff, size_t count)
{
	int dc_actived = -1;
	static int dc_actived_pre = -1;

	if (!g_the_chip) {
		printk("g_the_chip null\n");
		return count;
	}

	if (sscanf(buff, "%d", &dc_actived) == 1) {
		g_the_chip->dc_actived = dc_actived;
		printk("[oppo_dc_actived_store] dc_actived %d dc_actived_pre %d",dc_actived,dc_actived_pre);
		if (dc_actived_pre != dc_actived) {
			dc_actived_pre = dc_actived;
			printk("[oppo_dc_actived_store] report dc_actived_event");
			kobject_uevent(&g_the_chip->dev->kobj,KOBJ_CHANGE);
		}
	}

	return count;
}

static ssize_t dc_actived_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (!g_the_chip){
		return -ENOMEM;
	}

	len = sprintf(page,"%d",g_the_chip->dc_info.dc_actived);

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
static ssize_t dc_actived_write_proc(struct file *file, const char __user *buf,
		size_t count, loff_t *off)

{
	char page[256] = {0};
	int dc_actived = -1;
	static int dc_actived_pre = -1;

	if(!g_the_chip){
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

	if (sscanf(page, "%d", &dc_actived) == 1) {
		g_the_chip->dc_info.dc_actived = dc_actived;
		printk("[oppo_dc_actived_store] dc_actived %d dc_actived_pre %d",dc_actived,dc_actived_pre);
		if (dc_actived_pre != dc_actived) {
			dc_actived_pre = dc_actived;
			printk("[oppo_dc_actived_store] report dc_actived_event");
			kobject_uevent(&g_the_chip->dev->kobj,KOBJ_CHANGE);
		}
	}
	else {
		count = -EINVAL;
		return count;
	}

	return count;
}
static struct file_operations dc_actived_fops = {
	.read = dc_actived_read_proc,
	.write = dc_actived_write_proc,
};

static DEVICE_ATTR(brightness,	 S_IRUGO | S_IWUSR, oppo_brightness_show, oppo_brightness_store);
static DEVICE_ATTR(dc_actived,	 S_IRUGO | S_IWUSR, oppo_dc_actived_show, oppo_dc_actived_store);

static struct attribute * __attributes[] = {
	&dev_attr_brightness.attr,
	&dev_attr_dc_actived.attr,
	NULL
};

static struct attribute_group __attribute_group = {
	.attrs = __attributes
};

static int oppo_iface_platform_probe(struct platform_device *pdev)
{
	struct oppo_iface_chip *chip = NULL;
	int ret = 0;
	struct proc_dir_entry *pentry;

	printk("call\n");

	chip = devm_kzalloc(&pdev->dev,sizeof(struct oppo_iface_chip), GFP_KERNEL);
	if (!chip) {
		printk("kernel memory alocation was failed");
		return -ENOMEM;
	}

	chip->dev = &pdev->dev;

	ret = sysfs_create_group(&pdev->dev.kobj, &__attribute_group);
	if(ret) {
		printk("sysfs_create_group was failed(%d) \n", ret);
		return -EINVAL;
	}

	chip->dc_info.proc_oppo_als = proc_mkdir("oppoAls_dc_info", NULL);
	if(!chip->dc_info.proc_oppo_als) {
		pr_err("can't create proc_oppo_als proc\n");
		return -EFAULT;
	}
	chip->dc_info.dc_actived = 0;
	pentry = proc_create("dc_actived", 0666, chip->dc_info.proc_oppo_als,
		&dc_actived_fops);
	if(!pentry) {
		pr_err("create dc_actived proc failed.\n");
		return -EFAULT;
	}
	chip->is_need_to_report = false;
	chip->is_timeout = true;
	INIT_WORK(&chip->report_work, report_work_func);
	alarm_init(&chip->timer, ALARM_BOOTTIME, light_level_timer_func);

	g_the_chip = chip;

	printk("success \n");
	return 0;
}

static int oppo_iface_platform_remove(struct platform_device *pdev)
{
	if (g_the_chip) {
		kfree(g_the_chip);
		g_the_chip = NULL;
	}
	return 0;
}

static const struct of_device_id of_drv_match[] = {
	{ .compatible = "oppo_iface"},
	{},
};
MODULE_DEVICE_TABLE(of, of_motor_match);

static struct platform_driver _driver = {
	.probe		= oppo_iface_platform_probe,
	.remove		= oppo_iface_platform_remove,
	.driver		= {
		.name	= "oppo_iface",
		.of_match_table = of_drv_match,
	},
};

static struct platform_device _device = {
	.name	= "oppo_iface",
	.id = 0,
};

static int __init oppo_iface_init(void)
{
	printk("call\n");
	platform_driver_register(&_driver);
	platform_device_register(&_device);
	return 0;
}

static void __exit oppo_iface_exit(void)
{
	printk("call\n");
}

module_init(oppo_iface_init);
module_exit(oppo_iface_exit);
MODULE_DESCRIPTION("oppo interface for ap to ssc");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("mofei@oppo.com");

