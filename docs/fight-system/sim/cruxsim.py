#!/usr/bin/env python3
"""Balance of THE CRUCIBLE on the live slice (crux.py's exact rules). Versus: four policies, knowledge, bags, shelves,
the opener. Bosses: first-try win rate by tier and faction for typical players.
  random   any legal move it knows of (and now and then a blind try onto theirs)
  greedy   the cartridge AI at skill 0: the biggest thing now
  baiter   the cartridge AI at skill 256: never leaves something they can take, sets traps
  reader   three plies (its move, their best known answer, its best follow-up): a strong human
Usage: python3 cruxsim.py [N] [versus,boss]   (JOBS=8 processes)"""

import collections
import os
import random
import sys
from multiprocessing import Pool

import crux as X
from crux import CAT, DEP, FOUR, NONE, TR, Side, State, stars

K = {"know": 154}  # 0.6 of 256


def mine(st, k, seed, know):
    return [w for w in st.legal(k) if X.seen(st, k, w, seed, know)] or st.legal(k)[:1]


class Pol:
    def __init__(s, kind, seed, know, rng):
        s.kind = kind
        s.seed = seed
        s.know = know
        s.rng = rng
        s.r = [seed | 1]

    def choose(s, st, k):
        if s.kind == "greedy":
            return X.ai_choose(st, k, s.seed, s.know, 0, s.r)
        if s.kind == "baiter":
            return X.ai_choose(st, k, s.seed, s.know, 256, s.r)
        if s.kind.startswith("mix"):
            return X.ai_choose(st, k, s.seed, s.know, int(s.kind[3:]), s.r)
        L = mine(st, k, s.seed, s.know)
        if s.kind == "random":
            if st.pot != NONE and st.owner != k and s.rng.random() < 0.1:
                L = st.legal(k)
            nonpass = [w for w in L if X.a_kind(w) != X.PASS] or L
            return s.rng.choice(nonpass)
        return s.reader(st, k, L)

    def follow(s, h, k):
        best = None
        for w in mine(h, k, s.seed, s.know):
            h3 = h.clone()
            h3.do(w)
            v = X.value(h3, k) + (800 if h3.s[1 - k].hp <= 0 else 0)
            if best is None or v > best:
                best = v
        return X.value(h, k) if best is None else best

    def reply(s, h, k):
        o = 1 - k
        worst = None
        for w in [w for w in h.legal(o) if X.seen(h, o, w, s.seed, s.know)] or h.legal(o)[:1]:
            h2 = h.clone()
            h2.begin_turn()
            h2.do(w)
            v = -900 if h2.s[k].hp <= 0 else s.follow(h2, k)
            if worst is None or v < worst:
                worst = v
        return worst

    def reader(s, st, k, L):
        best, bv = L[0], None
        for w in L:
            h = st.clone()
            h.do(w)
            if h.s[1 - k].hp <= 0:
                return w
            v = s.reply(h, k)
            if bv is None or v > bv:
                best, bv = w, v
        return best


# ---------------- shelves (how the bench grows) ----------------
PAIRS = sorted({(a, b) for (a, b, r) in X.ROWS})
BYIN = collections.defaultdict(list)
for p in PAIRS:
    BYIN[p[0]].append(p)
    BYIN[p[1]].append(p)
_SH = {}


def shelf(n, seed):
    if (n, seed) in _SH:
        return _SH[(n, seed)]
    rng = random.Random(seed)
    own = set(FOUR)
    order = list(FOUR)
    front = set()
    uses = collections.Counter()
    for x in FOUR:
        for p in BYIN[x]:
            if p[0] in own and p[1] in own:
                front.add(p)
    while len(own) < n and front:
        p = rng.choice(sorted(front))
        front.discard(p)
        r = X.MIX[p]
        uses[p[0]] += 1
        uses[p[1]] += 1
        if r in own:
            continue
        own.add(r)
        order.append(r)
        for q in BYIN[r]:
            if q[0] in own and q[1] in own and X.MIX[q] not in own:
                front.add(q)
    _SH[(n, seed)] = (order, uses)
    return order, uses


def bag(sh, how, rng, n=8):
    order, uses = sh
    pool = [x for x in order if x not in FOUR and stars(x) <= 2]
    if how == "auto":
        return X.auto_bag(order, (), uses, n)
    if how == "random":
        o = pool[:]
        rng.shuffle(o)
        return o[:n]
    if how == "twostar":
        return sorted(pool, key=lambda x: (-stars(x), rng.random()))[:n]
    if how == "onestar":
        return sorted(pool, key=lambda x: (stars(x), rng.random()))[:n]
    if how.startswith("cat:"):
        c = X.CATS.index(how[4:])
        m = [x for x in pool if CAT[x] == c]
        rng.shuffle(m)
        rest = [x for x in pool if CAT[x] != c]
        rng.shuffle(rest)
        return (m[:5] + rest)[:n]
    raise ValueError(how)


def bout(args):
    p1, p2, seed, kw = args
    rng = random.Random(seed)
    sh1 = shelf(kw.get("shelf1", 150), seed * 2 + 1)
    sh2 = shelf(kw.get("shelf2", 150), seed * 2 + 2)
    a = Side(bag(sh1, kw.get("bag1", "auto"), rng), 10)
    b = Side(bag(sh2, kw.get("bag2", "auto"), rng), 10)
    first = seed & 1
    st = State(a, b, first)
    pa = Pol(p1, seed * 7 + 1, kw.get("k1", K["know"]), random.Random(seed + 11))
    pb = Pol(p2, seed * 7 + 2, kw.get("k2", K["know"]), random.Random(seed + 12))
    log = collections.Counter()
    chain = 0
    while not st.over():
        k = st.act
        st.begin_turn()
        w = (pa if k == 0 else pb).choose(st, k)
        ev = st.do(w)
        log[ev[0]] += 1
        chain = max(chain, st.chain)
    return st.over(), dict(log), st.turn, chain, first


POOL = None


def match(p1, p2, n, **kw):
    res = POOL.map(bout, [(p1, p2, 1000 + i, kw) for i in range(n)], chunksize=2)
    w = [0, 0, 0]
    lg = collections.Counter()
    T = 0
    ch = collections.Counter()
    fw = [0, 0]
    for r, l, t, c, first in res:
        w[{1: 0, 2: 1, 3: 2}[r]] += 1
        lg.update(l)
        T += t
        ch[c] += 1
        if r != 3:
            fw[(r - 1) == first] += 1
    return dict(pct=100 * (w[0] + w[2] / 2) / n, w=w, log=lg, turns=T / n, chains=ch, firstwin=fw)


# ---------------- bosses ----------------
FAC = ["PROGRAM", "DAEMON", "GHOST", "AI", "OPERATOR", "RELIC"]
LIKE_CAT = [5, 3, 2, 1, 4, 6]
HOT, COLD, WET, AIRY, STONE, SHINY, GLOWS, ALIVE, GREEN, MADE, BIG, MAGIC = [1 << i for i in range(12)]
LIKE = [
    MADE | GLOWS | SHINY,
    HOT | BIG | STONE,
    AIRY | COLD | MAGIC,
    STONE | SHINY | COLD,
    ALIVE | GREEN | WET,
    STONE | MAGIC | MADE,
]
FEAR = [MAGIC | WET, COLD | GREEN, HOT | GLOWS, ALIVE | GREEN, SHINY | MADE, HOT | AIRY]
TWIST = [X.TW_COMPILE, X.TW_CHARGE, X.TW_SKYALL, 0, X.TW_ROOT, X.TW_LOCK]
# tier: HP, bag, depth allowance, skill (of 256), knowledge (of 256)
import json as _j

TIER = {
    1: (7, 6, 3, 0, 115),
    2: (10, 6, 3, 60, 128),
    3: (11, 7, 3, 140, 170),
    4: (9, 6, 4, 60, 135),
    5: (10, 8, 4, 130, 165),
    6: (11, 8, 4, 180, 190),
}
if os.environ.get("TIER"):
    TIER = {int(k): tuple(v) for k, v in _j.loads(os.environ["TIER"]).items()}


def affinity(f, x):
    return (2 if CAT[x] == LIKE_CAT[f] else 0) + bin(TR[x] & LIKE[f]).count("1") - bin(TR[x] & FEAR[f]).count("1")


def boss_bag(f, tier, seed, you):
    """the cartridge's opponent bag (crucible_fight_story.c fs_prep_step): from a seeded place in its category's shelf
    (category, then id), each favourite (affinity 2+, depth 1..dmax) adds the shallow ingredients of its first recipe
    (else itself) until the bag is full; a boss of AI also brings partners for two of yours"""
    hp, n, dmax, skill, know = TIER[tier]
    shelf = [x for x in range(X.N) if CAT[x] == LIKE_CAT[f]]
    out = []
    if shelf:
        at = X.xs16(seed) % len(shelf)
        for k in range(len(shelf)):
            if len(out) >= n:
                break
            x = shelf[(at + k) % len(shelf)]
            if not (1 <= DEP[x] <= dmax and affinity(f, x) >= 2):
                continue
            rs = [(a, b) for a, b in X.ROUTES.get(x, ()) if DEP[a] <= 3 and DEP[b] <= 3]
            if rs:
                for y in rs[0]:
                    if y not in FOUR and len(out) < n:
                        out.append(y)
            else:
                out.append(x)
    if FAC[f] == "AI":
        for y in you[:2]:
            for b, r in X.PARTNERS_A.get(y, ()):
                if DEP[b] <= 4 and b not in FOUR:
                    out.append(b)
                    break
    return out[: X.BAG_MAX]


def boss_fight(args):
    pol, f, tier, seed, know, hp = args
    rng = random.Random(seed)
    sh = shelf(40 + 30 * tier, seed * 3 + 1)
    yb = bag(sh, "auto", rng)
    you = Side(yb, hp)
    bhp, n, dmax, skill, bknow = TIER[tier]
    duel = os.environ.get("DUEL") == "1"
    boss = Side(
        boss_bag(f, tier, seed, yb),
        bhp - (1 if duel or FAC[f] in ("RELIC", "OPERATOR") else 0),
        0 if duel else TWIST[f],
    )
    st = State(you, boss, seed & 1)
    py = Pol(pol, seed * 5 + 1, know, random.Random(seed))
    r = [seed | 1]
    while not st.over():
        k = st.act
        st.begin_turn()
        if k == 0:
            w = py.choose(st, 0)
        else:
            plan = X.boss_plan(st, 1, seed * 5 + 2, bknow) if not duel and st.pot == NONE else None
            w = plan if plan is not None and plan in st.legal(1) else X.ai_choose(st, 1, seed * 5 + 2, bknow, skill, r)
        st.do(w)
    return st.over() == 1, st.turn


if __name__ == "__main__":
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 120
    parts = sys.argv[2].split(",") if len(sys.argv) > 2 else ["versus", "boss"]
    POOL = Pool(int(os.environ.get("JOBS", "8")))
    print(
        f"slice: {X.N} objects, {len(X.ROWS)} recipes; opener +{X.OPEN_HP} HP; HP 10; bags of 8 (auto); knowledge {K['know']}/256",
        flush=True,
    )
    if "versus" in parts:
        names = ["random", "greedy", "baiter", "reader"]
        fw = [0, 0]
        print("\n== versus: row win % ==\n         " + " ".join(f"{q:>7s}" for q in names) + "   field", flush=True)
        for p in names:
            row = []
            for q in names:
                m = match(p, q, N // 2)
                row.append(m["pct"])
                fw[0] += m["firstwin"][0]
                fw[1] += m["firstwin"][1]
            print(f"{p:8s} " + " ".join(f"{x:7.0f}" for x in row) + f"   {sum(row) / 4:5.0f}", flush=True)
        print(f"the opener wins {100 * fw[1] / max(1, sum(fw)):.1f}% of decided bouts", flush=True)
        for k in (77, 154, 230):
            m = match("reader", "reader", N, k1=k, k2=k)
            print(
                f"reader mirror, knowledge {k}: turns {m['turns']:.1f}, longest chains {dict(sorted(m['chains'].items()))}, per bout "
                + " ".join(f"{a} {b / N:.2f}" for a, b in sorted(m["log"].items())),
                flush=True,
            )
        for k1, k2 in ((230, 77), (154, 77), (230, 154)):
            print(
                f"knowledge {k1} vs {k2} (reader): {match('reader', 'reader', N, k1=k1, k2=k2)['pct']:.0f}%", flush=True
            )
        opts = [
            "auto",
            "random",
            "twostar",
            "onestar",
            "cat:place",
            "cat:life",
            "cat:weather",
            "cat:craft",
            "cat:matter",
            "cat:energy",
        ]
        print(
            "bags vs the auto bag (reader): "
            + " ".join(f"{o} {match('reader', 'reader', N, bag1=o, bag2='auto')['pct']:.0f}" for o in opts),
            flush=True,
        )
        for s1, s2 in ((300, 60), (300, 150)):
            print(
                f"shelf {s1} vs {s2} (reader): {match('reader', 'reader', N, shelf1=s1, shelf2=s2)['pct']:.0f}%",
                flush=True,
            )
    if "boss" in parts:
        for pol, know, hp in [
            tuple(x)
            for x in _j.loads(
                os.environ.get("YOU", '[["greedy",102,12],["mix128",128,12],["baiter",166,12],["reader",179,12]]')
            )
        ]:
            print(
                f"\n-- bosses, first try: you play {pol}, knowing {know}/256, HP {hp} --\ntier "
                + " ".join(f"{x:>9s}" for x in FAC)
                + "      all  turns",
                flush=True,
            )
            for tier in range(1, 7):
                row = []
                tt = 0
                for f in range(6):
                    res = POOL.map(boss_fight, [(pol, f, tier, 5000 + i, know, hp) for i in range(N)], chunksize=2)
                    row.append(100 * sum(r[0] for r in res) / N)
                    tt += sum(r[1] for r in res) / N
                print(
                    f"T{tier}   " + " ".join(f"{x:9.0f}" for x in row) + f"   {sum(row) / 6:6.0f}  {tt / 6:5.1f}",
                    flush=True,
                )
