#!/usr/bin/env python3
"""Boss fights: tiers, faction personalities, tells, learning; nemesis rematches; co-op handoff.
Uses the versus rules in fightsim.py (stances, edges, loadouts). Deterministic per seed."""

import collections
import random
import sys

import fightsim as F
from fightsim import ITEMS, beats, edges

HUM, WALL, DREAM = 0, 1, 2


def beater(s):
    return (s + 1) % 3  # the stance that beats s


FACS = ["PROGRAM", "DAEMON", "GHOST", "AI", "OPERATOR", "RELIC"]
LIKE_CAT = [5, 3, 2, 1, 4, 6]

# tier table (generated in the cartridge as base + tier*step; listed here for the sim and the doc)
#            hp  pow cue(pips) player-shelf-depth
#        hp  pow pips depth  exec-error  last-pip miss
TIER = {
    1: (22, 2, 4, 4, 0.04, 0.05),
    2: (30, 3, 4, 5, 0.05, 0.08),
    3: (30, 3, 3, 6, 0.06, 0.10),
    4: (30, 3, 3, 7, 0.08, 0.14),
    5: (28, 4, 2, 8, 0.10, 0.18),
    6: (30, 4, 2, 9, 0.12, 0.22),
}
# signature mechanics per faction, in the order the tiers add them (T1 gets the first, T2 the second...)
SIG = {
    "PROGRAM": ["LOOP", "DOUBLE", "FEINT", "SHIELD", "COMPILE"],
    "DAEMON": ["CHARGE", "DOUBLE", "STEAL", "FEINT", "OVERHEAT"],
    "GHOST": ["STEAL", "FEINT", "HAUNT", "CHARGE", "POSSESS"],
    "AI": ["MIRROR", "ADAPT", "SHIELD", "FEINT", "PREDICT"],
    "OPERATOR": ["GROW", "STEAL", "DOUBLE", "ADAPT", "JACK"],
    "RELIC": ["ARMOR", "SHIELD", "CHARGE", "HAUNT", "ERODE"],
}
NEW_KNOW, OLD_KNOW, RETRY_KNOW, DEMO_KNOW = 0.30, 0.92, 0.85, 0.55
VEIL = {1: 0.2, 2: 0.35, 3: 0.55, 4: 0.7, 5: 0.85, 6: 1.0}  # share of strikes shown as ingredients + '?' only
RECIPE_KNOWN, LEAN_RULE = 0.5, 0.74
CHIP = {
    1: 3,
    2: 3,
    3: 3,
    4: 3,
    5: 2,
    6: 2,
}  # you made it before / the 'shared stance, else the deeper half' rule   # how often a competent player reads a tell: first sight, taught, after a loss


class Boss:
    def __init__(s, fac, tier, rng, memory=None):
        s.fac = fac
        s.tier = tier
        hp, s.pow, s.cue, _, s.err, s.miss = TIER[tier]
        if fac == "RELIC" and tier >= 4:
            hp -= 6  # armour already doubles its effective HP
        if fac == "AI" and tier >= 4:
            hp -= 2
        s.hp = s.max = hp
        s.moves = SIG[fac][
            : min(5, tier)
        ]  # T1 one mechanic ... T5 all five; T6 = T5 + faster/heavier (and phase moves)
        s.home = F.CAT_FAM[LIKE_CAT[FACS.index(fac)]]
        s.pool = [i["id"] for i in ITEMS if i["cat"] == F.CATS[LIKE_CAT[FACS.index(fac)]] and i["d"] <= 3 + tier]
        s.loop = [s.home, beater(s.home), beater(beater(s.home))]
        rng.shuffle(s.loop)
        s.li = 0
        s.charged = False
        s.cool = {}
        s.seen = set()
        s.shield = None
        s.shield_t = 0
        s.memory = memory
        s.phase = 0
        s.erode = 0
        s.rng = rng


def boss_move(b, pl, t):
    """what the boss does this round: (kind, shown stance, real stance, power, attacks)"""
    r = b.rng
    ms = list(b.moves)
    if b.phase >= 1 and b.tier >= 3:
        ms = ms + ms[-1:]  # a phase leans on its newest trick
    kind = "STRIKE" if r.random() < 0.45 - 0.05 * b.tier else r.choice(ms)
    if kind != "STRIKE" and b.cool.get(kind, -9) >= t - 2:
        kind = "STRIKE"  # every trick has a 3-round cooldown
    if kind != "STRIKE":
        b.cool[kind] = t
    st = r.choice([b.home, b.home, beater(b.home), beater(beater(b.home))])
    if "LOOP" in b.moves or "COMPILE" in b.moves:
        st = b.loop[b.li % 3]
        b.li += 1
    if b.memory is not None and t <= 4:  # nemesis: shields what beat it last time and opens on its beater
        st = beater(b.memory)
        kind = "RECALL"
    if b.memory is not None and t <= 5:
        b.shield = b.memory
        b.shield_t = 1  # and keeps it shut for five rounds
    real = st
    pw = b.pow + (b.phase >= 2)
    n = 1
    if kind == "FEINT":
        real = beater(beater(st))  # shown s, real s+2: it beats your natural answer; mirror it
    if kind == "CHARGE" and not b.charged:
        b.charged = True
        return ("CHARGE", None, None, 0, 0)
    if b.charged:
        pw *= 2
        b.charged = False
    if kind in ("DOUBLE", "OVERHEAT"):
        n = 2
    if kind == "MIRROR" and pl.last is not None:
        st = real = pl.last
    if kind in ("ADAPT", "PREDICT") and pl.hist:
        fav = collections.Counter(pl.hist[-4:]).most_common(1)[0][0]
        st = real = beater(fav)
    if kind == "SHIELD" and b.shield is None:
        b.shield = pl.last if pl.last is not None else HUM
        b.shield_t = 2
    if kind == "ERODE":
        b.erode = 1  # each erode: your hand shrinks by one for the fight
    return (kind, st, real, pw, n)


def know(pl, kind):
    if kind in ("STRIKE", "CHARGE"):
        return 1.0
    return pl.know.get(kind, NEW_KNOW)


def answer(pl, b, mv, rng):
    """a competent player: read the tell (or not), keep coverage, guard when nothing answers"""
    kind, shown, real, pw, n = mv
    if kind == "CHARGE":  # a free round: play the biggest card into it, or fuse
        return ["free"]
    seen = real if rng.random() < know(pl, kind) else shown
    if kind == "STRIKE" and rng.random() < VEIL[b.tier]:
        p = RECIPE_KNOWN + (1 - RECIPE_KNOWN) * LEAN_RULE
        if rng.random() >= p:
            seen = rng.choice([x for x in range(3) if x != real])
    if kind == "HAUNT" and rng.random() >= know(pl, kind):
        seen = rng.randrange(3)
    picks = []
    hand = list(range(len(pl.hand)))
    for k in range(n):
        best = None
        for i in hand:
            if any(p and p[0] == i for p in picks):
                continue
            c = ITEMS[pl.hand[i]]
            for s in c["s"]:
                if b.shield is not None and s == b.shield:
                    continue
                sc = (3 if beats(s, seen) else 1 if s == seen else -2) * 10 + c["p"]
                if best is None or sc > best[0]:
                    best = (sc, i, s)
        if best and best[0] > 0:
            picks.append((best[1], best[2]))
        else:
            picks.append(None)
    return picks


def boss_fight(seed, fac, tier, know, attrs=(0, 0), memory=None, coop=None):
    rng = random.Random(seed)
    b = Boss(fac, tier, random.Random(seed ^ 0x5EED), memory)
    shelf_rng = random.Random(seed ^ 0x77)
    F.R["BUDGET"] = 12 + attrs[1]
    # the player's shelf grows with the campaign: depth cap by tier
    pool = [i["id"] for i in ITEMS if i["d"] <= TIER[tier][3]]
    sh = shelf_rng.sample(pool, 40)
    out = []
    for s in (0, 1, 2, 0, 1, 2):
        c = [i for i in sh if ITEMS[i]["s"] == [s] and i not in out]
        c.sort(key=lambda i: -ITEMS[i]["p"] + shelf_rng.random())
        out.append(c[0] if c else shelf_rng.choice(sh))
    deck = F.fit(out, shelf_rng, sh)
    F.R["BUDGET"] = 12
    pl = F.P(deck, rng, [], "A")
    pl.hp = 12 + attrs[0]
    pl.know = dict(know)
    pl.hist = []
    won_stance = collections.Counter()
    t = 0
    while pl.hp > 0 and b.hp > 0 and t < 40:
        t += 1
        pl.hand = pl.hand[: max(1, 3 - b.erode)] if b.erode else pl.hand
        pl.draw(rng)
        if b.erode:
            pl.hand = pl.hand[: max(1, 3 - b.erode)]
        mv = boss_move(b, pl, t)
        kind, shown, real, pw, n = mv
        demo = kind not in ("STRIKE", "CHARGE") and kind not in b.seen  # a new trick is rehearsed once at half force
        if demo:
            b.seen.add(kind)
            mv = (kind, shown, real, max(1, pw // 2), n)
            pw = mv[3]
        elif kind in b.seen and pl.know.get(kind, NEW_KNOW) < DEMO_KNOW:
            pl.know[kind] = DEMO_KNOW
        if coop:
            picks = coop(pl, b, mv, rng)
        else:
            picks = answer(pl, b, mv, rng)
        if picks == ["free"]:
            c = max(range(len(pl.hand)), key=lambda i: ITEMS[pl.hand[i]]["p"]) if pl.hand else None
            if c is not None:
                d = ITEMS[pl.hand[c]]["p"]
                if "ARMOR" in b.moves:
                    d = min(d, 1)
                b.hp -= d
                pl.disc.append(pl.hand.pop(c))
            continue
        bitem = rng.choice(b.pool) if b.pool else 0
        used = []
        for k, pk in enumerate(picks):
            if pk is None:
                pl.hp -= (pw + 1) // 2  # guard: half
                pl.focus = min(3, pl.focus + 1)
                continue
            i, s = pk
            card = pl.hand[i]
            c = ITEMS[card]
            if rng.random() < b.err:  # a slip under the pips: the wrong card
                i = rng.randrange(len(pl.hand))
                card = pl.hand[i]
                c = ITEMS[card]
                s = c["s"][0]
            if i in used:
                continue
            used.append(i)
            close = rng.random() < 0.5  # going for the last pip
            late = close and rng.random() < b.miss  # and missing it
            if late:
                pl.hp -= pw + 1
                continue
            if b.shield is not None and s == b.shield:
                pass
            elif beats(s, real):
                d = c["p"] + min(1, edges(card, bitem)) + (1 if close else 0)
                if "ARMOR" in b.moves and not close:
                    d = min(d, 3)
                b.hp -= d
                won_stance[s] += 1
            elif s == real:
                b.hp -= min(1, edges(card, bitem))
                pl.hp -= min(1, edges(bitem, card))
            else:
                pl.hp -= pw + min(1, edges(bitem, card))
                if kind == "STEAL":
                    pl.deck = [x for x in pl.deck if x != card]
            pl.hist.append(s)
            pl.last = s
        for i in sorted(used, reverse=True):
            pl.disc.append(pl.hand.pop(i))
        if CHIP[b.tier] and t % CHIP[b.tier] == 0:
            pl.hp -= 1  # the room hums: a slow drain that punishes stalling
        if kind == "GROW" and b.hp > 0:
            b.hp = min(b.max, b.hp + 1)
        if kind == "JACK" and pl.hand:
            pl.hand.pop()  # it unplugs a card from your hand
        if b.shield_t:
            b.shield_t -= 1
        if not b.shield_t:
            b.shield = None
        if b.hp <= b.max // 4:
            b.phase = 2
        elif b.hp <= b.max // 2:
            b.phase = 1
    fav = won_stance.most_common(1)[0][0] if won_stance else None
    return b.hp <= 0, t, fav


def campaign_report(n):
    out = [
        "### Boss first-try and retry win rate by tier (competent player, %d fights per faction per tier)" % n,
        "| tier | " + " | ".join(FACS) + " | all, first try | all, after one loss | turns |",
        "|" + "---|" * (len(FACS) + 4),
    ]
    for tier in range(1, 7):
        row = []
        tot = 0
        tot2 = 0
        tl = 0
        for fac in FACS:
            w = 0
            for k in range(n):
                know = {m: OLD_KNOW for m in SIG[fac][: tier - 1]}
                know.update({m: NEW_KNOW for m in SIG[fac][tier - 1 :]})
                if tier == 6:
                    know = {m: OLD_KNOW for m in SIG[fac]}
                ok, t, _ = boss_fight(
                    k * 977 + tier * 31 + FACS.index(fac),
                    fac,
                    tier,
                    know,
                    attrs=(min(3, tier - 1) // 1, min(2, (tier - 1) // 2)),
                )
                w += ok
                tl += t
                if not ok:  # the retry: the tell it beat you with is now known
                    know2 = {m: max(v, RETRY_KNOW) for m, v in know.items()}
                    ok2, _, _ = boss_fight(
                        k * 977 + tier * 31 + FACS.index(fac) + 1,
                        fac,
                        tier,
                        know2,
                        attrs=(min(3, tier - 1), min(2, (tier - 1) // 2)),
                    )
                    tot2 += ok2
                else:
                    tot2 += 1
            row.append(w / n)
            tot += w
        out.append(
            "| T%d | " % tier
            + " | ".join("%.0f%%" % (100 * x) for x in row)
            + " | **%.0f%%** | %.0f%% | %.1f |" % (100 * tot / (n * 6), 100 * tot2 / (n * 6), tl / (n * 6))
        )
    # nemesis: a rematch opening on your favourite winning stance
    out.append("\n### Nemesis rematch (T3 GHOST): first meeting, then a rematch that remembers your best stance")
    a = b2 = c2 = 0
    for k in range(n * 2):
        know = {m: OLD_KNOW for m in SIG["GHOST"][:2]}
        know.update({m: NEW_KNOW for m in SIG["GHOST"][2:]})
        ok, t, fav = boss_fight(k * 13 + 5, "GHOST", 3, know, attrs=(2, 1))
        a += ok
        know2 = {m: max(v, RETRY_KNOW) for m, v in know.items()}
        okm, _, _ = boss_fight(k * 13 + 6, "GHOST", 3, know2, attrs=(2, 1), memory=fav if fav is not None else 0)
        b2 += okm
        # a player who reads RECALL (varies their opening) beats it like a plain rematch
        okv, _, _ = boss_fight(k * 13 + 6, "GHOST", 3, know2, attrs=(2, 1), memory=None)
        c2 += okv
    out.append(
        "- first meeting %.0f%%; rematch with memory %.0f%%; same rematch without memory %.0f%% (the memory costs %.0f points until you vary your opening)"
        % (100 * a / (n * 2), 100 * b2 / (n * 2), 100 * c2 / (n * 2), 100 * (c2 - b2) / (n * 2))
    )
    return "\n".join(out)


# ---------------- co-op: one scene, control handed over per beat ----------------
def coop_report(n):
    out = [
        "### Co-op boss (T3, all factions): control passes per beat from the session seed",
        "| pairing | strong solo | weak solo | co-op, no influence | co-op + nudge | co-op + veto (1 per phase) |",
        "|---|---|---|---|---|---|",
    ]

    def mk(strong_know, weak_know, mode):
        state = {"veto": 1, "phase": 0}

        def pick(pl, b, mv, rng):
            if b.phase != state["phase"]:
                state["phase"] = b.phase
                state["veto"] = 1
            strong_turn = rng.random() < 0.5  # host/guest from the session seed
            act = strong_know if strong_turn else weak_know
            if mode == "nudge" and not strong_turn:
                # the watcher marks a card; the active player takes the hint when their own read is unsure
                act = {k: max(v, (v + strong_know.get(k, v)) / 2) for k, v in weak_know.items()}
            if mode == "veto" and not strong_turn and state["veto"] and mv[0] not in ("STRIKE", "RECALL", "CHARGE"):
                state["veto"] -= 1
                act = strong_know  # the watcher takes this beat
            pl.know = act
            return answer(pl, b, mv, rng)

        return pick

    for sk, wk, name in ((OLD_KNOW, NEW_KNOW, "veteran + newcomer"), (OLD_KNOW, 0.6, "veteran + casual")):
        res = []
        for mode in ("strong", "weak", "plain", "nudge", "veto"):
            w = 0
            for k in range(n):
                fac = FACS[k % 6]
                S = {m: sk for m in SIG[fac]}
                Wk = {m: wk for m in SIG[fac]}
                if mode == "strong":
                    ok, _, _ = boss_fight(k * 7 + 1, fac, 3, S, attrs=(2, 1))
                elif mode == "weak":
                    ok, _, _ = boss_fight(k * 7 + 1, fac, 3, Wk, attrs=(2, 1))
                else:
                    ok, _, _ = boss_fight(k * 7 + 1, fac, 3, S, attrs=(2, 1), coop=mk(S, Wk, mode))
                w += ok
            res.append(w / n)
        out.append("| %s | " % name + " | ".join("%.0f%%" % (100 * x) for x in res) + " |")
    return "\n".join(out)


if __name__ == "__main__":
    n = 60 if "--quick" in sys.argv else 250
    print(campaign_report(n))
    print()
    print(coop_report(n * 2))
