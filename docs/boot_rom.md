# 可选 Boot ROM

核心默认 `GBC_ENABLE_BOOT_ROM=0`；SDL 配置启用接口，但没有 `--boot-rom`
时仍快速启动，不读取或分配启动镜像。

```c
gbc_init_static(&context);
gbc_boot_set_rom(&context, image, size, GBC_MODEL_CGB);
gbc_load_rom(&context, cartridge, cartridge_size);
```

接口必须在加载卡带前调用，返回错误码。镜像只借用，不能在绑定期间修改或释放；
MCU 可以直接引用 Flash。卸载卡带不会解绑镜像；无卡带时用 NULL/0 解绑。
DMG 接受256字节；CGB接受2048字节紧凑格式或2304字节地址布局格式。
紧凑镜像的第256字节对应地址0200，地址0100–01FF始终读取卡带。
核心不内置镜像，测试只包含项目自行编写的最小启动指令。

上电路径从PC=0、LCD/APU关闭开始，采用确定的内存初值；不声称复现硬件未定义值。
FF50写非零解除映射，写零不能重新映射，重新加载/重置卡带可重新启动。
CGB启动时硬件型号与游戏模式独立，解除映射时按KEY0位2锁定DMG兼容模式；
OPRI选择对象优先级，启动后锁定。兼容模式使用启动镜像写入的BG/OBJ颜色，
并按BGP/OBP选择色阶。PGB、PSM NMI及重新映射行为不在实现范围。

即时存档包含型号、映射、KEY0、OPRI和兼容模式；启动中的存档必须绑定匹配镜像。
镜像CRC只计算映射字节，紧凑和地址布局镜像可互换。解除映射后的存档无需镜像。
功能开关不同的即时存档不互通；电池RAM格式不受这些开关影响。

SDL运行：`gbc.exe --boot-rom boot.bin game.gbc`。F5保存即时存档，F9恢复，
F6保存RAM/RTC。外部完整DMG/CGB镜像仍需调用方提供并人工验证，仓库不包含这些镜像。

跳过启动时补齐SC/TAC未使用位及HDMA寄存器初值，保留现有快速CPU初始化。
DIV相位和CGB未定义初值仍采用近似确定值，未进行像素FIFO或全周期重构。
参考：[启动序列](https://gbdev.io/pandocs/Power_Up_Sequence.html)、
[CGB寄存器](https://gbdev.io/pandocs/CGB_Registers.html)。
