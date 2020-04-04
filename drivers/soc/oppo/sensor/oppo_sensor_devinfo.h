#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/smem.h>
#include <linux/sysfs.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>

#include <linux/delay.h>
#include <linux/slab.h>

#include <soc/oppo/oppo_project.h>
#include <soc/oppo/device_info.h>

#define SENSOR_DEVINFO_SYNC_TIME  10000 //10s

//SMEM_SENSOR = SMEM_VERSION_FIRST + 23,
#define SMEM_SENSOR 130

static int retry_count = 5;

struct devinfo {
	int id;
	char* ic_name;
	char* vendor_name;
};

struct sensor_info {
	uint32_t ps_match_id;
	uint32_t als_match_id;
	int als_type;
	int ps_type;
	int ps_cali_type;
	uint8_t   is_ps_initialed;
	uint8_t   is_als_initialed;
	uint8_t   is_unit_device;
	uint8_t   is_als_dri;
	uint8_t   irq_number;
	uint8_t   bus_number;
	uint32_t  als_factor;
	uint8_t	  ak_msensor_dir;
	uint8_t	  mx_msensor_dir;
	uint8_t	  st_gsensor_dir;
	uint8_t	  bs_gsensor_dir;
};

struct oppo_als_cali_data {
	int red_max_lux;
	int green_max_lux;
	int blue_max_lux;
	int white_max_lux;
	int cali_coe;
	int row_coe;
	int ssr;
	struct proc_dir_entry 		*proc_oppo_als;
};

enum VENDOR_ID {
	STK3A5X = 0x01,
	TCS3701 = 0x02,
};

typedef enum {
	NORMAL	= 0x01,
	UNDER_LCD = 0x02,
} alsps_position_type;

typedef enum {
	SOFTWARE_CAIL = 0x01,
	HARDWARE_CAIL = 0x02,
} ps_calibration_type;
