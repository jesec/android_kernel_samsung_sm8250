/*
 *  sec_audio_sysfs.c
 *
 *  Copyright (c) 2017 Samsung Electronics
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <sound/samsung/sec_audio_sysfs.h>

struct sec_audio_sysfs_data *audio_data;

int wdsp_count = 100;
int wdsp_delay = 5;

#define MAX_SIZE 4096

#ifdef CONFIG_SND_SOC_SAMSUNG_JACK
int audio_register_jack_select_cb(int (*set_jack) (int))
{
	if (audio_data->set_jack_state) {
		dev_err(audio_data->jack_dev,
				"%s: Already registered\n", __func__);
		return -EEXIST;
	}

	audio_data->set_jack_state = set_jack;

	return 0;
}
EXPORT_SYMBOL_GPL(audio_register_jack_select_cb);

static ssize_t audio_jack_select_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	if (audio_data->set_jack_state) {
		if ((!size) || (buf[0] != '1')) {
			dev_info(dev, "%s: Forced remove jack\n", __func__);
			audio_data->set_jack_state(0);
		} else {
			dev_info(dev, "%s: Forced detect jack\n", __func__);
			audio_data->set_jack_state(1);
		}
	} else {
		dev_info(dev, "%s: No callback registered\n", __func__);
	}

	return size;
}

static DEVICE_ATTR(select_jack, S_IRUGO | S_IWUSR | S_IWGRP,
			NULL, audio_jack_select_store);

int audio_register_jack_state_cb(int (*jack_state) (void))
{
	if (audio_data->get_jack_state) {
		dev_err(audio_data->jack_dev,
				"%s: Already registered\n", __func__);
		return -EEXIST;
	}

	audio_data->get_jack_state = jack_state;

	return 0;
}
EXPORT_SYMBOL_GPL(audio_register_jack_state_cb);

static ssize_t audio_jack_state_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int report = 0;

	if (audio_data->get_jack_state)
		report = audio_data->get_jack_state();
	else {
		dev_info(dev, "%s: No callback registered\n", __func__);
		panic("sound card is not registered");
	}

	return snprintf(buf, 4, "%d\n", report);
}

static DEVICE_ATTR(state, S_IRUGO | S_IWUSR | S_IWGRP,
			audio_jack_state_show, NULL);

int audio_register_key_state_cb(int (*key_state) (void))
{
	if (audio_data->get_key_state) {
		dev_err(audio_data->jack_dev,
				"%s: Already registered\n", __func__);
		return -EEXIST;
	}

	audio_data->get_key_state = key_state;

	return 0;
}
EXPORT_SYMBOL_GPL(audio_register_key_state_cb);

static ssize_t audio_key_state_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int report = 0;

	if (audio_data->get_key_state)
		report = audio_data->get_key_state();
	else
		dev_info(dev, "%s: No callback registered\n", __func__);

	return snprintf(buf, 4, "%d\n", report);
}

static DEVICE_ATTR(key_state, S_IRUGO | S_IWUSR | S_IWGRP,
			audio_key_state_show, NULL);

#ifndef CONFIG_SND_SOC_SAMSUNG_MBHC_SUPPORT
int audio_register_mic_adc_cb(int (*mic_adc) (void))
{
	if (audio_data->get_mic_adc) {
		dev_err(audio_data->jack_dev,
				"%s: Already registered\n", __func__);
		return -EEXIST;
	}

	audio_data->get_mic_adc = mic_adc;

	return 0;
}
EXPORT_SYMBOL_GPL(audio_register_mic_adc_cb);

static ssize_t audio_mic_adc_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int report = 0;

	if (audio_data->get_mic_adc)
		report = audio_data->get_mic_adc();
	else
		dev_info(dev, "%s: No callback registered\n", __func__);

	return snprintf(buf, 16, "%d\n", report);
}

static DEVICE_ATTR(mic_adc, S_IRUGO | S_IWUSR | S_IWGRP,
			audio_mic_adc_show, NULL);
#endif

static struct attribute *sec_audio_jack_attr[] = {
	&dev_attr_select_jack.attr,
	&dev_attr_state.attr,
	&dev_attr_key_state.attr,
#ifndef CONFIG_SND_SOC_SAMSUNG_MBHC_SUPPORT
	&dev_attr_mic_adc.attr,
#endif
	NULL,
};

static struct attribute_group sec_audio_jack_attr_group = {
	.attrs = sec_audio_jack_attr,
};
#endif

#ifdef CONFIG_SND_SOC_CS35L41
/* bigdata */
int audio_register_temperature_max_cb(int (*temperature_max) (enum amp_id))
{
	if (audio_data->get_amp_temperature_max) {
		dev_err(audio_data->amp_dev,
				"%s: Already registered\n", __func__);
		return -EEXIST;
	}

	audio_data->get_amp_temperature_max = temperature_max;

	return 0;
}
EXPORT_SYMBOL_GPL(audio_register_temperature_max_cb);

static ssize_t audio_amp_l_temperature_max_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int report = 0;

	if (audio_data->get_amp_temperature_max)
		report = audio_data->get_amp_temperature_max(LEFT_AMP);
	else
		dev_info(dev, "%s: No callback registered\n", __func__);

	return snprintf(buf, PAGE_SIZE, "%d\n", report);
}

static DEVICE_ATTR(l_temperature_max, S_IRUGO | S_IWUSR | S_IWGRP,
			audio_amp_l_temperature_max_show, NULL);

static ssize_t audio_amp_r_temperature_max_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int report = 0;

	if (audio_data->get_amp_temperature_max)
		report = audio_data->get_amp_temperature_max(RIGHT_AMP);
	else
		dev_info(dev, "%s: No callback registered\n", __func__);

	return snprintf(buf, PAGE_SIZE, "%d\n", report);
}

static DEVICE_ATTR(r_temperature_max, S_IRUGO | S_IWUSR | S_IWGRP,
			audio_amp_r_temperature_max_show, NULL);

int audio_register_temperature_keep_max_cb(int (*temperature_keep_max) (enum amp_id))
{
	if (audio_data->get_amp_temperature_keep_max) {
		dev_err(audio_data->amp_dev,
				"%s: Already registered\n", __func__);
		return -EEXIST;
	}

	audio_data->get_amp_temperature_keep_max = temperature_keep_max;

	return 0;
}
EXPORT_SYMBOL_GPL(audio_register_temperature_keep_max_cb);

static ssize_t audio_amp_l_temperature_keep_max_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int report = 0;

	if (audio_data->get_amp_temperature_keep_max)
		report = audio_data->get_amp_temperature_keep_max(LEFT_AMP);
	else
		dev_info(dev, "%s: No callback registered\n", __func__);

	return snprintf(buf, PAGE_SIZE, "%d\n", report);
}

static DEVICE_ATTR(l_temperature_keep_max, S_IRUGO | S_IWUSR | S_IWGRP,
			audio_amp_l_temperature_keep_max_show, NULL);

static ssize_t audio_amp_r_temperature_keep_max_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int report = 0;

	if (audio_data->get_amp_temperature_keep_max)
		report = audio_data->get_amp_temperature_keep_max(RIGHT_AMP);
	else
		dev_info(dev, "%s: No callback registered\n", __func__);

	return snprintf(buf, PAGE_SIZE, "%d\n", report);
}

static DEVICE_ATTR(r_temperature_keep_max, S_IRUGO | S_IWUSR | S_IWGRP,
			audio_amp_r_temperature_keep_max_show, NULL);

int audio_register_temperature_overcount_cb(int (*temperature_overcount) (enum amp_id))
{
	if (audio_data->get_amp_temperature_overcount) {
		dev_err(audio_data->amp_dev,
				"%s: Already registered\n", __func__);
		return -EEXIST;
	}

	audio_data->get_amp_temperature_overcount = temperature_overcount;

	return 0;
}
EXPORT_SYMBOL_GPL(audio_register_temperature_overcount_cb);

static ssize_t audio_amp_l_temperature_overcount_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int report = 0;

	if (audio_data->get_amp_temperature_overcount)
		report = audio_data->get_amp_temperature_overcount(LEFT_AMP);
	else
		dev_info(dev, "%s: No callback registered\n", __func__);

	return snprintf(buf, PAGE_SIZE, "%d\n", report);
}

static DEVICE_ATTR(l_temperature_overcount, S_IRUGO | S_IWUSR | S_IWGRP,
			audio_amp_l_temperature_overcount_show, NULL);

static ssize_t audio_amp_r_temperature_overcount_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int report = 0;

	if (audio_data->get_amp_temperature_overcount)
		report = audio_data->get_amp_temperature_overcount(RIGHT_AMP);
	else
		dev_info(dev, "%s: No callback registered\n", __func__);

	return snprintf(buf, PAGE_SIZE, "%d\n", report);
}

static DEVICE_ATTR(r_temperature_overcount, S_IRUGO | S_IWUSR | S_IWGRP,
			audio_amp_r_temperature_overcount_show, NULL);

int audio_register_excursion_max_cb(int (*excursion_max) (enum amp_id))
{
	if (audio_data->get_amp_excursion_max) {
		dev_err(audio_data->amp_dev,
				"%s: Already registered\n", __func__);
		return -EEXIST;
	}

	audio_data->get_amp_excursion_max = excursion_max;

	return 0;
}
EXPORT_SYMBOL_GPL(audio_register_excursion_max_cb);

static ssize_t audio_amp_l_excursion_max_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int report = 0;

	if (audio_data->get_amp_excursion_max)
		report = audio_data->get_amp_excursion_max(LEFT_AMP);
	else
		dev_info(dev, "%s: No callback registered\n", __func__);

	return snprintf(buf, PAGE_SIZE, "%04d\n", report);
}

static DEVICE_ATTR(l_excursion_max, S_IRUGO | S_IWUSR | S_IWGRP,
			audio_amp_l_excursion_max_show, NULL);

static ssize_t audio_amp_r_excursion_max_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int report = 0;

	if (audio_data->get_amp_excursion_max)
		report = audio_data->get_amp_excursion_max(RIGHT_AMP);
	else
		dev_info(dev, "%s: No callback registered\n", __func__);

	return snprintf(buf, PAGE_SIZE, "%04d\n", report);
}

static DEVICE_ATTR(r_excursion_max, S_IRUGO | S_IWUSR | S_IWGRP,
			audio_amp_r_excursion_max_show, NULL);

int audio_register_excursion_overcount_cb(int (*excursion_overcount) (enum amp_id))
{
	if (audio_data->get_amp_excursion_overcount) {
		dev_err(audio_data->amp_dev,
				"%s: Already registered\n", __func__);
		return -EEXIST;
	}

	audio_data->get_amp_excursion_overcount = excursion_overcount;

	return 0;
}
EXPORT_SYMBOL_GPL(audio_register_excursion_overcount_cb);

static ssize_t audio_amp_l_excursion_overcount_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int report = 0;

	if (audio_data->get_amp_excursion_overcount)
		report = audio_data->get_amp_excursion_overcount(LEFT_AMP);
	else
		dev_info(dev, "%s: No callback registered\n", __func__);

	return snprintf(buf, PAGE_SIZE, "%d\n", report);
}

static DEVICE_ATTR(l_excursion_overcount, S_IRUGO | S_IWUSR | S_IWGRP,
			audio_amp_l_excursion_overcount_show, NULL);

static ssize_t audio_amp_r_excursion_overcount_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int report = 0;

	if (audio_data->get_amp_excursion_overcount)
		report = audio_data->get_amp_excursion_overcount(RIGHT_AMP);
	else
		dev_info(dev, "%s: No callback registered\n", __func__);

	return snprintf(buf, PAGE_SIZE, "%d\n", report);
}

static DEVICE_ATTR(r_excursion_overcount, S_IRUGO | S_IWUSR | S_IWGRP,
			audio_amp_r_excursion_overcount_show, NULL);

static struct attribute *sec_audio_amp_attr[] = {
	&dev_attr_l_temperature_max.attr,
	&dev_attr_r_temperature_max.attr,
	&dev_attr_l_temperature_keep_max.attr,
	&dev_attr_r_temperature_keep_max.attr,
	&dev_attr_l_temperature_overcount.attr,
	&dev_attr_r_temperature_overcount.attr,
	&dev_attr_l_excursion_max.attr,
	&dev_attr_r_excursion_max.attr,
	&dev_attr_l_excursion_overcount.attr,
	&dev_attr_r_excursion_overcount.attr,
	NULL,
};

static struct attribute_group sec_audio_amp_attr_group = {
	.attrs = sec_audio_amp_attr,
};
#endif

static int __init sec_audio_sysfs_init(void)
{
	int ret = 0;

	audio_data = kzalloc(sizeof(struct sec_audio_sysfs_data), GFP_KERNEL);
	if (audio_data == NULL)
		return -ENOMEM;

	audio_data->audio_class = class_create(THIS_MODULE, "audio");
	if (IS_ERR(audio_data->audio_class)) {
		pr_err("%s: Failed to create audio class\n", __func__);
		ret = PTR_ERR(audio_data->audio_class);
		goto err_alloc;
	}

#ifdef CONFIG_SND_SOC_SAMSUNG_JACK
	audio_data->jack_dev =
			device_create(audio_data->audio_class,
					NULL, 0, NULL, "earjack");
	if (IS_ERR(audio_data->jack_dev)) {
		pr_err("%s: Failed to create earjack device\n", __func__);
		ret = PTR_ERR(audio_data->jack_dev);
		goto err_class;
	}

	ret = sysfs_create_group(&audio_data->jack_dev->kobj,
				&sec_audio_jack_attr_group);
	if (ret) {
		pr_err("%s: Failed to create earjack sysfs\n", __func__);
		goto err_jack_device;
	}
#endif

#ifdef CONFIG_SND_SOC_CS35L41
	audio_data->amp_dev =
			device_create(audio_data->audio_class,
					NULL, 1, NULL, "amp");
	if (IS_ERR(audio_data->amp_dev)) {
		pr_err("%s: Failed to create amp device\n", __func__);
		ret = PTR_ERR(audio_data->amp_dev);
		goto err_class;
	}

	ret = sysfs_create_group(&audio_data->amp_dev->kobj,
				&sec_audio_amp_attr_group);
	if (ret) {
		pr_err("%s: Failed to create amp sysfs\n", __func__);
		goto err_amp_device;
	}
#endif

	return 0;

#ifdef CONFIG_SND_SOC_CS35L41
err_amp_device:
	device_destroy(audio_data->audio_class, 1);
	audio_data->amp_dev = NULL;
#endif
#ifdef CONFIG_SND_SOC_SAMSUNG_JACK
err_jack_attr:
	sysfs_remove_group(&audio_data->jack_dev->kobj,
				&sec_audio_jack_attr_group);
err_jack_device:
	device_destroy(audio_data->audio_class, 0);
	audio_data->jack_dev = NULL;
#endif
err_class:
	class_destroy(audio_data->audio_class);
	audio_data->audio_class = NULL;
err_alloc:
	kfree(audio_data);
	audio_data = NULL;

	return ret;
}
subsys_initcall(sec_audio_sysfs_init);

static void __exit sec_audio_sysfs_exit(void)
{
#ifdef CONFIG_SND_SOC_CS35L41
	if (audio_data->amp_dev) {
		sysfs_remove_group(&audio_data->amp_dev->kobj,
				&sec_audio_amp_attr_group);
		device_destroy(audio_data->audio_class, 1);
	}
#endif
#ifdef CONFIG_SND_SOC_SAMSUNG_JACK
	if (audio_data->jack_dev) {
		sysfs_remove_group(&audio_data->jack_dev->kobj,
				&sec_audio_jack_attr_group);
		device_destroy(audio_data->audio_class, 0);
	}
#endif
	if (audio_data->audio_class)
		class_destroy(audio_data->audio_class);

	kfree(audio_data);
}
module_exit(sec_audio_sysfs_exit);

MODULE_DESCRIPTION("Samsung Electronics Audio SYSFS driver");
MODULE_LICENSE("GPL");
