/*
 * Everest ES8388 audio codec (I2C control port).
 *
 * 板载接法：ES8388 挂在 I2C2（PH4/PH5），地址 0x10；音频数据线走 SAI1：
 *   PE2 MCLK / PE3 SDOUT / PE4 LRCK / PE5 SCLK / PE6 SDIN。
 *
 * 说明：ES8388 的完整初始化表取决于具体模块（MIC 增益、输出方式），
 * 这里的初始化只做「复位 -> 从模式 I2S 16bit -> DAC/ADC 上电 -> 默认音量」，
 * 更细的模拟通路调整请通过 es8388_reg_write() 逐寄存器完成。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT everest_es8388

#include <zephyr/device.h>
#include <zephyr/drivers/audio/es8388.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(es8388, LOG_LEVEL_INF);

#define ES8388_REG_CONTROL1   0x00
#define ES8388_REG_CONTROL2   0x01
#define ES8388_REG_CHIPPOWER  0x02
#define ES8388_REG_ADCPOWER   0x03
#define ES8388_REG_DACPOWER   0x04
#define ES8388_REG_MASTERMODE 0x08
#define ES8388_REG_DACFORMAT  0x17
#define ES8388_REG_DACCTRL1   0x10  /* DAC L volume */
#define ES8388_REG_DACCTRL2   0x11  /* DAC R volume */
#define ES8388_REG_DACCTRL17  0x27  /* DAC output enable */

struct es8388_config {
	struct i2c_dt_spec i2c;
};

struct es8388_data {
	uint8_t dac_volume_l;
	uint8_t dac_volume_r;
	bool muted;
};

int es8388_reg_write(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct es8388_config *cfg = dev->config;

	return i2c_reg_write_byte_dt(&cfg->i2c, reg, val);
}

int es8388_reg_read(const struct device *dev, uint8_t reg, uint8_t *val)
{
	const struct es8388_config *cfg = dev->config;

	return i2c_reg_read_byte_dt(&cfg->i2c, reg, val);
}

int es8388_set_dac_volume(const struct device *dev, uint8_t left, uint8_t right)
{
	struct es8388_data *data = dev->data;
	int err;

	err = es8388_reg_write(dev, ES8388_REG_DACCTRL1, left);
	if (err < 0) {
		return err;
	}

	err = es8388_reg_write(dev, ES8388_REG_DACCTRL2, right);
	if (err < 0) {
		return err;
	}

	data->dac_volume_l = left;
	data->dac_volume_r = right;

	return 0;
}

int es8388_mute(const struct device *dev, bool mute)
{
	struct es8388_data *data = dev->data;
	int err;

	/* DACCTRL17: bit7 = DAC L mute, bit6 = DAC R mute (与 ALIENTEK 例程一致) */
	err = es8388_reg_write(dev, ES8388_REG_DACCTRL17,
			       mute ? 0xC0 : 0x00);
	if (err < 0) {
		return err;
	}

	data->muted = mute;

	return 0;
}

int es8388_configure(const struct device *dev)
{
	struct es8388_data *data = dev->data;
	static const struct {
		uint8_t reg;
		uint8_t val;
	} seq[] = {
		{ES8388_REG_CONTROL1, 0x80},   /* soft reset, 复位后自动清零 */
		{ES8388_REG_CONTROL1, 0x00},
		{ES8388_REG_CONTROL2, 0x50},   /* 从模式，DAC/ADC 使能 */
		{ES8388_REG_CHIPPOWER, 0x00},  /* 全部上电 */
		{ES8388_REG_ADCPOWER, 0x00},   /* ADC 上电 */
		{ES8388_REG_DACPOWER, 0x3C},   /* DAC L/R + 输出使能 */
		{ES8388_REG_MASTERMODE, 0x00}, /* 从模式（SAI 提供 BCLK/LRCK） */
		{ES8388_REG_DACFORMAT, 0x18},  /* I2S, 16bit */
		{ES8388_REG_DACCTRL17, 0x00},  /* 取消静音 */
	};

	for (size_t i = 0U; i < ARRAY_SIZE(seq); i++) {
		int err = es8388_reg_write(dev, seq[i].reg, seq[i].val);

		if (err < 0) {
			LOG_ERR("write reg 0x%02x failed (%d)", seq[i].reg, err);
			return err;
		}

		if (seq[i].reg == ES8388_REG_CONTROL1 && seq[i].val == 0x80) {
			k_msleep(5);
		}
	}

	data->muted = false;
	data->dac_volume_l = 0x08;
	data->dac_volume_r = 0x08;

	return es8388_set_dac_volume(dev, 0x08, 0x08);
}

static int es8388_init(const struct device *dev)
{
	const struct es8388_config *cfg = dev->config;
	uint8_t id = 0U;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	if (es8388_reg_read(dev, ES8388_REG_CONTROL2, &id) < 0) {
		LOG_ERR("codec not responding at 0x%02x", cfg->i2c.addr);
		return -EIO;
	}

	LOG_INF("ES8388 detected (reg01=0x%02x)", id);

	return es8388_configure(dev);
}

#define ES8388_INIT(n)                                                         \
	static const struct es8388_config es8388_config_##n = {                \
		.i2c = I2C_DT_SPEC_INST_GET(n),                                \
	};                                                                     \
	static struct es8388_data es8388_data_##n;                             \
	DEVICE_DT_INST_DEFINE(n, es8388_init, NULL, &es8388_data_##n,          \
			      &es8388_config_##n, POST_KERNEL, 50, NULL);

DT_INST_FOREACH_STATUS_OKAY(ES8388_INIT)
