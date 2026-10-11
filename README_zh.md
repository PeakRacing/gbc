# gbc

面向低内存 MCU 的 GB/GBC 模拟器，工程结构、xmake 构建方式和可裁剪配置风格参考 `nes_github`。

## 特性

- 同时支持 GB(DMG) 与 GBC(CGB) 卡带模式。
- 默认跳过 Boot ROM，按卡带类型直接进入 post-boot 寄存器初始状态。
- 核心使用 C11 编写，`src/` 完全不依赖 SDL 或操作系统接口。
- 平台相关能力通过 `port/` 弱函数替换：内存、文件系统、日志、绘制、音频、时间。
- 默认关闭音频，低资源 MCU 优先；开启后使用整数/定点运算输出 S16LE 立体声 PCM，无浮点。
- 支持常见 MBC：ROM ONLY、MBC1、MBC2、MBC3（含 RTC）、MBC5。
- Battery-backed SRAM 存档自动读写 `.sav` 文件（通过 `GBC_ENABLE_SRAM_SAVE` 控制）。
- 支持整屏帧缓存和半屏/块刷新模式；支持全量 ROM 与流式 ROM+LRU bank 缓存模式。

## 目录结构

```text
inc/                公开 API 与核心模块头文件
src/                平台无关的模拟器核心实现
port/               默认配置和弱平台函数，供 MCU/RTOS/裸机替换
sdl/sdl2/           SDL2 桌面验证端口与 xmake 构建脚本
tests/              各模块回归用例、无头真实 ROM 冒烟程序及其端口配置
AGENTS.md           中文代理/贡献者说明
README.md           英文 README
```

## 构建 SDL2 示例

```powershell
Set-Location .\sdl\sdl2
xmake
xmake run gbc path\to\game.gb
xmake run gbc path\to\game.gbc

# 回归测试
xmake build core_test && xmake run core_test
xmake build sound_test && xmake run sound_test

# 真实 ROM 无头冒烟测试
xmake build rom_test && xmake run rom_test path\to\game.gb 600
xmake build rom_test_stream && xmake run rom_test_stream path\to\game.gb 600
```

`xmake run rom_test <rom 文件> [帧数] [音频输出文件] [周期预算]` 会无头启动一张真实卡带，模拟指定帧数（缺省 60 帧），并打印解析出的卡带头、最终 CPU 寄存器状态、帧缓冲指纹与 APU 产生的字节数。镜像无法加载或卡带控制器不受支持时以非零码退出，因此可对整个 ROM 库批量执行。可选的周期预算（缺省每帧两百万周期）用于兜住自行关闭 LCD 的卡带：这类卡带永远不产生帧，没有预算程序会一直空转而无法报告停顿。`rom_test_stream` 用 `GBC_ROM_STREAM=1` 编译同一套程序，对同一镜像分别运行两者即可比对运行结束状态「整份 ROM 常驻」与「LRU bank 缓存」两条路径（指纹与寄存器完全一致说明缓存路径等价）。该程序不会写电池存档，可安全指向只读 ROM 目录。

`sdl/sdl2/xmake.lua` 会自动拉取 `libsdl2` 并编译 `src/**.c`、`sdl/sdl2/main.c` 和 SDL 平台端口。

SDL2 示例默认按键：`W/A/S/D` 或方向键控制方向，`J` = A，`K` = B，`V` = Select，`B` = Start，`Esc` 退出。

## 核心 API

```c
#include "gbc.h"

gbc_t *gbc = gbc_init();
gbc_set_model(gbc, GBC_MODEL_CGB); /* 可选：双模式卡带默认用 CGB，也可指定 GBC_MODEL_DMG */
gbc_load_file(gbc, "game.gbc");    /* 同时加载 battery RAM（如有存档） */
gbc_run(gbc);
gbc_unload_file(gbc);              /* 自动保存 battery RAM */
gbc_deinit(gbc);
```

从内存加载 ROM：

```c
gbc_load_rom(gbc, rom_data, rom_size);
```

## 主要配置宏

配置位于 `port/gbc_conf.h`，默认偏向 PC/SDL 调试；MCU 通过宏按资源裁剪。

| 宏 | 默认值 | 说明 |
| --- | --- | --- |
| `GBC_ENABLE_DMG` | `1` | 启用 GB/DMG 卡带支持 |
| `GBC_ENABLE_CGB` | `1` | 启用 GBC/CGB 卡带支持 |
| `GBC_ENABLE_SOUND` | `0` | 音频开关；开启后输出 S16LE 立体声 PCM，无浮点运算 |
| `GBC_APU_SAMPLE_RATE` | `32000` | APU 输出采样率 |
| `GBC_APU_BUFFER_SAMPLES` | `256` | 每次音频端口输出的立体声帧数 |
| `GBC_RAM_LACK` | `0` | `1` 时使用半屏/块刷新以减少帧缓存占用 |
| `GBC_FRAME_SKIP` | `0` | 每帧跳过渲染的帧数 |
| `GBC_COLOR_DEPTH` | `32` | `16` = RGB565，`32` = ARGB8888 |
| `GBC_COLOR_SWAP` | `0` | RGB565 字节序交换，适配部分 SPI 屏 |
| `GBC_USE_FS` | `1` | 启用文件系统 ROM 加载 |
| `GBC_ENABLE_SRAM_SAVE` | `(GBC_USE_FS)` | 启用 battery RAM 存档到 `.sav` 文件；`GBC_USE_FS=0` 时自动关闭 |
| `GBC_ROM_STREAM` | `0` | `1` 时从文件流式读取 ROM bank，配合 LRU 缓存 |
| `GBC_ROM_CACHE_BANKS` | `4` | 流式 ROM 模式下的 16 KiB bank LRU 缓存数量 |
| `GBC_LOG_LEVEL` | `GBC_LOG_LEVEL_INFO` | 日志级别 |

低内存 MCU 推荐组合：

```c
#define GBC_COLOR_DEPTH      (16)
#define GBC_RAM_LACK         (1)
#define GBC_ROM_STREAM       (1)
#define GBC_ROM_CACHE_BANKS  (3)
#define GBC_ENABLE_SOUND     (0)
#define GBC_ENABLE_SRAM_SAVE (0)   /* 无持久存储时禁用 */
```

## 平台移植

移植时替换或实现以下弱函数：

- `gbc_malloc` / `gbc_free`
- `gbc_memcpy` / `gbc_memset` / `gbc_memcmp`
- `gbc_fopen` / `gbc_fread` / `gbc_fwrite` / `gbc_fseek` / `gbc_ftell` / `gbc_fclose`
- `gbc_draw` — 接收当前扫描线块的像素缓冲区
- `gbc_sound_output` — 当 `GBC_ENABLE_SOUND=1` 时接收 S16LE 立体声 PCM 字节流
- `gbc_log_printf`
- `gbc_get_ticks` / `gbc_delay`
- `gbc_initex` / `gbc_deinitex` / `gbc_frame` — 可选生命周期钩子

## 当前兼容性限制

这是面向 MCU 的 MVP 实现，不是周期级精确模拟器：

- SM83 CPU 已覆盖所有常用指令路径，但少数边界时序尚未精确。
- PPU 使用扫描线渲染，Mode 3 时长按 `SCX&7 + 可见精灵数×6` 近似，不追求像素 FIFO 级精度。
- APU 默认关闭；开启后提供四声道近似立体声（方波×2、波形、LFSR 噪声），全定点计算，无浮点。
- 已支持 MBC3 RTC、HBlank 与通用型 HDMA、基础 OAM DMA 阻塞、基础内部时钟串口；红外、串口联机等高级功能暂未实现。通用型 HDMA 传输期间 CPU 被挡在总线之外，每块消耗普通模式 32 个 CPU T 周期，双速模式 64 个。
- 支持 ROM ONLY、MBC1、MBC2、MBC3、MBC5；其他 MBC 类型返回不支持错误。

### 低内存 MCU 集成

使用 `gbc_init_static()` / `gbc_deinit_static()` 管理调用方提供的 `gbc_t`。
原有 `gbc_load_rom()` 已直接引用 ROM，无须复制；ROM 缓冲区或 Flash/XIP 映射必须
保持到卸载。静态实例不能传给会释放实例的 `gbc_deinit()`。

`GBC_FRAMEBUFFER_ENABLE=0` 直接通过 `ppu.line` 逐行绘制，取消整屏/半屏缓存；
绘制回调须同步消费或复制像素。`GBC_ENABLE_CGB=0` 将 WRAM/VRAM 各裁剪到 8 KiB。
`GBC_USE_SRAM=0` 取消卡带 RAM 分配及 RAM/RTC 寄存器访问，依赖这些功能的游戏可能
无法运行。只关闭磁盘存档可用 `GBC_ENABLE_SRAM_SAVE=0`。默认端口配置支持 `-D` 覆盖。

在 `sdl/sdl2` 执行 `xmake build low_memory_test && xmake run low_memory_test`，
验证 RGB565 逐行绘制、DMG bank 边界、ROM 引用生命周期，以及关闭文件系统、声音和
SRAM 后的零实例/卡带/热路径分配。主机测试不代表真实单片机帧率或栈占用。
