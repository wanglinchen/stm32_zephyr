/*
 * AP3216C ambient light + proximity sensor (I2C2 on Apollo V2).
 *
 * 寄存器（公开 datasheet）：
 *   0x00 system configuration: [2:0] mode (001 ALS, 010 PS+IR, 011 ALS+PS+IR, 100 reset)
 *   0x0A/0x0B IR data low(2bit)/high
 *   0x0C/0x0D ALS data low/high
 *   0x0E/0x0F PS data low(2bit)/high
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT alientek_ap3216c

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ap3216c, LOG_LEVEL_INF);

#define AP3216C_REG_SYS  0x00
#define AP3216C_REG_IR   0x0A
#define AP3216C_REG_ALS  0x0C
#define AP3216C_REG_PS   0x0E

#define AP3216C_MODE_ALS_PS_IR 0x03

struct ap3216c_config {
	struct i2c_dt_spec i2c;
};

struct ap3216c_data {
	uint16_t als;
	uint16_t ps;
};

static int ap3216c_reg_write(const struct ap3216c_config *cfg, uint8_t reg, uint8_t val)
{
	return i2c_reg_write_byte_dt(&cfg->i2c, reg, val);
}

static int ap3216c_reg_read(const struct ap3216c_config *cfg, uint8_t reg, uint8_t *val)
{
	return i2c_reg_read_byte_dt(&cfg->i2c, reg, val);
}

static int ap3216c_init(const struct device *dev)
{
	const struct ap3216c_config *cfg = dev->config;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	/* soft reset */
	if (ap3216c_reg_write(cfg, AP3216C_REG_SYS, 0x04) < 0) {
		return -EIO;
	}
	k_msleep(10);

	/* ALS + PS + IR 同时工作 */
	if (ap3216c_reg_write(cfg, AP3216C_REG_SYS, AP3216C_MODE_ALS_PS_IR) < 0) {
		return -EIO;
	}
	k_msleep(10);

	LOG_INF("AP3216C ready on %s", cfg->i2c.bus->name);

	return 0;
}

static int ap3216c_sample_fetch(const struct device *dev,
				enum sensor_channel chan)
{
	const struct ap3216c_config *cfg = dev->config;
	struct ap3216c_data *data = dev->data;
	uint8_t buf[2];

	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_AMBIENT_LIGHT &&
	    chan != SENSOR_CHAN_PROX) {
		return -ENOTSUP;
	}

	if (chan == SENSOR_CHAN_ALL || chan == SENSOR_CHAN_AMBIENT_LIGHT) {
		if (ap3216c_reg_read(cfg, AP3216C_REG_ALS, &buf[0]) < 0 ||
		    ap3216c_reg_read(cfg, AP3216C_REG_ALS + 1, &buf[1]) < 0) {
			return -EIO;
		}
		data->als = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
	}

	if (chan == SENSOR_CHAN_ALL || chan == SENSOR_CHAN_PROX) {
		if (ap3216c_reg_read(cfg, AP3216C_REG_PS, &buf[0]) < 0 ||
		    ap3216c_reg_read(cfg, AP3216C_REG_PS + 1, &buf[1]) < 0) {
			return -EIO;
		}
		data->ps = ((uint16_t)buf[0] & 0x03U) | ((uint16_t)buf[1] << 2);
	}

	return 0;
}

static int ap3216c_channel_get(const struct device *dev,
			       enum sensor_channel chan,
			       struct sensor_value *val)
{
	const struct ap3216c_data *data = dev->data;

	switch (chan) {
	case SENSOR_CHAN_AMBIENT_LIGHT:
		val->val1 = data->als;
		val->val2 = 0;
		return 0;
	case SENSOR_CHAN_PROX:
		val->val1 = data->ps;
		val->val2 = 0;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static const struct sensor_driver_api ap3216c_api = {
	.sample_fetch = ap3216c_sample_fetch,
	.channel_get = ap3216c_channel_get,
};

#define AP3216C_INIT(n)                                                        \
	static const struct ap3216c_config ap3216c_config_##n = {              \
		.i2c = I2C_DT_SPEC_INST_GET(n),                                \
	};                                                                     \
	static struct ap3216c_data ap3216c_data_##n;                           \
	SENSOR_DEVICE_DT_INST_DEFINE(n, ap3216c_init, NULL,                    \
				     &ap3216c_data_##n, &ap3216c_config_##n,   \
				     POST_KERNEL, 90,                          \
				     &ap3216c_api);

DT_INST_FOREACH_STATUS_OKAY(AP3216C_INIT)
