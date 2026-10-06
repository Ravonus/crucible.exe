"""The gauntlet (chapter 4 and the finale): three figures in a row, as crucible_fight_story.c plays it: the figures are
DROP tiers down (and duels: 1 HP less), you open the first door (+2 HP), every door brings a fresh bag and the four,
your HP carries with +DOOR more. How often a typical player (the cartridge's AI at half skill, knowing half the recipes,
the pacing bot's player) gets through. Usage: python3 gauntsim.py [N]"""

import random
import sys
from multiprocessing import Pool

import crux as X
import cruxsim as C
from crux import Side, State


def run(args):
    seed, tier, drop, door_hp, hp0 = args
    rng = random.Random(seed)
    sh = C.shelf(40 + 30 * tier, seed * 3 + 1)
    yb = C.bag(sh, "auto", rng)
    t = max(1, tier - drop)
    hp = hp0
    for door in range(3):
        f = (seed + door) % 6
        bhp, n, dmax, skill, know = C.TIER[t]
        you = Side(yb, hp)
        boss = Side(C.boss_bag(f, t, seed + door * 101, yb), bhp - 1, 0)
        st = State(you, boss, 0)
        if door:
            st.s[0].hp = hp + door_hp
            st.s[0].max = max(st.s[0].max, st.s[0].hp)  # no opener bonus after the first door
        py = C.Pol("mix128", seed * 5 + 1 + door, 128, random.Random(seed + door))
        r = [seed | 1]
        while not st.over():
            k = st.act
            st.begin_turn()
            w = py.choose(st, 0) if k == 0 else X.ai_choose(st, 1, seed * 5 + 2 + door, know, skill, r)
            st.do(w)
        if st.over() != 1:
            return False
        hp = st.s[0].hp
    return True


if __name__ == "__main__":
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    P = Pool(10)
    rules = [tuple(map(int, r.split(","))) for r in sys.argv[2:]] or [
        (3, 1, 4, 10),
        (3, 2, 4, 10),
        (3, 2, 6, 10),
        (3, 2, 8, 10),
        (5, 2, 6, 10),
        (5, 3, 6, 10),
    ]
    for tier, drop, door, hp0 in rules:
        res = P.map(run, [(5000 + i, tier, drop, door, hp0) for i in range(N)])
        print(
            f"tier {tier} (chapter {2 * tier - 2}), figures {drop} down, +{door} HP a door, start HP {hp0}: through {100 * sum(res) / N:.0f}%",
            flush=True,
        )
