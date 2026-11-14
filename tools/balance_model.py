#!/usr/bin/env python3
"""Class matchup table from content/default_rules.json (anonymous units, morale 100, plain): damage per hit,
hits to kill, and whether the defender can strike back. Run after changing class numbers."""
import json
from pathlib import Path
R = json.loads((Path(__file__).resolve().parent.parent / "content/default_rules.json").read_text(encoding="utf-8"))
def tp(r,a,d):
    if [a,d] in r['type_advantage']: return r['type_percent']['advantage']
    if [d,a] in r['type_advantage']: return r['type_percent']['disadvantage']
    return 100
def stats(c,L,base=(70,60,70)):
    lv=L-1; s,i,cm=base
    return dict(hp=c['hp']+12*lv+2*cm, atk=c['atk']+2*lv+s//2, df=c['def']+2*lv+cm//2)
def dmg(r,a,sa,d,sd,hp_frac=1.0):
    P=sa['atk']*(50+int(50*hp_frac))//100
    Rv=max(2*P-sd['df'], P//5, 1)
    Rv=Rv*tp(r,a['family'],d['family'])//100
    return Rv*125//100
def report(r, ids, L, title):
    cs={c['id']:c for c in r['classes']}
    print(f"== {title} (Lv{L}) : rows attack columns; cell = damage/hits-to-kill, *=no counter back")
    print("        "+"".join(f"{cs[i]['name'][:3]:>9}" for i in ids))
    for a in ids:
        row=f"{cs[a]['name'][:3]:<6}"
        for d in ids:
            A,D=cs[a],cs[d]; sa,sd=stats(A,L),stats(D,L)
            x=dmg(r,A,sa,D,sd)
            rng=A['range'][1]
            free = (not D['counter']) or not (D['range'][0]<=rng<=D['range'][1])
            htk=-(-sd['hp']//x)
            row+=f"{x:>5}/{htk}{'*' if free else ' '} "
        st=stats(cs[a],L)
        print(row, f" hp{st['hp']} atk{st['atk']} def{st['df']} mv{cs[a]['move']} r{cs[a]['range']}")
if __name__=="__main__":
    report(R,[0,3,6,14,15,9],3,"tier 1")
    report(R,[1,4,7,14,15,10],14,"tier 2")
    report(R,[2,5,8,14,15,11],26,"tier 3")
