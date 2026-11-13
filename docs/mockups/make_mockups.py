import runpy, sys, numpy as np
sys.path.insert(0,'/Users/leichen/Documents/3kindomheros/tools')
from png_shot import write_png
g=runpy.run_path('/Users/leichen/Documents/3kindomheros/tools/gen_portraits.py', run_name='x')
from pathlib import Path
w,h,rows=g['read_png'](Path('/tmp/ref/ref.png'))
img=np.array([list(r) for r in rows],dtype=np.float32).reshape(h,w,3)
L=img.mean(axis=2)/255.0
PAL=[(232,220,184),(176,164,136),(104,92,76),(32,26,20)]   # paper, light wash, mid ink, black ink
def down(region, ow, oh):
    y0,y1,x0,x1=region
    a=L[y0:y1,x0:x1]
    # area average into ow x oh
    ys=np.linspace(0,a.shape[0],oh+1).astype(int); xs=np.linspace(0,a.shape[1],ow+1).astype(int)
    o=np.zeros((oh,ow))
    for j in range(oh):
        for i in range(ow):
            blk=a[ys[j]:ys[j+1],xs[i]:xs[i+1]]
            # ink-preserving: mix mean with min so thin lines survive
            o[j,i]=0.55*blk.mean()+0.45*np.percentile(blk,20)
    return o
def quant(o, t=(0.30,0.55,0.78)):
    q=np.zeros(o.shape,dtype=int)
    q[o<t[2]]=1; q[o<t[1]]=2; q[o<t[0]]=3
    return q
def tiles(q):
    hh,ww=q.shape; s=set()
    for ty in range(0,hh-hh%8,8):
        for tx in range(0,ww-ww%8,8):
            s.add(q[ty:ty+8,tx:tx+8].tobytes())
    return len(s)
def save(q,name,scale=3):
    hh,ww=q.shape
    rows=[]
    for y in range(hh):
        r=bytearray()
        for x in range(ww): r+=bytes(PAL[q[y,x]])*scale
        for _ in range(scale): rows.append(r)
    write_png(f'/tmp/ref/out/{name}.png',ww*scale,hh*scale,rows)
    print(name, ww,'x',hh,'unique tiles',tiles(q))
    return q
# regions (y0,y1,x0,x1) in the 1024x559 reference
bust=(88,440,40,488)        # portrait frame interior
face=(88,330,150,400)       # head and beard
mapr=(0,480,516,1020)       # map panel
p64=save(quant(down(face,64,64)),'portrait64')
p96=save(quant(down(bust,112,88)),'portrait112x88')
pfull=save(quant(down(bust,144,112)),'card144x112')
m=save(quant(down((60,410,516,1020),160,112)),'map160x112')

# ---------------------------------------------------------------- full 160x144 screen mockups with the game font
fg=runpy.run_path('/Users/leichen/Documents/3kindomheros/tools/gen_font.py', run_name='x')
BDF=fg['BDF']
def text(scr, x, y, s, col=3):
    asc, gl = fg['load_bdf'](BDF, {ord(c) for c in s})
    for ch in s:
        gg = gl.get(ord(ch))
        if not gg: x += 6; continue
        rows = fg['raster'](gg, asc)
        wdt = 6 if gg['dw'] <= 6 else 12
        for r in range(12):
            for px in range(wdt):
                if rows[r] >> (15 - px) & 1 and 0 <= y + r < 144 and 0 <= x + px < 160:
                    scr[y + r, x + px] = col
        x += wdt
    return x
def frame(scr, x0, y0, x1, y1):
    scr[y0, x0:x1 + 1] = 3; scr[y1, x0:x1 + 1] = 3; scr[y0:y1 + 1, x0] = 3; scr[y0:y1 + 1, x1] = 3
def blit(scr, q, x, y):
    scr[y:y + q.shape[0], x:x + q.shape[1]] = q
def screen(name, scr):
    save(scr, name, 3)

# A: character card (portrait screen)
A = np.zeros((144, 160), dtype=int)
text(A, 26, 1, "【关羽·汉寿亭侯】")
frame(A, 6, 14, 153, 129); frame(A, 7, 15, 152, 128)
blit(A, pfull, 8, 16)
text(A, 4, 131, "兵力980 武力105 统率92")
screen('screen_card', A)
# B: dialogue (current engine layout: 64x64 portrait, name, two lines)
B = np.zeros((144, 160), dtype=int)
blit(B, quant(down((60,410,516,1020),160,112)), 0, 0)
B[72:144, :] = 0
frame(B, 0, 72, 159, 143)
blit(B, p64, 4, 4 + 0)
frame(B, 3, 3, 68, 68)
text(B, 6, 76, "关羽")
text(B, 6, 92, "吾观颜良，如插标卖首耳！")
text(B, 6, 108, "兄长且看关某取他首级。")
screen('screen_dialog', B)
# C: battle map + HUD (current engine layout: 160x112 view, 2 HUD lines)
C = np.zeros((144, 160), dtype=int)
blit(C, m, 0, 0)
text(C, 2, 114, "关羽 骁骑 Lv12")
text(C, 2, 128, "兵力 980/980  回合15")
screen('screen_map', C)
