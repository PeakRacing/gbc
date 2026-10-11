# AGENTS.md

本仓库用于实现面向低内存 MCU 的 GB/GBC 模拟器。所有后续代理和贡献者都应优先参考 `F:\code\codeup\nes_github` 的工程结构、xmake 构建方式、C 代码风格和可裁剪配置思路。

## 项目目标

- 同时支持 GB(DMG) 与 GBC(CGB) 卡带。
- 默认跳过 Boot ROM，按卡带类型进入 DMG 或 CGB 的 post-boot 初始状态。
- 首版优先实现可运行、可移植、可裁剪的核心；允许适当牺牲周期级精度，换取低资源 MCU 上更流畅的运行。
- 音频默认关闭；启用时必须使用整数/定点计算，不允许在核心中使用浮点运算。

## 目录和构建

- `inc/`：公开头文件和核心数据结构。
- `src/`：平台无关的模拟器核心实现。
- `port/`：默认配置和弱平台函数，供 MCU/RTOS/裸机替换。
- `sdl/sdl2/`：SDL2 桌面验证端口，使用 xmake 构建。
- 构建示例应保持与 NES 项目类似：C11、`add_requires("libsdl2")`、核心不依赖 SDL。

## 代码风格

- 使用 C11 和固定宽度整数类型，例如 `uint8_t`、`uint16_t`、`uint32_t`。
- 头文件使用 `#pragma once`，并保持 C++ 兼容的 `extern "C"`。
- 硬件寄存器、状态位和 bank 切换逻辑尽量用小结构体、宏和 `static` 辅助函数表达。
- 平台相关功能必须通过 `gbc_malloc/free`、`gbc_fopen/fread/fseek`、`gbc_draw`、`gbc_log_printf` 等 port 函数访问。
- 不要在核心 `src/` 中直接依赖 SDL、操作系统窗口、线程或非标准库。
- 错误必须通过明确返回值和日志暴露，不要静默失败。
- 不做与当前任务无关的大规模重构。

## MCU 裁剪要求

- 保留 `GBC_RAM_LACK`，用于整屏缓存和半屏/块刷新模式切换。
- 保留 `GBC_ROM_STREAM` 与 `GBC_ROM_CACHE_BANKS`，用于文件系统流式 ROM 与 LRU bank 缓存。
- 保留 `GBC_FRAME_SKIP`、`GBC_COLOR_DEPTH`、`GBC_COLOR_SWAP`、`GBC_ENABLE_SOUND`、`GBC_ENABLE_DMG`、`GBC_ENABLE_CGB` 等宏。
- 保留 `GBC_ENABLE_SRAM_SAVE`：默认跟随 `GBC_USE_FS`（有文件系统时自动开启），MCU 无需存档时可设为 `0` 节省代码空间。
- 默认配置应方便 PC/SDL 调试；MCU 通过宏裁剪 RAM、ROM 缓存、颜色深度和音频。

## 验证要求

- 修改核心后，优先在 `sdl/sdl2` 下运行 `xmake` 验证构建。
- README、配置宏和公开 API 变更必须同步更新文档。
- 当前实现偏 MVP，若发现不支持的 opcode、MBC 或 PPU 边界行为，应明确记录限制，而不是假装完整兼容。
