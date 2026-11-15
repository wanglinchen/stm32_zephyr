#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "app_led.h"

LOG_MODULE_REGISTER(app_led, LOG_LEVEL_INF);

/* 硬件描述来自 devicetree，不在这里硬编码端口和引脚。
 * app-heartbeat-led 别名由板级 overlay 提供，换板只改 overlay。
 */
#define LED_NODE DT_ALIAS(app_heartbeat_led)

#if !DT_NODE_EXISTS(LED_NODE)
#error "devicetree 中缺少 app-heartbeat-led 别名，请在 board overlay 中补充"
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

K_THREAD_STACK_DEFINE(led_stack, 1024);
static struct k_thread led_thread_data;

static volatile unsigned int heartbeat_ms = 500;

static void led_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		unsigned int period = heartbeat_ms;

		if (period == 0U) {
			gpio_pin_set_dt(&led, 0);
			k_sleep(K_MSEC(100));
			continue;
		}

		gpio_pin_toggle_dt(&led);
		k_sleep(K_MSEC(period));
	}
}

int app_led_init(void)
{
	if (!gpio_is_ready_dt(&led)) {
		return -ENODEV;
	}

	int rc = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	if (rc != 0) {
		return rc;
	}

	k_thread_create(&led_thread_data, led_stack, K_THREAD_STACK_SIZEOF(led_stack),
			led_entry, NULL, NULL, NULL, K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
	k_thread_name_set(&led_thread_data, "led_heartbeat");

	LOG_INF("心跳线程已启动，周期 %ums", heartbeat_ms);
	return 0;
}

void app_led_set_period(unsigned int period_ms)
{
	heartbeat_ms = period_ms;
}
