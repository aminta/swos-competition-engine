# simdrv — the mass simulation at match level (item g)

`tests/sim/simdrv.c` plays Serie A, Serie B and Serie C's group A (20 clubs each) match by match. Every table is ranked by the FIGC tie-breakers (S1.2b), and then the season end goes through the rules library.

```
make sim-italy                    # 10^5 seasons (~25 s) + the controls pts / order / move
build/simdrv [SEASONS [SEED]]     # default 100000 1
SIMDRV_MODEL=h0,a0,c,rho          # goal model, default 1.40,1.08,70,0.12
SIMDRV_BREAK=pts|order|move       # controls: each must FAIL
```

## Model
- **Matches:** a double round robin from `sr_rr_calendar`.
- **Goals:** Poisson, with home λ = h0·e^(ΔS/c) and away λ = a0·e^(−ΔS/c), where ΔS is the strength difference. A one-goal margin is levelled with probability ρ, because independent Poisson goals give too few draws.
- **Strengths:** italyfull's levels (A 290..375, B 240..320, C 180..280). Clubs relegated from C are replaced by new clubs from a D sink (170..250).
- **Calibration** against Serie A, 2010s–2020s:

  | | Simulated | Serie A |
  |---|---|---|
  | Goals per match | 2.71 | 2.6–2.8 |
  | Home / draw / away | 43 / 26 / 31 % | 43 / 26 / 31 % |
  | Champion | ~87 points | |
  | 17th place | ~32 points | ~33–36 |

**Tables:** each table is built by `sr_rr_table` (from the matches) and again by `sr_league_rank` (from the rows plus the n×n pairs block, in the exe's layout: `h*n+a`, `hg<<4|ag`, capped at 14, `0xFF` = none). The two orders must be equal.

**Deciding criterion:** for each adjacent pair of clubs level on points, it is the shortest prefix of the list (+ draw lots) that fixes their order under two different draw-lots seeds.

**Avulsa vs per-block h2h:** for each block of 3+ clubs level on points, the avulsa's order is compared with the same list using `h2h_points, h2h_goal_difference` in place of `avulsa`.

## Season end
1. **Serie A's play-off matches** (FIGC since 2022-23), when clubs are level on points. If more than two are level, the avulsa picks the two.
   - Title: one match at the better placed club's ground, no extra time, then penalties.
   - 17th place: two legs, the better placed hosts the return, aggregate, no away goals, then penalties.
   - The loser takes 2nd / 18th.
2. **The library:**
   - `sr_italy`: B's play-off (3rd..8th, held unless 3rd > 4th + 14) and play-out (16th v 17th, held unless 16th > 17th + 4). Their ties are played by the model through `sr_force.win`: level on aggregate, the better placed goes through; extra time and penalties only when level on points.
   - Then `sr_knockouts` and `sr_final_moves`.
   - Serie C's play-outs are spec knockouts (4.2 spec). No spec is given here, so only C's 20th goes down. italyfull is the chain test.
3. **Invariants:**
   - In A, only 18th–20th go down.
   - B sends up 1st, 2nd and one play-off winner from 3rd..8th.
   - A down = B up = 3, and B down = C up.
   - Every division has 20 clubs after the moves, plus the per-table invariants (matches, points, goals, wins/losses, the two rankers equal, points non-increasing).

## Numbers (10^5 seasons, seed 1)

**Serie A play-off matches:**

| | Share of seasons | Of which decided on penalties |
|---|---|---|
| Title | 7.8% | 31% |
| 17th place | 16.8% | 19% |
| Either one | 23.2% | |

**Serie B:**
- The play-off is held in 99.5% of seasons.
- The play-out is held in 74% over the long run. It is 44% in the first 2·10^4 seasons: club strengths are fixed for life, so the levels drift.
- Where the play-off winner finished: 3rd 44%, 4th 29%, 5th 13%, 6th 8.5%, 7th 2.8%, 8th 2.0%.

**Tie-breakers:**

| | Serie A | Serie B | Serie C group A |
|---|---|---|---|
| Adjacent pairs level on points, per season | 2.55 | 2.22 | 4.33 |
| Decided by avulsa | 86.8% | 86.6% | 88.9% |
| Decided by goal difference | 12.5% | 12.8% | 10.2% |
| Decided by goals for | 0.5% | 0.4% | 0.6% |
| List runs past goals for (pairs) | 0.028% | 0.026% | 0.037% |
| List reaches the draw (pairs) | 0.0012% | 0 | 0.0007% |
| Blocks of 3+ level, per season | 0.22 | 0.16 | 0.66 |
| Avulsa ≠ per-block h2h, share of those blocks | 6.6% | 6.5% | 7.2% |
| Avulsa ≠ per-block h2h, per 10^4 tables | 143 | 105 | 476 |

**Serie C group A's composition drifts:** strong clubs come down from B and new clubs come up from D, so the group gets compressed. Its tie counts are therefore higher than in a group with fixed strengths. A fixed group of 20, before phase 2, gave 1.95 pairs per season, 0.021% past goals for and 0.010% to the draw.

**The draw in Serie C:** the list reaches draw lots in about 3 pairs per 10^5 seasons per group, i.e. about 0.0001 per season over Serie C's 3 groups. That is the size of the declared gap: the Serie C text has no draw.
