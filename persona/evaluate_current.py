"""Forward-only evaluation of current-period command choices, not bot skill."""
import argparse
import json
import math
from pathlib import Path
from statistics import mean

from .imitation import SUPPORTED_UNITS, WORKERS, fit_model, phase, samples_for, timestamp
from .temporal import build_current, current_period, production_intents

PROBABILITY_FLOOR = 1e-9


def filtered_sample(sample, race):
    return {**sample, 'player': {**sample['player'], 'events':
            list(production_intents(sample['player']['events'], race))}}


def command_score(model, sample):
    policies = model['policies'][sample['opponent_race']]
    events = [e for e in sample['player']['events']
              if e['kind'] == 'train' and e['id'] in SUPPORTED_UNITS[model['race']]]
    result = {}
    for label, subset in (('all', events), ('combat', [e for e in events
                           if e['id'] != WORKERS[model['race']]])):
        correct, loss, unseen = 0, 0.0, 0
        for event in subset:
            probabilities = policies[phase(event['frame'])]
            if label == 'combat':
                probabilities = {k: v for k, v in probabilities.items()
                                 if int(k) != WORKERS[model['race']]}
            total = sum(probabilities.values())
            probability = probabilities.get(str(event['id']), 0) / total if total else 0
            prediction = max(probabilities, key=probabilities.get) if total else None
            correct += prediction == str(event['id'])
            unseen += probability == 0
            loss -= math.log(max(PROBABILITY_FLOOR, probability))
        result[label] = {'commands': len(subset), 'zero_probability_commands': unseen,
                         'top1_accuracy': correct / len(subset) if subset else None,
                         'mean_negative_log_likelihood': loss / len(subset) if subset else None}
    return result


def evaluate_current(document, name, race, opponent, minimum_history=2, policy_prior_games=0):
    if minimum_history < 1:
        raise ValueError('minimum_history must be positive')
    if not math.isfinite(policy_prior_games) or not 0 <= policy_prior_games <= 10:
        raise ValueError('policy_prior_games must be finite and between 0 and 10')
    samples = samples_for(document, name, race)
    recent = current_period(samples)
    folds, skipped = [], []
    for target in recent:
        if target['opponent_race'] != opponent:
            continue
        cutoff = timestamp(target['start_time'])
        current_history = [s for s in recent if timestamp(s['start_time']) < cutoff
                           and s['opponent_race'] == opponent]
        if len(current_history) < minimum_history:
            skipped.append({'match_id': target['match_id'], 'reason': 'insufficient_prior_current_matchup',
                            'prior_matchup_games': len(current_history)})
            continue
        # Filter the whole document, including opponents and equal-time matches.
        # This keeps all model-derived values and opening candidates out of the future.
        prefix = {**document, 'games': [g for g in document['games']
                                       if timestamp(g['start_time']) < cutoff]}
        current = build_current(prefix, name, race, opponent, target['map'],
                                policy_prior_games=policy_prior_games)
        history = [filtered_sample(s, race) for s in samples if timestamp(s['start_time']) < cutoff]
        historical = fit_model(history, name, race)
        held_out = filtered_sample(target, race)
        folds.append({'test_match_id': target['match_id'], 'test_time': target['start_time'],
                      'test_map': target['map'], 'current_training_match_ids': current['training_match_ids'],
                      'historical_training_match_ids': historical['training_match_ids'],
                      'context': current['context'], 'current': command_score(current, held_out),
                      'historical': command_score(historical, held_out)})
    aggregate = {}
    for method in ('current', 'historical'):
        aggregate[method] = {}
        for label in ('all', 'combat'):
            eligible = [f[method][label] for f in folds if f[method][label]['commands']]
            aggregate[method][label] = {
                'evaluated_games': len(eligible),
                'commands': sum(s['commands'] for s in eligible),
                'zero_probability_commands': sum(s['zero_probability_commands'] for s in eligible),
                'game_mean_top1_accuracy': mean(s['top1_accuracy'] for s in eligible) if eligible else None,
                'game_mean_negative_log_likelihood': mean(s['mean_negative_log_likelihood'] for s in eligible)
                if eligible else None}
    return {'name': name, 'race': race, 'opponent': opponent,
            'method': 'current_period_forward_chaining_strictly_earlier_games',
            'minimum_prior_matchup_games': minimum_history,
            'policy_prior_games': policy_prior_games,
            'period_games': len(recent), 'folds': folds, 'skipped': skipped, 'aggregate': aggregate,
            'probability_floor': PROBABILITY_FLOOR,
            'scope': 'Filtered command prediction, equally weighted by game; not successful production, '
                     'build fidelity, win rate, micro quality or personal likeness. Historical baseline '
                     'uses the same player and identical command filtering. No automatic model selection.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('name')
    parser.add_argument('--race', required=True, choices=sorted(SUPPORTED_UNITS))
    parser.add_argument('--opponent', required=True, choices=sorted(SUPPORTED_UNITS))
    parser.add_argument('--input', default='data/replays-v2.json')
    parser.add_argument('--output', required=True)
    parser.add_argument('--policy-prior-games', type=float, default=0)
    args = parser.parse_args()
    document = json.loads(Path(args.input).read_text(encoding='utf-8'))
    report = evaluate_current(document, args.name, args.race, args.opponent,
                              policy_prior_games=args.policy_prior_games)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'name': args.name, 'evaluated_games': len(report['folds']),
                      'skipped_games': len(report['skipped']), 'aggregate': report['aggregate']},
                     ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
