# 开发日志

按时间顺序记录自主开发过程中的决定、发现的问题和修复。每条都对应设计文档中的某一节。

## 2026-11-08

### 基线（main 分支）
- 规则核心 + 寻路 + 地图滚动 + 单位选择/移动预览。主机 86,228 项检查、PyBoy 20 项全部通过。

### 战斗结算与 AI（dev 分支）
- `src/core/battle.c`：40B 单位记录、攻击/反击/经验/升级、阶段开始/结束、胜败判定。
- `src/core/ai.c`：逐终点候选、分层评分、确定性决胜。
- **发现**：按设计文档原权重，"−40×危险"压过"+20×接近"，双方 AI 在威胁圈外永久对峙（自对弈 30 回合 0 次攻击）。
  **修复**：AGGRESSIVE 只在致死格计危险；其他模式不变。已写回文档 8.3。

### HUD 中文
- 字体：Ark Pixel 12px Monospaced zh_Hans（OFL 1.1），仓库只收 zh_hans BDF 与许可证，登记在 `third_party/LICENSES.md`。
- `tools/gen_font.py`：只收录实际用到的字；glyph ID 写入 `content/text/glyph_registry.json`，只追加不重排；缺字构建失败。

### 代码分银行（文档 11.3 落地）
- 未分银行的代码与数据超过 32 KiB 平铺区。
- 资源（图块、地形、字形、文本池）放进 ROM bank 2，只通过 bank 0 的 NONBANKED 网关 `src/gb/rom.c` 访问。
- 规则核心放进代码 bank 3，公共接口用 `CORE_API`（GB 上为 `BANKED`，主机上为空）。

### HUD 性能与 VBlank 纪律
- **发现**：HUD 同步刷新约 5 帧（光栅化 2 帧 + `set_bkg_data` 上传 2 帧 + 写 map 1 帧），阻塞输入。
  **修复**：改为非阻塞流水线：可见期每帧光栅化一行 → VBlank 内 GDMA 576B → 准备 window map 编号 → VBlank 内 GDMA 128B 翻页。光栅化的移位按 0/2/4/6 四种常量情况展开。
- **发现**：主循环某帧超时后，`wait_vbl_done` 会在帧中途返回，可见期写入的 VRAM 被丢弃，表现为 window map 第 0 行只写进 11 格，HUD 显示花字。
  **修复**：`mapview_commit` 与 `hud_commit` 都加 LY 门（LY 不在 144..150 就整帧跳过，挂起的内容保留到下一帧）；window map 翻页改用 GDMA。
