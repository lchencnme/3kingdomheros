# 许可台账（设计文档 1.3 / 10.4）

| 组件 | 版本 | 用途 | 许可 | 来源 |
|---|---|---|---|---|
| Fusion Pixel Font 12px Monospaced (zh_Hans, BDF) | 2026.10.25 | 游戏内 12×12 中文字形 | SIL OFL 1.1（`fonts/licenses/OFL-fusion-pixel-font.txt`） | https://github.com/TakWolf/fusion-pixel-font |
| ├ Ark Pixel Font（组成部分） | — | 主体字形 | SIL OFL 1.1（`fonts/licenses/OFL-ark-pixel-font.txt`） | https://github.com/TakWolf/ark-pixel-font |
| ├ Cubic 11（组成部分） | — | 补字 | SIL OFL 1.1（`fonts/licenses/OFL-cubic-11.txt`） | 见许可文件 |
| └ Galmuri（组成部分） | — | 补字 | SIL OFL 1.1（`fonts/licenses/OFL-galmuri.txt`） | https://github.com/quiple/galmuri |
| GBDK-2020 | 4.5.0 | 编译工具链（不随仓库分发，`tools/get_gbdk.sh` 拉取） | GBDK-2020 许可（见其发行包） | https://github.com/gbdk-2020/gbdk-2020 |

字体从 Ark Pixel 换成 Fusion Pixel 的原因：Ark Pixel 12px 的 zh_hans 版缺"施""滚""雍"等常用字（构建时因缺字报错）；Fusion Pixel 用同风格的字体补全了这些缺字，字体度量相同。
