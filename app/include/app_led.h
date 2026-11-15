#ifndef APP_LED_H_
#define APP_LED_H_

/**
 * 初始化心跳线程（内部使用 devicetree 中的 led0 别名）。
 *
 * @return 0 成功，负值表示 GPIO 句柄不可用。
 */
int app_led_init(void);

/** 运行时修改心跳周期；0 表示停止翻转。 */
void app_led_set_period(unsigned int period_ms);

#endif /* APP_LED_H_ */
