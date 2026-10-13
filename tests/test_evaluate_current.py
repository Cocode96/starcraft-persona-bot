import copy
import unittest

from persona.evaluate_current import command_score, evaluate_current
from persona.imitation import timestamp
from test_imitation import dataset


class CurrentEvaluationTests(unittest.TestCase):
    def test_future_and_equal_time_records_never_enter_training(self):
        data = dataset()
        data['games'][8]['start_time'] = data['games'][7]['start_time']
        before = copy.deepcopy(data)
        report = evaluate_current(data, 'PlayerA', 'Zerg', 'Protoss')
        self.assertEqual(data, before)
        times = {g['match_id']: timestamp(g['start_time']) for g in data['games']}
        self.assertEqual(len(report['folds']), 8)
        for fold in report['folds']:
            for method in ('current', 'historical'):
                ids = fold[method + '_training_match_ids']
                self.assertNotIn(fold['test_match_id'], ids)
                self.assertTrue(all(times[key] < timestamp(fold['test_time']) for key in ids))
            candidates = fold['context']['opening_candidates']
            self.assertTrue(all(times[c['match_id']] < timestamp(fold['test_time']) for c in candidates))

    def test_old_period_does_not_satisfy_current_history_requirement(self):
        data = dataset()
        data['games'][-1]['start_time'] = '2026-10-10T20:00:00+09:00'
        report = evaluate_current(data, 'PlayerA', 'Zerg', 'Protoss')
        self.assertEqual(report['period_games'], 1)
        self.assertEqual(report['folds'], [])
        self.assertEqual(report['skipped'][0]['prior_matchup_games'], 0)
        self.assertIsNone(report['aggregate']['current']['all']['game_mean_top1_accuracy'])
        with self.assertRaises(ValueError):
            evaluate_current(data, 'PlayerA', 'Zerg', 'Protoss', policy_prior_games=float('nan'))

    def test_unseen_choices_are_errors_not_crashes_and_workers_do_not_win_combat_prediction(self):
        model = {'race': 'Zerg', 'policies': {'Protoss': [{'41': 0.9, '38': 0.1}] * 4}}
        sample = {'opponent_race': 'Protoss', 'player': {'events': [
            {'kind': 'train', 'id': 38, 'frame': 5000},
            {'kind': 'train', 'id': 37, 'frame': 5001}]}}
        score = command_score(model, sample)
        self.assertEqual(score['all']['top1_accuracy'], 0)
        self.assertEqual(score['combat']['top1_accuracy'], 0.5)
        self.assertEqual(score['combat']['zero_probability_commands'], 1)
        self.assertGreater(score['combat']['mean_negative_log_likelihood'], 10)

    def test_aggregate_weights_games_equally_not_button_volume(self):
        report = evaluate_current(dataset(), 'PlayerA', 'Zerg', 'Protoss')
        scores = [f['current']['all']['top1_accuracy'] for f in report['folds']]
        self.assertAlmostEqual(report['aggregate']['current']['all']['game_mean_top1_accuracy'],
                               sum(scores) / len(scores))


if __name__ == '__main__':
    unittest.main()
