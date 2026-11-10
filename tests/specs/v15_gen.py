"""The swc/1.5 test specs in tests/specs (ok/v15_*, the bad ones listed in schema/v1.md v1.5): python3 tests/specs/v15_gen.py"""
import json, sys, copy
E = sys.argv[1] if len(sys.argv) > 1 else "."
GREEK = ["ALFA","BETA","GAMMA","DELTA","EPSILON","ZETA","ETA","THETA","IOTA","KAPPA","LAMBDA","MU","NU","XI","OMICRON","PI"]
NAMES = {"ESP":"SPAGNA","ENG":"INGHILTERRA","ITA":"ITALIA","GER":"GERMANIA","FRA":"FRANCIA","RUS":"RUSSIA","UKR":"UCRAINA",
         "POR":"PORTOGALLO","NED":"OLANDA","KOS":"KOSOVO","BIH":"BOSNIA","SRB":"SERBIA","SCO":"SCOZIA","ARM":"ARMENIA",
         "AZE":"AZERBAIGIAN","BEL":"BELGIO","EGY":"EGITTO","MAR":"MAROCCO","TUN":"TUNISIA","ALG":"ALGERIA",
         "RSA":"SUDAFRICA","NGA":"NIGERIA","ANG":"ANGOLA","TAN":"TANZANIA","SUD":"SUDAN","COD":"CONGO RD",
         "KSA":"ARABIA SAUDITA","UAE":"EMIRATI ARABI","QAT":"QATAR","IRN":"IRAN","UZB":"UZBEKISTAN","IRQ":"IRAQ","JOR":"GIORDANIA"}
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
# v1.5.1: the quota bound of a linked first stage (participants "from": "link": the places the leagues give).
def lg(code, cls, top, cup="tst.cup"):
    return {"id": f"tst.{code.lower()}", "kind": "league", "level": 1, "country": code, "names": NM("LIGA " + code, "LEAGUE " + code),
            "participants": {"count": len(cls), "clubs": [c["id"] for c in cls]},
            "stages": [{"id": "table", "type": "round_robin", "legs": 2, "tiebreakers": ["points", "goal_difference", "goals_for", "draw_lots"]}],
            "outcomes": [{"stage": "table", "positions": "1", "tag": "champion"},
                         {"stage": "table", "positions": top, "tag": "continental", "to": cup, "season": "next"}],
            "modes": ["career", "season", "preset", "friendly"]}
def linked(leagues, cl, codes, n, g, draw, note):
    return {"schema": "swc/1.5", "id": "test.v151-linked", "host_world": 9, "_note": note,
            "languages": ["it", "en", "fr", "de"], "countries": countries(codes), "clubs": cl,
            "competitions": leagues + [
                {"id": "tst.cup", "kind": "continental", "country": codes[0], "names": NM("COPPA", "CUP"),
                 "participants": {"count": n, "from": "link", "source": [l["id"] for l in leagues]},
                 "stages": [{"id": "groups", "type": "groups", "groups": g, "legs": 2,
                             "tiebreakers": ["points", "goal_difference", "goals_for", "draw_lots"], "draw": draw},
                            {"id": "ko", "type": "knockout", "entrants": ["groups#1"], "rounds": [r(1, 1, "draw")] if g == 2 else [r(2, 2, "draw"), r(1, 1, "draw")]}],  # (g 2 or 4)
                 "outcomes": [{"stage": "ko", "positions": "1", "tag": "champion"}],
                 "modes": ["career", "season", "preset", "friendly"]}]}
SEP = {"pots": "access_order", "separate": ["association"]}
# ESP's league gives 3, the mixed league (1 ESP + 3 POR clubs, POR's) gives 2 -- at most 1 of them ESP: ESP <= 4 for 4
# groups (the plain sum of places, 5, would refuse it: the mutant test), POR <= 2, ENG 3
c1 = clubs([("ESP", 4), ("ENG", 4)]); mix = clubs([("POR", 3)]) + [dict(clubs([("ESP", 5)])[4], host={"file": 70, "ordinal": 3})]
for k, c in enumerate(mix[:3]): c["host"] = {"file": 70, "ordinal": k}
L1 = [lg("ESP", c1[:4], "1-3"), lg("ENG", c1[4:], "1-3"), lg("POR", mix, "1-2")]
okl = linked(L1, c1 + mix, ["ESP", "ENG", "POR"], 8, 4, SEP,
             "swc/1.5.1 (synthetic): 8 linked clubs into 4 groups; ESP gets 3 places from its league and at most 1 from POR's league (its one ESP club): 4 for 4 groups.")
json.dump(okl, open(f"{E}/tests/specs/ok/v151_linked_ok.json", "w"), indent=1, ensure_ascii=False)
c2 = clubs([("ESP", 4), ("ENG", 4)])
b1 = linked([lg("ESP", c2[:4], "1-3"), lg("ENG", c2[4:], "1")], c2, ["ESP", "ENG"], 4, 2, SEP, "")
b1["_expect"] = "association ESP can have up to 3 clubs for 2 groups"
json.dump(b1, open(f"{E}/tests/specs/bad/v151_linked_quota.json", "w"), indent=1, ensure_ascii=False)
c3 = clubs([("ESP", 4), ("ENG", 4), ("FRA", 4)])
b2 = linked([lg("ESP", c3[:4], "1"), lg("ENG", c3[4:8], "1-2"), lg("FRA", c3[8:], "1")], c3, ["ESP", "ENG", "FRA"], 4, 2,
            dict(SEP, protect=[["ESP", "ENG"]]), "")
b2["_expect"] = "protect: ESP and the countries kept apart from it can have 3 clubs for 2 groups"
json.dump(b2, open(f"{E}/tests/specs/bad/v151_linked_clique.json", "w"), indent=1, ensure_ascii=False)
# swc/1.6: max_per_group (CAF CL / CC regulations art. 17: no more than 2 clubs of one association in a group)
CAF16 = [("EGY",5),("MAR",3),("TUN",2),("ALG",2),("RSA",2),("NGA",1),("ANG",1)]   # (EGY 5 > 4 groups: only with 2 per group)
CAPD = {"pots": "access_order", "separate": ["association"], "max_per_group": 2}
KO8 = [r(4, 2, "seeded", ["same_group"]), r(2, 2, "draw"), r(1, 1, "draw")]
v16 = cup(CAF16, 4, CAPD, KO8, schema="swc/1.6",
          note="swc/1.6 (synthetic, CAF-shaped): 16 clubs drawn into 4 groups by access order, at most 2 clubs of one association per group (CAF CL / CC regulations art. 17); the top two to seeded quarter-finals avoiding group rematches.")
v16["id"] = "test.v16-caf"
json.dump(v16, open(f"{E}/tests/specs/ok/v16_caf_cup.json", "w"), indent=1, ensure_ascii=False)
b16 = {
 "v16_cap_too_big": (cup([("EGY",9),("MAR",3),("TUN",4)], 4, CAPD, KO8, schema="swc/1.6"),
                     "association EGY has 9 clubs for 4 groups (max_per_group 2: never three in one group)"),
 "v16_cap_minor": (cup(CAF16, 4, CAPD, KO8, schema="swc/1.5"), "'max_per_group' is swc/1.6: this spec says \"swc/1.5\""),
 "v16_cap_no_separate": (cup(CAF16, 4, {"pots": "access_order", "max_per_group": 2}, KO8, schema="swc/1.6"),
                         "'max_per_group' needs \"separate\": [\"association\"]"),
 "v16_cap_protect": (cup(CAF16, 4, dict(CAPD, protect=[["NGA","ANG"]]), KO8, schema="swc/1.6"),
                     "'max_per_group' 2 with 'protect': not supported"),
 "v16_cap_three": (cup(CAF16, 4, dict(CAPD, max_per_group=3), KO8, schema="swc/1.6"), "max_per_group"),
}
for k, (v, exp) in b16.items():
    v = copy.deepcopy(v); v["_expect"] = exp
    json.dump(v, open(f"{E}/tests/specs/bad/{k}.json", "w"), indent=1, ensure_ascii=False)
# swc/1.7: unavoidable "fewest" (AFC ACL Two 2026/27 art. 8.1.3 / 7.1.4.1: same-MA clubs not in one group "except in
# cases where it is unavoidable") - one region's groups, KSA with 5 clubs for 4 groups: one group with two, never three
ACL16 = [("KSA",5),("UAE",3),("QAT",3),("IRN",2),("UZB",1),("IRQ",1),("JOR",1)]
FEWD = {"pots": "access_order", "separate": ["association"], "unavoidable": "fewest"}
v17 = cup(ACL16, 4, FEWD, KO8, schema="swc/1.7",
          note="swc/1.7 (synthetic, ACL Two West-shaped): 16 clubs drawn into 4 groups by access order, clubs of one association apart unless unavoidable, then as few in a group as can be (AFC ACL Two 2026/27 art. 8.1.3); the top two to seeded quarter-finals avoiding group rematches.")
v17["id"] = "test.v17-acl2"
json.dump(v17, open(f"{E}/tests/specs/ok/v17_acl2_region.json", "w"), indent=1, ensure_ascii=False)
b17 = {
 "v17_fewest_minor": (cup(ACL16, 4, FEWD, KO8, schema="swc/1.6"), "'unavoidable' is swc/1.7: this spec says \"swc/1.6\""),
 "v17_fewest_no_separate": (cup(ACL16, 4, {"pots": "access_order", "unavoidable": "fewest"}, KO8, schema="swc/1.7"),
                            "'unavoidable' needs \"separate\": [\"association\"]"),
 "v17_fewest_protect": (cup(ACL16, 4, dict(FEWD, protect=[["IRQ","JOR"]]), KO8, schema="swc/1.7"),
                        "'unavoidable' with 'protect': not supported"),
 "v17_fewest_cap2": (cup(ACL16, 4, dict(FEWD, max_per_group=2), KO8, schema="swc/1.7"),
                     "'unavoidable' with 'max_per_group' 2: not supported"),
 "v17_fewest_value": (cup(ACL16, 4, dict(FEWD, unavoidable="relax"), KO8, schema="swc/1.7"), "'unavoidable': \"fewest\""),
 "v17_strict_unavoidable": (cup(ACL16, 4, {"pots": "access_order", "separate": ["association"]}, KO8, schema="swc/1.7"),
                            "association KSA has 5 clubs for 4 groups (separate: never two in one group)"),
}
for k, (v, exp) in b17.items():
    v = copy.deepcopy(v); v["_expect"] = exp
    json.dump(v, open(f"{E}/tests/specs/bad/{k}.json", "w"), indent=1, ensure_ascii=False)
