import bosssim as B

T = {1: 0.85, 2: 0.75, 3: 0.65, 4: 0.55, 5: 0.45, 6: 0.35}


def rate(tier, n=40):
    w = 0
    for fac in B.FACS:
        for k in range(n):
            know = {m: B.OLD_KNOW for m in B.SIG[fac][: tier - 1]}
            know.update({m: B.NEW_KNOW for m in B.SIG[fac][tier - 1 :]})
            if tier == 6:
                know = {m: B.OLD_KNOW for m in B.SIG[fac]}
            ok, _, _ = B.boss_fight(
                k * 977 + tier * 31 + B.FACS.index(fac),
                fac,
                tier,
                know,
                attrs=(min(3, tier - 1), min(2, (tier - 1) // 2)),
            )
            w += ok
    return w / (n * 6)


if __name__ == "__main__":
    for tier in range(1, 7):
        hp, pw, cue, d, e, m = B.TIER[tier]
        for h in range(hp, 80, 2):
            B.TIER[tier] = (h, pw, cue, d, e, m)
            r = rate(tier)
            if r <= T[tier]:
                break
        print(tier, h, r, flush=True)
