"""Current-form deployment, kept separate from historical held-out evaluation.

Recency measures relevance, never a presumed increase in skill. No win-rate
handicap or user-reported form adjustment is applied to the bot.
"""
from collections import Counter
import json
from pathlib import Path
import re
import random
from statistics import median

from .imitation import fit_model, phase, samples_for, timestamp, SUPPORTED_UNITS, WORKERS
from .replays import UNIT_TYPES

GAP_DAYS = 90
HALF_LIFE_DAYS = 30
OPENING_END = 6000


def production_intents(events, race):
    """Conservative production opportunities, NOT reconstructed successful units.

    A train button can be spammed with an empty wallet. Collapse inputs to one
    opportunity per producer/build duration. This deliberately does not recover
    queue depth/cancellations; retain the raw events for historical evaluation.
    """
    ready = {}
    for event in events:
        if event['kind'] != 'train' or race == 'Zerg':
            yield event
            continue
        tags = event.get('producer_tags', [])
        metadata = UNIT_TYPES.get(str(event['id']), {})
        key = tuple(tags) if tags else ('unknown', metadata.get('producer', event['id']))
        if event['frame'] < ready.get(key, 0):
            continue
        ready[key] = event['frame'] + metadata.get('build_time', 300)
        yield event


def map_family(name):
    name = name.lower().replace('_', ' ')
    name = re.sub(r'^\s*[\[(]\d+[\])]\s*', '', name)
    if '투혼' in name or re.search(r'fighting\s*spirit', name):
        return 'Fighting Spirit'
    # Runtime filenames often omit the space before a dotted version number.
    # Keep distinct prefixes such as Neo; only strip the player-count prefix
    # and a trailing version, not arbitrary words from the map name.
    return re.sub(r'(?:\s+v?\d[\w.]*|v?\d+\.\d+[\w.]*)$', '', name).strip()


def current_period(samples, gap_days=GAP_DAYS):
    ordered = sorted(samples, key=lambda s: (timestamp(s['start_time']), s['match_id']))
    start = 0
    for index in range(1, len(ordered)):
        if timestamp(ordered[index]['start_time']) - timestamp(ordered[index-1]['start_time']) > gap_days * 86400:
            start = index
    return ordered[start:]


def recency_weight(sample, latest):
    days = max(0, (latest - timestamp(sample['start_time'])) / 86400)
    return 2 ** (-days / HALF_LIFE_DAYS)


def choose_opening(candidates, latest, seed):
    """Sample whole observed openings, so repeated choices retain their frequency.

    No guessed 9-pool/12-hatch labels or synthetic mixture of build timings.
    Sorting and a local RNG make a recorded seed reproducible.
    """
    ordered = sorted(candidates, key=lambda s: (timestamp(s['start_time']), s['match_id']))
    if not ordered:
        raise ValueError('No opening candidates')
    weights = [recency_weight(sample, latest) for sample in ordered]
    total = sum(weights)
    selected = random.Random(seed).choices(ordered, weights=weights, k=1)[0]
    evidence = [{'match_id': sample['match_id'], 'start_time': sample['start_time'],
                 'map': sample['map'], 'probability': weight / total}
                for sample, weight in zip(ordered, weights)]
    return selected, evidence


def build_current(document, name, race, opponent, target_map='Fighting Spirit', seed=42):
    all_samples = samples_for(document, name, race)
    recent = current_period(all_samples)
    if not recent:
        raise ValueError(f'No eligible games: {name}/{race}')
    matchup = [s for s in recent if s['opponent_race'] == opponent]
    if not matchup:
        raise ValueError(f'No {opponent} matchup in current period for {name}; refusing an old opening silently')
    same_map = [s for s in matchup if map_family(s['map']) == map_family(target_map)]
    candidates = same_map or matchup
    # Actual recent games condition the policy. Sparse exact-map samples back off
    # to the SAME player's current-period matchup, never a different person.
    policy_samples = same_map if len(same_map) >= 3 else matchup
    latest = max(timestamp(s['start_time']) for s in recent)
    selected, opening_candidates = choose_opening(candidates, latest, seed)
    model = fit_model(recent, name, race)
    policies = []
    for index in range(4):
        weighted, total_weight = Counter(), 0.0
        for sample in policy_samples:
            counts = Counter(e['id'] for e in production_intents(sample['player']['events'], race)
                             if e['kind'] == 'train' and e['id'] in SUPPORTED_UNITS[race] and phase(e['frame']) == index)
            if not counts:
                continue
            weight = recency_weight(sample, latest)
            total_weight += weight
            weighted.update({unit: weight * count / sum(counts.values()) for unit, count in counts.items()})
        policies.append({str(unit): count / total_weight for unit, count in sorted(weighted.items())}
                        if total_weight else model['policies'][opponent][index])
    model['policies'][opponent] = policies
    model['schema_version'] = 3
    model['method'] = 'current_period_map_matchup_imitation'
    model['context'] = {
        'period_start': recent[0]['start_time'], 'period_end': recent[-1]['start_time'],
        'period_games': len(recent), 'matchup_games': len(matchup), 'exact_map_games': len(same_map),
        'long_gap_days': GAP_DAYS, 'recency_half_life_days': HALF_LIFE_DAYS,
        'target_map_family': map_family(target_map), 'opponent_race': opponent,
        'opening_match_id': selected['match_id'], 'opening_time': selected['start_time'],
        'opening_map': selected['map'], 'opening_fallback': 'none' if same_map else 'current_period_same_matchup_other_map',
        'evaluation': 'Deployment uses latest observations. Historical held-out scores do not evaluate this refit.',
        'skill_trend_assumption': 'none',
        'production_evidence': 'Producer build-time filtered command opportunities; not successful unit counts.',
        'opening_selection': 'Whole observed game sampled by frequency and recency within map/matchup candidates.',
        'seed': seed, 'opening_candidates': opening_candidates,
    }
    model['selected_events'] = selected['player']['events']
    attacks = [s['player']['first_distant_attack_command_frame'] for s in policy_samples
               if s['player'].get('first_distant_attack_command_frame') is not None]
    if attacks:
        model['first_distant_attack_frame_proxy'] = round(median(attacks))
    return model


def export_current(model, output):
    seed = model['context'].get('seed', 42)
    race = model['race']
    counts = Counter({WORKERS[race]: 4})
    if race == 'Zerg':
        counts[42] = 1
    rows = ['# Current-period deployment; see context.json for evidence and sparse-data fallback.',
            'schema_version=3', f"name={model['name']}", f'race={race}',
            f"sample_games={model['sample_games']}", f'seed={seed}',
            f"worker_target={model['worker_target_proxy']}",
            f"first_attack_frame={model['first_distant_attack_frame_proxy']}",
            f'opening_end_frame={OPENING_END}']
    steps, orders = [], []
    for event in production_intents(model['selected_events'], race):
        kind, unit = event['kind'], event['id']
        if kind == 'train':
            if unit not in SUPPORTED_UNITS[race] and not (race == 'Zerg' and unit == 42):
                continue
            counts[unit] += 2 if unit in (37, 47) else 1
            if event['frame'] <= OPENING_END:
                orders.append((event['frame'], 3, unit, counts[unit]))
        elif kind in ('build', 'tech', 'upgrade'):
            if kind == 'build' and UNIT_TYPES.get(str(unit), {}).get('race') != race:
                continue
            step = (event['frame'], {'build': 0, 'tech': 1, 'upgrade': 2}[kind], unit, event['count'])
            if len(steps) < 96:
                steps.append(step)
                if event['frame'] <= OPENING_END:
                    orders.append(step)
    for step in steps:
        rows.append('step=' + ','.join(map(str, step)))
    for order in sorted(orders, key=lambda o: o[0]):
        rows.append('opening=' + ','.join(map(str, order)))
    for index, policy in enumerate(model['policies'][model['context']['opponent_race']]):
        for unit, weight in policy.items():
            if int(unit) in model['observed_unit_ids']:
                rows.append(f'weight={index},{unit},{max(1,round(weight*10000))}')
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(rows) + '\n', encoding='utf-8')
    output.with_suffix('.context.json').write_text(json.dumps(model['context'], ensure_ascii=False, indent=2), encoding='utf-8')
    return orders


def export_placements(document, model, output):
    """Reuse observed forge-expand coordinates per spawn, without rotating walls.

    Older geometry can be used, with source dates retained separately. It does
    not replace the current opening's timing or policy. Version transfer still
    requires terrain and live collision validation by the runtime.
    """
    rows, evidence = [], []
    if model['race'] == 'Protoss' and model['context']['target_map_family'] == 'Fighting Spirit':
        selected_builds = [e['id'] for e in model['selected_events'] if e['kind'] == 'build']
        if selected_builds[:2] == [156, 166]:
            per_spawn = {}
            for sample in samples_for(document, model['name'], model['race']):
                player = sample['player']
                start = player.get('start_position')
                builds = [e for e in player['events'] if e['kind'] == 'build']
                if (map_family(sample['map']) != 'Fighting Spirit' or not start or
                        sample['opponent_race'] != model['context']['opponent_race'] or
                        [e['id'] for e in builds[:2]] != [156, 166]):
                    continue
                per_spawn[(start['X'], start['Y'])] = (sample, builds)
            limits = {156: 1, 166: 1, 160: 1, 162: 2, 154: 2}
            for start, (sample, builds) in sorted(per_spawn.items()):
                for event in builds:
                    if event['id'] in limits and event['count'] <= limits[event['id']] and 'tile' in event:
                        rows.append((*start, event['id'], event['count'], *event['tile']))
                evidence.append({k: sample[k] for k in ('match_id', 'map', 'start_time')})
    output = Path(output)
    output.write_text(''.join(' '.join(map(str, row)) + '\n' for row in rows), encoding='ascii')
    output.with_suffix('.context.json').write_text(json.dumps({
        'sources': evidence, 'evidence': 'Observed building commands, not proof of a ling-tight wall.',
        'transfer': 'Same map family and spawn only; runtime validates buildable terrain.'
    }, ensure_ascii=False, indent=2), encoding='utf-8')
    return rows


def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('name')
    parser.add_argument('--race', required=True)
    parser.add_argument('--opponent', required=True)
    parser.add_argument('--map', default='Fighting Spirit')
    parser.add_argument('--input', default='data/replays-v2.json')
    parser.add_argument('--output', required=True)
    parser.add_argument('--seed', type=int, default=42)
    args = parser.parse_args()
    if not 0 <= args.seed <= 2147483647:
        parser.error('seed must be 0..2147483647')
    document = json.loads(Path(args.input).read_text(encoding='utf-8'))
    model = build_current(document, args.name, args.race, args.opponent, args.map, args.seed)
    export_current(model, args.output)
    print(json.dumps(model['context'], ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
