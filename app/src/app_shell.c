#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/version.h>
#include <stdlib.h>

#include "app_led.h"

static int cmd_info(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "board      : %s", CONFIG_BOARD);
	shell_print(sh, "soc        : %s", CONFIG_SOC);
	shell_print(sh, "kernel     : %s", KERNEL_VERSION_STRING);
	shell_print(sh, "uptime     : %u ms", (uint32_t)k_uptime_get());
	shell_print(sh, "sysclk     : %u Hz", (uint32_t)CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC);
	return 0;
}

static int cmd_heartbeat(const struct shell *sh, size_t argc, char **argv)
{
	unsigned int period = (unsigned int)strtoul(argv[1], NULL, 10);

	app_led_set_period(period);
	shell_print(sh, "heartbeat period = %u ms%s", period, period == 0U ? "（已停止）" : "");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(app_sub,
	SHELL_CMD(info, NULL, "打印板级与内核信息", cmd_info),
	SHELL_CMD_ARG(heartbeat, NULL, "设置心跳周期：heartbeat <ms>，0 为停止", cmd_heartbeat, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(app, &app_sub, "应用层命令", NULL);
