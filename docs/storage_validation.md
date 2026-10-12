# 启动与存档验证记录

2026-10-12，主仓库 F:\code\codeup\gbc。主机为Windows x64 / MSVC，
资源编译使用Arm GNU 14.3、Cortex-M4 Thumb、`-Os -Wall -Wextra -Werror`。

## 主机回归

在 sdl/sdl2 执行 xmake build/run：

| 目标 | 覆盖 |
| --- | --- |
| core_test / sound_test | 原有CPU、MMU、PPU、timer、mapper及APU回归 |
| feature_test / feature_sound_test | RAM、RTC、即时存档；静音及音频重放 |
| feature_stream_test | 流式文件加载、路径自动绑定、电池RAM恢复及即时存档 |
| rtc_only_test / battery_only_test | RAM与RTC文件开关独立关闭 |
| boot_test | 三种镜像尺寸、地址空洞、FF50、KEY0/OPRI、兼容颜色、启动存档及镜像不匹配 |
| low_memory_test | 无文件系统、无SRAM、无音频、DMG扫描线配置 |
| low_memory_boot_test | 上述配置启用借用Flash镜像；强制启用文件功能仍不引入文件依赖 |
| sdl_storage_test | SDL dummy驱动下真实F5/F6/F9事件、恢复及正常退出 |
| rom_state_test / rom_state_stream_test | 外部ROM真实执行后的恢复重放 |

RAM用例包括MBC1/MBC2原始字节格式、显式路径、已有文件、缺失文件、长度错误、
读写失败、同步/关闭/替换失败、保存失败保留实例重试。
RTC用例覆盖秒/分/天进位、halt/carry、锁存、毫秒余量及模拟长时间关机计数不推进。
即时存档用例覆盖CRC损坏、错误ROM/配置、CRC正确但字段非法、写失败旧文件保留，
以及应用阶段读失败停止模拟且禁止写电池存档。

三款外部ROM：Super Mario Bros. Deluxe、Pocket Monsters Crystal (Japan)、
Dragon Quest Monsters Terry (Japan Rev1)。常驻/流式各运行600帧，保存后运行60帧，
恢复并重放相同输入，CPU、MMU、画面与音频哈希一致。
这未覆盖人工进入游戏后长期保存的游戏进度，也未证明所有游戏场景兼容。
600帧画面哈希分别为488EC2F5、1C82EF96、19AF96DC，常驻与流式一致，
与原有回归结果一致；哈希在重放前采集，避免把派生画面的660帧内容当作600帧结果。

## 资源测量

`python tests/measure_resources.py` 重现核心对象编译和 `-fstack-usage` 测量。
对象text包含只读数据但没有最终链接/裁剪，不能作为最终固件Flash占用。

| 配置 | ARM实例字节 | 核心对象text字节 |
| --- | ---: | ---: |
| 默认 | 143912 | 16484 |
| DMG低内存 | 18400 | 14036 |
| DMG低内存 + Boot | 18420 | 14690 |
| 即时存档，静音 | 143920 | 19721 |
| 四功能 + 音频 | 145056 | 24785 |
| 即时存档 + 2个流式缓存银行 | 176720 | 20165 |
| 仅RTC持久化，无即时存档 | 143912 | 15596 |
| 即时存档，无SRAM | 143656 | 17843 |

MSVC低内存实例仍为18432字节；启用Boot为18456字节，均0堆分配。
新增Boot字段在ARM为20字节，即时存档CRC/状态字段增加8字节；关闭功能后字段移除。
存档I/O缓冲区256字节，不分配完整状态副本；首次流式ROM CRC局部缓冲区256字节。
ARM编译器单函数栈帧：RAM保存552、RAM加载288、RTC576、即时保存624–632、
即时加载416、流式CRC288字节。栈帧不能当作总调用链峰值；首次流式保存核心已知
调用链约1KiB，另需文件系统/端口/中断栈余量。应在目标MCU测栈水位并调整线程栈。

无文件功能的核心对象不产生对应存档函数；运行热路径和保存测试未新增堆分配。
所有结果都是主机回归或ARM交叉编译，未进行真实MCU速度、Flash写入或掉电验收。
SDL快捷键及正常退出通过dummy驱动事件测试；错误弹窗的交互式重试UI和外部
完整Boot镜像保留人工验收。
