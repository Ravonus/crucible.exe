#!/usr/bin/env python3
"""The cartridge's deterministic fight engine, in Python: the simulator's rules (fightsim.py) driven by frng, the 16-bit
xorshift the cartridge uses, plus the duel AI the cartridge runs (integer only, faction personalities). The C engine
(chromatic/plugins/crucible/engine/src/crucible_fight_rules.c) must match this turn by turn; golden vectors are exported
from here (python3 fightref.py --golden) and checked against a host C build of the engine and the ROM (PyBoy).

Actions are bytes, the P_FTURN layout: kind:2 (0 PLAY, 1 FUSE, 2 GUARD) | a:2 | b:2 | stance:2 (bits 7..6 kind,
5..4 hand slot a, 3..2 hand slot b, 1..0 stance)."""

import collections
import json
import os
import random
import sys

import fightsim as F
from fightsim import PASSIVES, P, R, act_card, actions, clash, turn

HERE = os.path.dirname(os.path.abspath(__file__))
PLAY, FUSE, GUARD = 0, 1, 2


class Frng:
    """16-bit xorshift (7, 9, 8), the cartridge's fight rng; below(n) is an 8x8 multiply-shift, the shuffle Fisher-Yates
    from the top"""

    def __init__(self, seed):
        self.x = (seed & 0xFFFF) or 0x1D2B

    def next(self):
        x = self.x
        x ^= (x << 7) & 0xFFFF
        x ^= x >> 9
        x ^= (x << 8) & 0xFFFF
        self.x = x
        return x

    def below(self, n):
        return ((self.next() & 255) * n) >> 8

    def shuffle(self, lst):
        for i in range(len(lst) - 1, 0, -1):
            j = self.below(i + 1)
            lst[i], lst[j] = lst[j], lst[i]

    # the simulator's policies are never fed an Frng (they use floats); refuse loudly
    def random(self):
        raise TypeError("Frng drives shuffles only")


def encode(p, a):
    """a simulator action tuple -> the action byte"""
    if a[0] == "G":
        return GUARD << 6
    if a[0] == "P":
        return (PLAY << 6) | (a[1] << 4) | a[2]
    return (FUSE << 6) | (a[1] << 4) | (a[2] << 2) | a[3]


def decode(p, byte):
    """an action byte -> the simulator's tuple for side p (None when it is not legal now)"""
    k, i, j, st = byte >> 6, (byte >> 4) & 3, (byte >> 2) & 3, byte & 3
    for a in actions(p):
        if encode(p, a) == byte and (k != FUSE or (a[1], a[2]) == (i, j)):
            return a
    return None


# ---------------- the duel AI (the cartridge's crucible_fight_rules.c, fr_ai) ----------------
# personalities by faction (section 5): PROGRAM loops, DAEMON big cards, GHOST plays the stance you just beat,
# AI counts your last 4 plays, OPERATOR guards when hurt, RELIC fuses whenever it can.
def habit_init(p):
    p.after = [[0, 0, 0] for _ in range(4)]
    p.hist4 = []
    p.turns = 0
    p.repeats = 0


def habit_note(p, card):
    """after a turn, before p.last moves: what p played (its own habits, which the other side reads)"""
    if card is None:
        return
    st = card[1]
    prev = p.last if p.last is not None else 3
    if p.after[prev][st] < 15:
        p.after[prev][st] += 1
    if p.turns < 255:
        p.turns += 1
    if st == p.last and p.repeats < 255:
        p.repeats += 1
    p.hist4 = (p.hist4 + [st])[-4:]


def weight(op, b, persona, uniform):
    c = act_card(op, b)
    if c is None:
        return (
            2
            if uniform
            else 2 + (3 if op.focus == 0 and op.hand else 0) + (3 if "VIGIL" in op.pas and "VIGIL" not in op.on else 0)
        )
    if uniform:
        return 4
    st, pw = c[1], c[2]
    w = 4 + min(pw, 4) + 2 * op.after[op.last if op.last is not None else 3][st]
    if st == op.last and op.turns and op.repeats * 2 >= op.turns:
        w += 3
    for k in op.pas:
        if k in op.on:
            continue
        if k == "ECHO" and st == op.last:
            w += 3
        elif k == "PRISM" and st not in op.window[-2:]:
            w += 2
        elif k == "NOCLIP" and st == 2:
            w += 2
        elif k == "OVERCLOCK" and pw >= 3:
            w += 2
        elif k == "SALVE" and pw == 1:
            w += 2
        elif k == "MEMORY" and c[0] in op.played:
            w += 2
    if persona == 3:
        w += 3 * op.hist4.count(st)
    return min(w, 31)


def bias(me, op, a, persona, t, loop):
    c = act_card(me, a)
    if persona == 0:
        return 6 if c and c[1] == loop[t % 3] else 0
    if persona == 1:
        return 2 * (c[2] - 1) if c else 0
    if persona == 2:
        return 5 if c and op.last is not None and c[1] == (op.last + 2) % 3 else 0
    if persona == 4:
        if c is None:
            return 8 if me.hp * 2 <= me.max else -2
        return 0
    if persona == 5:
        return 6 if a[0] == "F" else 0
    return 0


def ai_choose(me, op, rng, persona, t, loop, skill, uniform=False):
    """the AI's action for this turn: the best expected trade against a model of the opponent's options, sometimes the
    second best (1 in skill), with the faction's bias. Integer only; ties go to the first in action order."""
    acts = actions(me)
    opps = actions(op)
    ws = [weight(op, b, persona, uniform) for b in opps]
    W = sum(ws)
    scored = []
    for a in acts:
        sc = 0
        for b, w in zip(opps, ws):
            da, db, _ = clash(me, a, op, b)
            sc += w * (da - db)
        sc += bias(me, op, a, persona, t, loop) * (W >> 3)
        scored.append(sc)
    best = 0
    for i in range(1, len(acts)):
        if scored[i] > scored[best]:
            best = i
    second = -1
    for i in range(len(acts)):
        if i != best and (second < 0 or scored[i] > scored[second]):
            second = i
    if second >= 0 and rng.below(skill) == 0:
        return acts[second]
    return acts[best]


# ---------------- a deterministic bout ----------------
def side(kit, rng, pas, name, hp, focus):
    p = P(list(kit), rng, list(pas), name)
    p.hp = p.max = hp
    p.focus = focus
    habit_init(p)
    return p


def fsum(A, B, rng, t):
    """the 8-bit state hash before a turn (P_FTURN b.lo): both HP, focus, deck tops, rng.lo, turn"""
    s = (A.hp & 255) + (B.hp & 255) * 3 + A.focus * 5 + B.focus * 7 + (rng.x & 255) + t * 11
    s += len(A.deck) * 13 + len(B.deck) * 17 + len(A.disc) + len(B.disc) * 19
    return s & 255


def run(seed, kit_a, kit_b, pas_a, pas_b, choose_a, choose_b, hp=(12, 12), focus=(0, 0), rules=None, ai_seed=None):
    """one bout, both sides' choices from callbacks (A, B, t) -> action tuple. Returns the trace."""
    saved = dict(R)
    R.update(rules or {})
    try:
        rng = Frng(seed)
        A = side(kit_a, rng, pas_a, "A", hp[0], focus[0])
        B = side(kit_b, rng, pas_b, "B", hp[1], focus[1])
        trace = []
        t = 0
        while t < R["TURNS"] and A.hp > 0 and B.hp > 0:
            t += 1
            A.draw(rng)
            B.draw(rng)
            h = fsum(A, B, rng, t)
            hand_a, hand_b = list(A.hand), list(B.hand)
            a = choose_a(A, B, t)
            b = choose_b(B, A, t)
            ea, eb = encode(A, a), encode(B, b)
            ca, cb = act_card(A, a), act_card(B, b)
            dA, dB, out = turn(A, B, a, b, rng, None, t)
            # habits are noted after the turn with the cards it played (last still moved by turn(): use the trace's)
            trace.append(
                {
                    "t": t,
                    "a": ea,
                    "b": eb,
                    "hash": h,
                    "handA": hand_a,
                    "handB": hand_b,
                    "out": 9 if out is None else out,
                    "hp": [A.hp, B.hp],
                    "focus": [A.focus, B.focus],
                    "on": [onbits(A), onbits(B)],
                    "prog": [progs(A), progs(B)],
                    "deck": [len(A.deck), len(B.deck)],
                    "disc": [len(A.disc), len(B.disc)],
                    "rng": rng.x,
                }
            )
        w = 0 if (A.hp <= 0 and B.hp <= 0) or A.hp == B.hp else 1 if (B.hp <= 0 or (A.hp > B.hp and A.hp > 0)) else -1
        return {"turns": t, "win": w, "trace": trace}
    finally:
        R.clear()
        R.update(saved)


def onbits(p):
    return sum(1 << i for i, k in enumerate(p.pas) if k in p.on)


def progs(p):
    return [p.prog[k] for k in p.pas]


def note_after(fn):
    """wrap a chooser so the side's habit record follows what it played (the AI reads the opponent's)"""
    return fn


# a recorded chooser: replays bytes
def replay(bytes_):
    it = iter(bytes_)

    def f(me, op, t):
        b = next(it)
        a = decode(me, b)
        assert a is not None, f"illegal recorded action {b:#x} at turn {t}"
        return a

    return f


def habit_wrap(choose):
    """the habit record is kept for both sides every turn (the AI reads it); the chooser sees it as of before the turn"""

    def f(me, op, t):
        return choose(me, op, t)

    return f


PASS_IDS = {k: i for i, k in enumerate(PASSIVES)}


def ai_bout(
    seed, kit_a, kit_b, pas_a, pas_b, choose_a, persona, skill, hp=(12, 12), focus=(0, 0), rules=None, flicker=False
):
    """player A (recorded or a sim policy) against the cartridge's AI as B. The AI rng is seeded from the fight seed."""
    ai = Frng(seed ^ 0xA15E)
    loop = [0, 1, 2]
    ai.shuffle(loop)

    def b_choose(me, op, t):
        return ai_choose(me, op, ai, persona, t, loop, skill, uniform=flicker and t % 3 == 0)

    saved = dict(R)
    R.update(rules or {})
    try:
        rng = Frng(seed)
        A = side(kit_a, rng, pas_a, "A", hp[0], focus[0])
        B = side(kit_b, rng, pas_b, "B", hp[1], focus[1])
        trace = []
        t = 0
        while t < R["TURNS"] and A.hp > 0 and B.hp > 0:
            t += 1
            A.draw(rng)
            B.draw(rng)
            h = fsum(A, B, rng, t)
            hand_a, hand_b = list(A.hand), list(B.hand)
            a = choose_a(A, B, t)
            b = b_choose(B, A, t)
            ea, eb = encode(A, a), encode(B, b)
            ca, cb = act_card(A, a), act_card(B, b)
            la, lb = A.last, B.last
            dA, dB, out = turn(A, B, a, b, rng, None, t)
            A.last, B.last = la, lb
            habit_note(A, ca)
            habit_note(B, cb)
            A.last = ca[1] if ca else None
            B.last = cb[1] if cb else None
            trace.append(
                {
                    "t": t,
                    "a": ea,
                    "b": eb,
                    "hash": h,
                    "handA": hand_a,
                    "handB": hand_b,
                    "out": 9 if out is None else out,
                    "hp": [A.hp, B.hp],
                    "focus": [A.focus, B.focus],
                    "on": [onbits(A), onbits(B)],
                    "prog": [progs(A), progs(B)],
                    "deck": [len(A.deck), len(B.deck)],
                    "disc": [len(A.disc), len(B.disc)],
                    "rng": rng.x,
                    "ai": ai.x,
                }
            )
        w = 0 if (A.hp <= 0 and B.hp <= 0) or A.hp == B.hp else 1 if (B.hp <= 0 or (A.hp > B.hp and A.hp > 0)) else -1
        return {"turns": t, "win": w, "trace": trace, "loop": loop}
    finally:
        R.clear()
        R.update(saved)


# ---------------- golden vectors ----------------
def pick_kit(rng, arch):
    return F.build(arch, rng)


def sim_policy(name, prng):
    """a simulator policy as a chooser (its floats come from its own random.Random; only its choices are recorded)"""
    st = {
        "model": {
            n: {"after": collections.Counter(), "n": collections.Counter(), "repeat": 0, "turns": 0} for n in "AB"
        }
    }
    pol = F.POLICIES[name]

    def f(me, op, t):
        return pol(me, op, prng, st)

    return f


RULESETS = [
    dict(),
    dict(ARENA=0),
    dict(ARENA=1),
    dict(ARENA=2),
    dict(ARENA=4),
    dict(ARENA=5),
    dict(ARENA=6),
    dict(SUDDEN=1),
    dict(NOSTACK=1),
    dict(NIGHT=1),
]


def golden(out_dir, n_clash=2000, n_duel=300, n_ai=100):
    os.makedirs(out_dir, exist_ok=True)
    r = random.Random(20261004)
    # 1. clash cases: two random sides mid-bout, every pair of their actions
    clashes = []
    while len(clashes) < n_clash:
        rules = r.choice(RULESETS)
        saved = dict(R)
        R.update(rules)
        try:
            ka, kb = F.build(r.choice(F.ARCHES), r), F.build(r.choice(F.ARCHES), r)
            fr = Frng(r.randrange(1, 65536))
            A = side(ka, fr, r.sample(PASSIVES, 2), "A", r.randrange(1, 17), r.randrange(0, 4))
            B = side(kb, fr, r.sample(PASSIVES, 2), "B", r.randrange(1, 17), r.randrange(0, 4))
            for p in (A, B):
                p.on = {k for k in p.pas if r.random() < 0.4}
                p.last = r.choice([None, 0, 1, 2])
                p.lostrow = r.randrange(0, 3)
                for _ in range(r.randrange(0, 4)):  # some cards cycled through
                    p.draw(fr)
                    if p.hand:
                        p.disc.append(p.hand.pop(r.randrange(len(p.hand))))
                p.draw(fr)
            t = r.randrange(1, 31)
            F.CTX["turn"] = t
            for a in actions(A):
                for b in actions(B):
                    if len(clashes) >= n_clash or r.random() > 0.35:
                        continue
                    da, db, out = clash(A, a, B, b)
                    clashes.append(
                        {
                            "rules": rules,
                            "t": t,
                            "A": dump_side(A),
                            "B": dump_side(B),
                            "a": encode(A, a),
                            "b": encode(B, b),
                            "toB": da,
                            "toA": db,
                            "out": 9 if out is None else out,
                        }
                    )
        finally:
            R.clear()
            R.update(saved)
    json.dump(clashes, open(os.path.join(out_dir, "clash.json"), "w"))
    # 2. full duels, both sides' inputs recorded (sim policies), final state per turn
    duels = []
    pols = ["reader", "greedy", "rusher", "stacker", "random"]
    for k in range(n_duel):
        rules = RULESETS[k % len(RULESETS)]
        rr = random.Random(k * 7919 + 3)
        ka, kb = F.build(rr.choice(F.ARCHES), rr), F.build(rr.choice(F.ARCHES), rr)
        pa, pb = rr.sample(PASSIVES, rr.choice([0, 1, 2])), rr.sample(PASSIVES, rr.choice([1, 2]))
        hp = (rr.choice([8, 12, 12, 14, 16]), rr.choice([8, 12, 12, 13]))
        fo = (rr.randrange(0, 3), rr.randrange(0, 3))
        seed = rr.randrange(1, 65536)
        res = run(
            seed,
            ka,
            kb,
            pa,
            pb,
            sim_policy(pols[k % 5], random.Random(k)),
            sim_policy(pols[(k // 5) % 5], random.Random(k + 1)),
            hp,
            fo,
            rules,
        )
        duels.append(
            {
                "seed": seed,
                "kitA": ka,
                "kitB": kb,
                "pasA": [PASS_IDS[x] for x in pa],
                "pasB": [PASS_IDS[x] for x in pb],
                "hp": hp,
                "focus": fo,
                "rules": rules,
                **res,
            }
        )
    json.dump(duels, open(os.path.join(out_dir, "duels.json"), "w"))
    # 3. the AI: a recorded player against the cartridge's AI, every persona and skill
    ais = []
    for k in range(n_ai):
        rules = RULESETS[k % len(RULESETS)]
        rr = random.Random(k * 104729 + 11)
        ka, kb = F.build(rr.choice(F.ARCHES), rr), F.build("balanced", rr)
        pa, pb = rr.sample(PASSIVES, rr.choice([1, 2])), rr.sample(PASSIVES, rr.choice([0, 1, 2]))
        persona, skill = k % 6, [2, 4, 6][k % 3]
        seed = rr.randrange(1, 65536)
        flick = k % 7 == 3
        res = ai_bout(
            seed, ka, kb, pa, pb, sim_policy("reader", random.Random(k)), persona, skill, (12, 8), (0, 0), rules, flick
        )
        ais.append(
            {
                "seed": seed,
                "kitA": ka,
                "kitB": kb,
                "pasA": [PASS_IDS[x] for x in pa],
                "pasB": [PASS_IDS[x] for x in pb],
                "hp": [12, 8],
                "focus": [0, 0],
                "rules": rules,
                "persona": persona,
                "skill": skill,
                "flicker": flick,
                **res,
            }
        )
    json.dump(ais, open(os.path.join(out_dir, "ai.json"), "w"))
    return len(clashes), len(duels), len(ais)


def dump_side(p):
    return {
        "hand": list(p.hand),
        "deck": list(p.deck),
        "disc": list(p.disc),
        "hp": p.hp,
        "max": p.max,
        "focus": p.focus,
        "pas": [PASS_IDS[x] for x in p.pas],
        "on": onbits(p),
        "last": 3 if p.last is None else p.last,
        "lostrow": p.lostrow,
    }


if __name__ == "__main__":
    if "--golden" in sys.argv:
        print(golden(os.path.join(HERE, "golden")))
