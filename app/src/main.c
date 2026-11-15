#include <zephyr/kernel.h>
#include <zephyr/version.h>
#include <zephyr/logging/log.h>

#include "app_led.h"

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

/*
 * 应用启动顺序（全部在 main 线程上下文，初始化级别 POST_KERNEL 之后）：
 *   1. 业务模块自检（驱动句柄是否 ready）
 *   2. 拉起各业务线程
 *   3. 主线程保持存活，业务负载交给专门线程
 */
int main(void)
{
	LOG_INF("board=%s soc=%s kernel=%s", CONFIG_BOARD, CONFIG_SOC, KERNEL_VERSION_STRING);

	int rc = app_led_init();
	if (rc != 0) {
		LOG_ERR("LED 模块初始化失败: %d", rc);
		return rc;
	}

	LOG_INF("应用初始化完成");

	while (true) {
		k_sleep(K_SECONDS(5));
	}
}
