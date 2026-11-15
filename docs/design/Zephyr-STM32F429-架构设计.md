# STM32F429 + Zephyr 架构设计

> 目标硬件：STM32F429ZIT6（Cortex-M4F，2MB Flash / 192KB SRAM / 64KB CCM）
> 参考板：STM32F429I-DISC1（Zephyr 板名 `stm32f429i_disc1`，SoC `stm32f429xx`）
> RTOS：Zephyr v4.5.0　工具链：Zephyr SDK 1.0.0（arm-zephyr-eabi）
> 文档版本：v1（2026-11-14）

---

## 1. 需求与约束

### 1.1 功能需求

| 编号 | 需求 | 架构落点 |
| --- | --- | --- |
| F1 | 上电即启动，串口输出启动横幅与运行日志 | `CONFIG_BOOT_BANNER` + USART1 控制台 |
| F2 | 多任务并发：心跳输出、采样处理、命令交互互不阻塞 | 内核线程 + 抢占式优先级调度 |
| F3 | 中断到线程的数据通路，中断内不做耗时处理 | 定时器 ISR → `k_msgq` → 处理线程 |
| F4 | 运行时命令交互与状态查询 | Shell 子系统（`app` 命令组） |
| F5 | 外设通过统一驱动 API 访问，不直接读写寄存器 | Zephyr 设备模型 + devicetree |
| F6 | 板级差异只在 overlay/conf 中体现，应用代码不写死引脚 | 分层配置 |
| F7 | 可编译、可烧录、可观测（日志 / Shell / 内存报告） | `west build` / `west flash` / `-t ram_report` |

### 1.2 非功能约束

| 维度 | 约束 | 架构应对 |
| --- | --- | --- |
| 实时性 | 中断响应不能被日志阻塞 | 日志延迟模式（deferred），ISR 只入环形缓冲 |
| 内存 | SRAM 192KB，其中 CCM 64KB 不可被 DMA 访问 | 栈与热数据放 SRAM0；CCM 作 `zephyr,dtcm` 区域 |
| 可靠性 | 栈溢出、空指针必须可定位 | `CONFIG_HW_STACK_PROTECTION` + MPU + `CONFIG_ASSERT` |
| 可维护性 | 硬件改动不牵动业务代码 | devicetree 为硬件唯一真相源 |
| 可移植 | 换 F7/H7 时应用层零改动 | 只依赖 `include/zephyr/drivers` 抽象层 |

---

## 2. 总体分层架构

```
┌──────────────────────────────────────────────────────────────┐
│ L4 应用层        app/src/*.c  include/app_*.h                 │
│                 业务线程、状态机、命令处理                      │
├──────────────────────────────────────────────────────────────┤
│ L3 服务层        subsys/: logging / shell / settings / fs ... │
├──────────────────────────────────────────────────────────────┤
│ L2 内核层        kernel/: 调度、线程、信号量、消息队列、定时器    │
├──────────────────────────────────────────────────────────────┤
│ L1 设备抽象层     include/zephyr/drivers + drivers/            │
│                  devicetree / pinctrl / clock_control          │
├──────────────────────────────────────────────────────────────┤
│ L0 架构与 SoC     arch/arm/core/cortex_m（异常、上下文切换）    │
│                  soc/st/stm32/stm32f4x（时钟、SoC 初始化）      │
│                  modules/hal/stm32（STM32Cube HAL/LL）          │
│                  modules/hal/cmsis_6（CMSIS-Core 寄存器定义）   │
├──────────────────────────────────────────────────────────────┤
│ 硬件             STM32F429ZIT6 + 板载外设                     │
└──────────────────────────────────────────────────────────────┘
```

### 2.1 依赖规则（强约束）

1. **向下单向依赖**：L4 只能调 L3/L2/L1 的公开 API，禁止调用 `hal_stm32` 的 `HAL_*` / `LL_*` 函数。
2. **硬件事实只在两处**：`dts`（引脚、地址、中断号、时钟）与 `Kconfig`（能力开关）。C 代码里不出现端口号、引脚号、外设基地址。
3. **ISR 不做阻塞操作**：ISR 内只用 `k_msgq_put(..., K_NO_WAIT)`、`k_sem_give()`、`k_timer` 等非阻塞原语。
4. **跨层不反向**：驱动不感知业务，驱动只实现 `device` 结构与标准 API。

### 2.2 关键子系统与源码位置

| 子系统 | 路径 | 说明 |
| --- | --- | --- |
| Cortex-M 异常与切换 | `zephyr/arch/arm/core/cortex_m/` | `Reset_Handler`、`z_arm_pendsv`、FPU 上下文 |
| STM32F4 SoC | `zephyr/soc/st/stm32/stm32f4x/` | `soc.c` 时钟初始化、`Kconfig.defconfig` 默认值 |
| 板级定义 | `zephyr/boards/st/stm32f429i_disc1/` | `board.yml`、`.dts`、`_defconfig` |
| 设备树源 | `zephyr/dts/arm/st/f4/stm32f429Xi.dtsi` | Flash 2MB、SRAM 192KB、CCM 64KB |
| 驱动 | `zephyr/drivers/{gpio,serial,i2c,spi,adc,pwm,...}/` | STM32 系列驱动 + `hal_stm32` 后端 |
| 内核 | `zephyr/kernel/` | `sched.c`、`msg_q.c`、`timeout.c`、`mem_slab.c` |
| 服务 | `zephyr/subsys/{logging,shell,settings,fs,pm}/` | 可裁剪，按 `prj.conf` 启用 |

---

## 3. 目标硬件资源模型

### 3.1 存储器映射（来自 `stm32f429Xi.dtsi`）

| 区域 | 起始地址 | 容量 | Zephyr 节点/选择器 | 用途 |
| --- | --- | --- | --- | --- |
| Flash | `0x0800_0000` | 2 MB | `flash0` / `zephyr,flash` | 代码 + 只读数据 |
| SRAM（SRAM1+2+3 连续） | `0x2000_0000` | 192 KB | `sram0` / `zephyr,sram` | `.data` `.bss` 栈 堆 |
| CCM RAM | `0x1000_0000` | 64 KB | `ccm0` / `zephyr,dtcm` | 零等待热数据（DMA 不可达） |
| SDRAM（FMC） | `0xD000_0000` | 8 MB | `sdram2`（`zephyr,memory-region`） | LTDC 显存 |
| 外设 | `0x4000_0000` 起 | — | 各外设节点 | 寄存器区 |

> 注意：CCM 只能被 CPU 访问，**任何 DMA 缓冲都不能放在 CCM**。板级 defconfig 已打开 `CONFIG_ARM_MPU=y` 与 `CONFIG_HW_STACK_PROTECTION=y`。

### 3.2 时钟树（参考板实测配置）

```
HSE 8MHz ──/M=8──> 1MHz ──*N=336──> 336MHz VCO ──/P=2──> SYSCLK 168MHz
                                                  ├─/Q=7──> 48MHz 域（USB 等）
                                                  └─ AHB /1  = 168MHz
                                                     APB1 /4 = 42MHz
                                                     APB2 /2 = 84MHz
```

- 参考板 dts 中 `&rcc { clock-frequency = <168MHz> }`，即 Zephyr 默认把 F429 跑在 **168MHz**（芯片上限 180MHz）。
- 想跑满 180MHz：改 overlay 中 `&pll { mul-n = <360>; }`，并把 `&rcc clock-frequency` 改为 180MHz，同时复核 APB 分频不超过 45/90MHz 上限。
- 串口控制台：**USART1，PA9/PA10，115200 8N1**（`chosen zephyr,console`）。
- 所有时钟频率均从 dts 推导，驱动通过 `clock_control` API 获取，不在代码里写死。

### 3.3 参考板外设（可直接复用的验证资源）

| 外设 | 连接 | 节点 | 状态 |
| --- | --- | --- | --- |
| LD3 / LD4 | PG13 / PG14 | `led_3`/`led_4`，`led0` 别名 | 可用 |
| 用户按键 | PA0 | `user_button`，`sw0` 别名 | 可用 |
| 触摸屏控制器 | I2C3，地址 0x41 | `stmpe811` | 可用 |
| LCD | SPI5 + LTDC + DMA2D | `ili9341` / `ltdc` | 可用 |
| SDRAM | FMC | `sdram` / `sdram2` | 可用 |
| USB | OTG HS，PB14/PB15 | `zephyr_udc0` | 可用 |

---

## 4. 启动流程与初始化级别

```
上电/复位
   │
   ├─ Reset_Handler（arch/arm/core/cortex_m/reset.S）
   │     ├─ 关中断、清 BSS 前的必要准备
   │     └─ SystemInit()：FPU 使能、向量表基址、早期时钟
   │
   ├─ z_cstart()（C 运行时入口）
   │     ├─ 拷贝 .data、清 .bss、（可选）初始化 CCM 段
   │     ├─ 建立中断栈 / 主线程栈
   │     └─ 切到主线程上下文
   │
   ├─ z_sys_init_run_level(EARLY)                 ← 极早期，时钟/看门狗
   ├─ z_sys_init_run_level(PRE_KERNEL_1)          ← 时钟控制、串口底层
   ├─ z_sys_init_run_level(PRE_KERNEL_2)          ← GPIO、pinmux
   ├─ z_sys_init_run_level(POST_KERNEL)           ← 大多数驱动 probe
   ├─ z_sys_init_run_level(APPLICATION)           ← 应用级 SYS_INIT 钩子
   │
   └─ main()（主线程）
```

| 级别 | 可用能力 | 本工程使用 |
| --- | --- | --- |
| `EARLY` | 无内核对象，只能碰寄存器 | 不使用 |
| `PRE_KERNEL_1` | 中断可用，内核调度不可用 | 驱动内部使用 |
| `PRE_KERNEL_2` | 同上 | 驱动内部使用 |
| `POST_KERNEL` | 完整内核对象可用 | 驱动 `device_init`、`SYS_INIT` 钩子 |
| `APPLICATION` | 全部可用，早于 `main()` | 业务模块预初始化（可选） |

应用侧初始化统一放在 `main()` 里显式调用，顺序清晰、失败可返回错误码，避免隐式的 `SYS_INIT` 依赖。

---

## 5. 并发与中断模型

### 5.1 线程划分

| 线程 | 优先级 | 栈 | 职责 | 阻塞点 |
| --- | --- | --- | --- | --- |
| `main` | 0（协作） | 4 KB（`CONFIG_MAIN_STACK_SIZE`） | 保持存活（业务在各专用线程） | `k_sleep(5s)` |
| `led_heartbeat` | 7（抢占） | 1 KB | GPIO 翻转 | `k_sleep(500ms)` |
| 系统日志线程 | 由 `CONFIG_LOG` 决定 | `CONFIG_LOG_PROCESS_THREAD_STACK_SIZE` | 消费日志缓冲、写串口 | 日志队列 |
| Shell 线程 | `CONFIG_SHELL` 决定 | 2 KB | 命令解析与执行 | 串口输入 |

### 5.2 中断到线程的标准通路

> 注：`app_sensor` 采样链路当前已从应用移除（源码备份于 `build/_sensor_backup_20261115`），本节保留作为“中断只搬运、计算下沉线程”的参考通路。

```
k_timer 到期（系统时钟 ISR 上下文）
        │  k_msgq_put(&sample_q, &raw, K_NO_WAIT)   ← 不阻塞、可丢弃
        ▼
   [ k_msgq 环形缓冲，深度 64 ]
        ▼
sensor_proc 线程  k_msgq_get(K_FOREVER)
        │  滤波 / 统计（自旋锁保护共享数据）
        ▼
main 线程定期读取快照 → LOG_INF
```

设计要点：

- **队列满即丢**：中断里绝不等待，丢样本优于丢中断响应。
- **共享数据用 `k_spinlock`**：跨越线程与中断的极小临界区，不用互斥量（互斥量不能在 ISR 中使用）。
- **日志延迟模式**：`CONFIG_LOG_MODE_DEFERRED=y`，`LOG_INF` 在 ISR 中只入缓冲，由日志线程落串口。
- **优先级分层**：数值越小优先级越高；业务线程用 `K_PRIO_PREEMPT(7..8)`，为驱动/协议栈留出 0..6。

---

## 6. 设备与驱动接入

### 6.1 设备获取方式

```c
/* 编译期由 devicetree 生成，零运行时开销 */
#define LED_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
```

| 环节 | 发生时间 | 产物 |
| --- | --- | --- |
| dts 解析 | CMake 配置阶段 | `edt.pickle`、`devicetree_generated.h` |
| 设备注册 | 链接阶段（section 收集） | `__device_dts_ord_*` 结构 |
| 设备初始化 | 启动时按 init level | 驱动 `init` 函数被调用 |
| 运行时访问 | 应用代码 | `device_is_ready()` + 标准 API |

### 6.2 驱动选用矩阵（F429 可用）

| 功能 | Zephyr 驱动 | 依赖 Kconfig | 备注 |
| --- | --- | --- | --- |
| GPIO | `drivers/gpio/gpio_stm32.c` | `CONFIG_GPIO` | 支持 EXTI 中断 |
| UART | `drivers/serial/uart_stm32.c` | `CONFIG_SERIAL` | 控制台/Shell |
| I2C | `drivers/i2c/i2c_stm32.c` | `CONFIG_I2C` | 触摸、传感器 |
| SPI | `drivers/spi/spi_stm32.c` | `CONFIG_SPI` | LCD、外部 Flash |
| ADC | `drivers/adc/adc_stm32.c` | `CONFIG_ADC` | 多通道 + DMA |
| PWM | `drivers/pwm/pwm_stm32.c` | `CONFIG_PWM` | 定时器输出 |
| 计数器 | `drivers/counter/counter_stm32_timer.c` | `CONFIG_COUNTER` | 编码器/脉宽测量 |
| 看门狗 | `drivers/watchdog/wddg_stm32.c` | `CONFIG_WATCHDOG` | IWDG |
| RTC | `drivers/rtc/rtc_stm32.c` | `CONFIG_RTC` | 后备域 |
| DMA | `drivers/dma/dma_stm32.c` | `CONFIG_DMA` | 注意 CCM 不可达 |
| 显示 | `drivers/display/display_stm32_ltdc.c` | `CONFIG_DISPLAY` | 需 SDRAM 显存 |

---

## 7. 构建与配置体系

### 7.1 构建流水线

```
west build -b stm32f429i_disc1 app
   │
   ├─ 读取 .west/config → 定位 manifest 仓库（zephyr/west.yml）
   ├─ CMake 配置
   │     ├─ 解析 board.yml → soc / arch / 板级 dts
   │     ├─ Kconfig：prj.conf + board defconfig + soc defconfig + Kconfig 依赖求解
   │     ├─ devicetree：dts + overlay → 生成 C 头文件
   │     └─ 工具链探测：ZEPHYR_SDK_INSTALL_DIR → arm-zephyr-eabi-gcc
   ├─ Ninja 编译 → 目标文件
   ├─ 链接：链接脚本由 dts 内存节点自动生成
   └─ 产物：zephyr.elf / zephyr.bin / zephyr.map
```

### 7.2 配置分层（单一职责）

| 层级 | 文件 | 允许写什么 | 禁止写什么 |
| --- | --- | --- | --- |
| 应用能力 | `app/prj.conf` | 功能开关（LOG/SHELL/协议栈） | 引脚、外设地址、板名 |
| 板级差异 | `app/boards/<board>.conf` | 板特有开关（如启用 LCD） | 业务逻辑开关 |
| 硬件描述 | `app/boards/<board>.overlay` | 引脚复用、外设状态、时钟 | 应用数据结构 |
| SoC/板默认 | `zephyr/soc/**/Kconfig.defconfig`、板级 `_defconfig` | 上游默认值 | 本项目改动（不要改上游） |

合并顺序（后者覆盖前者）：`soc defconfig` → `board defconfig` → `prj.conf` → `boards/<board>.conf` → `-DEXTRA_CONF_FILE`。

### 7.3 工作区拓扑

```
stm32-zephyr/
├── .west/config            # manifest 指向 zephyr/west.yml
├── zephyr/                 # 上游源码 v4.5.0（只读，不修改）
├── modules/hal/            # cmsis、cmsis_6、stm32（west 拉取）
├── app/                    # 本项目应用（唯一改动区）
│   ├── CMakeLists.txt
│   ├── prj.conf
│   ├── boards/             # 板级 overlay / conf
│   ├── include/
│   └── src/
├── docs/                   # 架构文档与架构图
├── scripts/                # env / build / flash 脚本
└── build/                  # 构建产物（不入库）
```

---

## 8. 关键设计决策（ADR）

### ADR-001：以 Zephyr 而非裸机/CubeMX 作为底座

- **背景**：需要多任务、驱动抽象、可移植性；团队后续可能迁移到 F7/H7。
- **决策**：采用 Zephyr v4.5.0 + STM32 HAL 后端。
- **备选**：FreeRTOS + CubeMX（生态成熟但驱动抽象弱、板级差异靠宏）；裸机（无法支撑多任务与文件/网络等子系统）。
- **代价**：构建体系（west/CMake/Kconfig/dts）学习成本高；二进制体积大于裸机。
- **结论**：以约 100KB 级 Flash 开销换取驱动复用与可移植性。

### ADR-002：devicetree 作为硬件唯一真相源

- **决策**：所有引脚、外设实例、中断、时钟均写在 dts/overlay；C 代码只通过 `DT_*` 宏与 `device` 句柄访问。
- **理由**：换板只改 overlay；同一份应用代码可在不同板子上编译。
- **代价**：dts 语法门槛；错误在编译期暴露（相对地也是优点）。

### ADR-003：中断只搬运，计算下沉到线程

- **决策**：ISR 内只做 `k_msgq_put`/`k_sem_give`；滤波、统计、协议处理在独立线程。
- **理由**：保证中断响应时间与抖动可控；避免日志/浮点在 ISR 中。
- **代价**：引入队列深度与丢样策略，需要显式设计。

### ADR-004：日志采用延迟模式

- **决策**：`CONFIG_LOG_MODE_DEFERRED=y`，缓冲 4KB，UART 后端。
- **理由**：串口 115200 下输出一行的耗时约 1ms 级，同步日志会拖垮实时性。
- **代价**：掉电/复位前可能丢最后几条日志（后续可加 `LOG_PANIC()`）。

### ADR-005：CCM 只放 CPU 热数据

- **决策**：`zephyr,dtcm` 指向 `ccm0`；栈与频繁访问的结构可放 CCM，DMA 缓冲严禁放 CCM。
- **理由**：CCM 零等待、独立总线，但不受 DMA 控制器访问。

---

## 9. 风险与验证清单

| 风险 | 影响 | 缓解 | 验证方式 |
| --- | --- | --- | --- |
| 工具链/CMake 版本不匹配 | 配置阶段直接失败 | 固定 SDK 1.0.0 + Zephyr 4.5.0，版本写入 README | `west build` 通过 |
| 模块缺失（hal_stm32/cmsis） | 编译报 `HAL_*` 未定义 | `west update cmsis cmsis_6 hal_stm32` | 编译零错误 |
| 中断优先级配置冲突 | 偶发死锁/丢中断 | 使用 `DT_IRQ` 派生优先级，不手写数值 | 长时间运行 + Shell 统计 |
| 栈溢出 | 随机 HardFault | `CONFIG_HW_STACK_PROTECTION` + `-t ram_report` | 压测后查看栈水位 |
| 队列丢样不可见 | 数据静默丢失 | 统计 `k_msgq_num_dropped`（或计数器） | Shell 查询 |
| 日志抢占串口导致 Shell 卡顿 | 交互体验差 | 延迟模式 + 合理日志级别 | 手动交互验证 |

### 构建期自检命令

```powershell
west build -t ram_report      # 各段 RAM 占用
west build -t rom_report      # Flash 占用排行
west build -t puncover        # 可选：可视化分析
```

---

## 10. 落地步骤（已在本地完成）

1. 工作区：`.west/config` 指向 `zephyr/west.yml`，`zephyr/` 为 v4.5.0 源码快照。
2. 最小模块集：`west update cmsis cmsis_6 hal_stm32`（只拉编译所需，避免整仓 GB 级下载）。
3. 工具链：Zephyr SDK 1.0.0，minimal 包 + `toolchain_gnu_windows-x86_64_arm-zephyr-eabi`，装在工作区内 `.sdk\zephyr-sdk-1.0.0`，工具链置于 `gnu\arm-zephyr-eabi`。
4. 环境注入：`scripts/env.ps1` 设置 `ZEPHYR_BASE`、`ZEPHYR_SDK_INSTALL_DIR`、`WEST_PYTHON`。
5. 构建：`scripts/build.ps1` → `build/stm32f429i_disc1/zephyr/zephyr.elf`。
6. 烧录：`scripts/flash.ps1`（pyocd + ST-Link，目标 `stm32f429xi`）。
7. 观测：串口 115200 查看启动日志，`app info` / `devices` 验证运行时状态。

## 11. 实测结果（2026-11-14，本机）

| 项目 | 结果 |
| --- | --- |
| CMake 配置 | 通过（107 s），识别 board / SoC / dtc / SDK |
| 编译链接 | 通过（199 个目标），产物 `zephyr.elf` / `.bin` / `.hex` / `.map` |
| Flash 占用 | 76,252 B / 2 MB（3.64%） |
| RAM 占用 | 24,896 B / 192 KB（12.66%） |
| text / data / bss | 75,832 B / 412 B / 24,495 B |
| 烧录通道 | pyocd 0.46.1 内置 `stm32f429xi`；已识别 ST-Link，未实际写入芯片 |

> 说明：RAM 统计里 `SRAM0` 与 `DTCM` 显示 0 是链接脚本分段口径（实际运行段计入 `RAM`）；如需逐段明细用 `west build -t ram_report`。

