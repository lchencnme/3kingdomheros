# 《三国豪杰传》GBC

设计契约：[三国豪杰传_GBC_最终设计文档_v2.0.md](三国豪杰传_GBC_最终设计文档_v2.0.md)

## 构建与测试

```bash
./build.sh
```

依次执行：
1. `tools/gen_rules.py`：把 `content/default_rules.json` 生成为 `src/gen/rules_data.{h,c}`（数值唯一来源）。
2. `tools/gen_tech01.py`：生成 TECH01 占位素材，以及 `content/battles/BT_TECH01.json`（schema v2）。
3. `tools/ref_model.py`：独立 Python 参考模型，生成随机交叉对照用例。
4. 主机测试 `tests/host/test_core.c`：附录D 向量加交叉对照。
5. GBDK 4.5.0 构建 `build/gb/sanguo_hjz.gbc`（8 MiB，CGB 专用，MBC5+RAM+电池）。
6. PyBoy 端到端测试 `tests/emu/test_rom.py`，截图输出到 `build/gb/`。

工具链：GBDK-2020 4.5.0 位于 `tools/gbdk`（可用 `GBDK_LCC` 环境变量覆盖）；Python 3 加 PyBoy 2.8.1（只有模拟器测试需要）。

## 目录

| 路径 | 内容 |
|---|---|
| `src/core/` | 可移植规则核心（rng、rules、path/视线）；主机与 GB 共用 |
| `src/gb/` | GB 平台层：banked WRAM 访问器、输入、地图视图、主循环 |
| `src/gen/` | 生成代码（不要手改） |
| `content/` | 规则参数与关卡数据（JSON） |
| `tools/` | 生成器与参考模型 |
| `tests/host`、`tests/emu` | 主机测试、模拟器测试 |

## 操作（TECH01）

方向键移动光标（镜头自动跟随）；A 选己方单位，显示移动范围，再按 A 在合法终点提交移动；B 取消；START 暂代"回合结束"，重置已行动标记。
