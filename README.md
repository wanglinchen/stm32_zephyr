# stm32-zephyr

正点原子 **阿波罗 V2（STM32F429IGT6）** 上的 Zephyr 4.5 工程：自定义板级 + out-of-tree 驱动模块 + 可编译、可自检的应用骨架。同时保留官方 `stm32f429i_disc1` 作为对照板。

- 模块 → 驱动对照表：[`docs/design/模块-驱动对照表.md`](docs/design/模块-驱动对照表.md)
- 覆盖看板（浏览器直接打开，深色）：[`docs/design/模块驱动覆盖看板.html`](docs/design/模块驱动覆盖看板.html)
- 架构设计：[`docs/design/Zephyr-STM32F429-架构设计.md`](docs/design/Zephyr-STM32F429-架构设计.md)
- 架构图：[`docs/design/Zephyr-STM32F429-架构图.html`](docs/design/Zephyr-STM32F429-架构图.html)

## 1. 当前状态（编译/链接已验证，未上板实测）

| 项目 | 结果 |
| --- | --- |
| Zephyr 版本 | v4.5.0（工作区内 `zephyr/`） |
| 工具链 | Zephyr SDK 1.0.0，`arm-zephyr-eabi` GCC 14.3.0 |
| 默认板级 | `apollo_f429`（vendor `alientek`，SoC `stm32f429xx`，HSE 25 MHz，SYSCLK 168 MHz） |
| 构建 | **通过**：`build/apollo_f429/zephyr/zephyr.elf` |
| 占用 | FLASH 119,884 B / 1 MB（11.43%）；RAM 22,720 B / 192 KB（11.56%）；SDRAM1 750 KB（LTDC 帧缓冲） |
| 驱动 | 上游现成 14 项（SDRAM/LTDC/PCF8574/QMI8658A/XPT2046/SPI NOR/ADC/DAC/RTC/UART/GPIO Keys 等）+ 自写 5 项（NAND MT29F4G08、ES8388、AP3216C、DHT11、NEC 红外） |
| 自检 | 串口 shell `devices` 命令逐项打印 `device_is_ready()` |
| 烧录 | pyocd `--target=stm32f429xi`；本机已识别 ST-Link，**未自动烧录** |

## 2. 目录结构

```
stm32-zephyr/
├── west.yml                       # 本工程 manifest：allowlist zephyr + 三个 HAL 模块
├── zephyr/                        # 上游源码 v4.5.0（只读，不修改）
├── modules/hal/                   # cmsis / cmsis_6 / stm32
├── .sdk/zephyr-sdk-1.0.0/         # SDK（放在工作区内）
├── boards/apollo_f429/            # 自定义板
│   ├── board.yml / board.cmake / apollo_f429.yaml / apollo_f429_defconfig
│   ├── Kconfig.apollo_f429        # 必须：config BOARD_APOLLO_F429 + select SOC_STM32F429XX
│   ├── Kconfig.defconfig          # if BOARD_APOLLO_F429 → MEMC default y
│   └── apollo_f429.dts            # 模块节点 / 引脚 / 时序
├── modules/apollo_f429/           # out-of-tree 驱动模块（ZEPHYR_EXTRA_MODULES）
│   ├── zephyr/module.yml
│   ├── drivers/CMakeLists.txt     # 单一库 apollo_f429_drivers（幂等）
│   ├── drivers/{mt29f4g08, es8388, ap3216c, dht11, ir_nec}/
│   ├── dts/bindings/…             # 5 个自定义 binding
│   └── include/zephyr/drivers/{audio/es8388.h, input/ir_nec.h}
├── app/
│   ├── prj.conf                   # 通用能力开关
│   ├── boards/apollo_f429.conf    # 板级 Kconfig（SPI/I2C/FLASH/SPI_NOR/SENSOR/DISPLAY/INPUT/ADC/DAC/RTC）
│   ├── boards/apollo_f429.overlay
│   ├── include/app_led.h
│   └── src/
│       ├── main.c                 # 启动编排
│       ├── app_led.c              # 心跳线程（GPIO）
│       ├── app_shell.c            # app info / app heartbeat
│       └── app_devices.c          # devices：板载模块自检
├── docs/                          # hardware 资料 + design 文档/看板
├── scripts/{env.ps1, build.ps1, flash.ps1}
└── build/                         # 构建产物（不入库）
```

## 3. 环境要求

| 组件 | 版本/路径 | 说明 |
| --- | --- | --- |
| Python | `D:\ExPrograms\DevelopTool\Python314\python.exe`（3.14.4） | 需 pyelftools / pykwalify / anytree；由 `WEST_PYTHON` 传给 CMake |
| west | 1.5.0 | 已安装 |
| CMake / Ninja | 4.4.4 / 1.13.2 | 已安装 |
| dtc | `D:\ExPrograms\DevelopTool\msys64\mingw64\bin\dtc.exe`（1.8.1） | 构建时自动发现 |
| Zephyr SDK | `.sdk\zephyr-sdk-1.0.0` | minimal 包 + `gnu/arm-zephyr-eabi` 工具链 |
| pyocd | 0.46.1 | 烧录 ST-Link |

> SDK 1.0.0 的工具链必须位于 `<sdk>/gnu/<triple>`，不是 `<sdk>/<triple>`。
> `scripts/*.ps1` 含中文，必须保存为 **UTF-8 with BOM**，否则 PowerShell 5.1 会报 `string missing terminator`。

### 从零重建工作区（新机器 / 新目录）

前提：本目录是一个受 git 管理的仓库（`git clone` 本仓库即满足；若是手工拷贝的目录，先 `git init`）——`west init -l` 要求 manifest 仓库本身是 git 仓库。

本工程有自己的 `west.yml`：只 allowlist `zephyr` 与 `cmsis`、`cmsis_6`、`hal_stm32` 三个 HAL 模块；MCUboot 不在清单内（本工程是单镜像直启，不需要 bootloader）。

```powershell
pip install west
west init -l .      # 以本目录为 manifest 仓库，生成 .west/config
west update         # 只拉 zephyr + 三个 HAL 模块，而非整仓 GB 级下载
```

SDK 需另行解压到 `.sdk\zephyr-sdk-1.0.0`（工具链位于 `gnu/arm-zephyr-eabi`）。板定义与 out-of-tree 驱动模块已由 `app/CMakeLists.txt` 内置登记，无需再传 `-DBOARD_ROOT` / `-DZEPHYR_EXTRA_MODULES`。

## 4. 构建与烧录

```powershell
# 1) 载入环境（每个新开的 PowerShell 会话执行一次）
. .\scripts\env.ps1

# 2) 构建（默认 apollo_f429；板定义与驱动模块路径已由 app/CMakeLists.txt 内置）
.\scripts\build.ps1
.\scripts\build.ps1 -Pristine                 # 出现 multiple definition 时用
.\scripts\build.ps1 -Board stm32f429i_disc1   # 对照官方板

# 3) 体积报告
west build -t ram_report -d build\apollo_f429
west build -t rom_report -d build\apollo_f429

# 4) 烧录（会覆盖板内现有固件）
.\scripts\flash.ps1
```

等价的最简 west 命令（板与模块路径已内置，不再需要 `-D...`）：

```powershell
west build -b apollo_f429 -d build\apollo_f429 app
```

## 5. 上板自检

USART1（CH340，PA9/PA10，115200 8N1）既是 console 也是 shell。上电后：

```
uart:~$ devices          # 逐项打印板载模块设备状态
uart:~$ app info         # 板级/内核信息
```

`OK` = 驱动初始化成功；`FAIL` = 设备存在但 init 失败；命令未列出的模块 = 节点未启用或驱动未实例化（详见对照表 ⚪/⛔ 行）。

## 6. 已知约束与后续

- **未上板实测**：所有寄存器值/时序按手册与例程填写，ES8388 初值、W25Q256 4 字节地址、XPT2046 坐标范围、LTDC 时序均需现场校准（清单见对照表 §5.1）。
- **`flash0` / `sdram1` 设备对象缺失**：节点 okay，但 `DEVICE_DT_GET` 链接期找不到设备对象；内部 flash 上的 NVS/settings 与 SDRAM 运行期自检暂不可用（SDRAM 的帧缓冲已正常落在 SDRAM1）。
- **未启用模块**：以太网 YT8512C（PHY 地址/PCF8574 P7 复位未接）、TF 卡与 OV5640（F4 dtsi 无 `sdmmc`/`dcmi` 节点）、SAI1 音频数据通路（无 DMA 配置）、8080 并口屏与 CAN1（默认 disabled，引脚冲突）。
- **CCM 不可 DMA 访问**：`zephyr,dtcm` 指向 `ccm0`，DMA 缓冲必须放 `sram0`。
- **换板**：新增 `boards/<board>/` 与 `app/boards/<board>.{overlay,conf}`；非官方板记得带 `Kconfig.<board>`，否则 SOC 不会选中。
- **日志用延迟模式**：复位前最后几条日志可能丢失，需要时改用 `LOG_PANIC()`。
