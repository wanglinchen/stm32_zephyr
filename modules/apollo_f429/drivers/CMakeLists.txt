# SPDX-License-Identifier: Apache-2.0
# 阿波罗 V2 板载模块驱动（out-of-tree module）
#
# 这些驱动只在 devicetree 里存在对应节点时才会实例化
# (DT_INST_FOREACH_STATUS_OKAY 的常规用法)，因此可以无条件编译。
#
# 说明：这里把所有驱动收进一个库，并用 TARGET 存在性做幂等保护。
# 原因是在同一 build 目录做增量 CMake 重配时，模块 CMake 可能被再次执行，
# 多个 zephyr_library() 会重复把库加进链接列表，导致链接期 multiple definition。

if(NOT TARGET apollo_f429_drivers)
  zephyr_library_named(apollo_f429_drivers)

  zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

  zephyr_library_sources(
    mt29f4g08/mt29f4g08.c
    es8388/es8388.c
    ap3216c/ap3216c.c
    dht11/dht11.c
    ir_nec/ir_nec.c
    # Zephyr 的 HAL 构建列表里没有 FMC NAND（只有 SDRAM + LL_FMC），
    # 这里显式补上 HAL NAND 实现，避免链接期 undefined reference。
    ${ZEPHYR_HAL_STM32_MODULE_DIR}/stm32cube/stm32f4xx/drivers/src/stm32f4xx_hal_nand.c
  )
endif()
