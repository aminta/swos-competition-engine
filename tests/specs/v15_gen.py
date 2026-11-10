"""The swc/1.5 test specs in tests/specs (ok/v15_*, the bad ones listed in schema/v1.md v1.5): python3 tests/specs/v15_gen.py"""
import json, sys, copy
E = sys.argv[1] if len(sys.argv) > 1 else "."
GREEK = ["ALFA","BETA","GAMMA","DELTA","EPSILON","ZETA","ETA","THETA","IOTA","KAPPA","LAMBDA","MU","NU","XI","OMICRON","PI"]
NAMES = {"ESP":"SPAGNA","ENG":"INGHILTERRA","ITA":"ITALIA","GER":"GERMANIA","FRA":"FRANCIA","RUS":"RUSSIA","UKR":"UCRAINA",
         "POR":"PORTOGALLO","NED":"OLANDA","KOS":"KOSOVO","BIH":"BOSNIA","SRB":"SERBIA","SCO":"SCOZIA","ARM":"ARMENIA",
         "AZE":"AZERBAIGIAN","BEL":"BELGIO"}
def countries(codes):
    return {c: {"names": {l: NAMES[c] for l in ("it","en","fr","de")}} for c in codes}
def clubs(per):          # [(code, n)] -> club list, file per country
    out = []
    for fi, (code, n) in enumerate(per):
        for k in range(n):
            out.append({"id": f"club:{code.lower()}.{GREEK[k].lower()}", "name": f"{code} {GREEK[k]}", "country": code,
                        "host": {"file": 60 + fi, "ordinal": k}})
    return out
NM = lambda it, en: {"it": [it, it], "en": [en, en], "fr": [en, en], "de": [en, en]}
DEC2 = ["aggregate", "extra_time", "penalties"]
DEC1 = ["extra_time", "penalties"]
def cup(per, g, draw, rounds, stage_extra=None, ko_entrants=("groups#1", "groups#2"), schema="swc/1.5", note=""):
    cl = clubs(per)
    n = len(cl)
    gs = {"id": "groups", "type": "groups", "groups": g, "legs": 2,
          "tiebreakers": ["points", "goal_difference", "goals_for", "draw_lots"]}
    if draw is not None: gs["draw"] = draw
    if stage_extra: gs.update(stage_extra)
    return {"schema": schema, "id": "test.v15-cup", "host_world": 9, "_note": note,
            "languages": ["it", "en", "fr", "de"], "countries": countries([c for c, _ in per]), "clubs": cl,
            "competitions": [{"id": "tst.cup", "kind": "continental", "country": per[0][0], "names": NM("COPPA", "CUP"),
                              "participants": {"count": n, "clubs": [c["id"] for c in cl]},
                              "stages": [gs, {"id": "ko", "type": "knockout", "entrants": list(ko_entrants), "rounds": rounds}],
                              "outcomes": [{"stage": "ko", "positions": "1", "tag": "champion"}],
                              "modes": ["career", "season", "preset", "friendly"]}]}
def r(ties, legs, pairing, avoid=None):
    x = {"ties": ties, "legs": legs, "home": "better_placed_second_leg" if legs == 2 else "neutral", "pairing": pairing,
         "decider": DEC2 if legs == 2 else DEC1}
    if avoid is not None: x["avoid"] = avoid
    return x
UEFA32 = [("ESP",4),("ENG",4),("ITA",4),("GER",4),("FRA",3),("POR",3),("RUS",2),("UKR",2),("NED",2),("KOS",1),("BIH",1),("SRB",1),("SCO",1)]
KO16 = [r(8, 2, "seeded", ["same_group", "association"]), r(4, 2, "draw"), r(2, 2, "draw"), r(1, 1, "draw")]
DRAW = {"pots": "access_order", "separate": ["association"], "protect": [["RUS", "UKR"], ["KOS", "BIH"], ["KOS", "SRB"]]}
ok, bad = {}, {}
ok["v15_stage_cup"] = cup(UEFA32, 8, DRAW, KO16, note="swc/1.5 (synthetic): 32 clubs drawn into 8 groups of 4 by access order, associations apart, RUS-UKR / KOS-BIH / KOS-SRB never together; the top two to a seeded last 16 avoiding group rematches and associations.")
# a chain: KOS-BIH + KOS-SRB; BIH v SRB allowed (the chain's 3 clubs in 2 groups: fine as pairs, a merged class would refuse it)
ok["v15_protect_chain"] = cup([("KOS",1),("BIH",1),("SRB",1),("BEL",2),("SCO",2),("NED",1)], 2, {"pots": "access_order", "separate": ["association"], "protect": [["KOS","BIH"],["KOS","SRB"]]},
                              [r(2, 2, "seeded", ["association"]), r(1, 1, "draw")],
                              note="swc/1.5 (synthetic): a protect chain KOS-BIH + KOS-SRB, one club each, in 2 groups -- merged into one class it would be refused (3 > 2); as pairs BIH and SRB share the group KOS is not in.")
bad["minor_14_draw"] = (cup(UEFA32, 8, DRAW, [r(8, 2, "seeded"), r(4, 2, "draw"), r(2, 2, "draw"), r(1, 1, "draw")], schema="swc/1.4"),
                        "'draw' is swc/1.5: this spec says \"swc/1.4\"")
bad["minor_14_avoid"] = (cup(UEFA32, 8, None, KO16, schema="swc/1.4"), "a round's 'avoid' is swc/1.5: this spec says \"swc/1.4\"")
bad["draw_class_too_big"] = (cup([("ESP",9),("ENG",7),("ITA",8),("GER",8)], 8, {"pots": "access_order", "separate": ["association"]}, KO16),
                             "association ESP has 9 clubs for 8 groups")
bad["draw_protect_too_big"] = (cup([("ARM",2),("AZE",1),("BEL",1),("SCO",4)], 2, {"pots": "access_order", "separate": ["association"], "protect": [["ARM","AZE"]]},
                                   [r(2, 2, "seeded"), r(1, 1, "draw")]),
                               "protect: ARM and the countries kept apart from it have 3 clubs for 2 groups")
bad["draw_protect_alone"] = (cup(UEFA32, 8, {"pots": "access_order", "protect": [["RUS","UKR"]]}, KO16), "'protect' needs \"separate\": [\"association\"]")
bad["draw_not_groups"] = (None, "'draw' goes on a groups stage")
bad["avoid_same_group_one_group"] = (cup([("ESP",2),("ENG",2)], 1, None, [r(1, 2, "seeded", ["same_group"])], ko_entrants=("groups#1-2",)),
                                     "avoid \"same_group\": the stage's entrants must come from one groups stage of 2 or more groups")
bad["avoid_two_keys_draw"] = (cup(UEFA32, 8, DRAW, [r(8, 2, "draw", ["same_group", "association"]), r(4, 2, "draw"), r(2, 2, "draw"), r(1, 1, "draw")]),
                              "avoid with two keys needs the pairing \"seeded\"")
bad["avoid_bracket"] = (cup(UEFA32, 8, DRAW, [r(8, 2, "bracket", ["association"]), r(4, 2, "draw"), r(2, 2, "draw"), r(1, 1, "draw")]),
                        "'avoid' goes with the pairing \"draw\" or \"seeded\"")
bad["tb_fair_play_swos9697"] = (cup(UEFA32, 8, DRAW, KO16, stage_extra={"tiebreakers": ["points", "fair_play", "draw_lots"]}),
                                "the tie-breaker 'fair_play' needs game data the profile 'swos9697' does not supply")
bad["pots_coefficient_swos9697"] = (cup(UEFA32, 8, dict(DRAW, pots="coefficient"), KO16),
                                    "pots \"coefficient\" needs game data the profile 'swos9697' does not supply")
bad["prev_rank_no_source"] = (cup(UEFA32, 8, DRAW, KO16, stage_extra={"tiebreakers": ["points", "previous_stage_rank", "draw_lots"]}),
                              "the tie-breaker 'previous_stage_rank' needs the stage's entrants from one earlier stage of this competition")
d = copy.deepcopy(ok["v15_stage_cup"]); d["competitions"][0]["stages"][1]["draw"] = DRAW
bad["draw_not_groups"] = (d, "'draw' goes on a groups stage")
for k, v in ok.items():
    json.dump(v, open(f"{E}/tests/specs/ok/{k}.json", "w"), indent=1, ensure_ascii=False)
for k, (v, exp) in bad.items():
    v = copy.deepcopy(v); v["_expect"] = exp
    json.dump(v, open(f"{E}/tests/specs/bad/{k}.json", "w"), indent=1, ensure_ascii=False)
print(len(ok), "ok,", len(bad), "bad")
# linked entrants: two leagues' top two into a 2-group cup (swcc cannot count them: no bound, the runtime rule)
cl = clubs([("ESP", 4), ("ENG", 4)])
def league(code, idx):
    return {"id": f"tst.{code.lower()}", "kind": "league", "level": 1, "country": code, "names": NM("LIGA " + code, "LEAGUE " + code),
            "participants": {"count": 4, "clubs": [c["id"] for c in cl[idx * 4:idx * 4 + 4]]},
            "stages": [{"id": "table", "type": "round_robin", "legs": 2, "tiebreakers": ["points", "goal_difference", "goals_for", "draw_lots"]}],
            "outcomes": [{"stage": "table", "positions": "1", "tag": "champion"},
                         {"stage": "table", "positions": "1-2", "tag": "continental", "to": "tst.cup", "season": "next"}],
            "modes": ["career", "season", "preset", "friendly"]}
lk = {"schema": "swc/1.5", "id": "test.v15-linked", "host_world": 9,
      "_note": "swc/1.5 (synthetic): a cup of linked entrants (two leagues' top two) drawn into 2 groups, associations apart.",
      "languages": ["it", "en", "fr", "de"], "countries": countries(["ESP", "ENG"]), "clubs": cl,
      "competitions": [league("ESP", 0), league("ENG", 1),
                       {"id": "tst.cup", "kind": "continental", "country": "ESP", "names": NM("COPPA", "CUP"),
                        "participants": {"count": 4, "from": "link", "source": ["tst.esp", "tst.eng"]},
                        "stages": [{"id": "groups", "type": "groups", "groups": 2, "legs": 2,
                                    "tiebreakers": ["points", "goal_difference", "goals_for", "draw_lots"],
                                    "draw": {"pots": "access_order", "separate": ["association"]}},
                                   {"id": "ko", "type": "knockout", "entrants": ["groups#1"], "rounds": [r(1, 1, "draw", ["same_group"])]}],
                        "outcomes": [{"stage": "ko", "positions": "1", "tag": "champion"}],
                        "modes": ["career", "season", "preset", "friendly"]}]}
json.dump(lk, open(f"{E}/tests/specs/ok/v15_draw_linked.json", "w"), indent=1, ensure_ascii=False)
