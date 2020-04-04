#include "oppo_sensor_devinfo.h"

void oppo_device_dir_redirect(struct sensor_info * chip)
{
	if(((strcmp(get_PCB_Version(),"T0") == 0) || (strcmp(get_PCB_Version(),"EVB1") == 0) ||(strcmp(get_PCB_Version(),"T1") == 0))
		&& ((get_project() == 19125) ||(get_project() == 19126) || (get_project() == 19521)))
	{
		chip->ak_msensor_dir = 0;
	}
}

