// SPDX-License-Identifier: GPL-2.0
/*
 * devfreq_boost: input-triggered frequency boosting for devfreq devices.
 *
 * On a touch (input) event, boost registered devfreq devices to a high
 * frequency floor for a short duration, then release back to the governor.
 * This complements the CPU input boost (drivers/cpufreq/cpu-boost.c) for the
 * GPU and its bus, cutting the latency of ramping the GPU back up after it
 * has idled down between frames.
 *
 * The boost is applied by raising the devfreq device's min_freq and calling
 * update_devfreq() under the device lock; the governor's own decisions are
 * unaffected except for the temporary floor. min_freq is restored on unboost.
 */
#define pr_fmt(fmt) "devfreq_boost: " fmt

#include <linux/devfreq_boost.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

/*
 * Input boost duration in ms. Each new input event re-arms the timer, so the
 * floor is held for this long after the last touch.
 */
static unsigned int input_boost_duration __read_mostly =
	CONFIG_DEVFREQ_INPUT_BOOST_DURATION_MS;
module_param(input_boost_duration, uint, 0644);

/*
 * Per-device input-boost frequency floor in Hz. A value of 0 means "boost to
 * the device's max_freq".
 */
static unsigned long gpu_boost_freq __read_mostly =
	CONFIG_DEVFREQ_GPU_INPUT_BOOST_FREQ;
module_param(gpu_boost_freq, ulong, 0644);

static unsigned long gpu_bw_boost_freq __read_mostly =
	CONFIG_DEVFREQ_GPU_BW_INPUT_BOOST_FREQ;
module_param(gpu_bw_boost_freq, ulong, 0644);

struct boost_dev {
	struct devfreq *df;
	struct work_struct boost_work;
	struct delayed_work unboost_work;
	unsigned long boost_freq;
	unsigned long abs_min_freq;
};

static struct boost_dev boost_devices[DEVFREQ_MAX];

static void __devfreq_set_min(struct boost_dev *b, unsigned long freq)
{
	struct devfreq *df = b->df;

	if (!df)
		return;

	mutex_lock(&df->lock);
	df->min_freq = freq;
	/* Keep the floor below the ceiling. */
	if (df->max_freq && df->min_freq > df->max_freq)
		df->min_freq = df->max_freq;
	update_devfreq(df);
	mutex_unlock(&df->lock);
}

static void devfreq_boost_worker(struct work_struct *work)
{
	struct boost_dev *b = container_of(work, typeof(*b), boost_work);
	unsigned long freq = b->boost_freq;

	if (!b->df)
		return;

	/* 0 means "go to max". */
	if (!freq)
		freq = b->df->max_freq;

	__devfreq_set_min(b, freq);

	mod_delayed_work(system_unbound_wq, &b->unboost_work,
			 msecs_to_jiffies(input_boost_duration));
}

static void devfreq_unboost_worker(struct work_struct *work)
{
	struct boost_dev *b =
		container_of(to_delayed_work(work), typeof(*b), unboost_work);

	__devfreq_set_min(b, b->abs_min_freq);
}

void devfreq_boost_kick(enum df_device device)
{
	struct boost_dev *b;

	if (device >= DEVFREQ_MAX)
		return;

	b = &boost_devices[device];
	if (!READ_ONCE(b->df) || !input_boost_duration)
		return;

	queue_work(system_unbound_wq, &b->boost_work);
}

void devfreq_boost_kick_max(enum df_device device, unsigned int duration_ms)
{
	struct boost_dev *b;

	if (device >= DEVFREQ_MAX)
		return;

	b = &boost_devices[device];
	if (!READ_ONCE(b->df))
		return;

	/* Force to max regardless of the configured input-boost floor. */
	__devfreq_set_min(b, b->df->max_freq);
	mod_delayed_work(system_unbound_wq, &b->unboost_work,
			 msecs_to_jiffies(duration_ms));
}

void devfreq_register_boost_device(enum df_device device, struct devfreq *df)
{
	struct boost_dev *b;

	if (device >= DEVFREQ_MAX || !df)
		return;

	b = &boost_devices[device];
	b->abs_min_freq = df->min_freq;
	switch (device) {
	case DEVFREQ_GPU:
		b->boost_freq = gpu_boost_freq;
		break;
	case DEVFREQ_GPU_BW:
		b->boost_freq = gpu_bw_boost_freq;
		break;
	default:
		b->boost_freq = 0;
		break;
	}
	/* Publish last so a racing kick sees a fully-initialised device. */
	WRITE_ONCE(b->df, df);

	pr_info("registered devfreq boost device %d (min=%lu boost=%lu)\n",
		device, b->abs_min_freq, b->boost_freq);
}

static void devfreq_boost_input_event(struct input_handle *handle,
				      unsigned int type, unsigned int code,
				      int value)
{
	devfreq_boost_kick(DEVFREQ_GPU);
	devfreq_boost_kick(DEVFREQ_GPU_BW);
}

static int devfreq_boost_input_connect(struct input_handler *handler,
				       struct input_dev *dev,
				       const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "devfreq_boost";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void devfreq_boost_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id devfreq_boost_ids[] = {
	/* multi-touch touchscreen */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			 INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			BIT_MASK(ABS_MT_POSITION_X) |
			BIT_MASK(ABS_MT_POSITION_Y) },
	},
	/* touchpad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			 INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) },
	},
	/* Keypad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{ },
};

static struct input_handler devfreq_boost_input_handler = {
	.event		= devfreq_boost_input_event,
	.connect	= devfreq_boost_input_connect,
	.disconnect	= devfreq_boost_input_disconnect,
	.name		= "devfreq_boost",
	.id_table	= devfreq_boost_ids,
};

static int __init devfreq_boost_init(void)
{
	int i, ret;

	for (i = 0; i < DEVFREQ_MAX; i++) {
		INIT_WORK(&boost_devices[i].boost_work, devfreq_boost_worker);
		INIT_DELAYED_WORK(&boost_devices[i].unboost_work,
				  devfreq_unboost_worker);
	}

	ret = input_register_handler(&devfreq_boost_input_handler);
	if (ret)
		pr_err("failed to register input handler: %d\n", ret);

	return 0;
}
late_initcall(devfreq_boost_init);
