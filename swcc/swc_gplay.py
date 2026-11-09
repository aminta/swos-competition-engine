#!/usr/bin/env python3
"""swc -> the Mod's knockout play-offs (42-s3shape): a country's play-offs of a swcc-valid swc/1.4 spec as the
'ko_playoffs' list the Mod's gplay.py compile_spec takes (its bytes: the rules library's sr_gplay, the Mod's gplay.c).

    python3 -I swcc/swc_gplay.py SPEC COUNTRY [OUT.json]

Validate the spec with swcc first: this bridge assumes a valid one and refuses (exit 2, a message) the shapes the
bytes cannot carry. The country's leagues are its divisions, by level, then in the file's order (a level of groups:
consecutive divisions). Each play-off is
- a league's playoff stage: its outcome 'promoted' -> a promotion to that league ('to'), the other entrants of the
  winner's division down to this one; 'relegated' (the loser) -> a play-out, its loser to the 'to' league's division;
- a competition of kind "playoff": entrants "<league>#N-M" and {"cup_place"}, rounds 'per' league, promoted to its
  outcome's 'to', the losers' division = its first entrant's.
Round by round: pairing "fixed" (places, "winner:R.T" / "loser:R.T") = listed ties; "by_place" = best v worst over
the round's clubs; "draw" = drawn; "bracket" = the previous round's winners in order; "seeded" = seeds v drawn (a
{"best_of": "winners:R"} seed with the {"rest_of": "winners:R"} after it: the best of R's winners first). A round
without 'clubs' / 'fixed' / 'seeds' takes the previous round's winners. 'home': fixed -> listed, better_placed(_second_leg)
-> better, draw -> drawn, neutral. The deciders: "seed" -> second, "better_placed_unless_level_on_points" -> better_pts,
"better_placed" -> better (with extra time: et_better), else extra time + penalties -> et_pens, penalties -> pens.
A stage's 'skip_if' = every round's gap; 'direct_if' = direct_gap; 'rank': "place_then_record" = rank level."""
import json
import re
import sys

HOMES = {'fixed': 'listed', 'better_placed': 'better', 'better_placed_second_leg': 'better', 'draw': 'drawn',
         'neutral': 'neutral'}


class Refused(Exception):
    pass


def level_of(dec):
    d = list(dec)
    if 'seed' in d:
        return 'second'
    if 'better_placed_unless_level_on_points' in d:
        return 'better_pts'
    if 'better_placed' in d:
        return 'et_better' if 'extra_time' in d and d.index('extra_time') < d.index('better_placed') else 'better'
    if 'extra_time' in d:
        return 'et_pens'
    if 'penalties' in d:
        return 'pens'
    raise Refused('decider %s: no level of the bytes' % d)


def bridge(spec, country):
    comps = spec['competitions']
    leagues = [c for c in comps if c.get('kind') == 'league' and c.get('country') == country]
    leagues.sort(key=lambda c: c['level'])                      # (stable: the file's order within a level)
    div = {c['id']: i for i, c in enumerate(leagues)}
    out = []

    def names(c):
        return {k: v for k, v in c['names'].items()}

    def rounds_of(st, ents, own):
        """the stage's rounds -> compile_spec rounds; ents: [(league id, place) or 'cup_place'] (entrant order)"""
        gap = (st.get('skip_if') or {}).get('points_gap_ge', 0)
        res, wins = [], []                                      # wins[R]: {league or None: [tie codes]}
        nt = 0

        def ent(league, p):
            try:
                return ents.index((league, p))
            except ValueError:
                raise Refused('%s#%d is not an entrant of %s' % (league, p, st['id']))

        def item(x, league):
            """one club of a round -> a compile_spec member (or a list: 'winners:R' / '#N-M')"""
            if isinstance(x, dict):
                raise Refused('best_of / rest_of outside a seeded round')
            if x == 'cup_place':
                return [ents.index('cup_place')]
            m = re.fullmatch(r'(winner|loser):(\d+)\.(\d+)', x)
            if m:
                if m.group(1) == 'loser':
                    raise Refused('a loser reference (the bytes have L<k>, this bridge only winners)')
                return [wins[int(m.group(2)) - 1][None][int(m.group(3)) - 1]]
            m = re.fullmatch(r'winners:(\d+)', x)
            if m:
                w = wins[int(m.group(1)) - 1]
                return list(w.get(league, w[None]) if league else w[None])
            lg, _, pos = x.partition('#')
            a, _, b = pos.partition('-')
            lg = league if lg in ('', 'table') and league else (own if lg == 'table' else lg)
            return [ent(lg, p) for p in range(int(a), int(b or a) + 1)]

        for ri, r in enumerate(st['rounds']):
            per = r.get('per') or [None]
            legs, host = r['legs'], HOMES[r['home']]
            lvl = level_of(r['decider'])
            w = {None: []}
            for lg in per:
                rd = {'legs': legs, 'host': host, 'level': lvl}
                if gap:
                    rd['gap'] = gap
                pr = r['pairing']
                if pr == 'fixed':
                    rd['ties'] = [[m for x in pair for m in item(x, lg)] for pair in r['fixed']]
                elif pr == 'seeded':
                    flat, bf = [], None
                    for x in r['seeds'] + r.get('drawn', []):
                        if isinstance(x, dict) and 'best_of' in x:
                            bf = len(flat)
                            flat += item(x['best_of'], lg)
                        elif isinstance(x, dict) and 'rest_of' in x:
                            if bf is None or item(x['rest_of'], lg) != flat[bf:]:
                                raise Refused('a rest_of not right after the best_of of the same round')
                        else:
                            flat += item(x, lg)
                    t = r['ties']
                    if len(flat) != 2 * t:
                        raise Refused('round %d: %d clubs for %d ties' % (ri + 1, len(flat), t))
                    rd['seeds'], rd['drawn'] = flat[:t], flat[t:]
                    if bf is not None:
                        rd['best_first'] = [bf, 2 * t]
                else:
                    mem = [m for x in r['clubs'] for m in item(x, lg)] if 'clubs' in r else list(wins[-1][None])
                    if pr in ('by_place', 'draw'):
                        rd['pairing'] = 'best_worst' if pr == 'by_place' else 'draw'
                        rd['clubs'] = mem
                    elif pr == 'bracket':
                        rd['ties'] = [mem[i:i + 2] for i in range(0, len(mem), 2)]
                    else:
                        raise Refused('pairing %s: no form in the bytes' % pr)
                t = r['ties']
                codes = ['W%d' % (nt + i) for i in range(t)]
                nt += t
                if lg is not None:
                    w.setdefault(lg, []).extend(codes)
                w[None].extend(codes)
                res.append(rd)
            wins.append(w)
        return res

    def ext(po, st):
        if 'direct_if' in st:
            po['direct_gap'] = st['direct_if']['points_gap_ge']
        if st.get('rank') == 'place_then_record':
            po['rank'] = 'level'

    for c in leagues:                                           # the leagues' playoff stages
        oc = c.get('outcomes', [])
        for st in c['stages']:
            if st['type'] != 'playoff':
                continue
            mine = [o for o in oc if o['stage'] == st['id'] and o['tag'] in ('promoted', 'relegated')]
            if len(mine) != 1:
                raise Refused('%s %s: one promoted / relegated outcome' % (c['id'], st['id']))
            o = mine[0]
            m = re.fullmatch(r'table#(\d+)-(\d+)', st['entrants'])
            if not m:
                raise Refused('%s %s: entrants "table#N-M"' % (c['id'], st['id']))
            ents = [(c['id'], p) for p in range(int(m.group(1)), int(m.group(2)) + 1)]
            po = {'key': '%s.%s' % (c['id'], st['id']), 'names': names(c), 'entrants': [[div[c['id']], p] for _, p in ents]}
            if o['tag'] == 'promoted':
                po.update(kind='promotion', winner_div=div[o['to']], loser_div=div[c['id']])
            else:
                po.update(kind='playout', winner_div=div[c['id']], loser_div=div[o['to']])
            ext(po, st)
            po['rounds'] = rounds_of(st, ents, c['id'])
            out.append(po)
    for c in comps:                                             # the playoff competitions
        if c.get('kind') != 'playoff' or c.get('country') != country:
            continue
        st = c['stages'][0]
        o = [o for o in c.get('outcomes', []) if o['tag'] == 'promoted']
        if len(c['stages']) != 1 or len(o) != 1:
            raise Refused('%s: one playoff stage, one promoted outcome' % c['id'])
        ents, ent_b = [], []
        for e in st['entrants']:
            if isinstance(e, dict):
                cp = e['cup_place']
                g = cp.get('groups') or []
                if (cp.get('takes'), cp.get('takes_if_safe'), cp.get('unknown')) != ('table#11-15', 'table#16-17', 'best_of:#11'):
                    raise Refused('%s: cup_place takes table#11-15, takes_if_safe table#16-17, unknown best_of:#11' % c['id'])
                inst = cp['instead']
                real = inst == ['finalist', {'place': 'winner_group#4', 'scroll': True}]
                if not real and inst != ['group#11']:
                    raise Refused('%s: cup_place instead ["group#11"] (the Mod) or ["finalist", winner_group#4 scroll]' % c['id'])
                ds = [div[x] for x in g]
                if ds != list(range(ds[0], ds[0] + len(ds))):
                    raise Refused('%s: cup_place groups: consecutive divisions' % c['id'])
                ents.append('cup_place')
                ent_b.append({'cup_place': {'div': ds[0], 'groups': len(ds), 'playout_gap': cp.get('playout_gap', 0),
                                            'real': real}})
            else:
                lg, _, pos = e.partition('#')
                a, _, b = pos.partition('-')
                for p in range(int(a), int(b or a) + 1):
                    ents.append((lg, p))
                    ent_b.append([div[lg], p])
        first = next(x for x in ents if x != 'cup_place')
        po = {'key': c['id'], 'kind': 'promotion', 'names': names(c), 'entrants': ent_b,
              'winner_div': div[o[0]['to']], 'loser_div': div[first[0]]}
        ext(po, st)
        po['rounds'] = rounds_of(st, ents, None)
        out.append(po)
    return out


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    try:
        pos = bridge(json.load(open(sys.argv[1], encoding='utf-8')), sys.argv[2])
    except (Refused, KeyError, ValueError, IndexError, StopIteration) as e:
        sys.stderr.write('swc_gplay: %s\n' % e)
        sys.exit(2)
    txt = json.dumps(pos, indent=1, ensure_ascii=False)
    if len(sys.argv) > 3:
        open(sys.argv[3], 'w', encoding='utf-8').write(txt + '\n')
    else:
        print(txt)
