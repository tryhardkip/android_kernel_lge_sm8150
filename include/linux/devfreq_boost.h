/* SPDX-License-Identifier: GPL-2.0 */
/*
 * devfreq_boost: input-triggered frequency boosting for devfreq devices.
 *
 * Boosts registered devfreq devices (e.g. the Adreno GPU and its bus) to a
 * high floor for a short window when the user touches the screen, then lets
 * the normal governor take back over. This is the devfreq analogue of the
 * CPU input boost in drivers/cpufreq/cpu-boost.c and removes the "GPU idled
 * down, first frames after a touch stutter" latency.
 */
#ifndef _DEVFREQ_BOOST_H_
#define _DEVFREQ_BOOST_H_

#include <linux/devfreq.h>

enum df_device {
	DEVFREQ_GPU,
	DEVFREQ_GPU_BW,
	DEVFREQ_MAX,
};

#ifdef CONFIG_DEVFREQ_BOOST
void devfreq_boost_kick(enum df_device device);
void devfreq_boost_kick_max(enum df_device device, unsigned int duration_ms);
void devfreq_register_boost_device(enum df_device device, struct devfreq *df);
#else
static inline void devfreq_boost_kick(enum df_device device)
{
}
static inline void devfreq_boost_kick_max(enum df_device device,
					  unsigned int duration_ms)
{
}
static inline void devfreq_register_boost_device(enum df_device device,
						 struct devfreq *df)
{
}
#endif /* CONFIG_DEVFREQ_BOOST */

#endif /* _DEVFREQ_BOOST_H_ */
