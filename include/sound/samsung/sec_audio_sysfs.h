#ifndef _SEC_AUDIO_SYSFS_H
#define _SEC_AUDIO_SYSFS_H

enum amp_id {
	LEFT_AMP,
	RIGHT_AMP,
	BOTTOM_LEFT_AMP,
	BOTTOM_RIGHT_AMP,
	AMP_ID_MAX,
};

struct sec_audio_sysfs_data {
	struct class *audio_class;
	struct device *jack_dev;
	struct device *amp_dev;
	int (*get_jack_state)(void);
	int (*get_key_state)(void);
	int (*set_jack_state)(int);
	int (*get_mic_adc)(void);
	int (*get_water_state)(void);

	/* bigdata */
	int (*get_amp_temperature_max)(enum amp_id);
	int (*get_amp_temperature_keep_max)(enum amp_id);
	int (*get_amp_temperature_overcount)(enum amp_id);
	int (*get_amp_excursion_max)(enum amp_id);
	int (*get_amp_excursion_overcount)(enum amp_id);
};

#ifdef CONFIG_SND_SOC_SAMSUNG_AUDIO
int audio_register_jack_select_cb(int (*set_jack) (int));
int audio_register_jack_state_cb(int (*jack_status) (void));
int audio_register_key_state_cb(int (*key_state) (void));
int audio_register_mic_adc_cb(int (*mic_adc) (void));

/* bigdata */
int audio_register_temperature_max_cb(int (*temperature_max) (enum amp_id));
int audio_register_temperature_keep_max_cb(int (*temperature_keep_max) (enum amp_id));
int audio_register_temperature_overcount_cb(int (*temperature_overcount) (enum amp_id));
int audio_register_excursion_max_cb(int (*excursion_max) (enum amp_id));
int audio_register_excursion_overcount_cb(int (*excursion_overcount) (enum amp_id));
#else
inline int audio_register_jack_select_cb(int (*set_jack) (int))
{
	return -EACCES;
}

inline int audio_register_jack_state_cb(int (*jack_status) (void))
{
	return -EACCES;
}

inline int audio_register_key_state_cb(int (*key_state) (void))
{
	return -EACCES;
}

inline int audio_register_mic_adc_cb(int (*mic_adc) (void))
{
	return -EACCES;
}

inline int audio_register_temperature_max_cb(int (*temperature_max) (enum amp_id))
{
	return -EACCES;
}

inline int audio_register_temperature_keep_max_cb(int (*temperature_keep_max) (enum amp_id))
{
	return -EACCES;
}

inline int audio_register_temperature_overcount_cb(int (*temperature_overcount) (enum amp_id))
{
	return -EACCES;
}

inline int audio_register_excursion_max_cb(int (*excursion_max) (enum amp_id))
{
	return -EACCES;
}

inline int audio_register_excursion_overcount_cb(int (*excursion_overcount) (enum amp_id))
{
	return -EACCES;
}
#endif

#endif /* _SEC_AUDIO_SYSFS_H */
