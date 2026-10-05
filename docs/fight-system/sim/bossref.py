#!/usr/bin/env python3
"""The cartridge's boss engine (crucible_fight_boss.c), in Python: bosssim.py's rules, driven by frng and the real
recipes, round by round as the cartridge plays them. The player is a bot with bosssim's competence model (reads plain
strikes, knows tricks it was taught, infers veiled results by recipe knowledge or the lean rule, slips, goes for the last
pip). Run against this reference and against the ROM (a PyBoy step test) the bot's RNG is the same, so the
two traces must agree round by round; run against bosssim.py's numbers, the win rates must agree within 5 points.
  python3 bossref.py --rates [n]     win rate by tier, first try (this engine, the same bot)"""

import collections
import random
import sys

import bosssim as BS
import fightsim as F
from fightref import Frng
from fightsim import ITEMS, MIX, act_card

(
    STRIKE,
    LOOP,
    DOUBLE,
    FEINT,
    SHIELD,
    COMPILE,
    CHARGE,
    STEAL,
    OVERHEAT,
    HAUNT,
    POSSESS,
    MIRROR,
    ADAPT,
    PREDICT,
    GROW,
    JACK,
    ARMOR,
    ERODE,
    RECALL,
) = range(19)
KNAME = "STRIKE LOOP DOUBLE FEINT SHIELD COMPILE CHARGE STEAL OVERHEAT HAUNT POSSESS MIRROR ADAPT PREDICT GROW JACK ARMOR ERODE RECALL".split()
TIER = [
    (22, 2, 4, 20, 3),
    (30, 3, 4, 35, 3),
    (30, 3, 3, 55, 3),
    (30, 3, 3, 70, 3),
    (28, 4, 2, 85, 2),
    (30, 4, 2, 100, 2),
]
SIG = [
    [LOOP, DOUBLE, FEINT, SHIELD, COMPILE],
    [CHARGE, DOUBLE, STEAL, FEINT, OVERHEAT],
    [STEAL, FEINT, HAUNT, CHARGE, POSSESS],
    [MIRROR, ADAPT, SHIELD, FEINT, PREDICT],
    [GROW, STEAL, DOUBLE, ADAPT, JACK],
    [ARMOR, SHIELD, CHARGE, HAUNT, ERODE],
]
HOME = [1, 0, 2, 1, 2, 1]
NONE = 3
MISS = 0xFE
GUARD = 0x80


def beater(s):
    return 0 if s >= 2 else s + 1


def stance_mask(i):
    return sum(1 << s for s in ITEMS[i]["s"])


def tr(i):
    return ITEMS[i]["t"]


def bmask(t):
    m = 0
    for k in range(12):
        if t & F.BEATS[k]:
            m |= 1 << k
    return m


class Boss:
    def __init__(s, fac, tier, memory, pip_adj, ai):
        s.ai = ai
        s.fac = fac
        s.tier = tier
        hp, s.pow, pips, s.veil, s.chip = TIER[tier - 1]
        if fac == 5 and tier >= 4:
            hp -= 6
        if fac == 3 and tier >= 4:
            hp -= 2
        s.hp = s.max = hp
        s.pips = pips + pip_adj
        if s.pips < 1 or s.pips > 6:
            s.pips = 1
        s.moves = SIG[fac][: min(tier, 5)]
        s.home = HOME[fac]
        s.loop = [s.home, beater(s.home), beater(beater(s.home))]
        for i in (2, 1):
            j = s.below(i + 1)
            s.loop[i], s.loop[j] = s.loop[j], s.loop[i]
        s.li = 0
        s.cool = [0xF0] * 19
        s.seen = set()
        s.shield = NONE
        s.shield_t = 0
        s.erode = 0
        s.phase = 0
        s.memory = memory if memory is not None and memory < 3 else NONE
        s.t = 0
        s.charged = 0
        s.dealt = [0, 0, 0]
        s.fused = s.close = s.guards = 0
        s.possess_t = 0
        s.possess_slot = 0
        s.jack = 0

    def rnd(s):
        return s.ai.next() & 255

    def below(s, n):
        return (s.rnd() * n) >> 8

    def plan(s, hand):
        PA = [0, 0, 0, 0, 1, 1, 1, 2, 2, 3]
        PB = [0, 1, 2, 3, 1, 2, 3, 2, 3, 3]
        if not hasattr(s, "prod"):
            s.prod = [MIX.get((hand[PA[x]], hand[PB[x]])) for x in range(10)]
        t = s.below(4)
        s.target = s.home if t < 2 else beater(s.home) if t == 2 else beater(beater(s.home))
        start = s.below(10)
        pick = anyx = None
        for k in range(10):
            x = (start + k) % 10
            if s.prod[x] is None:
                continue
            if anyx is None:
                anyx = x
            if s.target in ITEMS[s.prod[x]]["s"]:
                pick = x
                break
        if pick is not None:
            s.a, s.b, s.atk = hand[PA[pick]], hand[PB[pick]], s.prod[pick]
        else:
            k = next((k for k in range(4) if s.target in ITEMS[hand[k]]["s"]), None)
            if k is not None:
                s.a = s.b = s.atk = hand[k]
            elif anyx is not None:
                s.a, s.b, s.atk = hand[PA[anyx]], hand[PB[anyx]], s.prod[anyx]
            else:
                s.a = s.b = s.atk = hand[s.rnd() & 3]
        s.atk_tr = tr(s.atk)
        s.atk_bt = bmask(s.atk_tr)

    def round(s, pl):
        s.t = (s.t + 1) & 255
        ms = list(s.moves)
        if s.phase >= 1 and s.tier >= 3:
            ms.append(s.moves[-1])
        kind = STRIKE if s.below(100) < 45 - 5 * s.tier else ms[s.below(len(ms))]
        if kind != STRIKE and ((s.t - s.cool[kind]) & 255) <= 2:
            kind = STRIKE
        if kind != STRIKE:
            s.cool[kind] = s.t
        st = s.target
        if LOOP in s.moves or COMPILE in s.moves:
            st = s.loop[s.li]
            s.li = 0 if s.li >= 2 else s.li + 1
        if s.memory != NONE and s.t <= 4:
            st = beater(s.memory)
            kind = RECALL
        if s.memory != NONE and s.t <= 5:
            s.shield = s.memory
            s.shield_t = 1
        real = st
        pw = s.pow + (1 if s.phase >= 2 else 0)
        n = 1
        if kind == FEINT:
            real = beater(beater(st))
        if kind == CHARGE and not s.charged:
            s.charged = 1
            s.kind = CHARGE
            s.shown = s.real = NONE
            s.pw = 0
            s.n = 0
            s.demo = 0
            s.veiled = 0
            return
        if s.charged:
            pw += pw
            s.charged = 0
        if kind in (DOUBLE, OVERHEAT):
            n = 2
        if kind == OVERHEAT:
            pw += 1
        if kind == MIRROR and pl.last is not None:
            st = real = pl.last
        if kind == COMPILE and pl.hist:
            st = real = beater(pl.hist[-1])
        if kind in (ADAPT, PREDICT) and pl.hist:
            st = real = beater(collections.Counter(pl.hist[-4:]).most_common(1)[0][0])
        if kind == SHIELD and s.shield == NONE:
            s.shield = pl.last if pl.last is not None else 0
            s.shield_t = 2
        if kind == ERODE:
            s.erode = 1
        if kind == POSSESS and pl.hand:
            s.possess_slot = s.below(len(pl.hand))
            s.possess_card = pl.hand[s.possess_slot]
            s.possess_t = 2
        if kind == JACK:
            s.jack = 1
        s.demo = 0
        if kind not in (STRIKE, RECALL) and kind not in s.seen:
            s.seen.add(kind)
            s.demo = 1
            pw >>= 1
            pw = pw or 1
        s.veiled = 1 if kind == HAUNT or (kind == STRIKE and s.below(100) < s.veil) else 0
        s.kind, s.shown, s.real, s.pw, s.n = kind, st, real, pw, n


def player(kit, hp, focus, rng):
    p = F.P(list(kit), rng, [], "A")
    p.hp = p.max = hp
    p.focus = focus
    p.hist = []
    p.lost_hp = 0
    return p


def resolve(b, p, picks, close, rng):
    """picks: action tuples or 'MISS' / ('G',); mirrors fb_resolve"""
    out = 0
    used = []
    steal = []

    def hurt(d):
        p.hp -= d
        p.lost_hp += max(0, d)

    def bhurt(d):
        b.hp = 0 if d >= b.hp else b.hp - d

    if b.kind == CHARGE:
        a = picks[0]
        if a != "MISS" and a[0] != "G":
            c = act_card(p, a)
            d = c[2]
            if ARMOR in b.moves and d > 1:
                d = 1
            bhurt(d)
            out |= 128 | 1
            b.dealt[c[1]] += d
            if a[0] == "F":
                i, j = sorted(a[1:3], reverse=True)
                p.disc += [p.hand.pop(i), p.hand.pop(j)]
                if p.focus >= 2:
                    p.focus -= 2
                b.fused += 1
            else:
                p.disc.append(p.hand.pop(a[1]))
        return out
    else:
        for k in range(b.n):
            a = picks[k]
            if a == "MISS":
                hurt(b.pw + 1)
                out |= 2
                continue
            if a == "PASS":
                continue
            if a[0] == "G":
                hurt((b.pw + 1) >> 1)
                p.focus = min(3, p.focus + 1)
                b.guards += 1
                out |= 8
                continue
            c = act_card(p, a)
            st = c[1]
            if a[0] == "P":
                c = (c[0], c[1], ITEMS[p.hand[a[1]]]["p"])  # plain power against a boss (bosssim)
            slot = a[1]
            if b.possess_t and a[0] == "P" and p.hand[slot] == getattr(b, "possess_card", None):
                st = beater(beater(st))
            if b.shield != NONE and st == b.shield:
                out |= 16
            elif st == beater(b.real):
                d = c[2] + (1 if bmask(tr(c[0])) & b.atk_tr else 0) + (1 if close[k] else 0)
                if ARMOR in b.moves and not close[k] and a[0] != "F" and d > 3:
                    d = 3
                bhurt(d)
                b.dealt[st] += d
                out |= 1
                if close[k]:
                    b.close += 1
                if a[0] == "F":
                    b.fused += 1
            elif st == b.real:
                bhurt(1 if bmask(tr(c[0])) & b.atk_tr else 0)
                hurt(1 if b.atk_bt & tr(c[0]) else 0)
                out |= 4
            else:
                hurt(b.pw + (1 if b.atk_bt & tr(c[0]) else 0))
                out |= 2
                if b.kind == STEAL and a[0] == "P":
                    p.deck = [x for x in p.deck if x != c[0]]
                    out |= 64
            p.hist = (p.hist + [st])[-4:]
            p.last = st
            if a[0] == "F":
                used += [a[2], a[1]]
                if p.focus >= 2:
                    p.focus -= 2
                break
            used.append(a[1])
    for k in sorted(used, reverse=True):
        p.disc.append(p.hand.pop(k))
    if b.chip and b.t % b.chip == 0:
        hurt(1)
    if b.kind == GROW and b.hp and b.hp < b.max:
        b.hp += 1
    if b.jack and p.hand:
        p.hand.pop()
        b.jack = 0
    if b.shield_t:
        b.shield_t -= 1
    if not b.shield_t:
        b.shield = NONE
    if b.possess_t:
        b.possess_t -= 1
    was = b.phase
    if b.hp <= b.max >> 2:
        b.phase = 2
    elif b.hp <= b.max >> 1:
        b.phase = 1
    if b.phase != was:
        out |= 32
    return out


# ---------------- the bot ----------------
def bot(mode, b, p, r, know):
    """mode: 'perfect' (reads the real stance), 'naive' (answers the shown one), 'competent' (bosssim's model).
    Returns (picks, close) as action tuples / 'MISS'. r: the bot's own random.Random."""
    if b.kind == CHARGE:
        if not p.hand:
            return ["MISS"], [0]
        i = max(range(len(p.hand)), key=lambda i: ITEMS[p.hand[i]]["p"])
        return [("P", i, ITEMS[p.hand[i]]["s"][0])], [0]
    real, shown = b.real, b.shown
    if mode == "perfect":
        seen = real
    elif mode == "naive":
        seen = shown
    else:
        kk = KNAME[b.kind]
        k = 1.0 if b.kind in (STRIKE, CHARGE) else know.get(kk, BS.NEW_KNOW)
        if b.demo and k < BS.DEMO_KNOW:
            k = BS.DEMO_KNOW
        seen = real if r.random() < k else shown
        if b.veiled and b.kind == STRIKE:
            pr = BS.RECIPE_KNOWN + (1 - BS.RECIPE_KNOWN) * BS.LEAN_RULE
            if r.random() >= pr:
                seen = r.choice([x for x in range(3) if x != real])
        if b.kind == HAUNT and r.random() >= k:
            seen = r.randrange(3)
    picks, close, taken = [], [], set()
    if mode == "perfect":
        for kk in range(b.n):
            best = None
            for i in range(len(p.hand)):
                if i in taken:
                    continue
                c = ITEMS[p.hand[i]]
                for st in c["s"]:
                    if b.shield != NONE and st == b.shield:
                        continue
                    if st == beater(seen) and (best is None or c["p"] > best[0]):
                        best = (c["p"], i, st)
            if best:
                taken.add(best[1])
                picks.append(("P", best[1], best[2]))
                close.append(1)
            else:
                picks.append(("G",))
                close.append(0)
        return picks, close
    for kk in range(b.n):
        best = None
        for i in range(len(p.hand)):
            if i in taken:
                continue
            c = ITEMS[p.hand[i]]
            for s in c["s"]:
                if b.shield != NONE and s == b.shield:
                    continue
                sc = (3 if s == beater(seen) else 1 if s == seen else -2) * 10 + c["p"]
                if best is None or sc > best[0]:
                    best = (sc, i, s)
        if best and best[0] > 0:
            i, s = best[1], best[2]
            if mode == "competent" and r.random() < BS.TIER[b.tier][4]:  # a slip: the wrong card
                i = r.randrange(len(p.hand))
                s = ITEMS[p.hand[i]]["s"][0]
            if i in taken:
                picks.append("PASS")
                close.append(0)
                continue
            taken.add(i)
            cl = 1 if (mode == "competent" and r.random() < 0.5) else 0
            if cl and r.random() < BS.TIER[b.tier][5]:
                picks.append("MISS")
                close.append(0)
                continue  # going for the last pip, and missing it
            picks.append(("P", i, s))
            close.append(cl)
        else:
            picks.append(("G",))
            close.append(0)
    return picks, close


def boss_hand(fac, tier, r):
    cat = F.CATS[BS.LIKE_CAT[fac]]
    pool = [i["id"] for i in ITEMS if i["cat"] == cat and i["d"] <= 3 + tier]
    return [r.choice(pool) for _ in range(4)]


def setup(seed, tier, fac, attrs=(0, 0)):
    """the player's kit (bosssim's balanced kit from a shelf of the tier's depth), the boss's hand, the fight seed"""
    shelf = random.Random(seed ^ 0x77)
    F.R["BUDGET"] = 12 + attrs[1]
    pool = [i["id"] for i in ITEMS if i["d"] <= BS.TIER[tier][3]]
    sh = shelf.sample(pool, 40)
    out = []
    for s in (0, 1, 2, 0, 1, 2):
        c = [i for i in sh if ITEMS[i]["s"] == [s] and i not in out]
        c.sort(key=lambda i: -ITEMS[i]["p"] + shelf.random())
        out.append(c[0] if c else shelf.choice(sh))
    kit = F.fit(out, shelf, sh)
    F.R["BUDGET"] = 12
    return kit, boss_hand(fac, tier, shelf), (seed & 0xFFFF) or 1


class Fight:
    """one boss fight on this reference, a round at a time (the ROM harness steps it beside the cartridge)"""

    def __init__(s, seed, fac, tier, attrs=(0, 0), memory=None):
        s.kit, s.hand, s.fseed = setup(seed, tier, fac, attrs)
        s.rng = Frng(s.fseed)
        s.ai = Frng(s.fseed ^ 0xA15E)
        s.p = player(s.kit, 12 + attrs[0], 0, s.rng)
        s.b = Boss(fac, tier, memory, 0, s.ai)
        s.t = 0

    def round(s):
        s.t += 1
        s.p.draw(s.rng)
        while s.b.erode and len(s.p.hand) > 2:
            s.p.disc.append(s.p.hand.pop())
        s.b.plan(s.hand)
        s.b.round(s.p)

    def resolve(s, picks, close):
        return resolve(s.b, s.p, picks, close, s.rng)

    def over(s):
        return 1 if s.b.hp <= 0 else 2 if s.p.hp <= 0 else 0


def fight(seed, fac, tier, know, mode="competent", attrs=(0, 0), memory=None, trace=None):
    """one boss fight on this reference; returns (won, rounds, best stance, kit, hand)"""
    r = random.Random(seed)
    f = Fight(seed, fac, tier, attrs, memory)
    while not f.over() and f.t < 40:
        f.round()
        picks, close = bot(mode, f.b, f.p, r, know)
        o = f.resolve(picks, close)
        if trace is not None:
            trace.append(
                {
                    "t": f.t,
                    "kind": f.b.kind,
                    "shown": f.b.shown,
                    "real": f.b.real,
                    "pw": f.b.pw,
                    "n": f.b.n,
                    "atk": f.b.atk,
                    "out": o,
                    "bhp": f.b.hp,
                    "php": f.p.hp,
                }
            )
    b = f.b
    best = max(range(3), key=lambda s_: b.dealt[s_]) if any(b.dealt) else None
    return b.hp <= 0, f.t, best, f.kit, f.hand


def rates(n):
    print("| tier | " + " | ".join(BS.FACS) + " | all, first try | bosssim |")
    print("|" + "---|" * 9)
    for tier in range(1, 7):
        row = []
        tot = 0
        for fi, fac in enumerate(BS.FACS):
            w = 0
            for k in range(n):
                know = {m: BS.OLD_KNOW for m in BS.SIG[fac][: tier - 1]}
                know.update({m: BS.NEW_KNOW for m in BS.SIG[fac][tier - 1 :]})
                if tier == 6:
                    know = {m: BS.OLD_KNOW for m in BS.SIG[fac]}
                ok, _, _, _, _ = fight(
                    k * 977 + tier * 31 + fi, fi, tier, know, attrs=(min(3, tier - 1), min(2, (tier - 1) // 2))
                )
                w += ok
            row.append(w / n)
            tot += w
        sim = 0
        for fi, fac in enumerate(BS.FACS):
            for k in range(n):
                know = {m: BS.OLD_KNOW for m in BS.SIG[fac][: tier - 1]}
                know.update({m: BS.NEW_KNOW for m in BS.SIG[fac][tier - 1 :]})
                if tier == 6:
                    know = {m: BS.OLD_KNOW for m in BS.SIG[fac]}
                ok, _, _ = BS.boss_fight(
                    k * 977 + tier * 31 + fi, fac, tier, know, attrs=(min(3, tier - 1), min(2, (tier - 1) // 2))
                )
                sim += ok
        print(
            "| T%d | " % tier
            + " | ".join("%.0f%%" % (100 * x) for x in row)
            + " | **%.0f%%** | %.0f%% |" % (100 * tot / (n * 6), 100 * sim / (n * 6))
        )


if __name__ == "__main__":
    if "--rates" in sys.argv:
        i = sys.argv.index("--rates")
        rates(int(sys.argv[i + 1]) if len(sys.argv) > i + 1 else 100)
