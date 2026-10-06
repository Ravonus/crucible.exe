#!/usr/bin/env python3
"""THE CRUCIBLE: the reference for the cartridge's fight engine (crucible_crux.c) and its AI, integer-exact.

One crucible (the pot) between two sides. Each side: HP, a BAG (up to 10 objects from its shelf) and THE FOUR
(EARTH WATER FIRE AIR, each usable once a round). One action a turn, turns alternate:
  ADD x       one of yours into the pot. Empty: it stands, yours (START). A real recipe x + pot: the product, heat +1,
              yours (BUILD on your own; HIJACK on theirs, which takes their heat). Theirs and x is one of its
              ingredients: SPLIT (it falls apart, you take the other ingredient). Theirs and x's traits beat its traits
              with at least its stars: BREAK (both gone). Anything else: MISS (x burns; on your own pot heat -1).
  FORGE x y   (an empty pot only) two of yours that make something: the product stands, yours, heat 1.
  POUR        your own pot strikes them: stars + heat (+1 when it shares a trait with the SKY); the pot empties.
  PASS        (their pot only) let it stand.
An empty pot must be filled; your own pot must be poured or built on (you never sit on it).
Category rules (the category byte): PLACE is locked (nothing is added onto it; it never heats), LIFE grows (+1 heat at
its owner's turn), WEATHER is the sky (a weather pot sets it), ELEMENT is open (everyone holds the four).
Stars from depth: 0-1 one, 2-3 two, 4-5 three, 6+ four. Heat caps at 4.

Action word (10 bits, the link's P_FTURN): kind:2 (0 ADD, 1 FORGE, 2 POUR, 3 PASS) << 8 | x:4 << 4 | y:4.
Tool index: 0..9 a bag slot, 12..15 one of the four (12 + element id).

Usage:
  python3 crux.py --golden OUT.json     golden traces (engine + AI) for the host build and the ROM
  python3 crux.py --demo SEED           one AI bout, printed
"""

import collections
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ITEMS = json.load(open(os.path.join(HERE, "..", "..", "..", "catalogue", "items.json")))
RECIPES = json.load(open(os.path.join(HERE, "..", "..", "..", "catalogue", "recipes.json")))
IDX = {i["c"]: i["id"] for i in ITEMS}
N = len(ITEMS)
CATS = ["element", "matter", "weather", "energy", "life", "craft", "place"]
CAT = [CATS.index(i["cat"]) for i in ITEMS]
DEP = [i["d"] for i in ITEMS]
TR = [i["t"] for i in ITEMS]
NAME = [i["n"] for i in ITEMS]
C_ELEMENT, C_MATTER, C_WEATHER, C_ENERGY, C_LIFE, C_CRAFT, C_PLACE = range(7)
# the cartridge's recipe rows: sorted by (a, b), a <= b; the routes to each result in authored order (recipes.json),
# as the table toolchain bakes TAB_ROUTE_LIST
ROWS = sorted((min(IDX[a], IDX[b]), max(IDX[a], IDX[b]), IDX[r]) for a, b, r in RECIPES)
MIX = {}
ROUTES = collections.defaultdict(list)
for a, b, r in ROWS:
    MIX[(a, b)] = r
    MIX[(b, a)] = r
for a, b, r in RECIPES:
    ROUTES[IDX[r]].append((min(IDX[a], IDX[b]), max(IDX[a], IDX[b])))
PARTNERS_A = collections.defaultdict(list)  # rows whose a is id: (b, r), in row order
for a, b, r in ROWS:
    PARTNERS_A[a].append((b, r))
FOUR = (0, 1, 2, 3)
NONE = 0xFFFF
# BEATS[k]: the traits that beat trait k (cru_saga.c)
BEATS = [6, 65, 258, 1040, 260, 20, 1040, 2049, 3, 2052, 2560, 576]
HEAT_CAP = 4
TURNS = 40
BAG_MAX = 10
OPEN_HP = 2  # the side that opens a round starts with 2 HP more


def recipe(a, b):
    return MIX.get((a, b), NONE)


def stars(x):
    d = DEP[x]
    return 1 if d <= 1 else 2 if d <= 3 else 3 if d <= 5 else 4


def edges(x, s):
    n = 0
    t = TR[x]
    for k in range(12):
        if (TR[s] >> k) & 1 and t & BEATS[k]:
            n += 1
    return n


def can_break(x, s):
    return edges(x, s) >= 1 and stars(x) >= stars(s)


def locked(s):
    return CAT[s] == C_PLACE


def split_of(x, s):
    """the other ingredient when x is one of s's (the first route in row order), else NONE"""
    if DEP[s] == 0:
        return NONE
    for a, b in ROUTES.get(s, ()):
        if a == x:
            return b
        if b == x:
            return a
    return NONE


ADD, FORGE, POUR, PASS = 0, 1, 2, 3


def act(kind, x=0, y=0):
    return (kind << 8) | (x << 4) | y


def a_kind(w):
    return w >> 8


def a_x(w):
    return (w >> 4) & 15


def a_y(w):
    return w & 15


O_START, O_BUILD, O_HIJACK, O_SPLIT, O_BREAK, O_MISS = range(6)
ONAME = ["START", "BUILD", "HIJACK", "SPLIT", "BREAK", "MISS"]

# boss and duel twists (bit per rule), set per side
TW_CHARGE = 1  # DAEMON: its own pot heats +1 at its turn (+2 from phase 2)
TW_SKYALL = 2  # GHOST: every pot it makes sets the sky
TW_ROOT = 4  # OPERATOR: from phase 1 its pot you let stand seeds a copy back into its bag; phase 2 its pots grow
TW_LOCK = 8  # RELIC: from phase 2 its pots are locked to you (break them or take the pour)
TW_COMPILE = 16  # PROGRAM: from phase 2 a pot you start teaches it a partner for it


class Side:
    __slots__ = ("bag", "four", "hp", "max", "tw", "made")

    def __init__(s, bag, hp, tw=0):
        s.bag = list(bag)[:BAG_MAX]
        s.four = 15
        s.hp = hp
        s.max = hp
        s.tw = tw
        s.made = 0

    def clone(s):
        c = Side.__new__(Side)
        c.bag = list(s.bag)
        c.four = s.four
        c.hp = s.hp
        c.max = s.max
        c.tw = s.tw
        c.made = s.made
        return c

    def tool(s, i):
        return s.bag[i] if i < 12 else i - 12

    def tools(s):
        out = list(range(len(s.bag)))
        for e in range(4):
            if s.four >> e & 1:
                out.append(12 + e)
        return out


class State:
    def __init__(s, a, b, first=0, phase_of=None):
        s.s = [a, b]
        s.pot = NONE
        s.owner = 0
        s.heat = 0
        s.sky = 0
        s.turn = 0
        s.act = first
        s.chain = 0
        s.passes = 0
        s.last = None
        s.dmg = 0
        s.boss = None  # boss: (side, maxhp) when one side is a boss
        s.shield = 0
        s.s[first].hp += OPEN_HP
        s.s[first].max += OPEN_HP

    def clone(s):
        c = State.__new__(State)
        c.__dict__.update(s.__dict__)
        c.s = [x.clone() for x in s.s]
        return c

    def phase(s, k):
        """a boss side's phase: 0 above 2/3 of its HP, 1 above 1/3, else 2"""
        p = s.s[k]
        m = p.max
        if p.hp * 3 > m * 2:
            return 0
        if p.hp * 3 > m:
            return 1
        return 2

    def lock_to(s, k):
        """is the pot closed to side k's additions?"""
        if s.pot == NONE:
            return False
        if locked(s.pot):
            return True
        o = s.owner
        return o != k and (s.s[o].tw & TW_LOCK) and s.phase(o) >= 2

    def outcome(s, k, x):
        """what ADD of item x by side k does now: (O_*, the product or the ingredient taken)"""
        p = s.pot
        if p == NONE:
            return O_START, x
        if s.lock_to(k):
            if s.owner != k and can_break(x, p):
                return O_BREAK, NONE
            return O_MISS, NONE
        r = recipe(x, p)
        if r != NONE:
            return (O_BUILD if s.owner == k else O_HIJACK), r
        if s.owner != k:
            y = split_of(x, p)
            if y != NONE:
                return O_SPLIT, y
            if can_break(x, p):
                return O_BREAK, NONE
        return O_MISS, NONE

    def power(s):
        p = s.pot
        v = stars(p)
        if not locked(p):
            v += s.heat
        if s.sky and (TR[p] & s.sky):
            v += 1
        return v

    def legal(s, k):
        """every legal action of side k, in the canonical order"""
        me = s.s[k]
        t = me.tools()
        out = []
        if s.pot == NONE:
            if not t:
                return [act(PASS)]
            for i in t:
                out.append(act(ADD, i))
            for ai in range(len(t)):
                for bi in range(ai + 1, len(t)):
                    i, j = t[ai], t[bi]
                    if i >= 12 and j >= 12:
                        continue
                    if recipe(me.tool(i), me.tool(j)) != NONE:
                        out.append(act(FORGE, i, j))
            return out
        if s.owner == k:
            out.append(act(POUR))
            if not s.lock_to(k):
                for i in t:
                    out.append(act(ADD, i))
            return out
        out.append(act(PASS))
        for i in t:
            out.append(act(ADD, i))
        return out

    def take(s, k, i):
        me = s.s[k]
        if i < 12:
            x = me.bag.pop(i)
        else:
            x = i - 12
            me.four &= ~(1 << x)
        return x

    def stand(s, k, x, heat):
        s.pot = x
        s.owner = k
        s.heat = heat if heat < HEAT_CAP else HEAT_CAP
        if CAT[x] == C_WEATHER or (s.s[k].tw & TW_SKYALL):
            s.sky = TR[x]

    def begin_turn(s):
        """the side to move: its own growing pot heats"""
        k = s.act
        if s.pot != NONE and s.owner == k and not locked(s.pot):
            g = 0
            if CAT[s.pot] == C_LIFE:
                g = 1
            tw = s.s[k].tw
            if tw & TW_CHARGE:
                g += 2 if s.phase(k) >= 2 else 1
            if (tw & TW_ROOT) and s.phase(k) >= 2:
                g += 1
            if g:
                s.heat = min(HEAT_CAP, s.heat + g)

    def do(s, w):
        """side s.act plays action word w (assumed legal); the turn passes"""
        k = s.act
        me = s.s[k]
        op = s.s[1 - k]
        kind = a_kind(w)
        s.dmg = 0
        ev = None
        if kind == PASS:
            ev = ("PASS",)
            if s.pot != NONE and s.owner != k and (op.tw & TW_ROOT) and s.phase(1 - k) >= 1 and len(op.bag) < BAG_MAX:
                op.bag.append(s.pot)
        elif kind == POUR:
            d = s.power()
            if s.shield and s.shield - 1 != k:
                d = d - 1 if d > 1 else 0  # RELIC's shield: one off your next pour
            if s.shield - 1 != k:
                s.shield = 0
            op.hp -= d
            s.dmg = d
            ev = ("POUR", s.pot, d)
            if (me.tw & TW_LOCK) and s.phase(k) >= 2:
                s.shield = k + 1
            s.pot = NONE
            s.heat = 0
            s.chain = 0
        elif kind == FORGE:
            i, j = a_x(w), a_y(w)
            if i > j:
                i, j = j, i
            x = me.tool(i)
            y = me.tool(j)
            r = recipe(x, y)
            s.take(k, j)
            s.take(k, i)  # the higher index first: the lower one keeps its place
            me.made += 1
            s.chain = 0
            s.stand(k, r, 1)
            ev = ("FORGE", x, y, r)
            s.compile_(k)
        else:
            i = a_x(w)
            x = me.tool(i)
            o, r = s.outcome(k, x)
            s.take(k, i)
            ev = (ONAME[o], x, r)
            if o == O_START:
                s.chain = 0
                s.stand(k, x, 0)
                s.compile_(k)
            elif o == O_BUILD or o == O_HIJACK:
                me.made += 1
                if o == O_HIJACK:
                    s.chain += 1
                s.stand(k, r, s.heat + 1)
            elif o == O_SPLIT:
                if len(me.bag) < BAG_MAX:
                    me.bag.append(r)
                s.pot = NONE
                s.heat = 0
                s.chain = 0
            elif o == O_BREAK:
                s.pot = NONE
                s.heat = 0
                s.chain = 0
            elif s.owner == k and s.heat:
                s.heat -= 1
        s.passes = s.passes + 1 if kind == PASS else 0
        s.turn += 1
        s.act = 1 - k
        s.last = ev
        return ev

    def compile_(s, k):
        """PROGRAM from phase 2: a pot the other side starts teaches it a partner (the first row whose a is the pot)"""
        o = 1 - k
        op = s.s[o]
        if not (op.tw & TW_COMPILE) or s.phase(o) < 2 or len(op.bag) >= BAG_MAX:
            return
        for b, r in PARTNERS_A.get(s.pot, ()):
            if DEP[b] <= 4:
                op.bag.append(b)
                return

    def over(s):
        """0 going; 1 side 0 won, 2 side 1 won, 3 a draw"""
        a, b = s.s
        if a.hp > 0 and b.hp > 0 and s.turn < TURNS and s.passes < 6 and (a.tools() or b.tools() or s.pot != NONE):
            return 0
        if a.hp <= 0 and b.hp <= 0:
            return 3
        if b.hp <= 0:
            return 1
        if a.hp <= 0:
            return 2
        return 1 if a.hp > b.hp else 2 if b.hp > a.hp else 3

    def hash(s):
        h = (
            s.s[0].hp
            + s.s[1].hp * 3
            + s.heat * 5
            + s.turn * 7
            + (s.pot & 255)
            + (s.pot >> 8) * 11
            + s.owner * 13
            + s.sky
        ) & 255
        for p in s.s:
            h = (h + len(p.bag) * 17 + p.four * 19) & 255
            for x in p.bag:
                h = (h * 3 + x + (x >> 8)) & 255
        return h


def play(st, w):
    st.begin_turn()
    return st.do(w)


# ---------------- the AI (integer; the cartridge runs the same) ----------------
def xs16(x):
    x &= 0xFFFF
    x ^= (x << 7) & 0xFFFF
    x ^= x >> 9
    x ^= (x << 8) & 0xFFFF
    return x


def knows(seed, know, a, b):
    """does a side with this seed and knowledge (0..256) know the recipe a + b? (its made ones are always known)"""
    if know >= 256:
        return True
    lo, hi = (a, b) if a <= b else (b, a)
    h = xs16((lo * 0x9E37 + hi * 0x7F4B + seed) & 0xFFFF)
    return (h >> 8) < know


def knows_split(seed, know, x, s):
    """the route that makes s with x: does it know it?"""
    y = split_of(x, s)
    return y != NONE and knows(seed, know, x, y)


W8 = (0, 4, 8, 13, 18)


def mat(p):
    return sum(W8[stars(x)] for x in p.bag) + 3 * bin(p.four).count("1")


def value(st, k):
    me, op = st.s[k], st.s[1 - k]
    v = (me.hp - op.hp) * 16 + mat(me) - mat(op)
    if st.pot != NONE:
        pw = st.power() * 8
        v += pw if st.owner == k else -pw
    return v


def seen(st, k, w, seed, know):
    """would a side that knows these recipes play w? (no planned misses)"""
    kind = a_kind(w)
    if kind == FORGE:
        me = st.s[k]
        return knows(seed, know, me.tool(a_x(w)), me.tool(a_y(w)))
    if kind != ADD or st.pot == NONE:
        return True
    x = st.s[k].tool(a_x(w))
    o, r = st.outcome(k, x)
    if o in (O_BUILD, O_HIJACK):
        return knows(seed, know, x, st.pot)
    if o == O_SPLIT:
        return knows_split(seed, know, x, st.pot)
    return o != O_MISS


def greedy_score(st, k, w):
    kind = a_kind(w)
    me = st.s[k]
    if kind == PASS:
        return 0
    if kind == POUR:
        return 50 + st.power() if (st.heat >= 1 or stars(st.pot) >= 2) else 20
    if kind == FORGE:
        return 40 + stars(recipe(me.tool(a_x(w)), me.tool(a_y(w)))) * 4
    i = a_x(w)
    x = me.tool(i)
    o, r = st.outcome(k, x)
    if o == O_HIJACK:
        return 200 + stars(r) * 4
    if o == O_SPLIT:
        return 150
    if o == O_BREAK:
        return 120
    if o == O_BUILD:
        return 60 + stars(r) * 4
    if o == O_START:
        return 30 + stars(x) * 2 - (6 if i >= 12 else 0)
    return -50


def exposure(st, k, seed, know):
    """after k's move: what the other side can do to k's pot, as k knows. 3 hijack (the first), 2 split, 1 break"""
    if st.pot == NONE or st.owner != k:
        return 0, NONE
    op = st.s[1 - k]
    e = 0
    for i in op.tools():
        y = op.tool(i)
        o, r = st.outcome(1 - k, y)
        if o == O_HIJACK and knows(seed, know, y, st.pot):
            return 3, y
        if o == O_SPLIT and knows_split(seed, know, y, st.pot):
            e = max(e, 2)
        elif o == O_BREAK:
            e = max(e, 1)
    return e, NONE


def baiter_score(st, k, w, seed, know, base):
    h = st.clone()
    h.do(w)
    v = value(h, k) - base
    if h.pot != NONE and h.owner == k:
        e, y = exposure(h, k, seed, know)
        pw = h.power()
        if e == 3:
            r2 = recipe(y, h.pot)
            trap = False
            for i in h.s[k].tools():
                z = h.s[k].tool(i)
                if recipe(z, r2) != NONE and knows(seed, know, z, r2):
                    trap = True
                    break
            v -= 3 * pw if trap else 22 * pw + 16
        elif e == 2:
            v -= 13 * pw
        elif e == 1:
            v -= 10 * pw
        elif a_kind(w) == ADD and h.heat > 0:
            v += 5
    return v


def ai_choose(st, k, seed, know, skill, rng):
    """skill 0..256: the chance (of 256) each decision plays the baiter, else greedy. rng: a 16-bit xorshift state
    (list of one), stepped once a decision. Returns the action word."""
    rng[0] = xs16(rng[0])
    bait = (rng[0] & 255) < skill
    L = [w for w in st.legal(k) if seen(st, k, w, seed, know)]
    if not L:
        L = st.legal(k)[:1]
    best = L[0]
    bv = None
    base = value(st, k) if bait else 0
    for w in L:
        v = baiter_score(st, k, w, seed, know, base) if bait else greedy_score(st, k, w)
        if bv is None or v > bv:
            best, bv = w, v
    return best


def boss_plan(st, k, seed, know):
    """a boss's telegraph (crucible_fight_story.c fs_plan): of the pairs in its bag (slot order) that make something and
    that it knows, the one making the most stars (the first on a tie): a FORGE word, or None"""
    p = st.s[k]
    best = None
    bs = 0
    for i in range(len(p.bag)):
        for j in range(i + 1, len(p.bag)):
            a, b = p.bag[i], p.bag[j]
            r = recipe(a, b)
            if r == NONE or not knows(seed, know, a, b):
                continue
            if stars(r) > bs:
                bs = stars(r)
                best = act(FORGE, i, j)
    return best


# ---------------- bags ----------------
def auto_bag(owned, pinned=(), uses=None, n=8, reach=1):
    """the default bag (the cartridge's crux_bag): what you pinned (three stars at most, REACH of them with three),
    then what you own of two stars, then one, each by how often you used it on the bench, at most two of a category; ties go to
    the lower id. owned: ids in any order; uses: id -> bench uses (default 0)"""
    uses = uses or {}
    own = sorted(x for x in owned if x not in FOUR)
    oset = set(own)
    out = []
    threes = 0
    for x in pinned:
        if len(out) >= n or x not in oset or x in out:
            continue
        s = stars(x)
        if s > 3 or (s == 3 and threes >= reach):
            continue
        out.append(x)
        threes += s == 3
    cats = collections.Counter(CAT[x] for x in out)
    for x in sorted((x for x in own if stars(x) <= 2), key=lambda x: (-stars(x), -min(255, uses.get(x, 0)), x)):
        if len(out) >= n:
            break
        if x in out or cats[CAT[x]] >= 2:
            continue
        out.append(x)
        cats[CAT[x]] += 1
    for x in own:
        if len(out) >= n:
            break
        if x not in out and stars(x) <= 2:
            out.append(x)
    return out


def demo(seed):
    import random

    rng = random.Random(seed)
    own = list(range(0, 120))
    a = Side(auto_bag(own[:80]), 10)
    b = Side(auto_bag(own[20:120]), 10)
    st = State(a, b, seed & 1)
    r = [seed | 1]
    while not st.over():
        k = st.act
        st.begin_turn()
        w = ai_choose(st, k, 0x1234 + k, 160, 128, r)
        ev = st.do(w)
        print(
            f"{st.turn:2d} {'AB'[k]} {ev} pot {NAME[st.pot] if st.pot != NONE else '-'} heat {st.heat} hp {a.hp} {b.hp}"
        )
    print("over", st.over())


def golden(n=200, seed0=1):
    """bouts with every rule in play: a random scripted side (blind tries, misses, passes) against the AI, AI against
    AI, boss twists on either side; each turn the word and the state hash after it"""
    import random

    out = []
    for g in range(n):
        rng = random.Random(seed0 + g)
        pool = [x for x in range(4, N) if DEP[x] <= rng.choice((3, 4, 5, 6))]

        def mk():
            b = rng.sample(pool, rng.randint(5, 10))
            tw = rng.choice((0, 0, 0, TW_CHARGE, TW_SKYALL, TW_ROOT, TW_LOCK, TW_COMPILE, TW_CHARGE | TW_LOCK))
            return b, rng.randint(4, 14), tw

        sides = [mk(), mk()]
        ai = [
            None
            if (g % 3 == 0 and k == 0)
            else (rng.randrange(65536), rng.choice((0, 64, 154, 200, 256)), rng.choice((0, 77, 128, 230, 256)))
            for k in range(2)
        ]
        first = rng.randrange(2)
        r0 = rng.randrange(1, 65536)
        st = State(Side(sides[0][0], sides[0][1], sides[0][2]), Side(sides[1][0], sides[1][1], sides[1][2]), first)
        r = [r0]
        turns = []
        while not st.over():
            k = st.act
            st.begin_turn()
            if ai[k] is None:
                w = rng.choice(st.legal(k))
            else:
                w = ai_choose(st, k, ai[k][0], ai[k][1], ai[k][2], r)
            st.do(w)
            turns.append([w if ai[k] is None else -1, w, st.hash()])
        out.append(
            {
                "sides": [list(s) for s in sides],
                "ai": ai,
                "first": first,
                "rng": r0,
                "turns": turns,
                "over": st.over(),
                "hp": [st.s[0].hp, st.s[1].hp],
            }
        )
    return out


if __name__ == "__main__":
    if "--golden" in sys.argv:
        g = golden(int(os.environ.get("N", "200")))
        json.dump(g, open(sys.argv[sys.argv.index("--golden") + 1], "w"))
        print(len(g), "bouts,", sum(len(x["turns"]) for x in g), "turns")
    if "--demo" in sys.argv:
        demo(int(sys.argv[sys.argv.index("--demo") + 1]))
