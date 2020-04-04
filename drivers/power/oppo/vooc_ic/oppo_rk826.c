/**********************************************************************************
* Copyright (c)  2017-2019  Guangdong OPPO Mobile Comm Corp., Ltd
* VENDOR_EDIT
* Description: For Rockchip RK826 ASIC
* Version   : 1.0
* Date      : 2019-08-15
* Author    : SJC@PhoneSW.BSP
* ------------------------------ Revision History: --------------------------------
* <version>       <date>        	<author>              		<desc>
* Revision 1.0    2019-08-15  	SJC@PhoneSW.BSP    		Created for new architecture
***********************************************************************************/

#define VOOC_ASIC_RK826

#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>

#ifdef CONFIG_OPPO_CHARGER_MTK
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <asm/atomic.h>

#include <linux/xlog.h>
//#include <upmu_common.h>
//#include <mt-plat/mtk_gpio.h>
#include <linux/dma-mapping.h>

//#include <mt-plat/battery_meter.h>
#include <linux/module.h>
#include <soc/oppo/device_info.h>

#else
#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <soc/oppo/device_info.h>
#endif
#include "oppo_vooc_fw.h"

extern int charger_abnormal_log;

#ifdef CONFIG_OPPO_CHARGER_MTK
#define I2C_MASK_FLAG	(0x00ff)
#define I2C_ENEXT_FLAG	(0x0200)
#define I2C_DMA_FLAG	(0xdead2000)
#endif

#define GTP_DMA_MAX_TRANSACTION_LENGTH	255 /* for DMA mode */

#define ERASE_COUNT			959 /*0x0000-0x3BFF*/

#define BYTE_OFFSET			2
#define BYTES_TO_WRITE		16
#define FW_CHECK_FAIL		0
#define FW_CHECK_SUCCESS	1


#define PAGE_UNIT			128
#define TRANSFER_LIMIT		72
#define I2C_ADDR			0x14
#define REG_RESET			0x5140
#define REG_SYS0			0x52C0
#define REG_HOST			0x52C8
#define	REG_SLAVE			0x52CC
#define REG_STATE			0x52C4
#define REG_MTP_SELECT		0x4308
#define REG_MTP_ADDR		0x4300
#define REG_MTP_DATA		0x4304
#define REG_SRAM_BEGIN		0x2000
#define SYNC_FLAG			0x53594E43
#define NOT_SYNC_FLAG		(~SYNC_FLAG)
#define REC_01_FLAG			0x52454301
#define REC_0O_FLAG			0x52454300
#define RESTART_FLAG		0x52455354
#define MTP_SELECT_FLAG	0x000f0001
#define MTP_ADDR_FLAG		0xffff8000
#define SLAVE_IDLE			0x49444C45
#define SLAVE_BUSY			0x42555359
#define SLAVE_ACK			0x41434B00
#define SLAVE_ACK_01		0x41434B01
#define FORCE_UPDATE_FLAG	0xaf1c0b76
#define SW_RESET_FLAG		0X0000fdb9

#define STATE_READY			0x0
#define STATE_SYNC			0x1
#define STATE_REQUEST		0x2
#define STATE_FIRMWARE		0x3
#define STATE_FINISH			0x4

typedef struct {
	u32 tag;
	u32 length;
	u32 timeout;
	u32 ram_offset;
	u32 fw_crc;
	u32 header_crc;
} struct_req, *pstruct_req;

static struct oppo_vooc_chip *the_chip = NULL;
struct wakeup_source *rk826_update_wake_lock = NULL;

#ifdef CONFIG_OPPO_CHARGER_MTK
#define GTP_SUPPORT_I2C_DMA		0
#define I2C_MASTER_CLOCK			300

DEFINE_MUTEX(dma_wr_access_rk826);

static char gpDMABuf_pa[GTP_DMA_MAX_TRANSACTION_LENGTH] = {0};

#if GTP_SUPPORT_I2C_DMA
static int i2c_dma_write(struct i2c_client *client, u8 addr, s32 len, u8 *txbuf);
static int i2c_dma_read(struct i2c_client *client, u8 addr, s32 len, u8 *txbuf);
static u8 *gpDMABuf_va = NULL;
static dma_addr_t gpDMABuf_pa = 0;
#endif

#if GTP_SUPPORT_I2C_DMA
static int i2c_dma_read(struct i2c_client *client, u8 addr, s32 len, u8 *rxbuf)
{
	int ret;
	s32 retry = 0;
	u8 buffer[1];

	struct i2c_msg msg[2] = {
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.flags = 0,
			.buf = buffer,
			.len = 1,
			.timing = I2C_MASTER_CLOCK
		},
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.ext_flag = (client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG),
			.flags = I2C_M_RD,
			.buf = (__u8 *)gpDMABuf_pa, /*modified by PengNan*/
			.len = len,
			.timing = I2C_MASTER_CLOCK
		},
	};

	mutex_lock(&dma_wr_access_rk826);
	/*buffer[0] = (addr >> 8) & 0xFF;*/
	buffer[0] = addr & 0xFF;
	if (rxbuf == NULL) {
		mutex_unlock(&dma_wr_access_rk826);
		return -1;
	}
	/*chg_err("vooc dma i2c read: 0x%x, %d bytes(s)\n", addr, len);*/
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg[0], 2);
		if (ret < 0) {
			continue;
		}
		memcpy(rxbuf, gpDMABuf_va, len);
		mutex_unlock(&dma_wr_access_rk826);
		return 0;
	}
	/*chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);*/
	mutex_unlock(&dma_wr_access_rk826);
	return ret;
}

static int i2c_dma_write(struct i2c_client *client, u8 addr, s32 len, u8 const *txbuf)
{
	int ret = 0;
	s32 retry = 0;
	u8 *wr_buf = gpDMABuf_va;
	struct i2c_msg msg = {
		.addr = (client->addr & I2C_MASK_FLAG),
		.ext_flag = (client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG),
		.flags = 0,
		.buf = (__u8 *)gpDMABuf_pa, /*modified by PengNan*/
		.len = 1 + len,
		.timing = I2C_MASTER_CLOCK
	};

	mutex_lock(&dma_wr_access_rk826);
	wr_buf[0] = (u8)(addr & 0xFF);
	if (txbuf == NULL) {
		mutex_unlock(&dma_wr_access_rk826);
		return -1;
	}
	memcpy(wr_buf + 1, txbuf, len);
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0) {
			continue;
		}
		mutex_unlock(&dma_wr_access_rk826);
		return 0;
	}
	/*chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);*/
	mutex_unlock(&dma_wr_access_rk826);
	return ret;
}
#endif /*GTP_SUPPORT_I2C_DMA*/

static int oppo_i2c_dma_read(struct i2c_client *client, u16 addr, s32 len, u8 *rxbuf)
{
	int ret;
	s32 retry = 0;
	u8 buffer[2] = {0};
	struct i2c_msg msg[2] = {
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.flags = 0,
			.buf = buffer,
			.len = 2,
		},
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.flags = I2C_M_RD,
			.buf = (__u8 *)gpDMABuf_pa,   /*modified by PengNan*/
			.len = len,
		},
	};

	mutex_lock(&dma_wr_access_rk826);
	buffer[0] = (u8)(addr & 0xFF);
	buffer[1] = (u8)((addr >> 8) & 0xFF);
	if (rxbuf == NULL) {
		mutex_unlock(&dma_wr_access_rk826);
		return -1;
	}
	//chg_debug("vooc dma i2c read: 0x%x, %d bytes(s)\n", addr, len);
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg[0], 2);
		if (ret < 0) {
			continue;
		}
		memcpy(rxbuf, gpDMABuf_pa, len);
		mutex_unlock(&dma_wr_access_rk826);
		return 0;
	}
	chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);
	mutex_unlock(&dma_wr_access_rk826);
	return ret;
}

static int oppo_i2c_dma_write(struct i2c_client *client, u16 addr, s32 len, u8 const *txbuf)
{
	int ret = 0;
	s32 retry = 0;
	u8 *wr_buf = gpDMABuf_pa;
	struct i2c_msg msg = {
		.addr = (client->addr & I2C_MASK_FLAG),
		.flags = 0,
		.buf = (__u8 *)gpDMABuf_pa, /*modified by PengNan*/
		.len = 2 + len,
	};

	mutex_lock(&dma_wr_access_rk826);
	wr_buf[0] = (u8)(addr & 0xFF);
	wr_buf[1] = (u8)((addr >> 8) & 0xFF);
	if (txbuf == NULL) {
		mutex_unlock(&dma_wr_access_rk826);
		return -1;
	}
	memcpy(wr_buf + 2, txbuf, len);
	//chg_debug("vooc dma i2c write: 0x%x, %d bytes(s)\n", addr, len);
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0) {
			continue;
		}
		mutex_unlock(&dma_wr_access_rk826);
		return 0;
	}
	chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);
	mutex_unlock(&dma_wr_access_rk826);
	return ret;
}

#else /*CONFIG_OPPO_CHARGER_MTK*/

static int oppo_i2c_dma_read(struct i2c_client *client, u16 addr, s32 len, u8 *rxbuf)
{
	return 0;
}

static int oppo_i2c_dma_write(struct i2c_client *client, u16 addr, s32 len, u8 const *txbuf)
{
	return 0;
}
#endif /*CONFIG_OPPO_CHARGER_MTK*/

static int oppo_vooc_i2c_read(struct i2c_client *client, u8 addr, s32 len, u8 *rxbuf)
{
#ifdef CONFIG_OPPO_CHARGER_MTK
#if GTP_SUPPORT_I2C_DMA
	return i2c_dma_read(client, addr, len, rxbuf);
#else
	return i2c_smbus_read_i2c_block_data(client, addr, len, rxbuf);
#endif
#else
	return i2c_smbus_read_i2c_block_data(client, addr, len, rxbuf);
#endif
}

static int oppo_vooc_i2c_write(struct i2c_client *client, u8 addr, s32 len, u8 const *txbuf)
{
#ifdef CONFIG_OPPO_CHARGER_MTK
#if GTP_SUPPORT_I2C_DMA
	return i2c_dma_write(client, addr, len, txbuf);
#else
	return i2c_smbus_write_i2c_block_data(client, addr, len, txbuf);
#endif
#else
	return i2c_smbus_write_i2c_block_data(client, addr, len, txbuf);
#endif
}

static bool rk826_fw_update_check(struct oppo_vooc_chip *chip)
{
	int ret = 0;
	u8 data_buf[4]= {0};
	u32 mtp_select_flag = MTP_SELECT_FLAG;
	u32 mtp_addr_flag = MTP_ADDR_FLAG;
	u32 i = 0;

	if (!chip->firmware_data) {
		chg_err("rk826_fw_data Null, Return\n");
		return FW_CHECK_FAIL;
	}
	ret = oppo_i2c_dma_write(chip->client, REG_MTP_SELECT, 4, (u8 *)(&mtp_select_flag));
	if (ret < 0) {
		chg_err("write mtp select reg error\n");
		goto fw_update_check_err;
	}

	for (i = chip->fw_data_count - 11; i <= chip->fw_data_count - 4; i++) {
		mtp_addr_flag = (MTP_ADDR_FLAG | i);
		ret = oppo_i2c_dma_write(chip->client, REG_MTP_ADDR, 4, (u8 *)(&mtp_addr_flag));
		if (ret < 0) {
			chg_err("write mtp addr error\n");
			goto fw_update_check_err;
		}

		do {
			ret  = oppo_i2c_dma_read(chip->client, REG_MTP_SELECT, 4, data_buf);
			if (ret < 0) {
				chg_err("read mtp select reg error\n");
				goto fw_update_check_err;
			}
		} while (!(data_buf[1] & 0x01));

		ret = oppo_i2c_dma_read(chip->client, REG_MTP_DATA, 4, data_buf);
		if (ret < 0) {
			chg_err("read mtp data error\n");
			goto fw_update_check_err;
		}
		chg_debug("the read compare data: %d\n", data_buf[0]);
		if (i == chip->fw_data_count - 4)
			chip->fw_mcu_version = data_buf[0];
		if (data_buf[0] != chip->firmware_data[i]) {
			//chg_err("rk826_fw_data check fail\n");
			goto fw_update_check_err;
		}
	}
	return FW_CHECK_SUCCESS;

fw_update_check_err:
	chg_err("rk826_fw_data check fail\n");
	return FW_CHECK_FAIL;
}

u32 js_hash_en(u32 hash, const u8 *buf, u32 len)
{
	u32 i;

	for (i = 0; i < len; i++)
		hash ^= ((hash << 5) + buf[i] + (hash >> 2));
	return hash;
}

u32 js_hash(const u8 *buf, u32 len)
{
	return js_hash_en(0x47C6A7E6, buf, len);
}

int WriteSram(struct oppo_vooc_chip *chip, const u8 *buf, u32 size)
{
	u8 offset = 0;
	u16 reg_addr;
	int ret = 0;
	int i = 0;
	int cur_size = 0;
	int try_count = 5;
	u8 readbuf[4] = {0};
	u8 tx_buff[4] = {0};
	u8 TEST[72] = {0};
	u32 rec_0O_flag = REC_0O_FLAG;
	u32 rec_01_flag = REC_01_FLAG;

	while (size) {
		if (size >= TRANSFER_LIMIT) {
			cur_size = TRANSFER_LIMIT;
		} else
			cur_size = size;
		memcpy(TEST, buf, 72);
		for (i = 0; i < cur_size / 4; i++) {
			reg_addr = REG_SRAM_BEGIN + i * 4;
			memcpy(tx_buff, buf + offset + i * 4, 4);
			ret = oppo_i2c_dma_write(chip->client, reg_addr, 4, tx_buff);
			if (ret < 0) {
				chg_err("write SRAM fail");
				return -1;
			}
		}
		//ret = oppo_i2c_dma_read(chip->client, REG_STATE, 4, readbuf);
		//chg_err("teh REG_STATE1: %d", *(u32*)readbuf);
		ret = oppo_i2c_dma_write(chip->client, REG_HOST, 4, (u8 *)(&rec_0O_flag));
		//mdelay(3);
		//write rec_00 into host
		if (ret < 0) {
			chg_err("write rec_00 into host");
			return -1;
		}
		//read slave
		do {
			ret = oppo_i2c_dma_read(chip->client, REG_STATE, 4, readbuf);
			chg_err(" the try_count: %d, the REG_STATE: %d", try_count, *(u32*)readbuf);
			//msleep(10);
			ret = oppo_i2c_dma_read(chip->client, REG_SLAVE, 4, readbuf);
			if (ret < 0) {
				chg_err("read slave ack fail");
				return -1;
			}
			try_count--;
		} while (*(u32 *)readbuf == SLAVE_BUSY);
		chg_debug("the try_count: %d, the readbuf: %x\n", try_count, *(u32 *)readbuf);
		if ((*(u32 *)readbuf != SLAVE_ACK) && (*(u32 *)readbuf != SLAVE_ACK_01)) {
			chg_err(" slave ack fail");
			return -1;
		}

		//write rec_01 into host
		ret = oppo_i2c_dma_write(chip->client, REG_HOST, 4, (u8 *)(&rec_01_flag));
		//write rec_00 into host
		if (ret < 0) {
			chg_err("write rec_00 into host");
			return -1;
		}

		//msleep(50);
		offset += cur_size;
		size -= cur_size;
		try_count = 5;
	}
	return 0;
}

int DownloadFirmware(struct oppo_vooc_chip *chip, const u8 *buf, u32 size)
{
	u8 transfer_buf[TRANSFER_LIMIT];
	u32 onetime_size = TRANSFER_LIMIT - 8;
	u32 index = 0;
	u32 offset = 0;
	int ret = 0;

	chg_debug("size: %d\n", size);
	do {
		memset(transfer_buf, 0, TRANSFER_LIMIT);
		if (size >= onetime_size) {
			memcpy(transfer_buf, buf + offset, onetime_size);
			size-= onetime_size;
			offset += onetime_size;
		} else {
			memcpy(transfer_buf, buf + offset, size);
			offset += size;
			size = 0;
		}
		*((u32 *)(transfer_buf + onetime_size)) = index;
		*((u32 *)(transfer_buf + onetime_size + 4)) = js_hash(transfer_buf, onetime_size + 4);
		ret = WriteSram(chip, transfer_buf, TRANSFER_LIMIT);
		if (ret != 0) {
			return ret;
		}
		chg_debug("index: %d\n", index);
		index++;
	} while (size);
	return 0;
}

static int rk826_fw_update(struct oppo_vooc_chip *chip)
{
	int ret = 0;
	int iTryCount = 3;
	struct_req req = {0};
	u32 sync_flag = SYNC_FLAG;
	u32 force_update_flag = FORCE_UPDATE_FLAG;
	u32 sw_reset_flag = SW_RESET_FLAG;
	u32 rec_01_flag = REC_01_FLAG;
	u8 read_buf[4] = {0};

	oppo_i2c_dma_write(chip->client, REG_SYS0, 4, (u8 *)(&force_update_flag));
	oppo_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	while (iTryCount) {
		ret = oppo_i2c_dma_write(chip->client, REG_HOST, 4, (u8 *)(&sync_flag));
		if (ret < 0) {
			chg_err("write sync failed!");
			goto update_fw_err;
		}

		//2.check ~sync
		ret = oppo_i2c_dma_read(chip->client, REG_HOST, 4, read_buf);
		printk("the data: %x, %x, %x, %x\n", read_buf[0], read_buf[1], read_buf[2], read_buf[3]);
		printk("the data: %x, %x, %x, %x\n", *(u8 *)(&sync_flag), *((u8 *)(&sync_flag) + 1), *((u8 *)(&sync_flag) + 2), *((u8 *)(&sync_flag) + 3));

		if (ret < 0) {
			chg_err("read sync failed!");
			goto update_fw_err;
		}
		if (*(u32 *)read_buf != NOT_SYNC_FLAG) {
			chg_err("check ~sync failed!");
			iTryCount--;
			msleep(50);
			continue;
		}
		break;
	}

	if (iTryCount == 0) {
		chg_err("Failed to sync!");
		goto update_fw_err;
	}

	// write rec_01
	ret = oppo_i2c_dma_write(chip->client, REG_HOST, 4, (u8 *)(&rec_01_flag));
	if (ret < 0) {
		chg_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	msleep(10);

	// read reg_state
	ret = oppo_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	if (ret<0) {
		chg_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_REQUEST) {
		chg_err("Failed to go into request_state!");
		goto update_fw_err;
	}

	// send req
	req.tag = 0x51455220;
	req.ram_offset = 0;
	req.length =  chip->fw_data_count;
	req.timeout = 0;
	req.fw_crc = js_hash(chip->firmware_data, req.length);
	req.header_crc = js_hash((const u8*)&req, sizeof(req) - 4);
	if ((ret = WriteSram(chip, (const u8* )&req, sizeof(req))) != 0) {
		chg_err("failed to send request!err=%d\n", ret);
		goto update_fw_err;
	}
	msleep(10);

	// read state firwware
	ret = oppo_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	printk("read state firwware: %x\n", *(u32 *)read_buf);
	if (ret < 0) {
		chg_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FIRMWARE) {
		chg_err("Failed to go into firmware_state");
		goto update_fw_err;
	}

	// send fw
	if ((ret = DownloadFirmware(chip, chip->firmware_data, chip->fw_data_count)) != 0) {
		chg_err("failed to send firmware");
		goto update_fw_err;
	}

	ret = oppo_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	if (ret < 0) {
		chg_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FINISH) {
		chg_err("Failed to go into finish_state");
		goto update_fw_err;
	}
	oppo_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	chip->fw_mcu_version = chip->fw_data_version;
	chg_debug("success\n");
	return 0;

update_fw_err:
	charger_abnormal_log = CRITICAL_LOG_VOOC_FW_UPDATE_ERR;
	chg_err("fail\n");
	return 1;
}

static int rk826_get_fw_verion_from_ic(struct oppo_vooc_chip *chip)
{
	unsigned char addr_buf[2] = {0x3B, 0xF0};
	unsigned char data_buf[4] = {0};
	int rc = 0;
	int update_result = 0;

	if (oppo_is_power_off_charging(chip) == true || oppo_is_charger_reboot(chip) == true) {
		chip->mcu_update_ing = true;
		update_result = rk826_fw_update(chip);
		chip->mcu_update_ing = false;
		if (update_result) {
			msleep(30);
			opchg_set_clock_sleep(chip);
			opchg_set_reset_active(chip);
		}
	} else {
		opchg_set_clock_active(chip);
		chip->mcu_boot_by_gpio = true;
		msleep(10);
		opchg_set_reset_active(chip);
		chip->mcu_update_ing = true;
		msleep(2500);
		chip->mcu_boot_by_gpio = false;
		opchg_set_clock_sleep(chip);

		//first:set address
		rc = oppo_vooc_i2c_write(chip->client, 0x01, 2, &addr_buf[0]);
		if (rc < 0) {
			chg_err(" i2c_write 0x01 error\n" );
			return FW_CHECK_FAIL;
		}
		msleep(2);
		oppo_vooc_i2c_read(chip->client, 0x03, 4, data_buf);
		//strcpy(ver,&data_buf[0]);
		chg_err("data:%x %x %x %x, fw_ver:%x\n", data_buf[0], data_buf[1], data_buf[2], data_buf[3], data_buf[0]);

		chip->mcu_update_ing = false;
		msleep(5);
		opchg_set_reset_active(chip);
	}
	return data_buf[0];
}

static int rk826_fw_check_then_recover(struct oppo_vooc_chip *chip)
{
	int update_result = 0;
	int try_count = 5;
	int ret = 0;

	if (!chip->firmware_data) {
		chg_err("rk826_fw_data Null, Return\n");
		return FW_ERROR_DATA_MODE;
	} else {
		chg_debug("begin\n");
	}

	if (oppo_is_power_off_charging(chip) == true || oppo_is_charger_reboot(chip) == true) {
		chip->mcu_update_ing = true;
		opchg_set_reset_active(chip);
		msleep(5);
		update_result = rk826_fw_update(chip);
		chip->mcu_update_ing = false;
		if (update_result) {
			msleep(30);
			opchg_set_clock_sleep(chip);
			opchg_set_reset_active(chip);
		}
		ret = FW_NO_CHECK_MODE;
	} else {
		opchg_set_clock_active(chip);
		chip->mcu_boot_by_gpio = true;
		msleep(10);
		opchg_set_reset_active(chip);
		chip->mcu_update_ing = true;
		msleep(2500);
		chip->mcu_boot_by_gpio = false;
		opchg_set_clock_sleep(chip);
		__pm_stay_awake(rk826_update_wake_lock);
		if (rk826_fw_update_check(chip) == FW_CHECK_FAIL) {
			chg_debug("firmware update start\n");
			do {
				update_result = rk826_fw_update(chip);
				if (!update_result)
					break;
			} while ((update_result) && (--try_count > 0));
			chg_debug("firmware update end, retry times %d\n", 5 - try_count);
		} else {
			chip->vooc_fw_check = true;
			chg_debug("fw check ok\n");
		}
		__pm_relax(rk826_update_wake_lock);
		chip->mcu_update_ing = false;
		msleep(5);
		opchg_set_reset_active(chip);
		ret = FW_CHECK_MODE;
	}
	opchg_set_reset_sleep(chip);

	return ret;
}

struct oppo_vooc_operations oppo_rk826_ops = {
	.fw_update = rk826_fw_update,
	.fw_check_then_recover = rk826_fw_check_then_recover,
	.set_switch_mode = opchg_set_switch_mode,
	.eint_regist = oppo_vooc_eint_register,
	.eint_unregist = oppo_vooc_eint_unregister,
	.set_data_active = opchg_set_data_active,
	.set_data_sleep = opchg_set_data_sleep,
	.set_clock_active = opchg_set_clock_active,
	.set_clock_sleep = opchg_set_clock_sleep,
	.get_gpio_ap_data = opchg_get_gpio_ap_data,
	.read_ap_data = opchg_read_ap_data,
	.reply_mcu_data = opchg_reply_mcu_data,
	.reset_fastchg_after_usbout = reset_fastchg_after_usbout,
	.switch_fast_chg = switch_fast_chg,
	.reset_mcu = opchg_set_reset_active,
	.set_vooc_chargerid_switch_val = opchg_set_vooc_chargerid_switch_val,
	.is_power_off_charging = oppo_is_power_off_charging,
	.get_reset_gpio_val = oppo_vooc_get_reset_gpio_val,
	.get_switch_gpio_val = oppo_vooc_get_switch_gpio_val,
	.get_ap_clk_gpio_val = oppo_vooc_get_ap_clk_gpio_val,
	.get_fw_version = rk826_get_fw_verion_from_ic,
	.get_clk_gpio_num = opchg_get_clk_gpio_num,
	.get_data_gpio_num = opchg_get_data_gpio_num,
};

static void register_vooc_devinfo(void)
{
	int ret = 0;
	char *version;
	char *manufacture;

	version = "rk826";
	manufacture = "ROCKCHIP";

	ret = register_device_proc("vooc", version, manufacture);
	if (ret) {
		chg_err(" fail\n");
	}
}

static void rk826_shutdown(struct i2c_client *client)
{
	if (!the_chip) {
		return;
	}
	opchg_set_switch_mode(the_chip, NORMAL_CHARGER_MODE);
	msleep(10);
	if (oppo_vooc_get_fastchg_started() == true) {
		opchg_set_clock_sleep(the_chip);
		msleep(10);
		opchg_set_reset_active(the_chip);
	}
	msleep(80);
	return;
}

static ssize_t vooc_fw_check_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = {0};
	char read_data[32] = {0};
	int len = 0;

	if (the_chip && the_chip->vooc_fw_check == true) {
		read_data[0] = '1';
	} else {
		read_data[0] = '0';
	}
	len = sprintf(page, "%s", read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations vooc_fw_check_proc_fops = {
	.read = vooc_fw_check_read,
	.llseek = noop_llseek,
};

static int init_proc_vooc_fw_check(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("vooc_fw_check", 0444, NULL, &vooc_fw_check_proc_fops);
	if (!p) {
		chg_err("proc_create vooc_fw_check_proc_fops fail!\n");
	}
	return 0;
}

static int rk826_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct oppo_vooc_chip *chip;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->client = client;
	chip->dev = &client->dev;
	i2c_set_clientdata(client, chip);

#ifdef CONFIG_OPPO_CHARGER_MTK
#if GTP_SUPPORT_I2C_DMA
	client->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	gpDMABuf_va = (u8 *)dma_alloc_coherent(&client->dev, GTP_DMA_MAX_TRANSACTION_LENGTH, &gpDMABuf_pa, GFP_KERNEL);
	if (!gpDMABuf_va) {
		chg_err("[Error] Allocate DMA I2C Buffer failed!\n");
	} else {
		chg_debug(" ppp dma_alloc_coherent success\n");
	}
	memset(gpDMABuf_va, 0, GTP_DMA_MAX_TRANSACTION_LENGTH);
#endif
#endif
	if (get_vooc_mcu_type(chip) != OPPO_VOOC_ASIC_HWID_RK826) {
		chg_err("It is not rk826\n");
		return -ENOMEM;
	}

	chip->pcb_version = g_hw_version;
	chip->vooc_fw_check = false;
	mutex_init(&chip->pinctrl_mutex);

	oppo_vooc_fw_type_dt(chip);
	if (chip->batt_type_4400mv) {
		chip->firmware_data = rk826_fw_data_4400_vooc_ffc_15c;
		chip->fw_data_count = sizeof(rk826_fw_data_4400_vooc_ffc_15c);
		chip->fw_data_version = rk826_fw_data_4400_vooc_ffc_15c[chip->fw_data_count - 4];
	} else {//default
		chip->firmware_data = rk826_fw_data_4400_vooc_ffc_15c;
		chip->fw_data_count = sizeof(rk826_fw_data_4400_vooc_ffc_15c);
		chip->fw_data_version = rk826_fw_data_4400_vooc_ffc_15c[chip->fw_data_count - 4];
	}

	if (chip->vooc_fw_type == VOOC_FW_TYPE_RK826_4400_VOOC_FFC_15C) {
		chip->firmware_data = rk826_fw_data_4400_vooc_ffc_15c;
		chip->fw_data_count = sizeof(rk826_fw_data_4400_vooc_ffc_15c);
		chip->fw_data_version = rk826_fw_data_4400_vooc_ffc_15c[chip->fw_data_count - 4];
	}

	chip->vops = &oppo_rk826_ops;
	chip->fw_mcu_version = 0;

	oppo_vooc_gpio_dt_init(chip);
	opchg_set_clock_sleep(chip);
	oppo_vooc_delay_reset_mcu_init(chip);
	rk826_update_wake_lock = wakeup_source_register("rk826_update_wake_lock");
	if (chip->vooc_fw_update_newmethod) {
		if (oppo_is_rf_ftm_mode()) {
			oppo_vooc_fw_update_work_init(chip);
		}
	} else {
		oppo_vooc_fw_update_work_init(chip);
	}

	oppo_vooc_init(chip);
	register_vooc_devinfo();
	init_proc_vooc_fw_check();
	the_chip = chip;
	chg_debug("rk826 success\n");
	return 0;
}

/**********************************************************
  *
  *   [platform_driver API]
  *
  *********************************************************/
static const struct of_device_id rk826_match[] = {
	{ .compatible = "oppo,rk826-fastcg"},
	{ },
};

static const struct i2c_device_id rk826_id[] = {
	{"rk826-fastcg", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, rk826_id);

struct i2c_driver rk826_i2c_driver = {
	.driver = {
		.name = "rk826-fastcg",
		.owner        = THIS_MODULE,
		.of_match_table = rk826_match,
	},
	.probe = rk826_driver_probe,
	.shutdown = rk826_shutdown,
	.id_table = rk826_id,
};

static int __init rk826_subsys_init(void)
{
	int ret = 0;
	chg_debug(" init start\n");
	init_hw_version();

	if (i2c_add_driver(&rk826_i2c_driver) != 0) {
		chg_err(" failed to register rk826 i2c driver.\n");
	} else {
		chg_debug(" Success to register rk826 i2c driver.\n");
	}
	return ret;
}

subsys_initcall(rk826_subsys_init);
MODULE_DESCRIPTION("Driver for oppo vooc rk826 fast mcu");
MODULE_LICENSE("GPL v2");

