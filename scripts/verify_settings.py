# SPDX-License-Identifier: Apache-2.0
"""
上板核对：把固件跑起来后，抓取内核与外设寄存器的真实值，
用于和 boards/apollo_f429/apollo_f429.dts、app/boards/apollo_f429.conf 里的
"功能设置"逐项对比。

用法：
    python scripts\\verify_settings.py
    python scripts\\verify_settings.py --no-reset   # 不复位，直接抓当前运行态

注意：脚本结束时会把目标恢复为运行态（resume），不会把板子留在 halt。
"""

import argparse
import os
import sys
import time

from pyocd.core.helpers import ConnectHelper

UID = "51FF6C067266535340421167"
TARGET = "stm32f429xi"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build", "apollo_f429", "zephyr", "zephyr.bin")

# ---- 地址表 ----
RCC = 0x40023800
FLASH_ACR = 0x40023C00
GPIO_BASE = {
    "A": 0x40020000, "B": 0x40020400, "C": 0x40020800, "D": 0x40020C00,
    "E": 0x40021000, "F": 0x40021400, "G": 0x40021800, "H": 0x40021C00,
    "I": 0x40022000,
}
USART1 = 0x40011000
USART2 = 0x40004400
USART3 = 0x40004800
I2C2 = 0x40005800
SPI5 = 0x40015000
FMC = 0xA0000000
LTDC = 0x40016800
ADC1 = 0x40012000
ADC_CCR = 0x40012304
DAC = 0x40007400
PWR = 0x40007000
SCB_VTOR = 0xE000ED08
SYSTICK = 0xE000E010

MODE_NAMES = {0: "IN", 1: "OUT", 2: "AF", 3: "ANALOG"}
PULL_NAMES = {0: "none", 1: "PU", 2: "PD", 3: "reserved"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-reset", action="store_true")
    args = ap.parse_args()

    session = ConnectHelper.session_with_chosen_probe(unique_id=UID, target_override=TARGET)
    if session is None:
        print("ERROR|probe_not_found")
        return 2
    session.open()
    t = session.target

    def rd32(addr):
        d = t.read_memory(addr, 4)
        if isinstance(d, (bytes, bytearray)):
            return int.from_bytes(bytes(d[:4]), "little")
        return int(d[0]) | (int(d[1]) << 8) | (int(d[2]) << 16) | (int(d[3]) << 24)

    def rd16(addr):
        d = t.read_memory(addr, 2)
        if isinstance(d, (bytes, bytearray)):
            return int.from_bytes(bytes(d[:2]), "little")
        return int(d[0]) | (int(d[1]) << 8)

    out = []
    def emit(k, v, note=""):
        out.append(f"{k}|{v}|{note}")
        print(f"{k}|{v}|{note}")

    try:
        # ---- 1. 复位并让固件跑起来 ----
        if not args.no_reset:
            t.reset_and_halt()
            time.sleep(0.15)
            t.resume()
            time.sleep(2.0)

        # ---- 2. 活体判定：采样 PC / SysTick VAL ----
        samples = []
        for _ in range(3):
            t.halt()
            pc = t.read_core_register("pc")
            sp = t.read_core_register("sp")
            val = rd32(SYSTICK + 0x08)
            samples.append((pc, sp, val))
            t.resume()
            time.sleep(0.25)
        emit("CORE.pc_samples", "; ".join(f"0x{s[0]:08X}" for s in samples), "取 3 次采样")
        emit("CORE.sp", f"0x{samples[0][1]:08X}", "")
        emit("SYSTICK.val_samples", "; ".join(str(s[2]) for s in samples), "递减说明 tick 在跑")

        # 抓完保持停止，后面读寄存器
        # （已在最后一次 resume 后处于运行态，这里再 halt 一次读取）
        t.halt()
        time.sleep(0.05)

        emit("CORE.xpsr", f"0x{t.read_core_register('xpsr'):08X}", "")
        emit("SCB.VTOR", f"0x{rd32(SCB_VTOR):08X}", "复位向量表基址")

        # ---- 3. 时钟树 ----
        cr = rd32(RCC + 0x00)
        pllcfgr = rd32(RCC + 0x04)
        cfgr = rd32(RCC + 0x08)
        pllsaicfgr = rd32(RCC + 0x88)
        emit("RCC.CR", f"0x{cr:08X}",
             f"HSEON={(cr >> 16) & 1} HSERDY={(cr >> 17) & 1} "
             f"PLLON={(cr >> 24) & 1} PLLRDY={(cr >> 25) & 1} "
             f"CSSON={(cr >> 19) & 1}")
        pllm = pllcfgr & 0x3F
        plln = (pllcfgr >> 6) & 0x1FF
        pllp = ((pllcfgr >> 16) & 0x3) * 2 + 2
        pllsrc = (pllcfgr >> 22) & 1
        pllq = (pllcfgr >> 24) & 0xF
        emit("RCC.PLLCFGR", f"0x{pllcfgr:08X}",
             f"M={pllm} N={plln} P={pllp} Q={pllq} src={'HSE' if pllsrc else 'HSI'}")
        sw = cfgr & 0x3
        sws = (cfgr >> 2) & 0x3
        hpre = (cfgr >> 4) & 0xF
        ppre1 = (cfgr >> 10) & 0x7
        ppre2 = (cfgr >> 13) & 0x7
        hpre_div = 1 if hpre == 0 else (2 << (hpre - 1)) if hpre < 8 else 0
        apb_div = {0: 1, 1: 1, 2: 1, 3: 1, 4: 2, 5: 4, 6: 8, 7: 16}
        emit("RCC.CFGR", f"0x{cfgr:08X}",
             f"SW={sw}({'PLL' if sw == 2 else 'HSI/HSE'}) SWS={sws} HPRE=/{hpre_div} "
             f"PPRE1=/{apb_div.get(ppre1, 0)} PPRE2=/{apb_div.get(ppre2, 0)}")
        vco = (25_000_000 / pllm) * plln if pllm else 0
        sysclk = vco / pllp if pllp else 0
        emit("DERIVED.sysclk_hz", f"{sysclk:.0f}",
             f"HCLK={sysclk / hpre_div:.0f} APB1={sysclk / hpre_div / apb_div.get(ppre1, 1):.0f} "
             f"APB2={sysclk / hpre_div / apb_div.get(ppre2, 1):.0f}")
        saim = pllsaicfgr & 0x3F
        sain = (pllsaicfgr >> 6) & 0x1FF
        sair = (pllsaicfgr >> 16) & 0x7
        saiq = (pllsaicfgr >> 24) & 0xF
        emit("RCC.PLLSAICFGR", f"0x{pllsaicfgr:08X}",
             f"M={saim} N={sain} R={sair} Q={saiq} R_out={(25_000_000 / saim) * sain / sair:.1f}Hz"
             if saim and sair else "未配置")
        emit("FLASH.ACR", f"0x{rd32(FLASH_ACR):08X}", f"LATENCY={(rd32(FLASH_ACR) & 0xF)} WS")

        # ---- 4. 外设时钟使能 ----
        emit("RCC.AHB1ENR", f"0x{rd32(RCC + 0x30):08X}",
             f"GPIOA={(rd32(RCC + 0x30) >> 0) & 1} GPIOB={(rd32(RCC + 0x30) >> 1) & 1} "
             f"GPIOC={(rd32(RCC + 0x30) >> 2) & 1} GPIOD={(rd32(RCC + 0x30) >> 3) & 1} "
             f"GPIOE={(rd32(RCC + 0x30) >> 4) & 1} GPIOF={(rd32(RCC + 0x30) >> 5) & 1} "
             f"GPIOG={(rd32(RCC + 0x30) >> 6) & 1} GPIOH={(rd32(RCC + 0x30) >> 7) & 1} "
             f"GPIOI={(rd32(RCC + 0x30) >> 8) & 1} "
             f"FMC={(rd32(RCC + 0x30) >> 23) & 1} "
             f"DMA1={(rd32(RCC + 0x30) >> 21) & 1} "
             f"CCMDATA={(rd32(RCC + 0x30) >> 22) & 1}")
        ahb3 = rd32(RCC + 0x38)
        emit("RCC.AHB3ENR", f"0x{ahb3:08X}", f"FMC={(ahb3 >> 0) & 1}")
        apb1 = rd32(RCC + 0x40)
        emit("RCC.APB1ENR", f"0x{apb1:08X}",
             f"TIM2={(apb1 >> 0) & 1} TIM3={(apb1 >> 1) & 1} TIM4={(apb1 >> 2) & 1} "
             f"TIM5={(apb1 >> 3) & 1} TIM6={(apb1 >> 4) & 1} TIM7={(apb1 >> 5) & 1} "
             f"TIM12={(apb1 >> 6) & 1} TIM13={(apb1 >> 7) & 1} TIM14={(apb1 >> 8) & 1} "
             f"WWDG={(apb1 >> 11) & 1} SPI2={(apb1 >> 14) & 1} SPI3={(apb1 >> 15) & 1} "
             f"USART2={(apb1 >> 17) & 1} USART3={(apb1 >> 18) & 1} "
             f"I2C1={(apb1 >> 21) & 1} I2C2={(apb1 >> 22) & 1} I2C3={(apb1 >> 23) & 1} "
             f"CAN1={(apb1 >> 25) & 1} PWR={(apb1 >> 28) & 1} DAC={(apb1 >> 29) & 1}")
        apb2 = rd32(RCC + 0x44)
        emit("RCC.APB2ENR", f"0x{apb2:08X}",
             f"TIM1={(apb2 >> 0) & 1} TIM8={(apb2 >> 1) & 1} USART1={(apb2 >> 4) & 1} "
             f"USART6={(apb2 >> 5) & 1} ADC1={(apb2 >> 8) & 1} "
             f"SDIO={(apb2 >> 11) & 1} SPI1={(apb2 >> 12) & 1} SPI4={(apb2 >> 13) & 1} "
             f"SYSCFG={(apb2 >> 14) & 1} TIM9={(apb2 >> 16) & 1} TIM10={(apb2 >> 17) & 1} "
             f"TIM11={(apb2 >> 18) & 1} SPI5={(apb2 >> 20) & 1} LTDC={(apb2 >> 26) & 1}")
        bdcr = rd32(RCC + 0x70)
        emit("RCC.BDCR", f"0x{bdcr:08X}",
             f"LSEON={(bdcr >> 0) & 1} LSERDY={(bdcr >> 1) & 1} RTCSEL={(bdcr >> 8) & 0x3} "
             f"RTCEN={(bdcr >> 15) & 1}")
        emit("PWR.CR", f"0x{rd32(PWR + 0x00):08X}", f"DBP={(rd32(PWR) >> 8) & 1}")

        # ---- 5. 引脚模式 ----
        def pin(port, n):
            base = GPIO_BASE[port]
            moder = (rd32(base + 0x00) >> (n * 2)) & 0x3
            pupdr = (rd32(base + 0x0C) >> (n * 2)) & 0x3
            afr = (rd32(base + 0x20 + (0x04 if n >= 8 else 0)) >> ((n % 8) * 4)) & 0xF
            return (f"{MODE_NAMES[moder]}"
                    f"{'/' + PULL_NAMES[pupdr] if pupdr else ''}"
                    f"{f'/AF{af}' if moder == 2 else ''}")

        checks = [
            ("LED.PB0_DS0", "B", 0), ("LED.PB1_DS1", "B", 1), ("LCD.PB5_BL", "B", 5),
            ("UART1.PA9_TX", "A", 9), ("UART1.PA10_RX", "A", 10),
            ("UART2.PA2_TX", "A", 2), ("UART2.PA3_RX", "A", 3),
            ("UART3.PB10_TX", "B", 10), ("UART3.PB11_RX", "B", 11),
            ("I2C2.PH4_SCL", "H", 4), ("I2C2.PH5_SDA", "H", 5),
            ("SPI5.PF7_SCK", "F", 7), ("SPI5.PF8_MISO", "F", 8), ("SPI5.PF9_MOSI", "F", 9),
            ("SPI5.PF6_CS", "F", 6),
            ("KEY.PH3", "H", 3), ("KEY.PH2", "H", 2), ("KEY.PC13", "C", 13), ("KEY.PA0", "A", 0),
            ("DHT11.PB12", "B", 12), ("IR.PA8", "A", 8),
            ("TOUCH.PH6_SCK", "H", 6), ("TOUCH.PG3_MISO", "G", 3),
            ("TOUCH.PI3_MOSI", "I", 3), ("TOUCH.PI8_CS", "I", 8), ("TOUCH.PH7_PEN", "H", 7),
            ("ADC.PA5", "A", 5), ("DAC.PA4", "A", 4),
            ("USB.PA11_DM", "A", 11), ("USB.PA12_DP", "A", 12),
            ("LTDC.PG7_CLK", "G", 7), ("LTDC.PI9_VSYNC", "I", 9), ("LTDC.PI10_HSYNC", "I", 10),
            ("FMC.PC2_SDNE0", "C", 2), ("FMC.PG8_SDCLK", "G", 8), ("FMC.PD7_NE1", "D", 7),
            ("FMC.PG9_NCE2", "G", 9),
        ]
        for name, port, n in checks:
            emit(f"PIN.{name}", pin(port, n), "")

        emit("GPIOB.ODR", f"0x{rd32(GPIO_BASE['B'] + 0x14):08X}",
             f"DS0={(rd32(GPIO_BASE['B'] + 0x14) >> 0) & 1} "
             f"DS1={(rd32(GPIO_BASE['B'] + 0x14) >> 1) & 1} "
             f"BL={(rd32(GPIO_BASE['B'] + 0x14) >> 5) & 1}")

        # ---- 6. 串口 / 总线 ----
        for lbl, base in (("USART1", USART1), ("USART2", USART2), ("USART3", USART3)):
            cr1 = rd32(base + 0x00)
            brr = rd32(base + 0x08)
            apb2 = 84_000_000
            apb1c = 42_000_000
            baud = None
            clk = apb2 if lbl == "USART1" else apb1c
            if brr:
                mant = brr >> 4
                frac = (brr & 0xF) / 16
                baud = clk / (mant + frac) / 16 if (mant + frac) else None
            emit(f"{lbl}.CR1", f"0x{cr1:08X}",
                 f"UE={(cr1 >> 13) & 1} TE={(cr1 >> 3) & 1} RE={(cr1 >> 2) & 1} "
                 f"M(9bit)={(cr1 >> 12) & 1} PCE={(cr1 >> 10) & 1} OVER8={(cr1 >> 15) & 1}")
            emit(f"{lbl}.BRR", f"0x{brr:08X}", f"推算波特率≈{baud:.0f}" if baud else "未配置")

        i2c_cr1 = rd32(I2C2 + 0x00)
        i2c_ccr = rd32(I2C2 + 0x14)
        i2c_trise = rd32(I2C2 + 0x20)
        emit("I2C2.CR1", f"0x{i2c_cr1:08X}", f"PE={(i2c_cr1 >> 0) & 1} SWRST={(i2c_cr1 >> 15) & 1}")
        emit("I2C2.CCR", f"0x{i2c_ccr:08X}",
             f"FS={(i2c_ccr >> 15) & 1} DUTY={(i2c_ccr >> 14) & 1} CCR={(i2c_ccr & 0xFFF)} "
             f"→ 约 {(42_000_000 / (2 * (i2c_ccr & 0xFFF)) if (i2c_ccr & 0xFFF) else 0):.0f} Hz")
        emit("I2C2.TRISE", f"0x{i2c_trise:08X}", f"值={(i2c_trise & 0x3F)}")

        spi5_cr1 = rd32(SPI5 + 0x00)
        emit("SPI5.CR1", f"0x{spi5_cr1:08X}",
             f"SPE={(spi5_cr1 >> 6) & 1} MSTR={(spi5_cr1 >> 2) & 1} BR={(spi5_cr1 >> 3) & 0x7} "
             f"CPOL={(spi5_cr1 >> 1) & 1} CPHA={(spi5_cr1 >> 0) & 1} DFF(16bit)={(spi5_cr1 >> 11) & 1}")

        # ---- 7. FMC / LTDC / ADC / DAC / RTC ----
        sdcr1 = rd32(FMC + 0x140)
        sdtr1 = rd32(FMC + 0x148)
        emit("FMC.SDCR1", f"0x{sdcr1:08X}",
             f"NC={sdcr1 & 0x3} NR={(sdcr1 >> 2) & 0x3} MWID={(sdcr1 >> 4) & 0x3} "
             f"NB={(sdcr1 >> 6) & 0x3} CAS={(sdcr1 >> 7) & 0x3} SDCLK={(sdcr1 >> 10) & 0x3} "
             f"RBURST={(sdcr1 >> 12) & 1} RPIPE={(sdcr1 >> 13) & 0x3}")
        emit("FMC.SDTR1", f"0x{sdtr1:08X}",
             f"TMRD={(sdtr1 >> 0) & 0xF} TXSR={(sdtr1 >> 4) & 0xF} TRAS={(sdtr1 >> 8) & 0xF} "
             f"TRC={(sdtr1 >> 12) & 0xF} TWR={(sdtr1 >> 16) & 0xF} TRP={(sdtr1 >> 20) & 0xF} "
             f"TRCD={(sdtr1 >> 24) & 0xF}")
        emit("FMC.SDSR", f"0x{rd32(FMC + 0x154):08X}", "RE=刷新错误 MODES=模式寄存器就绪")
        # SDRAM 可访问性：读帧缓冲首尾各一个字（判总线是否通）
        emit("SDRAM.read_0xC0000000", f"0x{rd32(0xC0000000):08X}", "")
        emit("SDRAM.read_0xC1000000", f"0x{rd32(0xC1000000):08X}", "")

        ltdc_gcr = rd32(LTDC + 0x24)
        emit("LTDC.GCR", f"0x{ltdc_gcr:08X}", f"LTDCEN={(ltdc_gcr >> 0) & 1}")
        emit("LTDC.L1CFBAR", f"0x{rd32(LTDC + 0xAC):08X}", "层1帧缓冲基址")
        emit("LTDC.CPSR", f"0x{rd32(LTDC + 0x30):08X}", "当前扫描位置")

        emit("ADC1.CR2", f"0x{rd32(ADC1 + 0x08):08X}",
             f"ADON={(rd32(ADC1 + 0x08) >> 0) & 1} CONT={(rd32(ADC1 + 0x08) >> 1) & 1}")
        emit("ADC.CCR", f"0x{rd32(ADC_CCR):08X}",
             f"ADCPRE={(rd32(ADC_CCR) >> 16) & 0x3} TSVREFE={(rd32(ADC_CCR) >> 23) & 1}")
        emit("DAC.CR", f"0x{rd32(DAC + 0x00):08X}",
             f"EN1={(rd32(DAC) >> 0) & 1} BOFF1={(rd32(DAC) >> 1) & 1} TEN1={(rd32(DAC) >> 2) & 1}")

        # ---- 8. Flash 回读校验 ----
        if os.path.exists(BIN):
            local = open(BIN, "rb").read(256)
            remote = t.read_memory(0x08000000, 256)
            remote = bytes(remote) if not isinstance(remote, (bytes, bytearray)) else bytes(remote)
            same = remote[:256] == local[:256]
            emit("FLASH.readback_256B", "MATCH" if same else "MISMATCH",
                 f"bin={len(open(BIN, 'rb').read())}B 前256字节比对")
            emit("FLASH.local_size", str(os.path.getsize(BIN)), "")
        else:
            emit("FLASH.readback_256B", "NO_BIN", BIN)

    finally:
        try:
            t.resume()
        except Exception:
            pass
        session.close()

    print("== done ==")
    return 0


if __name__ == "__main__":
    sys.exit(main())
