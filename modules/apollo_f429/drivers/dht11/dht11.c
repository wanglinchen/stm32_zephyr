/*
 * DHT11 temperature / humidity sensor on a single GPIO (PB12, 与 DS18B20 共接口).
 *
 * 时序：主机拉低 >=18ms -> 释放 -> 传感器 80us 低 + 80us 高应答 ->
 * 40 bit（每 bit 50us 低 + 26~28us 高 = 0，70us 高 = 1）。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT alientek_dht11

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(dht11, LOG_LEVEL_INF);

struct dht11_config {
	struct gpio_dt_spec pin;
};

struct dht11_data {
	uint8_t humidity;
	uint8_t temperature;
};

static int dht11_init(const struct device *dev)
{
	const struct dht11_config *cfg = dev->config;

	if (!gpio_is_ready_dt(&cfg->pin)) {
		LOG_ERR("GPIO not ready");
		return -ENODEV;
	}

	int rc = gpio_pin_configure_dt(&cfg->pin, GPIO_INPUT);

	if (rc != 0) {
		LOG_ERR("GPIO configure failed: %d", rc);
		return rc;
	}

	return 0;
}

/* 等待引脚到达指定原始电平，返回等待的微秒数（超时返回负值） */
static int wait_level(const struct gpio_dt_spec *pin, int level, uint32_t timeout_us)
{
	uint32_t waited = 0U;

	while (gpio_pin_get_raw(pin->port, pin->pin) != level) {
		if (waited >= timeout_us) {
			return -ETIMEDOUT;
		}
		k_busy_wait(1);
		waited++;
	}

	return (int)waited;
}

static int dht11_read_frame(const struct device *dev)
{
	const struct dht11_config *cfg = dev->config;
	struct dht11_data *data = dev->data;
	uint8_t bytes[5] = {0};
	unsigned int key;
	int err = 0;

	/* 起始信号：拉低 18ms 以上 */
	gpio_pin_configure_dt(&cfg->pin, GPIO_OUTPUT_LOW);
	k_msleep(20);
	gpio_pin_configure_dt(&cfg->pin, GPIO_INPUT | GPIO_PULL_UP);

	key = irq_lock();

	/* 应答：80us 低 + 80us 高 */
	if (wait_level(&cfg->pin, 0, 200) < 0 ||
	    wait_level(&cfg->pin, 1, 200) < 0 ||
	    wait_level(&cfg->pin, 0, 200) < 0) {
		err = -EIO;
		goto out;
	}

	for (uint8_t bit = 0U; bit < 40U; bit++) {
		uint32_t t0;
		uint32_t high_us;

		/* 50us 低电平结束 */
		if (wait_level(&cfg->pin, 1, 100) < 0) {
			err = -EIO;
			goto out;
		}

		t0 = k_cycle_get_32();

		if (wait_level(&cfg->pin, 0, 100) < 0) {
			err = -EIO;
			goto out;
		}

		high_us = k_cyc_to_us_floor32(k_cycle_get_32() - t0);

		bytes[bit / 8U] <<= 1;
		if (high_us > 40U) {
			bytes[bit / 8U] |= 1U;
		}
	}

out:
	irq_unlock(key);

	if (err < 0) {
		return err;
	}

	uint8_t sum = (uint8_t)(bytes[0] + bytes[1] + bytes[2] + bytes[3]);

	if (sum != bytes[4]) {
		LOG_WRN("checksum mismatch: %02x != %02x", sum, bytes[4]);
		return -EBADMSG;
	}

	data->humidity = bytes[0];
	data->temperature = bytes[2];

	return 0;
}

static int dht11_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_AMBIENT_TEMP &&
	    chan != SENSOR_CHAN_HUMIDITY) {
		return -ENOTSUP;
	}

	return dht11_read_frame(dev);
}

static int dht11_channel_get(const struct device *dev, enum sensor_channel chan,
			     struct sensor_value *val)
{
	const struct dht11_data *data = dev->data;

	switch (chan) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		val->val1 = data->temperature;
		val->val2 = 0;
		return 0;
	case SENSOR_CHAN_HUMIDITY:
		val->val1 = data->humidity;
		val->val2 = 0;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static const struct sensor_driver_api dht11_api = {
	.sample_fetch = dht11_sample_fetch,
	.channel_get = dht11_channel_get,
};

#define DHT11_INIT(n)                                                          \
	static const struct dht11_config dht11_config_##n = {                  \
		.pin = GPIO_DT_SPEC_INST_GET(n, gpios),                        \
	};                                                                     \
	static struct dht11_data dht11_data_##n;                               \
	SENSOR_DEVICE_DT_INST_DEFINE(n, dht11_init, NULL, &dht11_data_##n,     \
				     &dht11_config_##n, POST_KERNEL, 90,       \
				     &dht11_api);

DT_INST_FOREACH_STATUS_OKAY(DHT11_INIT)
