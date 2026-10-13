# 已保留优化审核记录（2026-10-13）

本次核对代码、既有主机/固件/板测证据，并复跑源仓库测试。审核代码为 `4f0b68bfcf120e543f856ebcda61512c1697a156`；相对 `8a1cfd3`，运行代码、公开头文件、默认配置和 fast_run 文档没有净差异，仅多出寄存器传送回归测试。工作区在审核开始时干净。已保留优化此前已分别提交，本轮补充审核记录，不重复提交相同实现，不改写实验历史、不推送。

## 保留范围与结论

| 提交 | 内容 | 审核结论 |
|---|---|---|
| 3a73212 / 7049554 | resident ROM 区域映射、FAST 指令取数直接访问映射 | 保留；DMA、Boot、RAM 执行、流式回退及映射重建由回归覆盖 |
| 95dba8d / e418a8c / 1664ed8 | 可选 FAST 行事件调度、周期预算、HALT/APU 事件推进、预算边界 | 保留；核心默认关闭 FAST，参考路径保留，指令越界周期不能丢弃 |
| 2124e22 / 7c0f482 / ed4e933 | 图块行缓存、固定八像素展开、调色板写时更新、参考路径去掉 FAST 缓存分支 | 保留；像素/优先级/窗口、dirty 及重建回归覆盖，完整帧默认不变 |
| bcb6011 / dc68091 / 0bf0f4f | 有界音频批量结算、行精灵选择复用、LY 改写重选修复 | 保留；寄存器访问同步、OAM 顺序和恢复边界保持；LY 修复属于正确性要求 |
| d432871 | PPU 未到事件时内联累加 | 保留现有音频供给改进；与原事件入口随机状态/像素比较通过，不声称独立达到 +5% FPS |
| 2c34e30 | APU sequencer 边沿前先结算此前时钟 | 保留正确性修复；避免段末 sweep/length 倒用于段中；逐时钟四声道相位/状态/PCM 对照通过 |
| 8a1cfd3 | 空闲 DMA/serial 跳过，关闭且不待重载的 Timer 内联推进 DIV | 保留等价优化及板端音频收益；有活动事件继续原路径，零周期不改状态 |
| 0362475 | 63 个 LD r,r 编码 × 256 数据的完整 CPU/MMU 回归 | 保留；覆盖寄存器访问及 (HL)，排除 HALT 编码；不是性能优化收益 |

审核未发现要求撤回这些保留实现的新阻塞项。这里的保留结论不等于任意 ROM 兼容、真实硬件周期精度或声音验收通过。较早 FAST 与参考的行内图像/音频近似继续受 docs/fast_run.md 中约定约束。

## 不保留的候选

- da4ce00：寄存器 offsetof 查表。一对目标板预筛运行 FPS 约 +0.42%，补零减少约 1.72%；99bfb25 已撤回实现。
- 4900791：CPU 指令实现内联至批次；一对预筛约 +1.65%，不足独立 +5% 门槛，增加 ARM 文本约 15 KiB；4f0b68b 已撤回。
- 131a862：精灵行/调色板复用，与 CPU 批次叠加的一对预筛约 +1.40%，补零减少约 9.70%，仍约 52.88 模拟 FPS；1ee43ce 已撤回。它与 CPU 批次不是独立三对性能验收。

这些实验提交及回滚保留在 Git 历史用于追溯，当前 src/inc/port 没有上述候选的净改动。没有用主机吞吐代替板端收益，也没有把小于门槛的候选包装成独立提速成果。

## 本轮实际复验

在 sdl/sdl2 下 xmake 构建通过，以下 11 个目标均通过：

- core_test、sound_test、low_memory_test；
- fast_run_test、fast_run_rgb565_test、fast_run_rgb565_swap_test；
- feature_test、feature_stream_test、feature_fast_test、feature_fast_stream_test；
- sdl_storage_test。

FAST 测试包含随机行/优先级对照、PPU 内联事件对照、Timer/DMA/serial 完整状态差分、APU 事件相位、预算/HALT/双速和寄存器传送覆盖。Boot、流式、状态/持久化与低内存分别由相应目标覆盖。git diff --check 通过。

固件导入 SOURCE.json 的 48 个来源 SHA-256 与上述本地提交逐文件一致。机器记录与测试日志位于配套 f1c_rtthread/tools/host_test/results/gbc-core-review-20261013.json 及 gbc-core-review-*-20261013.log。此次仅源仓库复验与记录，不重新运行设备性能窗口，也不重新构建/烧录 NOR。

## 既有板端证据与限制

当前 8a1cfd3 对 APU 修复后基线的三对目标窗口：运行模拟 FPS 中位 50.2331 -> 51.8617（+3.24%），实际插入零 9349.22 -> 7640.00 ms（减少 18.28%）。窗口为 >=112 秒核心 PCM 预热后的 60 秒测量，包含双人及随后演示。正式比较关闭探针，完整模拟帧和呈现帧分别记录。

8a1cfd3 的独立 FPS 提升低于原 +5% 门槛；按后续减少声音欠载的目标保留其等价实现和音频供给收益，不列为满足 +5% 的独立速度成果。GB/MBC3 三对保护窗口的运行 FPS 变化约 +0.027% / -0.019%，实际补零仍非零。d432871 同样不能被宣传为独立 +5% 提速成果。

10 ROM 完整 CPU/PPU/PCM 及四分钟两模式图像/PCM 回放、固件默认/FAST 关闭构建、分区检查和 NOR 回读属于此前的验证，记录见配套固件仓库：

- tools/host_test/results/gbc-apu-sequencer-final-seal.json；
- tools/host_test/results/gbc-perf-20261012/idle-two-player-summary.json；
- tools/host_test/results/gbc-perf-20261012/idle-guards-summary.json；
- tools/host_test/results/gbc-audio-runtime-investigation-20261013.json。

当前 NOR 应用 SHA-256 为 7d29527fc8ea9976d5c0097a8cdf48701f780bcb05aca785247348da8d0961d7。用户确认画面正常，但双人演示仍有明显重音；连续主机 PCM 正常。声音问题未解决，不能将构建/测试/NOR 回读 PASS 当作听感验收通过。

## 本次提交边界

本轮只提交本审核记录。运行实现此前已在本地提交并同步至固件导入；没有未提交的新优化需要重复导入。固件中的 Java、文件系统等混合修改不提交。未实现去直流、时间拉伸、重采样或新的跳绘策略，不改变原始模拟帧率目标与完整帧默认配置。
