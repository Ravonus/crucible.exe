#!/usr/bin/env python3
"""Avatar attributes in link versus: FULL / CAPPED / NORMALISED rules, and the handicap the rules screen suggests."""

import random
import sys

import fightsim as F

# attributes: GRIT (+1 HP each), FOCUS (+1 starting focus each), REACH (+1 loadout budget each); SLOTS (passive slots)
LOW = dict(grit=0, focus=0, reach=0, slots=1)  # level 1
MID = dict(grit=2, focus=1, reach=1, slots=2)  # the NORMALISED versus profile
HIGH = dict(grit=4, focus=2, reach=3, slots=2)  # level 30, story caps
CAP = dict(grit=2, focus=1, reach=1, slots=2)


def rule(a, mode):
    if mode == "NORMALISED":
        return dict(MID)
    if mode == "CAPPED":
        return {k: min(a[k], CAP[k]) for k in a}
    return dict(a)


def power(a):
    return a["grit"] + 2 * a["focus"] + a["reach"] + 2 * (a["slots"] - 1)


def suggest(a, b):
    """the rules screen's handicap: HP to the weaker side, 2 per 3 points of attribute gap (max 8)"""
    gap = power(a) - power(b)
    return (0, min(8, (gap * 2) // 3)) if gap > 0 else (min(8, (-gap * 2) // 3), 0)


def play(seed, pa, pb, a, b, hcap=(0, 0)):
    rng = random.Random(seed ^ 0xBEEF)
    F.R["BUDGET"] = 12 + a["reach"]
    la = F.build("balanced", rng)
    F.R["BUDGET"] = 12 + b["reach"]
    lb = F.build("balanced", rng)
    F.R["BUDGET"] = 12
    r2 = random.Random(seed)
    pas_a = r2.sample(F.PASSIVES, 2)[: a["slots"]]
    pas_b = r2.sample(F.PASSIVES, 2)[: b["slots"]]
    hp0, f0 = F.R["HP"], F.R["FOCUS_START"]
    # per-side HP / focus: patch P after construction through a wrapper
    orig = F.P.__init__

    def init(s, deck, rng_, passives, name):
        orig(s, deck, rng_, passives, name)
        x = a if name == "A" else b
        h = hcap[0] if name == "A" else hcap[1]
        s.hp = s.max = hp0 + x["grit"] + h
        s.focus = min(F.R["FOCUS_MAX"], f0 + x["focus"])

    F.P.__init__ = init
    try:
        w, t, A, B = F.bout(seed, pa, pb, la, lb, pas_a, pas_b)
    finally:
        F.P.__init__ = orig
    return w


def rate(n, pa, pb, a, b, mode, hc=False):
    s = 0
    for k in range(n):
        x, y = rule(a, mode), rule(b, mode)
        h = suggest(x, y) if hc else (0, 0)
        # alternate sides so host order cancels
        if k & 1:
            w = -play(k * 31 + 7, pb, pa, y, x, (h[1], h[0]))
        else:
            w = play(k * 31 + 7, pa, pb, x, y, h)
        s += (w == 1) + (w == 0) / 2
    return s / n


if __name__ == "__main__":
    n = 120 if "--quick" in sys.argv else 600
    out = [
        "### Level 30 (GRIT 4, FOCUS 2, REACH 3, 2 slots) vs level 1 (all 0, 1 slot): win rate of the high-level side, %d bouts per row"
        % n,
        "| rules | reader vs reader | high greedy vs low reader |",
        "|---|---|---|",
    ]
    for mode, hc in (("FULL", False), ("FULL", True), ("CAPPED", False), ("CAPPED", True), ("NORMALISED", False)):
        out.append(
            "| %s%s | %.0f%% | %.0f%% |"
            % (
                mode,
                " + suggested handicap" if hc else "",
                100 * rate(n, "reader", "reader", HIGH, LOW, mode, hc),
                100 * rate(n, "greedy", "reader", HIGH, LOW, mode, hc),
            )
        )
    out.append(
        "Suggested handicap for this gap: FULL %s HP to the low side, CAPPED %s HP"
        % (suggest(HIGH, LOW)[1], suggest(rule(HIGH, "CAPPED"), rule(LOW, "CAPPED"))[1])
    )
    print("\n".join(out))
