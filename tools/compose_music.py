#!/usr/bin/env python3
"""Writes the scores of the event / scene themes into content/music/*.json (original melodies in the five
pentatonic modes 宫商角徵羽; run by hand when a theme changes - the build reads the JSON files).

Only the melody is written out per bar. The accompaniment follows from the bar's root note: a plucked
root / fifth / octave figure (no thirds, so the mode stays open, as in 民乐 accompaniment), a wave bass, and a
drum pattern chosen by the piece's character.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SEMI = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
NAME = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def tr(root, octave, semis):
    v = SEMI[root[0]] + (1 if root.endswith("#") else 0) + semis
    return f"{NAME[v % 12]}{octave + v // 12}"


STYLES = {
    # pluck figure (ticks sum 96), bass figure, drums
    "slow": (lambda r: f"-:24 {tr(r, 3, 0)}:12 -:12 {tr(r, 3, 7)}:12 -:12 {tr(r, 3, 12)}:12 -:12",
             lambda r: f"{tr(r, 2, 0)}:48 {tr(r, 2, 7)}:48", "K1:96"),
    "walk": (lambda r: f"-:12 {tr(r, 3, 0)}:6 -:18 {tr(r, 3, 7)}:6 -:18 {tr(r, 3, 12)}:6 -:18 {tr(r, 3, 7)}:6 -:6",
             lambda r: f"{tr(r, 2, 0)}:24 {tr(r, 2, 7)}:24 {tr(r, 2, 12)}:24 {tr(r, 2, 7)}:24", "K3:48 K2:48"),
    "drive": (lambda r: " ".join(f"{tr(r, 3, s)}:6 -:6" for s in (0, 7, 12, 7, 0, 7, 12, 7)),
              lambda r: " ".join(f"{tr(r, 2, s)}:12" for s in (0, 0, 7, 0, 0, 0, 7, 12)), "K3:24 K2:24 K3:12 K3:12 K2:24"),
    "gallop": (lambda r: " ".join(f"{tr(r, 3, s)}:6 -:6" for s in (0, 12, 7, 12, 0, 12, 7, 12)),
               lambda r: " ".join(f"{tr(r, 2, s)}:12" for s in (0, 7, 0, 7, 0, 7, 0, 12)), "K3:12 K2:12 K2:12 K3:12 K2:12 K2:12 K3:12 K2:12"),
}
INST = {"lead": {"duty": 1, "volume": 10, "env_down": 0, "gate": 0.85}, "pluck": {"duty": 0, "volume": 6, "env_down": 2, "gate": 1.0},
        "bass": {"wave": "soft", "volume": 2, "gate": 0.9}, "drum": {"noise": 85, "env_down": 1, "gate": 1.0}}


def song(fname, sid, name, comment, bpm, style, bars, roots, lead=None):
    assert len(bars) == len(roots), (sid, len(bars), len(roots))
    pl, bs, dr = STYLES[style]
    inst = json.loads(json.dumps(INST))
    if lead:
        inst["lead"].update(lead)
    s = {"_comment": comment, "id": sid, "name": name, "bpm": bpm, "ticks_per_bar": 96,
         "channels": {"ch1": {"inst": "lead", "notes": " | ".join(bars)},
                      "ch2": {"inst": "pluck", "notes": " | ".join(pl(r) for r in roots)},
                      "ch3": {"inst": "bass", "notes": " | ".join(bs(r) for r in roots)},
                      "ch4": {"inst": "drum", "notes": " | ".join(dr for _ in roots)}},
         "instruments": inst}
    (ROOT / "content/music" / fname).write_text(json.dumps(s, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")


A1 = "D4:24 G4:24 A4:24 B4:24"
song("title_taoyuan.json", "SONG_TITLE", "桃园", "封面曲《桃园》（原创）：G 宫五声，四分音符=72，16 小节，舒缓。", 72, "slow",
     [A1, "D5:48 B4:24 A4:24", "G4:24 A4:12 B4:12 A4:24 G4:24", "E4:48 D4:48",
      A1, "D5:36 E5:12 D5:24 B4:24", "A4:24 B4:12 A4:12 G4:24 E4:24", "G4:96",
      "B4:24 D5:24 E5:24 D5:24", "B4:48 A4:24 G4:24", "E5:24 D5:24 B4:24 A4:24", "B4:72 -:24",
      A1, "D5:48 E5:24 D5:24", "B4:24 A4:24 G4:24 E4:24", "G4:96"],
     ["G", "G", "E", "D", "G", "G", "E", "G", "E", "G", "C", "D", "G", "G", "E", "G"], lead={"volume": 9})
B1, B2 = "E4:12 E4:12 G4:12 A4:12 B4:24 A4:12 G4:12", "E4:12 E4:12 G4:12 A4:12 B4:24 D5:24"
song("boss_aozhan.json", "SONG_BOSS", "鏖战", "大战曲《鏖战》（原创）：E 羽五声，四分音符=138，16 小节，急促。用于虎牢关、赤壁、定军山等大战。", 138, "drive",
     [B1, B2, "E5:24 D5:12 B4:12 A4:12 G4:12 A4:24", "B4:48 -:24 B4:24",
      "E5:12 E5:12 D5:12 B4:12 A4:24 G4:12 A4:12", "B4:12 A4:12 G4:12 E4:12 G4:24 A4:24", "B4:24 D5:24 E5:24 G5:24", "E5:72 -:24",
      "G5:24 E5:12 D5:12 E5:24 B4:24", "D5:24 B4:12 A4:12 B4:24 G4:24", "A4:12 B4:12 D5:12 E5:12 G5:24 E5:24", "D5:48 B4:48",
      B1, B2, "E5:24 G5:24 E5:24 D5:24", "E5:96"],
     ["E", "E", "G", "D", "E", "G", "D", "E", "G", "D", "E", "D", "E", "E", "D", "E"])
T1, T3 = "A4:12 -:12 A4:12 C5:12 D5:24 C5:24", "A4:12 -:12 A4:12 C5:12 D5:24 E5:24"
T2, T4 = "A4:12 -:12 A4:12 G4:12 E4:48", "D5:24 C5:24 A4:48"
song("tense_weiju.json", "SONG_TENSE", "危局", "危局曲《危局》（原创）：A 羽五声，四分音符=120，16 小节，切分节奏。用于突围、护送、坚守。", 120, "gallop",
     [T1, T2, T3, T4, "E5:12 -:12 E5:12 D5:12 C5:24 D5:24", "E5:12 -:12 E5:12 G5:12 E5:48", "D5:24 C5:12 A4:12 G4:24 A4:24", "A4:72 -:24",
      T1, T2, T3, T4, "C5:24 D5:24 E5:24 G5:24", "A5:48 G5:24 E5:24", "D5:24 C5:24 A4:24 G4:24", "A4:96"],
     ["A", "A", "A", "D", "C", "A", "G", "A", "A", "A", "A", "D", "C", "A", "G", "A"])
song("sad_beige.json", "SONG_SAD", "悲歌", "悲歌《悲歌》（原创）：D 羽五声，四分音符=58，8 小节，缓慢。用于落凤坡、麦城、白帝城。", 58, "slow",
     ["A4:48 G4:24 F4:24", "D4:72 -:24", "F4:24 G4:24 A4:48", "C5:48 A4:48", "D5:48 C5:24 A4:24", "G4:48 F4:48", "G4:24 A4:24 F4:24 D4:24", "D4:96"],
     ["D", "D", "F", "C", "D", "G", "D", "D"], lead={"volume": 8, "duty": 2})
song("victory_kaixuan.json", "SONG_VICTORY", "凯旋", "胜利曲《凯旋》（原创）：C 宫五声，四分音符=126，8 小节，明亮。", 126, "walk",
     ["G4:12 G4:12 A4:24 C5:24 D5:24", "E5:48 D5:24 C5:24", "D5:12 E5:12 G5:24 E5:24 D5:24", "C5:72 -:24",
      "A4:12 C5:12 D5:24 E5:24 G5:24", "A5:48 G5:24 E5:24", "D5:24 E5:24 D5:24 C5:24", "C5:96"],
     ["C", "C", "G", "C", "A", "C", "G", "C"])
song("defeat_baizou.json", "SONG_DEFEAT", "败走", "败北曲《败走》（原创）：A 羽五声，四分音符=66，8 小节，下行。", 66, "slow",
     ["E5:48 D5:24 C5:24", "A4:96", "C5:48 A4:24 G4:24", "E4:96", "G4:24 A4:24 C5:48", "D5:48 C5:24 A4:24", "G4:48 E4:48", "A4:96"],
     ["A", "A", "C", "E", "G", "D", "E", "A"], lead={"volume": 8, "duty": 2})
D1 = "D5:12 D5:12 A4:12 D5:12 F5:12 D5:12 A4:12 D5:12"
song("duel_dantiao.json", "SONG_DUEL", "单挑", "单挑曲《单挑》（原创）：D 羽五声，四分音符=160，8 小节，快速。", 160, "gallop",
     [D1, "C5:12 C5:12 G4:12 C5:12 F5:12 C5:12 G4:12 C5:12", D1, "A5:24 G5:24 F5:24 D5:24",
      "F5:12 G5:12 A5:24 G5:12 F5:12 D5:24", "C5:12 D5:12 F5:24 D5:12 C5:12 A4:24", "D5:12 F5:12 G5:12 A5:12 G5:12 F5:12 D5:12 C5:12", "D5:72 -:24"],
     ["D", "C", "D", "D", "F", "C", "D", "D"])
M1 = "A4:24 D5:24 E5:24 F#5:24"
song("march_changqu.json", "SONG_MARCH", "长驱", "行军曲《长驱》（原创）：D 宫五声，四分音符=108，16 小节，昂扬。用于第四章以后的普通战。", 108, "walk",
     [M1, "A5:48 F#5:24 E5:24", "D5:24 E5:12 F#5:12 E5:24 D5:24", "B4:48 A4:48",
      M1, "A5:36 B5:12 A5:24 F#5:24", "E5:24 F#5:12 E5:12 D5:24 B4:24", "D5:96",
      "F#5:24 A5:24 B5:24 A5:24", "F#5:48 E5:24 D5:24", "B5:24 A5:24 F#5:24 E5:24", "F#5:72 -:24",
      M1, "A5:48 B5:24 A5:24", "F#5:24 E5:24 D5:24 B4:24", "D5:96"],
     ["D", "D", "G", "A", "D", "D", "G", "D", "B", "D", "G", "A", "D", "D", "A", "D"])
print("compose_music: 8 themes written")
