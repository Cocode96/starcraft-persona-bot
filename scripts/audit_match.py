"""Check actual runtime telemetry, keeping incomplete evidence inconclusive."""
import argparse
import csv
import json
from pathlib import Path


def rows(path):
    if not path.is_file():
        return []
    text = path.read_text(encoding='utf-8')
    lines = text.splitlines()
    if text and not text.endswith('\n'):
        lines = lines[:-1]  # A live writer may not have finished its last row.
    return list(csv.reader(lines, delimiter='\t'))


def audit_player(folder):
    folder = Path(folder)
    root = folder / 'bwapi-data/write'
    status = rows(root / 'live-status.tsv')
    status = [dict(zip(status[0], map(int, row))) for row in status[1:]] if status else []
    structures = [tuple(map(int, row)) for row in rows(root / 'structure-events.tsv')]
    orders = rows(root / 'build-orders.tsv')
    opening = rows(root / 'opening-execution.tsv')
    plan = rows(root / 'map-plan.tsv')
    natural = next((tuple(map(int, row[1:3])) for row in plan if row[0] == 'base'), None)
    frame = status[-1]['frame'] if status else 0
    results = rows(root / 'persona-results.txt')
    result = results[-1] if results else []
    finished = (len(result) == 3 and result[1] in ('win', 'loss', 'test_limit')
                and result[2].isdigit() and int(result[2]) >= frame)
    findings = []
    if not status:
        findings.append('No in-game telemetry; launch alone is not verification.')
    if any(row[-1] == 'timeout' for row in opening):
        findings.append('Opening contains timed-out commands.')
    swarms = []
    deaths = rows(root / 'unit-deaths.tsv')
    for index, order in enumerate(orders):
        for previous in orders[max(0, index - 10):index]:
            replacement_after_death = any(
                int(previous[0]) <= int(death[0]) <= int(order[0]) and
                (death[1] == previous[1] or death[2:5] == previous[2:5]) for death in deaths)
            if (0 <= int(order[0]) - int(previous[0]) <= 96 and order[1] != previous[1]
                    and order[2:5] == previous[2:5] and not replacement_after_death):
                swarms.append(order[:5])
    if swarms:
        findings.append('Different workers assigned to the same building site within 96 frames.')
    depots = [row for row in structures if row[2] in (106, 131, 154)]
    first_expansion = next((row for row in depots if row[0] > 0), None)
    expansion_ok = None
    if first_expansion and natural:
        expansion_ok = sum((first_expansion[3+i] - natural[i]) ** 2 for i in (0, 1)) <= 36
        if not expansion_ok:
            findings.append('First expansion is outside the planned natural mineral line.')
    elif frame >= 7200:
        findings.append('No observed first expansion by frame 7200.')
    stagnant = [row for row in status if row['frame'] >= 8000 and row['minerals'] > 1500 and row['army'] < 4]
    if len(stagnant) >= 4:
        findings.append('Sustained mineral bank with fewer than four combat units.')
    # Catch idle production even with a surviving army. This flags an issue for
    # inspection; a full-supply late-game bank is excluded when supply is known.
    bank_run = 0
    for row in status:
        bank_run = bank_run + 1 if row['minerals'] >= 2500 and row.get('supply_used', 0) < 380 else 0
        if bank_run >= 5:
            findings.append('Minerals stayed above 2500 for at least 960 frames below the supply cap.')
            break
    scout = rows(root / 'scout-orders.tsv')
    rallies = rows(root / 'rally-orders.tsv')
    if frame >= 6000 and not scout:
        findings.append('No scout assignment recorded by frame 6000.')
    if frame >= 6000 and not rallies:
        findings.append('No successful rally command recorded by frame 6000.')
    return {
        'observed_frame': frame,
        'state': 'issues_found' if findings else 'incomplete' if frame < 9000 or not finished else 'checks_passed',
        'match_finished': finished, 'result': result if finished else None,
        'findings': findings,
        'natural_tile': natural, 'first_expansion': first_expansion, 'first_expansion_at_natural': expansion_ok,
        'depots_started': len(depots), 'opening_timeouts': sum(row[-1] == 'timeout' for row in opening),
        'scout_first_frame': int(scout[0][0]) if scout else None,
        'rally_commands': len(rallies), 'worker_swarm_events': swarms,
        'final_status': status[-1] if status else None,
        'test_limit_reached': (root / 'test-limit.txt').is_file(),
        'scope': 'Basic execution telemetry only; not wall tightness, micro quality, win rate or personal likeness.'
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('manifest', nargs='?', default='dist/last-match.json')
    args = parser.parse_args()
    manifest = json.loads(Path(args.manifest).read_text(encoding='utf-8'))
    report = {'session': manifest['session'], 'dll_sha256': manifest.get('dll_sha256'),
              'players': {name: audit_player(folder) for name, folder in zip(manifest['people'], manifest['folders'])}}
    (Path(manifest['session']) / 'audit.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 1 if any(p['state'] != 'checks_passed' for p in report['players'].values()) else 0


if __name__ == '__main__':
    raise SystemExit(main())
