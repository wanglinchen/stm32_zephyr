/*
 * NEC infrared remote decoder for LF0038 style receivers (PA8).
 *
 * 接收头输出为「解调后的基带信号」，且低电平对应 38kHz 载波发射段：
 *   引导码：9ms 低 + 4.5ms 高
 *   数据位：560us 低 + 560us 高（0）/ 1680us 高（1），LSB 先发
 *   结束码：560us 低
 *
 * 采用双边沿 GPIO 中断 + 高精度 cycle counter 测宽，中断内只做状态机。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT alientek_ir_nec

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/input/ir_nec.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ir_nec, LOG_LEVEL_INF);

#define NEC_LEADER_MARK_MIN  7000U
#define NEC_LEADER_MARK_MAX  10000U
#define NEC_LEADER_SPACE_MIN 3500U
#define NEC_LEADER_SPACE_MAX 5200U
#define NEC_BIT_MARK_MAX     1000U
#define NEC_BIT_ONE_MIN      1100U
#define NEC_UNIT_MAX         2500U

struct ir_nec_config {
	struct gpio_dt_spec pin;
};

struct ir_nec_data {
	struct gpio_callback cb;
	struct k_spinlock lock;
	uint32_t last_cycles;
	uint32_t frame;
	uint8_t bit_index;
	uint8_t state;      /* 0: 等引导码, 1: 等引导码间隔, 2: 收数据位 */
	uint8_t last_level; /* 上一个电平（1=高） */
	struct ir_nec_frame out;
	uint32_t frame_count;
};

static void ir_nec_isr(const struct device *dev, struct gpio_callback *cb,
		       uint32_t pins)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	const struct ir_nec_config *cfg = dev->config;
	struct ir_nec_data *data = dev->data;
	uint32_t now = k_cycle_get_32();
	uint32_t width_us;
	bool level;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	width_us = k_cyc_to_us_floor32(now - data->last_cycles);
	data->last_cycles = now;
	level = gpio_pin_get_raw(cfg->pin.port, cfg->pin.pin);

	/* 刚刚结束的是「上一个电平」，其持续时间 = width_us */
	if (data->last_level == 0U) {
		/* 低电平（载波段）结束 */
		if (data->state == 0U) {
			if (width_us >= NEC_LEADER_MARK_MIN &&
			    width_us <= NEC_LEADER_MARK_MAX) {
				data->state = 1U;
				data->bit_index = 0U;
				data->frame = 0U;
			}
		} else if (data->state == 2U) {
			if (width_us > NEC_BIT_MARK_MAX) {
				data->state = 0U; /* 非法帧 */
			}
		}
	} else {
		/* 高电平（间隔段）结束 */
		if (data->state == 1U) {
			if (width_us >= NEC_LEADER_SPACE_MIN &&
			    width_us <= NEC_LEADER_SPACE_MAX) {
				data->state = 2U;
			} else {
				data->state = 0U;
			}
		} else if (data->state == 2U) {
			if (width_us > NEC_UNIT_MAX) {
				data->state = 0U;
			} else {
				data->frame <<= 1;
				if (width_us >= NEC_BIT_ONE_MIN) {
					data->frame |= 1U;
				}

				data->bit_index++;
				if (data->bit_index >= 32U) {
					/* NEC: 低字节地址 + 高字节命令 */
					data->out.address = (uint8_t)(data->frame & 0xFFU);
					data->out.command =
						(uint8_t)((data->frame >> 16) & 0xFFU);
					data->out.repeat = false;
					data->frame_count++;
					data->state = 0U;
				}
			}
		}
	}

	data->last_level = level ? 1U : 0U;

	k_spin_unlock(&data->lock, key);
}

int ir_nec_get_frame(const struct device *dev, struct ir_nec_frame *frame)
{
	struct ir_nec_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	if (data->frame_count == 0U) {
		k_spin_unlock(&data->lock, key);
		return -EAGAIN;
	}

	*frame = data->out;
	k_spin_unlock(&data->lock, key);

	return 0;
}

static int ir_nec_init(const struct device *dev)
{
	const struct ir_nec_config *cfg = dev->config;
	struct ir_nec_data *data = dev->data;

	if (!gpio_is_ready_dt(&cfg->pin)) {
		LOG_ERR("GPIO not ready");
		return -ENODEV;
	}

	int rc = gpio_pin_configure_dt(&cfg->pin, GPIO_INPUT);

	if (rc != 0) {
		LOG_ERR("GPIO configure failed: %d", rc);
		return rc;
	}

	data->last_level = gpio_pin_get_raw(cfg->pin.port, cfg->pin.pin) ? 1U : 0U;
	data->last_cycles = k_cycle_get_32();

	gpio_init_callback(&data->cb, ir_nec_isr, BIT(cfg->pin.pin));
	if (gpio_add_callback(cfg->pin.port, &data->cb) < 0) {
		return -EIO;
	}

	if (gpio_pin_interrupt_configure_dt(&cfg->pin, GPIO_INT_EDGE_BOTH) < 0) {
		return -EIO;
	}

	LOG_INF("IR receiver armed on %s.%u", cfg->pin.port->name, cfg->pin.pin);

	return 0;
}

#define IR_NEC_INIT(n)                                                         \
	static const struct ir_nec_config ir_nec_config_##n = {                \
		.pin = GPIO_DT_SPEC_INST_GET(n, gpios),                        \
	};                                                                     \
	static struct ir_nec_data ir_nec_data_##n;                             \
	DEVICE_DT_INST_DEFINE(n, ir_nec_init, NULL, &ir_nec_data_##n,          \
			      &ir_nec_config_##n, POST_KERNEL, 90, NULL);

DT_INST_FOREACH_STATUS_OKAY(IR_NEC_INIT)
