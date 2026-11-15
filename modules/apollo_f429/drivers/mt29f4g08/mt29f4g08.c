/*
 * Micron MT29F4G08 (4 Gbit, 8-bit) NAND on STM32F429 FMC bank2.
 *
 * 板载接法（阿波罗 V2 底板）：
 *   NCE2  = PG9   (FMC NAND bank2, 0x70000000)
 *   CLE   = PD11  (FMC_A16)
 *   ALE   = PD12  (FMC_A17)
 *   NWAIT = PD6
 *   D0..D7= PD14/PD15/PD0/PD1/PE7..PE10
 *
 * 上游 Zephyr 没有 FMC NAND 控制器驱动，这里直接用 STM32 HAL 的
 * FMC NAND（HAL_NAND_*）实现，并注册成 Zephyr flash 设备。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT alientek_mt29f4g08

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stm32f4xx_hal.h>

LOG_MODULE_REGISTER(mt29f4g08, LOG_LEVEL_INF);

#define NAND_PAGE_SIZE   2048U
#define NAND_PAGES_PER_BLOCK 64U
#define NAND_BLOCK_SIZE  (NAND_PAGE_SIZE * NAND_PAGES_PER_BLOCK) /* 128 KiB */

struct mt29f4g08_config {
	uint32_t blocks;
	uint32_t page_size;
	uint32_t block_size;
};

static NAND_HandleTypeDef hnand;

static void nand_to_addr(uint32_t offset, NAND_AddressTypeDef *addr)
{
	addr->Plane = 0U;
	addr->Block = (uint16_t)(offset / NAND_BLOCK_SIZE);
	addr->Page = (uint16_t)((offset % NAND_BLOCK_SIZE) / NAND_PAGE_SIZE);
}

static int mt29f4g08_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	hnand.Instance = FMC_Bank2_3;
	hnand.Init.NandBank = FMC_NAND_BANK2;
	/* 片上 ECC 关掉：MT29F4G08 自带 4-bit/512B ECC，Zephyr flash API 不暴露 ECC 状态 */
	hnand.Init.EccComputation = FMC_NAND_ECC_DISABLE;
	hnand.Init.ECCPageSize = FMC_NAND_ECC_PAGE_SIZE_512BYTE;
	hnand.Init.MemoryDataWidth = FMC_NAND_PCC_MEM_BUS_WIDTH_8;

	FMC_NAND_PCC_TimingTypeDef timing = {
		.SetupTime = 2U,
		.WaitSetupTime = 3U,
		.HoldSetupTime = 2U,
		.HiZSetupTime = 2U,
};

	__HAL_RCC_FMC_CLK_ENABLE();

	if (HAL_NAND_Init(&hnand, &timing, &timing) != HAL_OK) {
		LOG_ERR("HAL_NAND_Init failed");
		return -EIO;
	}

	if (HAL_NAND_Reset(&hnand) != HAL_OK) {
		LOG_ERR("NAND reset failed");
		return -EIO;
	}

	NAND_IDTypeDef id;

	if (HAL_NAND_Read_ID(&hnand, &id) != HAL_OK) {
		LOG_ERR("NAND read id failed");
		return -EIO;
	}

	LOG_INF("NAND id: maker=0x%02x device=0x%02x", id.Maker_Id, id.Device_Id);

	return 0;
}

static int mt29f4g08_read(const struct device *dev, off_t offset, void *buf, size_t len)
{
	const struct mt29f4g08_config *cfg = dev->config;
	uint8_t page_buf[NAND_PAGE_SIZE];
	NAND_AddressTypeDef addr;

	if (offset < 0 || (size_t)offset + len > cfg->blocks * cfg->block_size) {
		return -EINVAL;
	}

	while (len > 0U) {
		uint32_t page_off = (uint32_t)offset % NAND_PAGE_SIZE;
		size_t chunk = MIN(len, NAND_PAGE_SIZE - page_off);

		nand_to_addr((uint32_t)offset, &addr);
		if (HAL_NAND_Read_Page_8b(&hnand, &addr, page_buf, 1U) != HAL_OK) {
			return -EIO;
		}

		memcpy(buf, page_buf + page_off, chunk);
		buf = (uint8_t *)buf + chunk;
		offset += chunk;
		len -= chunk;
	}

	return 0;
}

static int mt29f4g08_write(const struct device *dev, off_t offset, const void *buf, size_t len)
{
	const struct mt29f4g08_config *cfg = dev->config;
	uint8_t page_buf[NAND_PAGE_SIZE];
	NAND_AddressTypeDef addr;

	if (offset < 0 || (size_t)offset + len > cfg->blocks * cfg->block_size) {
		return -EINVAL;
	}

	while (len > 0U) {
		uint32_t page_off = (uint32_t)offset % NAND_PAGE_SIZE;
		size_t chunk = MIN(len, NAND_PAGE_SIZE - page_off);

		nand_to_addr((uint32_t)offset, &addr);

		if (chunk != NAND_PAGE_SIZE) {
			/* 部分页写：NAND 只能整页编程，先读回同页再合并 */
			if (HAL_NAND_Read_Page_8b(&hnand, &addr, page_buf, 1U) != HAL_OK) {
				return -EIO;
			}
		}

		memcpy(page_buf + page_off, buf, chunk);

		if (HAL_NAND_Write_Page_8b(&hnand, &addr, page_buf, 1U) != HAL_OK) {
			return -EIO;
		}

		buf = (const uint8_t *)buf + chunk;
		offset += chunk;
		len -= chunk;
	}

	return 0;
}

static int mt29f4g08_erase(const struct device *dev, off_t offset, size_t len)
{
	const struct mt29f4g08_config *cfg = dev->config;
	NAND_AddressTypeDef addr;

	if (offset < 0 || len == 0U || ((size_t)offset % cfg->block_size) != 0U ||
	    (len % cfg->block_size) != 0U ||
	    (size_t)offset + len > cfg->blocks * cfg->block_size) {
		return -EINVAL;
	}

	for (size_t done = 0U; done < len; done += cfg->block_size) {
		nand_to_addr((uint32_t)offset + done, &addr);
		if (HAL_NAND_Erase_Block(&hnand, &addr) != HAL_OK) {
			return -EIO;
		}
	}

	return 0;
}

static const struct flash_parameters mt29f4g08_params = {
	.write_block_size = 1U,
	.erase_value = 0xFF,
};

static const struct flash_parameters *mt29f4g08_get_parameters(const struct device *dev)
{
	ARG_UNUSED(dev);

	return &mt29f4g08_params;
}

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
static const struct flash_pages_layout mt29f4g08_pages_layout = {
	.pages_count = 4096U,
	.pages_size = NAND_BLOCK_SIZE,
};

static void mt29f4g08_get_layout(const struct device *dev,
				 const struct flash_pages_layout **layout,
				 size_t *layout_size)
{
	ARG_UNUSED(dev);

	*layout = &mt29f4g08_pages_layout;
	*layout_size = 1U;
}
#endif

static const struct flash_driver_api mt29f4g08_api = {
	.read = mt29f4g08_read,
	.write = mt29f4g08_write,
	.erase = mt29f4g08_erase,
	.get_parameters = mt29f4g08_get_parameters,
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	.page_layout = mt29f4g08_get_layout,
#endif
};

#define MT29F4G08_INIT(n)                                                     \
	static const struct mt29f4g08_config mt29f4g08_config_##n = {        \
		.blocks = 4096U,                                             \
		.page_size = NAND_PAGE_SIZE,                                 \
		.block_size = NAND_BLOCK_SIZE,                               \
	};                                                                   \
	DEVICE_DT_INST_DEFINE(n, mt29f4g08_init, NULL, NULL,                 \
			      &mt29f4g08_config_##n, POST_KERNEL,              \
			      50, &mt29f4g08_api);

DT_INST_FOREACH_STATUS_OKAY(MT29F4G08_INIT)
