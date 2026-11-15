# SPDX-License-Identifier: Apache-2.0

board_set_flasher_ifnset(pyocd)
board_runner_args(pyocd "--target=stm32f429xi")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
