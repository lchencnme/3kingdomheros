#!/usr/bin/env python3
"""Summarise an autoplay run (tests/emu/autoplay.py) as a Markdown table.

usage: python3 tests/emu/autoplay_report.py build/autoplay_run1.json [out.md]
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KIND = {"ALL": "全灭", "DEFEAT": "击退", "SURVIVE": "坚守", "REACH": "护送/突围"}


def main():
    runs = json.load(open(sys.argv[1]))
    camp = json.load(open(ROOT / "content/campaign.json"))["battles"]
    rows = ["| # | 战名 | 类型 | 回合/上限 | 结果 | 尝试 | 撤退/出阵 | 败因 |", "|---|---|---|---|---|---|---|---|"]
    won = fast = slow = 0
    for r in runs:
        bid = camp[r["battle"]]["id"]
        bj = json.load(open(ROOT / f"content/battles/{bid}.json"))
        vk = bj["victory"].get("kind", "ALL")
        kind = KIND.get(vk, vk)
        limit = bj["turn_limit"]
        res = "胜" if r["won"] else "未胜"
        won += r["won"]
        if r["won"] and r["rounds"] <= 4:
            fast += 1
        if r["won"] and r["rounds"] >= limit - 2:
            slow += 1
        rows.append(f"| {r['battle']} | {r['title']} | {kind} | {r['rounds']}/{limit} | {res} | {r['tries']} | "
                    f"{len(r['retreated'])}/{r['deployed']} | {'' if r['won'] else r.get('loss', '')} |")
    head = [f"自动通关：{len(runs)} 战，机器人胜 {won} 战，{len(runs) - won} 战换遍全部打法都没打赢（之后强制过关继续往下测）。",
            f"4 回合内结束 {fast} 战；压线（距上限 2 回合内）{slow} 战。", ""]
    text = "\n".join(head + rows) + "\n"
    if len(sys.argv) > 2:
        Path(sys.argv[2]).write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
