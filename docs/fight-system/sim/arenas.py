#!/usr/bin/env python3
"""Plan step 12's simulator pass (docs/fight-system.md 3.1): every arena keeps the archetypes balanced. For
each arena (and the night rule), the archetype-vs-archetype matrix with both sides playing `reader`: no honest archetype
may exceed 60% against the field, and mono must stay the trap.
  python3 arenas.py [n]   n bouts per cell (default 40)"""

import random
import sys

import fightsim as F

NAMES = ["HUMMING LIGHTS", "DAMP CARPET", "STILL AIR", "FLICKER", "NARROW", "ECHO HALL", "EXIT SIGN", "EMPTY", "NIGHT"]


def field(n, rules):
    saved = dict(F.R)
    F.R.update(rules)
    try:
        res = {}
        for x in F.ARCHES:
            s = 0
            m = 0
            for y in F.ARCHES:
                for k in range(n):
                    seed = 50000 + k * 7919 + F.ARCHES.index(x) * 131 + F.ARCHES.index(y) * 17
                    rng = random.Random(seed ^ 0xBEEF)
                    la = F.build(x, rng)
                    lb = F.build(y, rng)
                    w, t, _, _ = F.bout(seed, "reader", "reader", la, lb)
                    s += (w == 1) + (w == 0) / 2
                    m += 1
            res[x] = s / m
        return res
    finally:
        F.R.clear()
        F.R.update(saved)


if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    print("| arena | " + " | ".join(F.ARCHES) + " | max honest |")
    print("|" + "---|" * (len(F.ARCHES) + 2))
    worst = 0
    for i, name in enumerate(NAMES):
        rules = {"NIGHT": 1} if name == "NIGHT" else {"ARENA": i}
        r = field(n, rules)
        hon = max(v for k, v in r.items() if k != "mono")
        worst = max(worst, hon)
        print(f"| {name} | " + " | ".join("%.0f%%" % (100 * r[a]) for a in F.ARCHES) + " | %.0f%% |" % (100 * hon))
    print(f"\nworst honest archetype in any arena: {100 * worst:.0f}%  ({'ok' if worst <= 0.6 else 'OVER 60%'})")
