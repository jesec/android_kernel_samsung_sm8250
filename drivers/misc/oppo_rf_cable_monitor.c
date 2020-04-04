/*For OEM project monitor RF cable connection status,
 * and config different RF configuration
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/sys_soc.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/types.h>
#include <soc/oppo/oppo_project.h>
#include <soc/oppo/boot_mode.h>
#include <linux/soc/qcom/smem.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <soc/qcom/subsystem_restart.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/kobject.h>
#include <linux/pm_wakeup.h>
#include <linux/oppo_kevent.h>

#define RF_CABLE_OUT        0
#define RF_CABLE_IN            1

#define CABLE_0                0
#define CABLE_1                1

#define INVALID_GPIO          -2

extern void op_restart_modem(void);

#define PAGESIZE 512
#define SMEM_DRAM_TYPE 136
#define MAX_CABLE_STS 4

struct rf_info_type
{
    union {
        unsigned int        rf_cable_sts;
        unsigned char        rf_cable_gpio_sts[MAX_CABLE_STS];
    };
    union {
        unsigned int        rf_pds_sts;
        unsigned char        rf_pds_gpio_sts[MAX_CABLE_STS];
    };
    unsigned int        reserver[6];

};

struct rf_cable_data {
    int             cable0_irq;
    int             cable1_irq;
    int             cable0_gpio;
    int             cable1_gpio;
    union {
        unsigned int        rf_cable_sts;
        unsigned char        rf_cable_gpio_sts[MAX_CABLE_STS];
    };
    int             pds0_irq;
    int             pds1_irq;
    int             pds0_gpio;
    int             pds1_gpio;
    union {
        unsigned int        rf_pds_sts;
        unsigned char        rf_pds_gpio_sts[MAX_CABLE_STS];
    };
    struct             device *dev;
    struct             wakeup_source cable_ws;
    struct             pinctrl *gpio_pinctrl;
    struct             pinctrl_state *rf_cable_active;
    struct             pinctrl_state *rf_pds_active;
};

enum
{
  SMEM_APPS         =  0,                     /**< Apps Processor */
  SMEM_MODEM        =  1,                     /**< Modem processor */
  SMEM_ADSP         =  2,                     /**< ADSP processor */
  SMEM_SSC          =  3,                     /**< Sensor processor */
  SMEM_WCN          =  4,                     /**< WCN processor */
  SMEM_CDSP         =  5,                     /**< Reserved */
  SMEM_RPM          =  6,                     /**< RPM processor */
  SMEM_TZ           =  7,                     /**< TZ processor */
  SMEM_SPSS         =  8,                     /**< Secure processor */
  SMEM_HYP          =  9,                     /**< Hypervisor */
  SMEM_NUM_HOSTS    = 10,                     /**< Max number of host in target */
};

static struct rf_cable_data *the_rf_data = NULL;
static struct rf_info_type *the_rf_format = NULL;

static bool rf_cable_0_support = false;
static bool rf_cable_1_support = false;
static bool rf_pds_0_support = false;
static bool rf_pds_1_support = false;

//====================================
static int kevent_usermsg(struct rf_cable_data *rf_data)
{
    #define BUF_SIZE 256
    char page[BUF_SIZE] = {0};
    int len = 0;
    struct kernel_packet_info *user_msg_info = (struct kernel_packet_info *)page;

    if (!rf_data || !the_rf_format) {
        pr_err("%s the_rf_data null\n", __func__);
        return 0;
    }
    pr_err("%s rf_pds status changed \n", __func__);
    memset(page,0,256);
    //set type and tag event id
    user_msg_info->type = 2;
    strncpy(user_msg_info->log_tag, "psw_network", sizeof(user_msg_info->log_tag)-1);
    strncpy(user_msg_info->event_id, "20190704", sizeof(user_msg_info->event_id)-1);
    len += snprintf(user_msg_info->payload,BUF_SIZE - 1 - sizeof(struct kernel_packet_info) ,
        "rf_pds_gpio_sts: [%d,%d]",rf_data->rf_pds_gpio_sts[0],rf_data->rf_pds_gpio_sts[1]);
    user_msg_info->payload_length = len + 1;
    kevent_send_to_user(user_msg_info);
    msleep(20);
    return 0;
}
//=====================================
static ssize_t cable_read_proc(struct file *file, char __user *buf, size_t count, loff_t *off)
{
    char page[128] = {0};
    int len = 0;

    if(!the_rf_data || !the_rf_format){
        return -EFAULT;
    }

    if (gpio_is_valid(the_rf_data->cable0_gpio)) {
        the_rf_data->rf_cable_gpio_sts[0] = gpio_get_value(the_rf_data->cable0_gpio);
    } else {
        the_rf_data->rf_cable_gpio_sts[0] = 0xFF;
    }
    if (gpio_is_valid(the_rf_data->cable1_gpio)) {
        the_rf_data->rf_cable_gpio_sts[1] = gpio_get_value(the_rf_data->cable1_gpio);
    } else {
        the_rf_data->rf_cable_gpio_sts[1] = 0xFF;
    }

    the_rf_format->rf_cable_sts = the_rf_data->rf_cable_sts;

    len += sprintf(&page[len], "%d,%d\n",the_rf_format->rf_cable_gpio_sts[0], the_rf_format->rf_cable_gpio_sts[1]);

    if (len > *off) {
        len -= *off;
    }else
        len = 0;
    if (copy_to_user(buf, page, (len < count ? len : count))) {
        return -EFAULT;
    }
    *off += len < count ? len : count;
    return (len < count ? len : count);
}

static ssize_t pds_read_proc(struct file *file, char __user *buf, size_t count, loff_t *off)
{
    char page[128] = {0};
    int len = 0;

    if(!the_rf_data || !the_rf_format){
        return -EFAULT;
    }

    if (gpio_is_valid(the_rf_data->pds0_gpio)) {
        the_rf_data->rf_pds_gpio_sts[0] = gpio_get_value(the_rf_data->pds0_gpio);
    } else {
        the_rf_data->rf_pds_gpio_sts[0] = 0xFF;
    }
    if (gpio_is_valid(the_rf_data->pds1_gpio)) {
        the_rf_data->rf_pds_gpio_sts[1] = gpio_get_value(the_rf_data->pds1_gpio);
    } else {
        the_rf_data->rf_pds_gpio_sts[1] = 0xFF;
    }

    if(the_rf_format->rf_pds_sts != the_rf_data->rf_pds_sts)
    {
        kevent_usermsg(the_rf_data);
        the_rf_format->rf_pds_sts = the_rf_data->rf_pds_sts;
    }

    //len += sprintf(&page[len], "rf_cable_gpio_sts: %d %d\n",the_rf_format->rf_cable_gpio_sts[0], the_rf_format->rf_cable_gpio_sts[1]);
    len += sprintf(&page[len], "%d,%d\n",the_rf_format->rf_pds_gpio_sts[0], the_rf_format->rf_pds_gpio_sts[1]);
    if (len > *off) {
        len -= *off;
    }else
        len = 0;
    if (copy_to_user(buf, page, (len < count ? len : count))) {
        return -EFAULT;
    }
    *off += len < count ? len : count;
    return (len < count ? len : count);
}

struct file_operations cable_proc_fops_cable = {
    .read = cable_read_proc,
};

struct file_operations cable_proc_fops_pds = {
    .read = pds_read_proc,
};

//=====================================

static irqreturn_t cable_interrupt(int irq, void *_dev)
{
    struct rf_cable_data *rf_data =  (struct rf_cable_data *)_dev;

    pr_err("%s enter\n", __func__);
    if (!rf_data || !the_rf_format) {
        pr_err("%s the_rf_data null\n", __func__);
        return IRQ_HANDLED;
    }
    __pm_stay_awake(&rf_data->cable_ws);
    usleep_range(10000, 20000);
    if (gpio_is_valid(rf_data->cable0_gpio)) {
        rf_data->rf_cable_gpio_sts[0] = gpio_get_value(rf_data->cable0_gpio);
    } else {
        rf_data->rf_cable_gpio_sts[0] = 0xFF;
    }

    if (gpio_is_valid(rf_data->cable1_gpio)) {
        rf_data->rf_cable_gpio_sts[1] = gpio_get_value(rf_data->cable1_gpio);
    } else {
        rf_data->rf_cable_gpio_sts[1] = 0xFF;
    }

    if (gpio_is_valid(rf_data->pds0_gpio)) {
        rf_data->rf_pds_gpio_sts[0] = gpio_get_value(rf_data->pds0_gpio);
    } else {
        rf_data->rf_pds_gpio_sts[0] = 0xFF;
    }

    if (gpio_is_valid(rf_data->pds1_gpio)) {
        rf_data->rf_pds_gpio_sts[1] = gpio_get_value(rf_data->pds1_gpio);
    } else {
        rf_data->rf_pds_gpio_sts[1] = 0xFF;
    }

    the_rf_format->rf_cable_sts = rf_data->rf_cable_sts;
    if(the_rf_format->rf_pds_sts != rf_data->rf_pds_sts)
    {
        kevent_usermsg(rf_data);
        the_rf_format->rf_pds_sts = rf_data->rf_pds_sts;
    }

    __pm_relax(&rf_data->cable_ws);

    return IRQ_HANDLED;
}

static int rf_cable_initial_request_irq(struct rf_cable_data *rf_data)
{
    int rc = 0;
    if (gpio_is_valid(rf_data->cable0_gpio)) {
        rc = request_threaded_irq(rf_data->cable0_irq, NULL,cable_interrupt,
            IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_NO_SUSPEND, "rf_cable0_irq", rf_data);
        if (rc) {
            pr_err("%s cable0_gpio, request falling fail\n",
                __func__);
            return rc;
        }
        //enable_irq(rf_data->cable0_irq);
    }

    if (gpio_is_valid(rf_data->cable1_gpio)) {
        rc = request_threaded_irq(rf_data->cable1_irq, NULL,cable_interrupt,
             IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT|IRQF_NO_SUSPEND,"rf_cable1_irq", rf_data);
        if (rc) {
            pr_err("%s cable1_gpio, request falling fail\n",
                __func__);
            return rc;
        }
        //enable_irq(rf_data->cable1_irq);
    }
    //pds check
    if (gpio_is_valid(rf_data->pds0_gpio)) {
        rc = request_threaded_irq(rf_data->pds0_irq, NULL,cable_interrupt,
            IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_NO_SUSPEND, "rf_pds0_irq", rf_data);
        if (rc) {
            pr_err("%s pds0_gpio, request falling fail\n",
                __func__);
            return rc;
        }
        //enable_irq(rf_data->pds0_irq);
    }

    if (gpio_is_valid(rf_data->pds1_gpio)) {
        rc = request_threaded_irq(rf_data->pds1_irq, NULL,cable_interrupt,
             IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT|IRQF_NO_SUSPEND,"rf_pds1_irq", rf_data);
        if (rc) {
            pr_err("%s pds1_gpio, request falling fail\n",
                __func__);
            return rc;
        }
        //enable_irq(rf_data->pds1_irq);
    }
    return rc;
}


static int rf_cable_gpio_pinctrl_init
    (struct platform_device *pdev, struct device_node *np, struct rf_cable_data *rf_data)
{
    int retval = 0;

    //request gpio 0 .gpio 1.
    if (rf_cable_0_support) {
        rf_data->cable0_gpio = of_get_named_gpio(np, "rf,cable0-gpio", 0);
    } else {
        rf_data->cable0_gpio = INVALID_GPIO;
    }

    if (rf_cable_1_support) {
        rf_data->cable1_gpio = of_get_named_gpio(np, "rf,cable1-gpio", 0);
    } else {
        rf_data->cable1_gpio = INVALID_GPIO;
    }

    if (rf_pds_0_support) {
        rf_data->pds0_gpio = of_get_named_gpio(np, "rf,pds0-gpio", 0);
    } else {
        rf_data->pds0_gpio = INVALID_GPIO;
    }

    if (rf_pds_1_support) {
        rf_data->pds1_gpio = of_get_named_gpio(np, "rf,pds1-gpio", 0);
    } else {
        rf_data->pds1_gpio = INVALID_GPIO;
    }


    rf_data->gpio_pinctrl = devm_pinctrl_get(&(pdev->dev));

    if (IS_ERR_OR_NULL(rf_data->gpio_pinctrl)) {
        retval = PTR_ERR(rf_data->gpio_pinctrl);
        pr_err("%s get gpio_pinctrl fail, retval:%d\n", __func__, retval);
        goto err_pinctrl_get;
    }

    rf_data->rf_cable_active = pinctrl_lookup_state(rf_data->gpio_pinctrl, "rf_cable_active");
    rf_data->rf_pds_active = pinctrl_lookup_state(rf_data->gpio_pinctrl, "rf_pds_active");
    if (!IS_ERR_OR_NULL(rf_data->rf_cable_active)) {
        retval = pinctrl_select_state(rf_data->gpio_pinctrl,
                rf_data->rf_cable_active);
        if (retval < 0) {
            pr_err("%s select pinctrl fail, retval:%d\n", __func__, retval);
        goto err_pinctrl_lookup;
        }
    }

    if (!IS_ERR_OR_NULL(rf_data->rf_pds_active)) {
        retval = pinctrl_select_state(rf_data->gpio_pinctrl,
                rf_data->rf_pds_active);
        if (retval < 0) {
            pr_err("%s select pinctrl fail, retval:%d\n", __func__, retval);
            goto err_pinctrl_lookup;
        }
    }
    mdelay(5);
    if (gpio_is_valid(rf_data->cable0_gpio)) {
        gpio_direction_input(rf_data->cable0_gpio);
        rf_data->cable0_irq = gpio_to_irq(rf_data->cable0_gpio);
        if (rf_data->cable0_irq < 0) {
            pr_err("Unable to get irq number for GPIO %d, error %d\n",
                rf_data->cable0_gpio, rf_data->cable0_irq);
            goto err_pinctrl_lookup;
        }
        rf_data->rf_cable_gpio_sts[0] = gpio_get_value(rf_data->cable0_gpio);
    } else {
        rf_data->rf_cable_gpio_sts[0] = 0xFF;
    }

    if (gpio_is_valid(rf_data->cable1_gpio)) {
        gpio_direction_input(rf_data->cable1_gpio);
            rf_data->cable1_irq = gpio_to_irq(rf_data->cable1_gpio);
        if (rf_data->cable1_irq < 0) {
            pr_err("Unable to get irq number for GPIO %d, error %d\n",
                rf_data->cable1_gpio, rf_data->cable1_irq);
            goto err_pinctrl_lookup;
        }
        rf_data->rf_cable_gpio_sts[1] = gpio_get_value(rf_data->cable1_gpio);
    } else {
        rf_data->rf_cable_gpio_sts[1] = 0xFF;
    }

    if (gpio_is_valid(rf_data->pds0_gpio)) {
        gpio_direction_input(rf_data->pds0_gpio);
        rf_data->pds0_irq = gpio_to_irq(rf_data->pds0_gpio);
        if (rf_data->pds0_irq < 0) {
            pr_err("Unable to get irq number for GPIO %d, error %d\n",
                rf_data->pds0_gpio, rf_data->pds0_irq);
            goto err_pinctrl_lookup;
        }
        rf_data->rf_pds_gpio_sts[0] = gpio_get_value(rf_data->pds0_gpio);
    } else {
        rf_data->rf_pds_gpio_sts[0] = 0xFF;
    }

    if (gpio_is_valid(rf_data->pds1_gpio)) {
        gpio_direction_input(rf_data->pds1_gpio);
            rf_data->pds1_irq = gpio_to_irq(rf_data->pds1_gpio);
        if (rf_data->pds1_irq < 0) {
            pr_err("Unable to get irq number for GPIO %d, error %d\n",
                rf_data->pds1_gpio, rf_data->pds1_irq);
            goto err_pinctrl_lookup;
        }
        rf_data->rf_pds_gpio_sts[1] = gpio_get_value(rf_data->pds1_gpio);
    } else {
        rf_data->rf_pds_gpio_sts[1] = 0xFF;
    }

    if(the_rf_format){
        the_rf_format->rf_cable_sts = rf_data->rf_cable_sts;
        the_rf_format->rf_pds_sts = rf_data->rf_pds_sts;
    }
    return 0;

err_pinctrl_lookup:
    devm_pinctrl_put(rf_data->gpio_pinctrl);
err_pinctrl_get:
    rf_data->gpio_pinctrl = NULL;
    return -1;
}


static int op_rf_cable_probe(struct platform_device *pdev)
{
	int rc = 0, ret = 0;
	size_t smem_size;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct rf_cable_data *rf_data = NULL;
	unsigned int len = (sizeof(struct rf_info_type) + 3)&(~0x3);
	struct proc_dir_entry *oppo_rf = NULL;

	pr_err("%s enter!\n", __func__);

	//if (get_oppo_region() == REGION_CHINA && is_project(OPPO_19031)) {
	//	return -EFAULT;
	//}

	rf_cable_0_support = of_property_read_bool(np, "rf_cable_0_support");
	rf_cable_1_support = of_property_read_bool(np, "rf_cable_1_support");
	rf_pds_0_support = of_property_read_bool(np, "rf_pds_0_support");
	rf_pds_1_support = of_property_read_bool(np, "rf_pds_1_support");

	ret = qcom_smem_alloc(SMEM_APPS, SMEM_DRAM_TYPE, len);
	if (ret < 0 && ret != -EEXIST) {
		pr_err("%s smem_alloc fail\n", __func__);
		return -EFAULT;
	}

	the_rf_format = (struct rf_info_type*)qcom_smem_get(SMEM_APPS, SMEM_DRAM_TYPE, &smem_size);
	if (IS_ERR(the_rf_format)) {
		pr_err("%s smem_get fail\n", __func__);
		return -EFAULT;
	}

    if (get_boot_mode() == MSM_BOOT_MODE__RF || get_boot_mode() == MSM_BOOT_MODE__WLAN) {
        pr_err("%s: rf/wlan mode FOUND! use 1 always\n", __func__);
        the_rf_format->rf_cable_sts = 0xFFFF;
        if (rf_cable_0_support) {
            the_rf_format->rf_cable_gpio_sts[0] = RF_CABLE_IN;
        }
        if (rf_cable_1_support) {
            the_rf_format->rf_cable_gpio_sts[1] = RF_CABLE_IN;
        }
        return 0;
    }

    rf_data = kzalloc(sizeof(struct rf_cable_data), GFP_KERNEL);
    if (!rf_data) {
        pr_err("%s: failed to allocate memory\n", __func__);
        rc = -ENOMEM;
        goto exit_nomem;
    }

    rf_data->dev = dev;
    dev_set_drvdata(dev, rf_data);
    wakeup_source_init(&rf_data->cable_ws, "rf_cable_wake_lock");
    rc = rf_cable_gpio_pinctrl_init(pdev, np, rf_data);
    if (rc) {
        pr_err("%s gpio_init fail\n", __func__);
        goto exit;
    }

    the_rf_data = rf_data;
    rc = rf_cable_initial_request_irq(rf_data);
    if (rc) {
        pr_err("could not request cable_irq\n");
        goto exit;
    }

    oppo_rf = proc_mkdir("oppo_rf", NULL);
    if (!oppo_rf) {
        pr_err("can't create oppo_rf proc\n");
        goto exit;
    }

    if (rf_cable_0_support || rf_cable_1_support) {
        proc_create("rf_cable", S_IRUGO, oppo_rf, &cable_proc_fops_cable);
    }

    if (rf_pds_0_support || rf_pds_1_support){
        proc_create("rf_pds", S_IRUGO, oppo_rf, &cable_proc_fops_pds);
    }

    pr_err("%s: probe ok, rf_cable, SMEM_RF_INFO:%d, sts:%d, [0_gpio:%d, 0_val:%d], [1_gpio:%d, 1_val:%d], 0_irq:%d, 1_irq:%d\n",
        __func__, SMEM_DRAM_TYPE, the_rf_format->rf_cable_sts,
        rf_data->cable0_gpio, the_rf_format->rf_cable_gpio_sts[0],
        rf_data->cable1_gpio, the_rf_format->rf_cable_gpio_sts[1],
        rf_data->cable0_irq, rf_data->cable1_irq);

    pr_err("%s: probe ok, rf_pds, SMEM_RF_INFO:%d, sts:%d, [0_gpio:%d, 0_val:%d], [1_gpio:%d, 1_val:%d], 0_irq:%d, 1_irq:%d\n",
        __func__, SMEM_DRAM_TYPE, the_rf_format->rf_pds_sts,
        rf_data->pds0_gpio, the_rf_format->rf_pds_gpio_sts[0],
        rf_data->pds1_gpio, the_rf_format->rf_pds_gpio_sts[1],
        rf_data->pds0_irq, rf_data->pds1_irq);

    return 0;

exit:
    wakeup_source_trash(&rf_data->cable_ws);
    kfree(rf_data);
exit_nomem:
    the_rf_data = NULL;
    the_rf_format = NULL;
    pr_err("%s: probe Fail!\n", __func__);

    return rc;
}

static const struct of_device_id rf_of_match[] = {
    { .compatible = "oppo,rf_cable", },
    {}
};
MODULE_DEVICE_TABLE(of, rf_of_match);

static struct platform_driver op_rf_cable_driver = {
    .driver = {
        .name       = "rf_cable",
        .owner      = THIS_MODULE,
        .of_match_table = rf_of_match,
    },
    .probe = op_rf_cable_probe,
};

static int __init op_rf_cable_init(void)
{
    int ret;

    ret = platform_driver_register(&op_rf_cable_driver);
    if (ret)
        pr_err("rf_cable_driver register failed: %d\n", ret);

    return ret;
}

MODULE_LICENSE("GPL v2");
late_initcall(op_rf_cable_init);
