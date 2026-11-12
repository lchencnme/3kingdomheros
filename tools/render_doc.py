#!/usr/bin/env python3
"""Design doc markdown -> print HTML (then Chrome --print-to-pdf). Usage: render_doc.py doc.md out.html"""
import sys, re
from markdown_it import MarkdownIt
src, out = sys.argv[1], sys.argv[2]
md = MarkdownIt('commonmark', {'html': False}).enable('table')
text = open(src, encoding='utf-8').read()
body = md.render(text)
# add ids to h2/h3 for anchors
css = """
@page { size: A4; margin: 16mm 14mm 16mm 14mm; }
body { font-family: "PingFang SC","Hiragino Sans GB","Songti SC",sans-serif; font-size: 10pt; line-height: 1.55; color:#222; }
h1 { font-size: 20pt; border-bottom: 3px double #8a2b1d; padding-bottom: 6px; color:#5a1a10; }
h2 { font-size: 15pt; color:#5a1a10; border-bottom: 1px solid #c9b79c; margin-top: 26px; page-break-before: always; }
h1 + blockquote + hr + h2, h2:first-of-type { page-break-before: auto; }
h3 { font-size: 12pt; color:#2d4a5a; margin-top: 18px; }
table { border-collapse: collapse; width: 100%; margin: 8px 0; font-size: 8.6pt; page-break-inside: auto; }
tr { page-break-inside: avoid; }
th, td { border: 1px solid #c9b79c; padding: 3px 5px; vertical-align: top; }
th { background: #f3ead9; }
code { font-family: Menlo, monospace; font-size: 8.6pt; background:#f6f2ea; padding: 0 2px; }
pre { background:#f6f2ea; border:1px solid #e0d4bf; padding: 8px; font-size: 8pt; white-space: pre-wrap; word-break: break-all; }
pre code { background: none; }
blockquote { border-left: 4px solid #8a2b1d; margin: 8px 0; padding: 4px 10px; background:#faf6ee; color:#444; }
hr { border: none; border-top: 1px solid #ddd; }
"""
html = f"<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'><title>绣像三国 GBC 最终设计文档 v2.0</title><style>{css}</style></head><body>{body}</body></html>"
open(out, 'w', encoding='utf-8').write(html)
print('ok', len(html))
