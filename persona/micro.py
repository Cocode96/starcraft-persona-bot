"""Replay input evidence for caster control, without inventing unit-state labels."""
import argparse
import json
from pathlib import Path
import subprocess


def selected_commands(commands, player_id):
    selected, groups = [], {}
    for command in commands:
        if command.get('PlayerID') != player_id:
            continue
        kind = command.get('Type', {}).get('Name')
        if kind == 'Select':
            selected = list(command.get('UnitTags', []))
        elif kind == 'Select Add':
            selected = list(dict.fromkeys(selected + command.get('UnitTags', [])))
        elif kind == 'Select Remove':
            selected = [tag for tag in selected if tag not in command.get('UnitTags', [])]
        elif kind == 'Hotkey':
            key, mode = command.get('Group'), command.get('HotkeyType', {}).get('Name')
            if mode == 'Assign':
                groups[key] = selected[:]
            elif mode == 'Select':
                selected = groups.get(key, [])[:]
            elif mode == 'Add':
                groups[key] = list(dict.fromkeys(groups.get(key, []) + selected))
        elif not command.get('IneffKind', 0):
            yield command, tuple(selected)


def caster_evidence(parsed, name):
    players = [p for p in parsed['Header']['Players'] if p['Name'] == name]
    if len(players) != 1:
        raise ValueError('Exact player name must identify one participant')
    inputs = list(selected_commands(parsed.get('Commands', {}).get('Cmds', []), players[0]['ID']))
    storms = [(command, tags) for command, tags in inputs
              if command.get('Order', {}).get('Name') == 'CastPsionicStorm']
    singles = {tags[0] for _, tags in storms if len(tags) == 1}
    windows = []
    for storm, tags in storms:
        movements = []
        for command, later_tags in inputs:
            if not 0 < command['Frame'] - storm['Frame'] <= 240:
                continue
            kind, order = command['Type']['Name'], command.get('Order', {}).get('Name')
            if not singles.intersection(tags).intersection(later_tags):
                continue
            if kind not in ('Right Click', 'Stop', 'Hold Position') and order not in ('Move', 'AttackMove', 'Attack1'):
                continue
            movements.append({'frame': command['Frame'], 'kind': kind, 'order': order,
                              'target': command.get('Pos'), 'selected_tags': later_tags})
        windows.append({'frame': storm['Frame'], 'target': storm.get('Pos'),
                        'selected_tags': tags, 'following_inputs_240_frames': movements})
    return {
        'player': name, 'storm_inputs': len(storms), 'singleton_caster_selection_tags': sorted(singles),
        'storms_followed_by_movement_or_hold': sum(bool(w['following_inputs_240_frames']) for w in windows),
        'windows': windows,
        'evidence_kind': 'Input commands only. Ability success, survival and actual paths are unknown.',
        'not_established': ['templar protection success', 'mutalisk losses', 'unit hit points', 'enemy reinforcement visibility'],
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('name')
    parser.add_argument('--replay-root', required=True)
    parser.add_argument('--catalog', default='data/replays-v2.json')
    parser.add_argument('--output', default='data/caster-evidence.json')
    parser.add_argument('--screp', default='tools/screp/screp.exe')
    args = parser.parse_args()
    catalog = json.loads(Path(args.catalog).read_text(encoding='utf-8'))
    candidates = []
    for game in catalog['games']:
        if not game['two_human_opponents']:
            continue
        player = next((p for p in game['players'] if p['name'] == args.name and p['race'] == 'Protoss'), None)
        if not player or not any(e['kind'] == 'train' and e['id'] == 67 for e in player['events']):
            continue
        if not any(p['name'] != args.name and p['race'] == 'Zerg' for p in game['players']):
            continue
        candidates.append(game)
    # Current samples and older examples are both retained, never averaged as a
    # claim about current form. No chat or complete command dump is exported.
    result = []
    for game in sorted(candidates, key=lambda g: g['start_time']):
        source = Path(args.replay_root) / game['sources'][0]
        raw = subprocess.run([str(Path(args.screp).resolve()), '-cmds', '-indent=false', str(source)],
                             capture_output=True, check=True, timeout=90)
        evidence = caster_evidence(json.loads(raw.stdout), args.name)
        if evidence['storm_inputs']:
            result.append({**{k: game[k] for k in ('match_id', 'start_time', 'map', 'sources')}, **evidence})
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({'games': result}, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'games': len(result), 'storm_inputs': sum(g['storm_inputs'] for g in result),
                      'output': str(output)}, ensure_ascii=False))


if __name__ == '__main__':
    main()
