/*====================================================================================*/
/*                                                                                    */
/*                        Copyright 2018 NXP                                          */
/*                                                                                    */
/*   All rights are reserved. Reproduction in whole or in part is prohibited          */
/*   without the written consent of the copyright owner.                              */
/*                                                                                    */
/*   NXP reserves the right to make changes without notice at any time. NXP makes     */
/*   no warranty, expressed, implied or statutory, including but not limited to any   */
/*   implied warranty of merchantability or fitness for any particular purpose,       */
/*   or that the use will not infringe any third party patent, copyright or trademark.*/
/*   NXP must not be liable for any loss or damage arising from its use.              */
/*                                                                                    */
/*====================================================================================*/

/**
* \addtogroup spi_driver
*
* @{ */

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
#include <linux/of_gpio.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/spi/spi.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/regulator/consumer.h>
#include <linux/wakelock.h>
#ifdef CONFIG_UWB_PMIC_CLOCK
#include <linux/clk.h>
#endif
#include "sr100.h"
#include <linux/mutex.h>
#include "../nfc/nfc_logger/nfc_logger.h"

#define SR100_IRQ_ENABLE
static bool Abort_ReadPending;
static bool IsFwDnldModeEnabled = false;
#define SR100_TXBUF_SIZE 4096
#define SR100_RXBUF_SIZE 4096
#define SR100_MAX_TXPKT_SIZE 2053
/* Macro to define SPI clock frequency */

#define SR100_SPI_CLOCK 16000000L;
#define ENABLE_THROUGHPUT_MEASUREMENT 0

/* size of maximum read/write buffer supported by driver */
#define MAX_BUFFER_SIZE 512U
#define DEBUG_LOG
/* Different driver debug lever */
enum SR100_DEBUG_LEVEL { SR100_DEBUG_OFF, SR100_FULL_DEBUG };

/* Variable to store current debug level request by ioctl */
static unsigned char debug_level;

#define SR100_DBG_MSG(msg...)                                              \
  switch (debug_level) {                                                   \
    case SR100_DEBUG_OFF:                                                  \
      break;                                                               \
    case SR100_FULL_DEBUG:                                                 \
      NFC_LOG_INFO(msg);                                                   \
      break;                                                               \
    default:                                                               \
      printk(KERN_ERR "[NXP-SR100] :  Wrong debug level %d", debug_level); \
      break;                                                               \
  }

#define SR100_ERR_MSG(msg...)                                              \
    NFC_LOG_INFO(msg);

/* Device specific macro and structure */
struct sr100_dev {
  wait_queue_head_t read_wq;      /* wait queue for read interrupt */
  wait_queue_head_t sync_wq;      /* wait queue for read/write sync */
  struct spi_device* spi;         /* spi device structure */
  struct miscdevice sr100_device; /* char device as misc driver */
  unsigned int ce_gpio;           /* SW Reset gpio */
  unsigned int irq_gpio;          /* SR100 will interrupt DH for any ntf */
  unsigned int ri_gpio;           /* DH will interrupt SR100 for read ready idication */
//  unsigned int switch_gpio;       /* switch for RX path */
  unsigned int wakeup_gpio;         /* wakeup gpio */
  bool irq_enabled;               /* flag to indicate irq is used */
  bool sync_enabled;               /* flag to indicate irq is used */
  unsigned char enable_poll_mode; /* enable the poll mode */
  spinlock_t irq_enabled_lock;    /*spin lock for read irq */
  unsigned char* tx_buffer;
  unsigned char* rx_buffer;
  const char *uwb_vdd;
  const char *uwb_vdd_pa;
  const char *uwb_vdd_io;
#ifdef CONFIG_UWB_PMIC_CLOCK
  struct   clk *clk;
#endif
  struct wake_lock uwb_wake_lock;
  spinlock_t sync_lock;  
};
#if (ENABLE_THROUGHPUT_MEASUREMENT == 1)
#define READ_THROUGH_PUT 0x01
#define WRITE_THROUGH_PUT 0x02
struct sr100_through_put {
  struct timeval rstart_tv;
  struct timeval wstart_tv;
  struct timeval rstop_tv;
  struct timeval wstop_tv;
  unsigned long total_through_put_wbytes;
  unsigned long total_through_put_rbytes;
  unsigned long total_through_put_rtime;
  unsigned long total_through_put_wtime;
};
static struct sr100_through_put sr100_through_put_t;
static void sr100_start_throughput_measurement(unsigned int type);
static void sr100_stop_throughput_measurement(unsigned int type,
                                              int no_of_bytes);

static void sr100_start_throughput_measurement(unsigned int type) {
  if (type == READ_THROUGH_PUT) {
    memset(&sr100_through_put_t.rstart_tv, 0x00, sizeof(struct timeval));
    do_gettimeofday(&sr100_through_put_t.rstart_tv);
  } else if (type == WRITE_THROUGH_PUT) {
    memset(&sr100_through_put_t.wstart_tv, 0x00, sizeof(struct timeval));
    do_gettimeofday(&sr100_through_put_t.wstart_tv);

  } else {
    pr_err(" sr100_start_throughput_measurement: wrong type = %d", type);
  }
}
static void sr100_stop_throughput_measurement(unsigned int type,
                                              int no_of_bytes) {
  if (type == READ_THROUGH_PUT) {
    memset(&sr100_through_put_t.rstop_tv, 0x00, sizeof(struct timeval));
    do_gettimeofday(&sr100_through_put_t.rstop_tv);
    sr100_through_put_t.total_through_put_rbytes += no_of_bytes;
    sr100_through_put_t.total_through_put_rtime +=
        (sr100_through_put_t.rstop_tv.tv_usec -
         sr100_through_put_t.rstart_tv.tv_usec) +
        ((sr100_through_put_t.rstop_tv.tv_sec -
          sr100_through_put_t.rstart_tv.tv_sec) *
         1000000);
  } else if (type == WRITE_THROUGH_PUT) {
    memset(&sr100_through_put_t.wstop_tv, 0x00, sizeof(struct timeval));
    do_gettimeofday(&sr100_through_put_t.wstop_tv);
    sr100_through_put_t.total_through_put_wbytes += no_of_bytes;
    sr100_through_put_t.total_through_put_wtime +=
        (sr100_through_put_t.wstop_tv.tv_usec -
         sr100_through_put_t.wstart_tv.tv_usec) +
        ((sr100_through_put_t.wstop_tv.tv_sec -
          sr100_through_put_t.wstart_tv.tv_sec) *
         1000000);
  } else {
    pr_err(" sr100_stop_throughput_measurement: wrong type = %d", type);
  }
}
#endif

/**
 * \ingroup spi_driver
 * \brief Called from SPI LibEse to initilaize the SR100 device
 *
 * \param[in]       struct inode *
 * \param[in]       struct file *
 *
 * \retval 0 if ok.
 *
*/

static int sr100_dev_open(struct inode* inode, struct file* filp) {
  struct sr100_dev* sr100_dev =
      container_of(filp->private_data, struct sr100_dev, sr100_device);

  filp->private_data = sr100_dev;
  SR100_DBG_MSG("%s : Major No: %d, Minor No: %d\n", __func__, imajor(inode),
                iminor(inode));

  return 0;
}
#ifdef SR100_IRQ_ENABLE

/**
 * \ingroup spi_driver
 * \brief To disable IRQ
 *
 * \param[in]       struct sr100_dev *
 *
 * \retval void
 *
*/

static void sr100_disable_irq(struct sr100_dev* sr100_dev) {
  unsigned long flags;
#ifdef DEBUG_LOG
  SR100_DBG_MSG("Entry : %s\n", __FUNCTION__);
#endif


  spin_lock_irqsave(&sr100_dev->irq_enabled_lock, flags);
  if (sr100_dev->irq_enabled) {
    disable_irq_nosync(sr100_dev->spi->irq);
    sr100_dev->irq_enabled = false;
  }
  spin_unlock_irqrestore(&sr100_dev->irq_enabled_lock, flags);
}

static void sr100_set_irq_flag(struct sr100_dev* sr100_dev) {
  unsigned long flags;
#ifdef DEBUG_LOG
  SR100_DBG_MSG("Entry : %s\n", __FUNCTION__);
#endif

  spin_lock_irqsave(&sr100_dev->irq_enabled_lock, flags);

  sr100_dev->irq_enabled = true;

  spin_unlock_irqrestore(&sr100_dev->irq_enabled_lock, flags);
}
/**
 * \ingroup spi_driver
 * \brief Will get called when interrupt line asserted from SR100
 *
 * \param[in]       int
 * \param[in]       void *
 *
 * \retval IRQ handle
 *
*/

static irqreturn_t sr100_dev_irq_handler(int irq, void* dev_id) {
  struct sr100_dev* sr100_dev = dev_id;
#ifdef DEBUG_LOG
  SR100_DBG_MSG("Entry : %s\n", __FUNCTION__);
#endif

  sr100_disable_irq(sr100_dev);

  /* Wake up waiting readers */
  wake_up(&sr100_dev->read_wq);
  wake_lock_timeout(&sr100_dev->uwb_wake_lock, 2*HZ);

  return IRQ_HANDLED;
}
#endif

/**
 * \ingroup spi_driver
 * \brief Will get called to update the sync flag status to synchronize between
 *  read and write thread
 * \param[in]       bool
 * \param[in]       void *
 *
*/

static void set_sync_flag_and_wakeup(bool sync_flag,struct sr100_dev* sr100_dev){
  spin_lock(&sr100_dev->sync_lock);
  if(sync_flag) {
    sr100_dev->sync_enabled = true;
    wake_up(&sr100_dev->sync_wq);
  } else {
    sr100_dev->sync_enabled = false;
  }
  spin_unlock(&sr100_dev->sync_lock);
}

/**
 * \ingroup spi_driver
 * \brief To configure the SR100_SET_PWR/SR100_SET_DBG/SR100_SET_POLL
 * \n         SR100_SET_PWR - hard reset (arg=2), soft reset (arg=1)
 * \n         SR100_SET_DBG - Enable/Disable (based on arg value) the driver
 *logs
 * \n         SR100_SET_POLL - Configure the driver in poll (arg = 1), interrupt
 *(arg = 0) based read operation
 * \param[in]       struct file *
 * \param[in]       unsigned int
 * \param[in]       unsigned long
 *
 * \retval 0 if ok.
 *
*/

static long sr100_dev_ioctl(struct file* filp, unsigned int cmd,
                            unsigned long arg) {
  int ret = 0;
  struct sr100_dev* sr100_dev = NULL;
  SR100_DBG_MSG("Entry : %s cmd :%d\n", __FUNCTION__, cmd);
  sr100_dev = filp->private_data;
  switch (cmd) {
    case SR100_SET_PWR:
      if (arg == PWR_ENABLE) {
        SR100_DBG_MSG(" enable power request\n");
        gpio_set_value(sr100_dev->ce_gpio, 1);
        //msleep(20);
      } else if (arg == PWR_DISABLE) {
        SR100_DBG_MSG("disable power request\n");
        gpio_set_value(sr100_dev->ce_gpio, 0);
        // msleep(20);
      } else if (arg == ABORT_READ_PENDING) {
        SR100_DBG_MSG( "Abort Read Pending\n");
        Abort_ReadPending = true;
        sr100_disable_irq(sr100_dev);
        /* Wake up waiting readers */
        wake_up(&sr100_dev->read_wq);
      }
      break;
    case SR100_SET_FWD:
      if (arg == 1) {
        IsFwDnldModeEnabled = true;
        pr_info("%s FW download enabled\n", __func__);
      } else if(arg == 0){
        IsFwDnldModeEnabled = false;
        pr_info("%s FW download disabled\n", __func__);
      }
      break;
    case SR100_GET_THROUGHPUT:
      if (arg == 0) {
#if (ENABLE_THROUGHPUT_MEASUREMENT == 1)
        pr_err(" **************** Write-Read Throughput: **************");
        pr_err(" No of Write Bytes = %ld", sr100_through_put_t.total_through_put_wbytes);
        pr_err(" No of Read Bytes = %ld", sr100_through_put_t.total_through_put_rbytes);
        pr_err(" Total Write Time (uSec) = %ld", sr100_through_put_t.total_through_put_wtime);
        pr_err(" Total Read Time (uSec) = %ld", sr100_through_put_t.total_through_put_rtime);
        pr_err(" Total Write-Read Time (uSec) = %ld", sr100_through_put_t.total_through_put_wtime +
                   sr100_through_put_t.total_through_put_rtime);
        sr100_through_put_t.total_through_put_wbytes = 0;
        sr100_through_put_t.total_through_put_rbytes = 0;
        sr100_through_put_t.total_through_put_wtime = 0;
        sr100_through_put_t.total_through_put_rtime = 0;
        pr_err(" **************** Write-Read Throughput: **************");
#endif
      }
      break;
    case SR100_SWITCH_RX_PATH:
      SR100_DBG_MSG(" switch rx path %d \n", (int)arg);
/*      if (arg == 1)
        gpio_set_value(sr100_dev->switch_gpio, 1);
      else if (arg == 0)
        gpio_set_value(sr100_dev->switch_gpio, 0);
      break;
  */  default:
      SR100_DBG_MSG(" Error case\n");
      ret = -EINVAL;  // ToDo: After adding proper switch cases we have to
                      // return with error statusi here
  }

  return ret;
}

/**
 * \ingroup spi_driver
 * \brief Write data to SR100 on SPI
 *
 * \param[in]       struct file *
 * \param[in]       const char *
 * \param[in]       size_t
 * \param[in]       loff_t *
 *
 * \retval data size
 *
*/

static ssize_t sr100_dev_write(struct file* filp, const char* buf, size_t count,
                               loff_t* offset) {
  int ret = -1;
  struct sr100_dev* sr100_dev;
#ifdef DEBUG_LOG
  SR100_DBG_MSG("Entry : %s\n", __FUNCTION__);
#endif

  sr100_dev = filp->private_data;
  if (count > SR100_MAX_TXPKT_SIZE || count > SR100_TXBUF_SIZE) {
    ret = -EFAULT;
    goto sr100_write_err;
  }

  if (copy_from_user(sr100_dev->tx_buffer, buf, count)) {
    SR100_ERR_MSG("%s : failed to copy from user space\n", __func__);
    return -EFAULT;
  }
write_wait:
#if (ENABLE_THROUGHPUT_MEASUREMENT == 1)
  sr100_start_throughput_measurement(WRITE_THROUGH_PUT);
#endif
  ret = wait_event_interruptible(sr100_dev->sync_wq, sr100_dev->sync_enabled);
  if (ret) {
    pr_err("wait_event_interruptible() : Failed.\n");
    goto sr100_write_err;
  }
  set_sync_flag_and_wakeup(false,sr100_dev);
  if(!IsFwDnldModeEnabled){
    if(gpio_get_value(sr100_dev->ri_gpio)){
      set_sync_flag_and_wakeup(true,sr100_dev);
      goto write_wait;
    }
    /* Write Header data */
    ret = spi_write(sr100_dev->spi, sr100_dev->tx_buffer, NORMAL_MODE_HEADER_LEN);
    if (ret < 0) {
      ret = -EIO;
    } else {
      ret = NORMAL_MODE_HEADER_LEN;
      count -= NORMAL_MODE_HEADER_LEN;
    }
    if(count > 0) {
      /* Write Payload data */
      ret = spi_write(sr100_dev->spi, sr100_dev->tx_buffer + NORMAL_MODE_HEADER_LEN, count);
      if (ret < 0) {
        ret = -EIO;
      } else {
        ret = count + NORMAL_MODE_HEADER_LEN;
      }
    }
  }else {
    /* Write data */
    ret = spi_write(sr100_dev->spi, sr100_dev->tx_buffer, count);
    if (ret < 0) {
      ret = -EIO;
    } else {
      ret = count;
    }
  }
#if (ENABLE_THROUGHPUT_MEASUREMENT == 1)
  sr100_stop_throughput_measurement(WRITE_THROUGH_PUT, ret);
#endif
#ifdef DEBUG_LOG
  SR100_DBG_MSG("sr100_dev_write ret %d- Exit \n", ret);
#endif
sr100_write_err:
  set_sync_flag_and_wakeup(true,sr100_dev);
  return ret;
}

/**
 * \ingroup spi_driver
 * \brief Used to read data from SR100 in Poll/interrupt mode configured using
 *ioctl call
 *
 * \param[in]       struct file *
 * \param[in]       char *
 * \param[in]       size_t
 * \param[in]       loff_t *
 *
 * \retval read size
 *
*/

static ssize_t sr100_dev_read(struct file* filp, char* buf, size_t count,
                              loff_t* offset) {
  struct sr100_dev* sr100_dev = filp->private_data;
  int ret = -EIO;
  size_t totalBtyesToRead = 0;
  size_t IsExtndLenIndication = 0;
#ifdef DEBUG_LOG
  SR100_DBG_MSG("Entry : %s\n", __FUNCTION__);
#endif

  memset(sr100_dev->rx_buffer, 0x00, SR100_RXBUF_SIZE);
#ifdef TEST_CODE
  pr_info("--1sr100_dev_read spi_read ce_gpio: 0x%x,irq_gpio: 0x%x\n", gpio_get_value(sr100_dev->ce_gpio),gpio_get_value(sr100_dev->irq_gpio));
#endif  
  if (!gpio_get_value(sr100_dev->irq_gpio)) {
#ifdef TEST_CODE
    pr_info("--2sr100_dev_read spi_read ce_gpio: 0x%x,irq_gpio: 0x%x\n", gpio_get_value(sr100_dev->ce_gpio),gpio_get_value(sr100_dev->irq_gpio));
#endif  

    if (filp->f_flags & O_NONBLOCK) {
      ret = -EAGAIN;
      goto fail;
    }
  }
  while (1) {
start_wait:
    if(sr100_dev->irq_enabled)
      enable_irq(sr100_dev->spi->irq);
    ret = wait_event_interruptible(sr100_dev->read_wq, !sr100_dev->irq_enabled);
    if (ret) {
      SR100_ERR_MSG("wait_event_interruptible() : Failed.\n");
      goto fail;
    }	
    sr100_disable_irq(sr100_dev);
    if(IsFwDnldModeEnabled) {
      sr100_set_irq_flag(sr100_dev);
      break;
    }
    if (Abort_ReadPending) {
      ret = -1;
      SR100_ERR_MSG("Abort Read pending......");
      goto fail;
    }
    ret = wait_event_interruptible(sr100_dev->sync_wq, sr100_dev->sync_enabled);
    if (ret) {
      SR100_ERR_MSG("wait_event_interruptible() : Failed.\n");
      goto fail;
    }
    set_sync_flag_and_wakeup(false,sr100_dev);
    sr100_set_irq_flag(sr100_dev);
    if(!gpio_get_value(sr100_dev->irq_gpio)){
      set_sync_flag_and_wakeup(true,sr100_dev);
      goto start_wait;
    }

    /*Hand shake between IRQ and Read Ready indication to avoid race condition
     between read and write sequence*/
    gpio_set_value(sr100_dev->ri_gpio, 1);
second_wait:
    if(sr100_dev->irq_enabled)
      enable_irq(sr100_dev->spi->irq);
    ret = wait_event_interruptible(sr100_dev->read_wq, !sr100_dev->irq_enabled);
    if (ret) {
      SR100_ERR_MSG("wait_event_interruptible() : Failed..\n");
      goto fail;
    }
    if(!gpio_get_value(sr100_dev->irq_gpio)){
      SR100_ERR_MSG("IRQ Flag corrupted go back to Wait state......");
      goto second_wait;
    }
    sr100_disable_irq(sr100_dev);
    sr100_set_irq_flag(sr100_dev);

    break;
  }// end of while
#if (ENABLE_THROUGHPUT_MEASUREMENT == 1)
  sr100_start_throughput_measurement(READ_THROUGH_PUT);
#endif
  ret = spi_read(sr100_dev->spi, (void*)sr100_dev->rx_buffer, count);
  if (ret < 0) {
    SR100_DBG_MSG("sr100_dev_read 1: spi read error %d\n", ret);
    return ret;
  }
  if(!IsFwDnldModeEnabled) {
    IsExtndLenIndication = (sr100_dev->rx_buffer[EXTND_LEN_INDICATOR_OFFSET] & EXTND_LEN_INDICATOR_OFFSET_MASK);
      totalBtyesToRead = sr100_dev->rx_buffer[NORMAL_MODE_LEN_OFFSET];
    if(IsExtndLenIndication){
      totalBtyesToRead = ((totalBtyesToRead << 8) | sr100_dev->rx_buffer[EXTENDED_LENGTH_OFFSET]);
    }

    ret = spi_read(sr100_dev->spi, (void*)(sr100_dev->rx_buffer + NORMAL_MODE_HEADER_LEN), totalBtyesToRead);
    if (ret < 0) {
      SR100_ERR_MSG("sr100_dev_read 2: spi read error.. %d\n ", ret);
      goto fail;
    }
    count = totalBtyesToRead + NORMAL_MODE_HEADER_LEN;
    ret = count;
  }
#if (ENABLE_THROUGHPUT_MEASUREMENT == 1)
  sr100_stop_throughput_measurement(READ_THROUGH_PUT, count);
#endif
  if (copy_to_user(buf, sr100_dev->rx_buffer, count)) {
    SR100_DBG_MSG("sr100_dev_read: copy to user failed\n");
    ret = -EFAULT;
  }

  if(!IsFwDnldModeEnabled){
    while(gpio_get_value(sr100_dev->irq_gpio)){
      udelay(5);
      if (Abort_ReadPending){
        ret = -1;
        SR100_DBG_MSG("Abort Read pending 2......\n");
        goto fail;
      }
    }
    gpio_set_value(sr100_dev->ri_gpio, 0);
  }
  set_sync_flag_and_wakeup(true,sr100_dev);
  return ret;
fail:
  SR100_ERR_MSG("Error sr100_dev_read ret %d Exit\n", ret);
  if(!IsFwDnldModeEnabled){
    gpio_set_value(sr100_dev->ri_gpio, 0);
  }
  Abort_ReadPending = false;
  set_sync_flag_and_wakeup(true,sr100_dev);
  SR100_ERR_MSG("Read Exit fail case");
  return ret;
}

/**
 * \ingroup spi_driver
 * \brief It will configure the GPIOs required for soft reset, read interrupt &
 *regulated power supply to SR100.
 *
 * \param[in]       struct sr100_spi_platform_data *
 * \param[in]       struct sr100_dev *
 * \param[in]       struct spi_device *
 *
 * \retval 0 if ok.
 *
*/

static int sr100_hw_setup(struct sr100_spi_platform_data* platform_data,
                          struct sr100_dev* sr100_dev, struct spi_device* spi) {
  int ret = -1;

  SR100_DBG_MSG("Entry : %s\n", __FUNCTION__);
#ifdef SR100_IRQ_ENABLE
  ret = gpio_request(platform_data->irq_gpio, "sr100 irq");
  if (ret < 0) {
    SR100_ERR_MSG("gpio request failed gpio = 0x%x\n", platform_data->irq_gpio);
    goto fail;
  }

  ret = gpio_direction_input(platform_data->irq_gpio);
  if (ret < 0) {
    SR100_ERR_MSG("gpio request failed gpio = 0x%x\n", platform_data->irq_gpio);
    goto fail_irq;
  }
#endif

  ret = gpio_request(platform_data->ce_gpio, "sr100 ce");
  if (ret < 0) {
    pr_info("sr100 - Failed requesting ce gpio - %d\n", platform_data->ce_gpio);
    goto fail_gpio;
  }
  ret = gpio_direction_output(platform_data->ce_gpio, 0);
  if (ret < 0) {
    pr_info("sr100 - Failed setting ce gpio - %d\n", platform_data->ce_gpio);
    goto fail_gpio;
  }
/*  ret = gpio_request(platform_data->switch_gpio, "sr100 switch");
  if (ret < 0) {
    pr_info("sr100 - Failed requesting switch gpio - %d\n", platform_data->switch_gpio);
    goto fail_gpio;
  }
*/  ret = gpio_request(platform_data->ri_gpio, "sr100 ri");
  if (ret < 0) {
    pr_info("sr100 - Failed requesting ri gpio - %d\n", platform_data->ri_gpio);
    goto fail_gpio;
  }
  ret = gpio_direction_output(platform_data->ri_gpio, 0);
  if (ret < 0) {
    pr_info("sr100 - Failed setting ri gpio - %d\n", platform_data->ri_gpio);
    goto fail_gpio;
  }  
/*
  ret = gpio_direction_output(platform_data->switch_gpio, 0);
  if (ret < 0) {
    pr_info("sr100 - Failed setting switch gpio - %d\n", platform_data->switch_gpio);
    goto fail_gpio;
  }
*/
  ret = gpio_request(platform_data->wakeup_gpio, "sr100 wakeup");
  if (ret < 0) {
    pr_info("sr100 - Failed requesting wake gpio - %d\n", platform_data->wakeup_gpio);
    goto fail_gpio;
  }
  ret = gpio_direction_output(platform_data->wakeup_gpio, 0);
  if (ret < 0) {
    pr_info("sr100 - Failed setting wake gpio - %d\n", platform_data->wakeup_gpio);
    goto fail_gpio;
  }
  ret = 0;
  SR100_DBG_MSG("Exit : %s\n", __FUNCTION__);
  return ret;

fail_gpio:
  gpio_free(platform_data->ce_gpio);
  gpio_free(platform_data->ri_gpio);
#ifdef SR100_IRQ_ENABLE
fail_irq:
  gpio_free(platform_data->irq_gpio);
fail:
  SR100_ERR_MSG("sr100_hw_setup failed\n");
#endif
  return ret;
}

/**
 * \ingroup spi_driver
 * \brief Set the SR100 device specific context for future use.
 *
 * \param[in]       struct spi_device *
 * \param[in]       void *
 *
 * \retval void
 *
*/

static inline void sr100_set_data(struct spi_device* spi, void* data) {
  dev_set_drvdata(&spi->dev, data);
}

/**
 * \ingroup spi_driver
 * \brief Get the SR100 device specific context.
 *
 * \param[in]       const struct spi_device *
 *
 * \retval Device Parameters
 *
*/

static inline void* sr100_get_data(const struct spi_device* spi) {
  return dev_get_drvdata(&spi->dev);
}

/* possible fops on the sr100 device */
static const struct file_operations sr100_dev_fops = {
    .owner = THIS_MODULE,
    .read = sr100_dev_read,
    .write = sr100_dev_write,
    .open = sr100_dev_open,
    .unlocked_ioctl = sr100_dev_ioctl,
};
static int sr100_parse_dt(struct device* dev,
                          struct sr100_spi_platform_data* pdata) {
  struct device_node* np = dev->of_node;
  SR100_DBG_MSG("Entry : %s\n", __FUNCTION__);
  
  pdata->irq_gpio = of_get_named_gpio(np, "nxp,sr100-irq", 0);
  if (!gpio_is_valid(pdata->irq_gpio)) {
    SR100_ERR_MSG("nxp,sr100-irq\n");
    return -EINVAL;
  }
  pdata->ce_gpio = of_get_named_gpio(np, "nxp,sr100-ce", 0);
  if (!gpio_is_valid(pdata->ce_gpio)) {
    SR100_ERR_MSG("nxp,sr100-ce\n");
    return -EINVAL;
  }
  pdata->ri_gpio = of_get_named_gpio(np, "nxp,sr100-ri", 0);
  if (!gpio_is_valid(pdata->ri_gpio)) {
  	SR100_ERR_MSG("nxp,sr100-ri\n");
    return -EINVAL;
  }  
/*
  pdata->switch_gpio = of_get_named_gpio(np, "nxp,sr100-switch", 0);
  if (!gpio_is_valid(pdata->switch_gpio)) {
    pr_err("nxp,sr100-switch\n");
    return -EINVAL;
  }*/
  pdata->wakeup_gpio = of_get_named_gpio(np, "nxp,sr100-wakeup", 0);
  if (!gpio_is_valid(pdata->wakeup_gpio)) {
    SR100_ERR_MSG("nxp,sr100-wakeup\n");
    return -EINVAL;
  }
  if (of_property_read_string(np, "nxp,vdd", &pdata->uwb_vdd) < 0) {
    SR100_ERR_MSG("get uwb_pvdd error\n");
    pdata->uwb_vdd = NULL;
  } else {
    SR100_ERR_MSG("uwb_vdd :%s\n", pdata->uwb_vdd);
  }
  if (of_property_read_string(np, "nxp,vdd-pa", &pdata->uwb_vdd_pa) < 0) {
    SR100_ERR_MSG("get uwb_vdd_pa error\n");
    pdata->uwb_vdd_pa = NULL;
  } else {
    SR100_ERR_MSG("uwb_vdd_pa :%s\n", pdata->uwb_vdd_pa);
  }

  if (of_property_read_string(np, "nxp,vdd-io", &pdata->uwb_vdd_io) < 0) {
    SR100_ERR_MSG("get uwb_vdd_io error\n");
    pdata->uwb_vdd_io = NULL;
  } else {
    SR100_ERR_MSG("uwb_vdd_io :%s\n", pdata->uwb_vdd_io);
  }

#ifdef CONFIG_UWB_PMIC_CLOCK
  if (of_get_property(np, "nxp,sr100-pm-clk", NULL)) {
    pdata->clk = clk_get(dev, "pll_clk");
    if (IS_ERR(pdata->clk)) {
      SR100_ERR_MSG("Couldn't get pll_clk\n");
    } else {
      SR100_DBG_MSG("got pll_clk\n");
      /* sdm845: if prepare_enable clk, clk always generated*/
      clk_prepare_enable(pdata->clk);
    }  
  }
#endif
  SR100_DBG_MSG("sr100 : irq_gpio = %d, ce_gpio = %d, ri_gpio = %d, wakeup_gpio = %d\n",
          pdata->irq_gpio, pdata->ce_gpio, pdata->ri_gpio, pdata->wakeup_gpio);
  return 0;
}

static int sr100_regulator_onoff(struct device *dev, struct sr100_dev* pdev, bool onoff)
{
  int rc = 0;
  struct regulator *regulator_uwb_vdd, *regulator_uwb_vdd_pa, *regulator_uwb_vdd_io;

  regulator_uwb_vdd = regulator_get(dev, pdev->uwb_vdd);
  if (IS_ERR(regulator_uwb_vdd) || regulator_uwb_vdd == NULL) {
    SR100_ERR_MSG("regulator_uwb_vdd regulator_get fail\n");
    return -ENODEV;
  }
	regulator_uwb_vdd_pa = regulator_get(dev, pdev->uwb_vdd_pa);
  if (IS_ERR(regulator_uwb_vdd_pa) || regulator_uwb_vdd_pa == NULL) {
    SR100_ERR_MSG("regulator_uwb_vdd_pa regulator_get fail\n");
    return -ENODEV;
  }

  regulator_uwb_vdd_io = regulator_get(dev, pdev->uwb_vdd_io);
  if (IS_ERR(regulator_uwb_vdd_io) || regulator_uwb_vdd_io == NULL) {
    SR100_ERR_MSG("regulator_uwb_vdd_io regulator_get fail\n");
    return -ENODEV;
  }

  SR100_DBG_MSG("sr100_regulator_onoff  = %d\n", onoff);
  if (onoff == true) {
    rc = regulator_set_load(regulator_uwb_vdd_io, 150000);
    if (rc) {
      SR100_ERR_MSG("regulator_uwb_vdd_io set_load failed, rc=%d\n", rc);
      goto done;
    }
    rc = regulator_enable(regulator_uwb_vdd_io);
    if (rc) {
      SR100_ERR_MSG("regulator_uwb_vdd_io enable failed, rc=%d\n", rc);
      goto done;
    }
    rc = regulator_set_load(regulator_uwb_vdd, 300000);
    if (rc) {
      SR100_ERR_MSG("regulator_uwb_vdd set_load failed, rc=%d\n", rc);
      goto done;
    }
    rc = regulator_enable(regulator_uwb_vdd);
    if (rc) {
      SR100_ERR_MSG("regulator_uwb_vdd enable failed, rc=%d\n", rc);
      goto done;
    }
    rc = regulator_set_load(regulator_uwb_vdd_pa, 150000);
    if (rc) {
      SR100_ERR_MSG("regulator_uwb_vdd_pa set_load failed, rc=%d\n", rc);
      goto done;
    }
    rc = regulator_enable(regulator_uwb_vdd_pa);
    if (rc) {
      SR100_ERR_MSG("regulator_uwb_vdd_pa enable failed, rc=%d\n", rc);
      goto done;
    }

/*    rc = regulator_set_voltage(regulator_uwb_vdd_io, 1800000, 1800000);
    if (rc) {
      SR100_ERR_MSG("regulator_uwb_vdd_io set_voltage failed, rc=%d\n", rc);
      goto done;
    }*/

  } else {
    rc = regulator_disable(regulator_uwb_vdd);
    if (rc) {
      SR100_ERR_MSG("regulator_uwb_vdd disable failed, rc=%d\n", rc);
      goto done;
    }
    rc = regulator_disable(regulator_uwb_vdd_pa);
    if (rc) {
      SR100_ERR_MSG("regulator_uwb_vdd_pa disable failed, rc=%d\n", rc);
      goto done;
    }

    rc = regulator_disable(regulator_uwb_vdd_io);
    if (rc) {
      SR100_ERR_MSG("regulator_uwb_vdd_io disable failed, rc=%d\n", rc);
      goto done;
    }

  }

done:
  regulator_put(regulator_uwb_vdd);
  regulator_put(regulator_uwb_vdd_pa);
  regulator_put(regulator_uwb_vdd_io);

  return rc;
}

#if 1//JH_DEBUG
void spi_intf_test(struct sr100_dev* sr100_dev)
{
  struct file filp;
  int ret = -EIO;
  unsigned char count=0, i=0,chk_cnt=0,j=0;
  //unsigned char test_cmd[] = { 0x02, 0x21, 0x00, 0x00 };
  unsigned char test_cmd[]= { 0x01, 0x21, 0x00, 0x00 , 0x03, 0x22, 0x00, 0x00 ,0x01, 0x21, 0x00, 0x00, 0x11, 0x08, 0x00, 0x00, 0x13, 0x02, 0x00, 0x00 };

  filp.private_data = sr100_dev;
  sr100_dev_ioctl(&filp, SR100_SET_PWR, 0);
  msleep(1); 
  sr100_dev_ioctl(&filp, SR100_SET_PWR, 1);
  msleep(10);
  for(j=0; j<5;j++){
    chk_cnt = 5;  
    for(i=0; i<chk_cnt; i++){

      count = 4;//sizeof(test_cmd/5);
      memset(sr100_dev->tx_buffer, 0x00, SR100_TXBUF_SIZE);
      memcpy(sr100_dev->tx_buffer, test_cmd+j*4, 4);
      sr100_dev->irq_enabled = true;
      enable_irq(sr100_dev->spi->irq);
      /* Write data */
      ret = spi_write(sr100_dev->spi, sr100_dev->tx_buffer, count);
      if (ret < 0) {
        ret = -EIO;
        SR100_ERR_MSG("spi_intf_test write test failed ret = 0x%x, retry cnt = %d \n", ret, i);	  
        msleep(10);
      } else {
        //ret = count;
        SR100_ERR_MSG("spi_intf_test write test success ret = 0x%x, count = %d cmd[0]=%x\n", ret, count, sr100_dev->tx_buffer[0]);
        break;
      }
    }
    sr100_dev->irq_enabled = true;
    enable_irq(sr100_dev->spi->irq);
    memset(sr100_dev->rx_buffer, 0x00, SR100_RXBUF_SIZE);
    if (!gpio_get_value(sr100_dev->irq_gpio)) {
      while (1) {
        sr100_dev->irq_enabled = true;
        enable_irq(sr100_dev->spi->irq);
        ret = wait_event_interruptible(sr100_dev->read_wq, !sr100_dev->irq_enabled);
        sr100_disable_irq(sr100_dev);
        if (ret) {
          SR100_ERR_MSG("wait_event_interruptible() : Failed\n");
          //return;
        }

        if (gpio_get_value(sr100_dev->irq_gpio)) break;

        SR100_ERR_MSG("%s: spurious interrupt detected\n", __func__);
      }
    }

    count = 10;
    ret = spi_read(sr100_dev->spi, (void*)sr100_dev->rx_buffer, count);
    if (ret < 0) {
      SR100_ERR_MSG("spi_intf_test: spi read error ret = 0x%x\n", ret);
      return;
    }

    SR100_ERR_MSG("spi_intf_test spi_read success ret = 0x%x, count = %d\n", ret, count);
    for(i=0;i<count;i++)
    {
      SR100_ERR_MSG("spi_intf_test spi_read[%d] = 0x%x\n",i, sr100_dev->rx_buffer[i]);
    }
  }
}
#endif
/**
 * \ingroup spi_driver
 * \brief To probe for SR100 SPI interface. If found initialize the SPI clock,
 bit rate & SPI mode.
          It will create the dev entry (SR100) for user space.
 *
 * \param[in]       struct spi_device *
 *
 * \retval 0 if ok.
 *
*/
static int sr100_probe(struct spi_device* spi) {
  int ret = -1;//,i;
  struct sr100_spi_platform_data* platform_data = NULL;
  struct sr100_spi_platform_data platform_data1;
  struct sr100_dev* sr100_dev = NULL;
#ifdef SR100_IRQ_ENABLE
  unsigned int irq_flags;
#endif
  SR100_DBG_MSG("Entry : %s\n", __FUNCTION__);
  SR100_DBG_MSG("%s chip select : %d , bus number = %d \n", __FUNCTION__,
                spi->chip_select, spi->master->bus_num);

  ret = sr100_parse_dt(&spi->dev, &platform_data1);
  if (ret) {
    pr_err("%s - Failed to parse DT\n", __func__);
    goto err_exit;
  }
  platform_data = &platform_data1;

  sr100_dev = kzalloc(sizeof(*sr100_dev), GFP_KERNEL);
  if (sr100_dev == NULL) {
    SR100_ERR_MSG("failed to allocate memory for module data\n");
    ret = -ENOMEM;
    goto err_exit;
  }
  ret = sr100_hw_setup(platform_data, sr100_dev, spi);
  if (ret < 0) {
    SR100_ERR_MSG("Failed to sr100_enable_SR100_IRQ_ENABLE\n");
    goto err_exit0;
  }

  spi->bits_per_word = 8;
  spi->mode = SPI_MODE_0;
  spi->max_speed_hz = SR100_SPI_CLOCK;
  ret = spi_setup(spi);
  if (ret < 0) {
    SR100_ERR_MSG("failed to do spi_setup()\n");
    goto err_exit0;
  }

  sr100_dev->spi = spi;
  sr100_dev->sr100_device.minor = MISC_DYNAMIC_MINOR;
  sr100_dev->sr100_device.name = "sr100";
  sr100_dev->sr100_device.fops = &sr100_dev_fops;
  sr100_dev->sr100_device.parent = &spi->dev;
  sr100_dev->irq_gpio = platform_data->irq_gpio;
  sr100_dev->ce_gpio = platform_data->ce_gpio;
  //sr100_dev->switch_gpio = platform_data->switch_gpio;
  sr100_dev->ri_gpio = platform_data->ri_gpio;
  sr100_dev->wakeup_gpio = platform_data->wakeup_gpio;
  sr100_dev->uwb_vdd = platform_data->uwb_vdd;
  sr100_dev->uwb_vdd_pa = platform_data->uwb_vdd_pa;
  sr100_dev->uwb_vdd_io = platform_data->uwb_vdd_io;
  sr100_dev->tx_buffer = kzalloc(SR100_TXBUF_SIZE, GFP_KERNEL);
  sr100_dev->rx_buffer = kzalloc(SR100_RXBUF_SIZE, GFP_KERNEL);

  if (sr100_dev->tx_buffer == NULL) {
    ret = -ENOMEM;
    goto exit_free_dev;
  }
  if (sr100_dev->rx_buffer == NULL) {
    ret = -ENOMEM;
    goto exit_free_dev;
  }

  dev_set_drvdata(&spi->dev, sr100_dev);

  /* init mutex and queues */
  init_waitqueue_head(&sr100_dev->read_wq);
  init_waitqueue_head(&sr100_dev->sync_wq);

  spin_lock_init(&sr100_dev->sync_lock);

#ifdef SR100_IRQ_ENABLE
  spin_lock_init(&sr100_dev->irq_enabled_lock);
#endif

  ret = misc_register(&sr100_dev->sr100_device);
  if (ret < 0) {
    SR100_ERR_MSG("misc_register failed! %d\n", ret);
    goto err_exit0;
  }

  ret = sr100_regulator_onoff(&spi->dev, sr100_dev, true);
  if (ret < 0) {
	SR100_ERR_MSG("regulator_on fail err:%d\n", ret);
  }
  usleep_range(1000, 1100);

#ifdef SR100_IRQ_ENABLE
  sr100_dev->spi->irq = gpio_to_irq(platform_data->irq_gpio);
  SR100_DBG_MSG("sr100_dev->spi->irq = 0x%x %d\n",
                  sr100_dev->spi->irq,sr100_dev->spi->irq);
  if (sr100_dev->spi->irq < 0) {
    SR100_ERR_MSG("gpio_to_irq request failed gpio = 0x%x\n",
                  platform_data->irq_gpio);
    goto err_exit1;
  }
  wake_lock_init(&sr100_dev->uwb_wake_lock, WAKE_LOCK_SUSPEND, "uwb_wake_lock");
  sr100_dev->sync_enabled = true;
  /* request irq.  the irq is set whenever the chip has data available
       * for reading.  it is cleared when all data has been read.
       */
  sr100_dev->irq_enabled = true;
#ifndef TEST_CODE
  irq_flags = IRQ_TYPE_LEVEL_HIGH;
#else
  irq_flags = IRQF_TRIGGER_RISING | IRQF_ONESHOT;
#endif
#ifndef TEST_CODE
  ret = request_irq(sr100_dev->spi->irq, sr100_dev_irq_handler,
                  irq_flags, sr100_dev->sr100_device.name, sr100_dev);
#else
  ret = request_threaded_irq(sr100_dev->spi->irq, NULL, sr100_dev_irq_handler,
                  irq_flags, sr100_dev->sr100_device.name, sr100_dev);
#endif
  if (ret) {
    SR100_ERR_MSG("request_irq failed\n");
    goto err_exit1;
  }
  sr100_disable_irq(sr100_dev);

#endif

  sr100_dev->enable_poll_mode = 0; /* Default IRQ read mode */

  SR100_DBG_MSG("gpio_set_value ri_gpio 0\n");
  gpio_set_value(sr100_dev->ri_gpio, 0);
  
  SR100_DBG_MSG("gpio_set_value wakeup_gpio 0\n");
  gpio_set_value(sr100_dev->wakeup_gpio, 0);
  SR100_DBG_MSG("Exit : %s\n", __FUNCTION__);
/*  pr_info("gpio_set_value ce_gpio 1\n");
  gpio_set_value(sr100_dev->ce_gpio, 1);
  for(i=0; i<60; i++){
    msleep(1000);
	gpio_set_value(sr100_dev->ce_gpio, i%2);
	
  	pr_info("sr100_regulator_onoff %d\n", i%2);
	ret = sr100_regulator_onoff(&spi->dev, sr100_dev, i%2);
    if (ret < 0) {
	  pr_err("regulator_on fail err:%d\n", ret);
    }
  }*/
#if 0//JH_DEBUG  
	spi_intf_test(sr100_dev);
#endif
  return ret;
exit_free_dev:
  if (sr100_dev != NULL) {
    if (sr100_dev->tx_buffer) {
      kfree(sr100_dev->tx_buffer);
    }
    if (sr100_dev->rx_buffer) {
      kfree(sr100_dev->rx_buffer);
    }
    kfree(sr100_dev);
  }
  return ret;
err_exit1:
  misc_deregister(&sr100_dev->sr100_device);
  wake_lock_destroy(&sr100_dev->uwb_wake_lock);

err_exit0:
  if (sr100_dev != NULL) kfree(sr100_dev);
err_exit:
  SR100_ERR_MSG("ERROR: Exit : %s ret %d\n", __FUNCTION__, ret);
  return ret;
}

/**
 * \ingroup spi_driver
 * \brief Will get called when the device is removed to release the resources.
 *
 * \param[in]       struct spi_device
 *
 * \retval 0 if ok.
 *
*/

static int sr100_remove(struct spi_device* spi) {
  struct sr100_dev* sr100_dev = sr100_get_data(spi);
  SR100_DBG_MSG("Entry : %s\n", __FUNCTION__);
  wake_lock_destroy(&sr100_dev->uwb_wake_lock);
  gpio_free(sr100_dev->ce_gpio);
//  gpio_free(sr100_dev->switch_gpio);
#ifdef SR100_IRQ_ENABLE
  free_irq(sr100_dev->spi->irq, sr100_dev);
  gpio_free(sr100_dev->irq_gpio);
#endif

  misc_deregister(&sr100_dev->sr100_device);
  if (sr100_dev->tx_buffer != NULL) kfree(sr100_dev->tx_buffer);
  if (sr100_dev->rx_buffer != NULL) kfree(sr100_dev->rx_buffer);
  if (sr100_dev != NULL) kfree(sr100_dev);
  SR100_DBG_MSG("Exit : %s\n", __FUNCTION__);
  return 0;
}
static struct of_device_id sr100_dt_match[] = {{
                                                .compatible = "nxp,sr100",
                                               },
                                               {}};
static struct spi_driver sr100_driver = {
    .driver =
        {
         .name = "sr100",
         .bus = &spi_bus_type,
         .owner = THIS_MODULE,
         .of_match_table = sr100_dt_match,
        },
    .probe = sr100_probe,
    .remove = (sr100_remove),
};

/**
 * \ingroup spi_driver
 * \brief Module init interface
 *
 * \param[in]       void
 *
 * \retval handle
 *
*/

static int __init sr100_dev_init(void) {
  nfc_logger_init();
  debug_level = SR100_FULL_DEBUG;

  SR100_DBG_MSG("Entry : %s\n", __FUNCTION__);

  return spi_register_driver(&sr100_driver);
}
module_init(sr100_dev_init);

/**
 * \ingroup spi_driver
 * \brief Module exit interface
 *
 * \param[in]       void
 *
 * \retval void
 *
*/

static void __exit sr100_dev_exit(void) {
  SR100_DBG_MSG("Entry : %s\n", __FUNCTION__);

  spi_unregister_driver(&sr100_driver);
  SR100_DBG_MSG("Exit : %s\n", __FUNCTION__);
}
module_exit(sr100_dev_exit);

MODULE_AUTHOR("Manjunatha Venkatesh");
MODULE_DESCRIPTION("NXP SR100 SPI driver");
MODULE_LICENSE("GPL");

/** @} */
