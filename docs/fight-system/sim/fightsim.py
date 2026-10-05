#!/usr/bin/env python3
"""CRUCIBLE.EXE fight simulator: versus bouts and boss fights on the real catalogue slice.

Deterministic: every bout is seeded. Rules mirror docs/fight-system.md. Run:
  python3 fightsim.py            # full report
  python3 fightsim.py --quick    # fewer bouts
"""

import collections
import itertools
import json
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ITEMS = json.load(open(os.path.join(HERE, "..", "..", "..", "catalogue", "items.json")))
RECIPES = json.load(open(os.path.join(HERE, "..", "..", "..", "catalogue", "recipes.json")))
BYC = {i["c"]: i for i in ITEMS}
MIX = {}
for a, b, r in RECIPES:
    if a in BYC and b in BYC and r in BYC:
        MIX[(BYC[a]["id"], BYC[b]["id"])] = BYC[r]["id"]
        MIX[(BYC[b]["id"], BYC[a]["id"])] = BYC[r]["id"]

# ---------------- rules (the numbers the doc quotes) ----------------
R = dict(
    STACK_BONUS=1,
    LOW_HAND=4,
    DUAL_PEN=1,
    HP=12,
    DECK=6,
    HAND=3,
    BUDGET=12,
    FOCUS_MAX=3,
    FUSE_COST=2,
    EDGE_CAP=1,
    ECHO=2,
    TURNS=30,
    GUARD_DIV=2,
    STACK_DISCOUNT=1,
    DUAL_COST=1,
    FOCUS_START=0,
    # the arena and the match toggles (section 3.1 and 9.2): ARENA 0 HUMMING LIGHTS .. 7 EMPTY; SUDDEN: from turn 15 every
    # hit +1; NOSTACK: no stack bonus; NIGHT: focus max +1
    ARENA=7,
    SUDDEN=0,
    NOSTACK=0,
    NIGHT=0,
)
CTX = {"turn": 0}  # the turn being resolved (sudden death reads it)
# traits: HOT COLD WET AIRY STONE SHINY GLOWS ALIVE GREEN MADE BIG MAGIC; families 0 HUM 1 WALL 2 DREAM
FAM = [0, 2, 1, 0, 1, 0, 0, 2, 2, 1, 1, 2]
CATS = ["element", "matter", "weather", "energy", "life", "craft", "place"]
CAT_FAM = [0, 1, 2, 0, 2, 1, 1]
H, C, W, A, S, SH, G, AL, GR, M, BG, MG = [1 << i for i in range(12)]
BEATS = [W | C, H | G, C | GR, S | BG, W | GR, S | W, BG | S, H | MG, H | C, MG | W, M | MG, M | G]
STN = "HWD"


def stances(it):
    v = [0, 0, 0]
    for i in range(12):
        if it["t"] >> i & 1:
            v[FAM[i]] += 1
    m = max(v)
    fs = [x for x in range(3) if v[x] == m]
    return fs if len(fs) < 3 else [CAT_FAM[CATS.index(it["cat"])]]


def power(it):
    d = it["d"]
    return 1 + (d >= 5) + (d >= 7)


def edges(a, b):
    """trait edges: how many of b's traits a's traits beat (the existing cru_counter without the depth bonus)"""
    ta, tb = ITEMS[a]["t"], ITEMS[b]["t"]
    n = 0
    for k in range(12):
        if tb >> k & 1 and ta & BEATS[k]:
            n += 1
    return n


for it in ITEMS:
    it["s"] = stances(it)
    it["p"] = power(it)


def beats(x, y):  # stance x beats y: WALL>HUM, DREAM>WALL, HUM>DREAM
    return (x - y) % 3 == 1


def card_cost(it):
    return it["p"] + (R["DUAL_COST"] if len(it["s"]) > 1 else 0)


def loadout_cost(ids):
    seen = collections.Counter()
    c = 0
    for i in ids:
        k = card_cost(ITEMS[i])
        c += max(1, k - R["STACK_DISCOUNT"] * (seen[i] > 0))
        seen[i] += 1
    return c


# ---------------- passives: template = pattern + effect ----------------
PASSIVES = [
    "ECHO",
    "PRISM",
    "SCAR",
    "VIGIL",
    "CATALYST",
    "STALEMATE",
    "SALVE",
    "UNDERTOW",
    "LUCID",
    "NOCLIP",
    "OVERCLOCK",
    "MEMORY",
]
NEED = dict(
    ECHO=2,
    PRISM=3,
    SCAR=5,
    VIGIL=2,
    CATALYST=3,
    STALEMATE=3,
    SALVE=3,
    UNDERTOW=2,
    LUCID=3,
    NOCLIP=2,
    OVERCLOCK=2,
    MEMORY=3,
)


class P:
    def __init__(s, deck, rng, passives, name):
        s.hp = s.max = R["HP"]
        s.deck = deck[:]
        rng.shuffle(s.deck)
        s.disc = []
        s.hand = []
        s.focus = R["FOCUS_START"]
        s.pas = passives
        s.prog = {p: 0 for p in passives}
        s.on = set()
        s.hist = []
        s.last = None
        s.lostrow = 0
        s.wins = 0
        s.ties = 0
        s.lost_hp = 0
        s.played = set()
        s.under = False
        s.lucid_t = 0
        s.name = name
        s.window = []
        s.kit = set(deck)
        s.prods = 0  # MEMORY remembers at most 2 fused products (the cartridge's 2 slots)

    def handsize(s):
        return R["HAND"] + ("PRISM" in s.on or s.hp <= R["LOW_HAND"])

    def draw(s, rng):
        while len(s.hand) < s.handsize():
            if not s.deck:
                if not s.disc:
                    break
                s.deck = s.disc
                s.disc = []
                rng.shuffle(s.deck)
            s.hand.append(s.deck.pop())


def actions(p):
    acts = [("G",)]
    for i, c in enumerate(p.hand):
        for st in ITEMS[c]["s"]:
            acts.append(("P", i, st))
    cost = R["FUSE_COST"] - ("CATALYST" in p.on)
    if p.focus >= cost:
        for i, j in itertools.combinations(range(len(p.hand)), 2):
            r = MIX.get((p.hand[i], p.hand[j]))
            if r is not None:
                for st in ITEMS[r]["s"]:
                    acts.append(("F", i, j, st, r))
    return acts


def act_card(p, a):
    """(item, stance, power) of an action, None for guard"""
    if a[0] == "G":
        return None
    if a[0] == "P":
        c = p.hand[a[1]]
        return (
            c,
            a[2],
            max(1, ITEMS[c]["p"] - (R["DUAL_PEN"] if len(ITEMS[c]["s"]) > 1 else 0))
            + (0 if R["NOSTACK"] else R["STACK_BONUS"] + (R["ARENA"] == 5) + ("MEMORY" in p.on))
            * min(2, p.disc.count(c)),
        )
    return (a[4], a[3], min(4, ITEMS[a[4]]["p"] + 1 + ("CATALYST" in p.on)))


def clash(pa, a, pb, b):
    """damage (to b, to a) and outcome for a: 1 win, 0 tie, -1 loss, None guard involved"""
    ca, cb = act_card(pa, a), act_card(pb, b)
    if ca is None and cb is None:
        return 0, 0, None
    if ca is None or cb is None:
        if ca is None:
            db, da, _ = clash(pb, b, pa, a)
            return da, db, None
        it, st, pw = ca
        dmg = pw + ("OVERCLOCK" in pa.on)
        if st == 2 and "NOCLIP" in pa.on:
            return dmg, 0, None
        if "VIGIL" in pb.on:
            return 0, 0, None
        if R["SUDDEN"] and CTX["turn"] >= 15:
            dmg += 1
        return (dmg if R["ARENA"] == 4 else dmg // R["GUARD_DIV"]), 0, None
    (ia, sa, wa), (ib, sb, wb) = ca, cb

    def hit(pp, it, st, pw, other, won_after_loss):
        d = pw + min(R["EDGE_CAP"], edges(it, other))
        if pp.last == st and "ECHO" in pp.on:
            d += R["ECHO"]
        if "SCAR" in pp.on and pp.hp * 2 <= pp.max:
            d += 1
        if "OVERCLOCK" in pp.on:
            d += 1
        if "NOCLIP" in pp.on and st == 2:
            d += 1
        if R["ARENA"] == st:
            d += 1  # HUMMING LIGHTS / DAMP CARPET / STILL AIR: that stance wins +1
        if R["SUDDEN"] and CTX["turn"] >= 15:
            d += 1
        if "UNDERTOW" in pp.on and won_after_loss:
            d *= 2
        return d

    if sa == sb:
        da = min(R["EDGE_CAP"], edges(ia, ib))
        db = min(R["EDGE_CAP"], edges(ib, ia))
        if da == db:
            if R["ARENA"] == 6 and pa.hp != pb.hp:
                da, db = (1, 0) if pa.hp < pb.hp else (0, 1)  # EXIT SIGN: the lower HP side
            else:
                da, db = (1, 0) if wa > wb else (0, 1) if wb > wa else (0, 0)
        if "STALEMATE" in pa.on:
            da += 1
        if "STALEMATE" in pb.on:
            db += 1
        if "ECHO" in pa.on and pa.last == sa:
            da += 1
        if "ECHO" in pb.on and pb.last == sb:
            db += 1
        return da, db, 0
    if beats(sa, sb):
        d = hit(pa, ia, sa, wa, ib, pa.lostrow > 0)
        if "LUCID" in pb.on:
            d = min(d, 2)
        return d, 0, 1
    d = hit(pb, ib, sb, wb, ia, pb.lostrow > 0)
    if "LUCID" in pa.on:
        d = min(d, 2)
    return 0, d, -1


def advance(p, a, out, card, opp_edges_win):
    """update pattern progress after a turn (the patterns are public: the opponent sees the pips)"""
    st = card[1] if card else None
    for k in p.pas:
        if k in p.on:
            continue
        g = p.prog[k]
        if k == "ECHO":
            g = g + 1 if st is not None and st == p.last else (1 if st is not None else 0)
        elif k == "PRISM":
            p.window = (p.window + [st])[-3:]
            g = len({x for x in p.window if x is not None})
        elif k == "SCAR":
            g = p.lost_hp
        elif k == "VIGIL":
            g = g + 1 if a[0] == "G" else 0
        elif k == "CATALYST":
            g = p.focus
        elif k == "STALEMATE":
            g += out == 0
        elif k == "SALVE":
            g = g + 1 if card is not None and card[2] == 1 else 0
        elif k == "UNDERTOW":
            g = p.lostrow
        elif k == "LUCID":
            g += out == 1
        elif k == "NOCLIP":
            g += out == 1 and st == 2
        elif k == "OVERCLOCK":
            g += card is not None and card[2] >= 3
        elif k == "MEMORY":
            g += card is not None and card[0] in p.played
        if opp_edges_win:
            g = 0  # SEVER: a clean counter breaks an unfinished pattern
        p.prog[k] = g
        if g >= NEED[k]:
            p.on.add(k)
    if card and card[0] not in p.played:
        if card[0] in p.kit:
            p.played.add(card[0])
        elif p.prods < 2:
            p.played.add(card[0])
            p.prods += 1


def apply_action(p, a, rng):
    if a[0] == "P":
        p.disc.append(p.hand.pop(a[1]))
    elif a[0] == "F":
        i, j = sorted(a[1:3], reverse=True)
        p.disc += [p.hand.pop(i), p.hand.pop(j)]
        p.focus -= R["FUSE_COST"] - ("CATALYST" in p.on)
    else:
        p.focus = min(R["FOCUS_MAX"] + R["NIGHT"], p.focus + 1)
        if p.hand:  # a guard cycles your worst card
            w = min(range(len(p.hand)), key=lambda i: ITEMS[p.hand[i]]["p"])
            p.disc.append(p.hand.pop(w))


# ---------------- policies ----------------
def ev(p, a, q, qdist):
    tot = 0.0
    for b, w in qdist:
        if w <= 0:
            continue
        da, db, _ = clash(p, a, q, b)
        tot += w * (da - db * 1.0)
    return tot


def uniform(q):
    acts = [x for x in actions(q) if x[0] != "G" or True]
    return [(b, 1 / len(acts)) for b in acts]


def pol_random(p, q, rng, st):
    acts = actions(p)
    return rng.choice(acts)


def pol_greedy(p, q, rng, st):
    acts = actions(p)
    qd = uniform(q)
    return max(acts, key=lambda a: (ev(p, a, q, qd), str(a)))


def pol_stacker(p, q, rng, st):
    acts = [a for a in actions(p) if a[0] != "G"]
    if not acts:
        return ("G",)
    same = [a for a in acts if act_card(p, a)[1] == p.last]
    pool = same or acts
    return max(pool, key=lambda a: (act_card(p, a)[2], str(a)))


def pursue(p, a):
    c = act_card(p, a)
    st = c[1] if c else None
    s = 0
    for k in p.pas:
        if k in p.on:
            continue
        if k == "ECHO" and st is not None and st == p.last:
            s += 2
        if k == "PRISM" and st is not None and st not in p.window[-2:]:
            s += 2
        if k == "VIGIL" and a[0] == "G":
            s += 2
        if k == "CATALYST" and a[0] == "G":
            s += 1.5
        if k == "OVERCLOCK" and c and c[2] >= 3:
            s += 2
        if k == "NOCLIP" and st == 2:
            s += 1
        if k == "SALVE" and c and c[2] == 1:
            s += 1.5
        if k == "MEMORY" and c and c[0] in p.played:
            s += 2
    return s


def pol_rusher(p, q, rng, st):
    acts = actions(p)
    qd = uniform(q)
    return max(acts, key=lambda a: (pursue(p, a) + 0.5 * ev(p, a, q, qd), str(a)))


def predict(q, p, qmodel):
    """the reader's model of q: what q's hand allows, q's habits (stance after stance), q's pattern needs"""
    acts = actions(q)
    out = []
    for b in acts:
        c = act_card(q, b)
        w = 1.0
        if c is None:
            w = 0.35 + 0.4 * (q.focus == 0 and len(q.hand) > 0)
        else:
            w += 1.6 * qmodel["after"][(q.last, c[1])] / (1 + qmodel["n"][q.last])
            w += 0.5 * pursue(q, b)
            if c[1] == q.last:
                w += 0.6 * qmodel["repeat"] / (1 + qmodel["turns"])
            w += 0.25 * c[2]
        out.append((b, w))
    s = sum(w for _, w in out)
    return [(b, w / s) for b, w in out]


def pol_reader(p, q, rng, st):
    acts = actions(p)
    qd = predict(q, p, st["model"][q.name])
    scored = sorted(((ev(p, a, q, qd) + 0.15 * pursue(p, a), str(a), a) for a in acts), reverse=True)
    # mostly the best response, sometimes the second (so a reader is never a fixed function of the board)
    if len(scored) > 1 and rng.random() < 0.2:
        return scored[1][2]
    return scored[0][2]


POLICIES = dict(random=pol_random, greedy=pol_greedy, stacker=pol_stacker, rusher=pol_rusher, reader=pol_reader)


# ---------------- loadouts from a mid-game shelf ----------------
def shelf(rng, n=48, maxd=7):
    pool = [i["id"] for i in ITEMS if i["d"] <= maxd]
    return rng.sample(pool, n)


def fit(ids, rng, pool):
    """trim/pad to DECK cards within BUDGET (cheapest swaps)"""
    ids = ids[: R["DECK"]]
    cheap = sorted(pool, key=lambda i: card_cost(ITEMS[i]))
    while loadout_cost(ids) > R["BUDGET"]:
        k = max(range(len(ids)), key=lambda x: card_cost(ITEMS[ids[x]]))
        for c in cheap:
            if c not in ids and card_cost(ITEMS[c]) < card_cost(ITEMS[ids[k]]):
                ids[k] = c
                break
        else:
            break
    return ids


def build(arch, rng):
    sh = shelf(rng)
    if arch == "balanced":
        out = []
        for s in (0, 1, 2, 0, 1, 2):
            c = [i for i in sh if ITEMS[i]["s"] == [s] and i not in out]
            c.sort(key=lambda i: -ITEMS[i]["p"] + rng.random() * 1.5)
            out.append(c[0] if c else rng.choice(sh))
        return fit(out, rng, sh)
    if arch == "stack":
        best = max(sh, key=lambda i: (ITEMS[i]["p"], rng.random()))
        rest = sorted(sh, key=lambda i: (ITEMS[i]["s"][0] == ITEMS[best]["s"][0], card_cost(ITEMS[i]), rng.random()))
        return fit([best, best, best] + [i for i in rest if i != best][:3], rng, sh)
    if arch == "heavy":
        hv = sorted(sh, key=lambda i: (-ITEMS[i]["p"], rng.random()))[:3]
        lt = sorted(sh, key=lambda i: (card_cost(ITEMS[i]), rng.random()))[:3]
        return fit(hv + lt, rng, sh)
    if arch == "swarm":
        dual = [i for i in sh if len(ITEMS[i]["s"]) > 1]
        one = [i for i in sh if ITEMS[i]["p"] == 1]
        return fit((dual[:3] + one + sh)[:6], rng, sh)
    if arch == "fuser":
        pairs = [(a, b) for a, b in itertools.combinations(sh, 2) if (a, b) in MIX]
        rng.shuffle(pairs)
        out = []
        for a, b in pairs:
            if a not in out and b not in out and len(out) < 6:
                out += [a, b]
        return fit((out + sh)[:6], rng, sh)
    if arch == "mono":
        s = rng.randrange(3)
        c = [i for i in sh if s in ITEMS[i]["s"]]
        return fit((c + sh)[:6], rng, sh)
    return fit(rng.sample(sh, 6), rng, sh)


ARCHES = ["balanced", "stack", "heavy", "swarm", "fuser", "mono", "random"]


def turn(A, B, a, b, rng, st, t):
    """one turn after both sides chose (the cards were drawn before the choice): the clash, damage, passives' patterns"""
    CTX["turn"] = t
    ca, cb = act_card(A, a), act_card(B, b)
    dB, dA, out = clash(A, a, B, b)
    if st is not None:
        for pp, cc in ((A, ca), (B, cb)):
            m = st["model"][pp.name]
            if cc:
                m["after"][(pp.last, cc[1])] += 1
                m["n"][pp.last] += 1
                m["repeat"] += cc[1] == pp.last
                m["turns"] += 1
    B.hp -= dB
    A.hp -= dA
    A.lost_hp += dA
    B.lost_hp += dB
    if "OVERCLOCK" in A.on and out == -1:
        A.hp -= 1
    if "OVERCLOCK" in B.on and out == 1:
        B.hp -= 1
    if st is not None:
        gap = B.hp - A.hp
        if gap > st["maxgapA"]:
            st["maxgapA"] = gap
        if -gap > st["maxgapB"]:
            st["maxgapB"] = -gap
    if "SALVE" in A.on and out == 1 and A.hp > 0:
        A.hp = min(A.max, A.hp + 1)
    if "SALVE" in B.on and out == -1 and B.hp > 0:
        B.hp = min(B.max, B.hp + 1)
    oa = out
    ob = None if out is None else -out
    for pp, o in ((A, oa), (B, ob)):
        if o == -1:
            pp.lostrow += 1
            pp.focus = min(R["FOCUS_MAX"] + R["NIGHT"], pp.focus + 1)
        elif o == 1:
            pp.lostrow = 0
            pp.wins += 1
    sevA = out == 1 and ca and edges(ca[0], cb[0]) >= 2
    sevB = out == -1 and cb and edges(cb[0], ca[0]) >= 2
    apply_action(A, a, rng)
    apply_action(B, b, rng)
    advance(A, a, oa, ca, sevB)
    advance(B, b, ob, cb, sevA)
    A.last = ca[1] if ca else None
    B.last = cb[1] if cb else None
    return dA, dB, out


def bout(seed, pa_pol, pb_pol, la, lb, pas_a=None, pas_b=None):
    rng = random.Random(seed)
    pas_a = pas_a or rng.sample(PASSIVES, 2)
    pas_b = pas_b or rng.sample(PASSIVES, 2)
    A = P(la, rng, pas_a, "A")
    B = P(lb, rng, pas_b, "B")
    st = {
        "maxgapA": 0,
        "maxgapB": 0,
        "model": {
            n: {"after": collections.Counter(), "n": collections.Counter(), "repeat": 0, "turns": 0} for n in "AB"
        },
    }
    t = 0
    while t < R["TURNS"] and A.hp > 0 and B.hp > 0:
        t += 1
        A.draw(rng)
        B.draw(rng)
        a = POLICIES[pa_pol](A, B, rng, st)
        b = POLICIES[pb_pol](B, A, rng, st)
        turn(A, B, a, b, rng, st, t)
    if A.hp <= 0 and B.hp <= 0 or A.hp == B.hp:
        w = 0
    elif B.hp <= 0 or (A.hp > B.hp and A.hp > 0):
        w = 1
    else:
        w = -1
    A.gap = st["maxgapA"]
    B.gap = st["maxgapB"]
    return w, t, A, B


def matrix(pols, n, arch_a="balanced", arch_b="balanced", seed0=0):
    res = {}
    lens = []
    for i, x in enumerate(pols):
        for j, y in enumerate(pols):
            s = 0
            d = 0
            for k in range(n):
                seed = seed0 + k * 7919 + i * 131 + j * 17
                rng = random.Random(seed ^ 0xBEEF)
                la = build(arch_a, rng)
                lb = build(arch_b, rng)
                w, t, _, _ = bout(seed, x, y, la, lb)
                s += w == 1
                d += w == 0
                lens.append(t)
            res[(x, y)] = (s + d / 2) / n
    return res, sum(lens) / len(lens)


def report(n):
    pols = list(POLICIES)
    out = []
    res, L = matrix(pols, n)
    out.append("### Policy vs policy (balanced loadouts, %d bouts per cell, row win rate)" % n)
    out.append("| row \\ col | " + " | ".join(pols) + " | field |")
    out.append("|" + "---|" * (len(pols) + 2))
    for x in pols:
        row = [res[(x, y)] for y in pols]
        fld = [res[(x, y)] for y in pols if y != "random"]
        out.append(
            "| %s | " % x
            + " | ".join("%.0f%%" % (100 * v) for v in row)
            + " | **%.0f%%** |" % (100 * sum(fld) / len(fld))
        )
    out.append(
        "Field = mean against the four non-random policies (random is the calibration floor). Mean bout length: %.1f turns"
        % L
    )
    # archetypes with reader play
    out.append("\n### Loadout archetype vs archetype (both sides play `reader`, %d bouts per cell)" % n)
    out.append("| row \\ col | " + " | ".join(ARCHES) + " | field |")
    out.append("|" + "---|" * (len(ARCHES) + 2))
    lens = []
    for x in ARCHES:
        row = []
        for y in ARCHES:
            s = 0
            for k in range(n):
                seed = 50000 + k * 7919 + ARCHES.index(x) * 131 + ARCHES.index(y) * 17
                rng = random.Random(seed ^ 0xBEEF)
                la = build(x, rng)
                lb = build(y, rng)
                w, t, _, _ = bout(seed, "reader", "reader", la, lb)
                s += (w == 1) + (w == 0) / 2
                lens.append(t)
            row.append(s / n)
        out.append(
            "| %s | " % x
            + " | ".join("%.0f%%" % (100 * v) for v in row)
            + " | **%.0f%%** |" % (100 * sum(row) / len(row))
        )
    out.append("Mean bout length: %.1f turns" % (sum(lens) / len(lens)))
    # host vs guest: same policy mirror
    s = d = 0
    N = n * 6
    for k in range(N):
        rng = random.Random(k ^ 0xABC)
        la = build("random", rng)
        lb = build("random", rng)
        w, t, _, _ = bout(900000 + k, "reader", "reader", la, lb)
        s += w == 1
        d += w == 0
    out.append(
        "\nHost (player A) vs guest, mirror `reader`, %d bouts: host wins %.1f%%, draws %.1f%%"
        % (N, 100 * s / N, 100 * d / N)
    )
    # passives
    pw = collections.Counter()
    pn = collections.Counter()
    pu = collections.Counter()
    pon = collections.Counter()
    for k in range(n * 12):
        rng = random.Random(k ^ 0x77)
        la = build("random", rng)
        lb = build("random", rng)
        pa = rng.sample(PASSIVES, 2)
        pb = rng.sample(PASSIVES, 2)
        w, t, A, B = bout(700000 + k, "reader", "reader", la, lb, pa, pb)
        for p in pa:
            pn[p] += 1
            pw[p] += (w == 1) + (w == 0) / 2
            pon[p] += p in A.on
        for p in pb:
            pn[p] += 1
            pw[p] += (w == -1) + (w == 0) / 2
            pon[p] += p in B.on
    out.append("\n### Passives (reader vs reader, random loadouts; win rate of the side that brought it, unlock rate)")
    out.append("| passive | win | unlocked |")
    out.append("|---|---|---|")
    for p in PASSIVES:
        out.append("| %s | %.0f%% | %.0f%% |" % (p, 100 * pw[p] / pn[p], 100 * pon[p] / pn[p]))
    # comebacks: down >= 6 HP at some point and still won
    cb = collections.Counter()
    tot = collections.Counter()
    for k in range(n * 6):
        rng = random.Random(k ^ 0x99)
        la = build("balanced", rng)
        lb = build("balanced", rng)
        w, t, A, B = bout(800000 + k, "reader", "reader", la, lb)
        for side, g, won in ((A, A.gap, w == 1), (B, B.gap, w == -1)):
            for th in (4, 6, 8):
                if g >= th:
                    tot[th] += 1
                    cb[th] += won
    out.append("\n### Comebacks (reader mirror, balanced): side that was ever behind by N HP and still won")
    for th in (4, 6, 8):
        out.append(
            "- behind by %d+: %d of %d bouts won (%.0f%%)" % (th, cb[th], tot[th], 100 * cb[th] / max(1, tot[th]))
        )
    return "\n".join(out)


if __name__ == "__main__":
    n = 60 if "--quick" in sys.argv else 300
    print(report(n))
