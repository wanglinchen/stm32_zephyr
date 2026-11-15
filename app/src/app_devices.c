#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

/*
 * 板载模块自检：逐个检查 devicetree 里已声明、并且驱动确实实例化的设备，
 * 打印 device_is_ready() 结果，方便在现场用串口 shell 确认哪块外设可用。
 */

#define CHECK_NODE(node_id, tag)                                                   \
	do {                                                                        \
		if (DT_NODE_HAS_STATUS(node_id, okay)) {                             \
			const struct device *d = DEVICE_DT_GET(node_id);              \
			shell_print(sh, "%-12s %-18s %s", tag, d->name,              \
				    device_is_ready(d) ? "OK" : "FAIL");             \
		}                                                                   \
	} while (0)

#define CHECK_LABEL(label, tag)                                                    \
	do {                                                                        \
		if (DT_NODE_EXISTS(DT_NODELABEL(label))) {                           \
			CHECK_NODE(DT_NODELABEL(label), tag);                        \
		}                                                                   \
	} while (0)

static int cmd_devices(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "%-12s %-18s %s", "模块", "设备名", "状态");

	/* 片上 */
	CHECK_NODE(DT_NODELABEL(usart1), "uart1");
	CHECK_NODE(DT_NODELABEL(usart2), "uart2");
	CHECK_NODE(DT_NODELABEL(usart3), "uart3");
	CHECK_NODE(DT_NODELABEL(rtc), "rtc");
	CHECK_NODE(DT_NODELABEL(i2c2), "i2c2");
	CHECK_NODE(DT_NODELABEL(spi5), "spi5");

	/* ADC1（PA5 通道） */
	CHECK_NODE(DT_NODELABEL(adc1), "adc1");

	/* 存储 */
	CHECK_LABEL(nand, "nand");
	CHECK_LABEL(w25q256, "w25q256");

	/* I2C 从设备 */
	CHECK_LABEL(pcf8574, "pcf8574");
	CHECK_LABEL(imu, "qmi8658a");
	CHECK_LABEL(light, "ap3216c");
	CHECK_LABEL(codec, "es8388");

	/* 单总线 / 红外 */
	CHECK_LABEL(w1_bus, "dht11");
	CHECK_LABEL(ir, "ir-nec");

	/* 显示 / 触摸 */
	CHECK_NODE(DT_NODELABEL(ltdc), "ltdc");
	CHECK_NODE(DT_CHOSEN(zephyr_touch), "touch");

	return 0;
}

SHELL_CMD_REGISTER(devices, NULL, "检查板载模块设备是否就绪", cmd_devices);
