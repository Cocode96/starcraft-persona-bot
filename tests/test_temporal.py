import copy
from datetime import datetime
import tempfile
from pathlib import Path
import unittest

from persona.temporal import current_period, map_family, build_current, export_current, recency_weight, production_intents, choose_opening
from persona.imitation import samples_for, timestamp
from test_imitation import dataset


class TemporalTests(unittest.TestCase):
    def test_spam_train_inputs_do_not_demand_dozens_of_probes_before_pylon(self):
        events = [{'frame': frame, 'kind': 'train', 'id': 64, 'producer_tags': [99]}
                  for frame in (3, 4, 103, 108, 112, 116, 121, 142, 147, 337, 342, 346)]
        events.append({'frame': 350, 'kind': 'train', 'id': 64, 'producer_tags': [100]})
        filtered = list(production_intents(events, 'Protoss'))
        self.assertEqual([e['frame'] for e in filtered], [3, 337, 350])

    def test_long_break_starts_new_period(self):
        samples = samples_for(dataset(), 'PlayerA', 'Zerg')
        recent = copy.deepcopy(samples[-1])
        recent['start_time'] = '2026-10-10T20:00:00+09:00'
        self.assertEqual(current_period(samples + [recent]), [recent])

    def test_deployment_uses_recent_games_without_claiming_holdout_score(self):
        model = build_current(dataset(), 'PlayerA', 'Zerg', 'Protoss')
        self.assertEqual(model['context']['period_games'], 10)
        self.assertIn(model['context']['opening_match_id'],
                      [s['match_id'] for s in samples_for(dataset(), 'PlayerA', 'Zerg')])
        self.assertEqual(model['context']['skill_trend_assumption'], 'none')
        self.assertNotIn('evaluation', model)
        with tempfile.TemporaryDirectory() as root:
            file = Path(root) / 'persona.ini'
            orders = export_current(model, file)
            self.assertTrue(any(order[1] == 3 for order in orders))
            self.assertIn('schema_version=3', file.read_text())

    def test_same_map_preferred_and_no_old_matchup_silently_reused(self):
        data = dataset()
        data['games'][7]['map'] = '투혼 1.4'
        model = build_current(data, 'PlayerA', 'Zerg', 'Protoss')
        self.assertEqual(model['context']['exact_map_games'], 1)
        self.assertEqual(model['context']['opening_time'], '2026-01-08T20:00:00+09:00')
        self.assertEqual(map_family('(4)Fighting_Spirit 1.3'), map_family('투혼 1.4'))
        with self.assertRaises(ValueError):
            build_current(data, 'PlayerA', 'Zerg', 'Terran')

    def test_recency_is_relevance_not_win_loss(self):
        sample = {'start_time': '2026-01-01T00:00:00+00:00'}
        self.assertAlmostEqual(recency_weight(sample, timestamp('2026-01-31T00:00:00+00:00')), 0.5)

    def test_runtime_map_filename_keeps_same_map_candidate(self):
        data = dataset()
        data['games'][7]['map'] = 'Polypoid 1.75'
        model = build_current(data, 'PlayerA', 'Zerg', 'Protoss', '(4)Polypoid1.75', seed=5)
        self.assertEqual(model['context']['exact_map_games'], 1)
        self.assertEqual(model['context']['opening_time'], '2026-01-08T20:00:00+09:00')
        self.assertEqual(model['context']['opening_fallback'], 'none')
        self.assertEqual(map_family('(4)FightingSpirit1.3'), map_family('투혼 1.4'))
        self.assertNotEqual(map_family('Neo Polypoid 1.75'), map_family('Polypoid 1.75'))

    def test_openings_preserve_frequency_recency_and_whole_source(self):
        samples = samples_for(dataset(), 'PlayerA', 'Zerg')[:3]
        for sample in samples:
            sample['start_time'] = '2026-01-01T00:00:00+00:00'
        samples[2]['start_time'] = '2026-01-31T00:00:00+00:00'
        latest = timestamp(samples[2]['start_time'])
        selected, evidence = choose_opening(samples, latest, 17)
        self.assertEqual([e['probability'] for e in evidence], [0.25, 0.25, 0.5])
        self.assertTrue(any(selected is sample for sample in samples))
        self.assertEqual(choose_opening(samples[::-1], latest, 17), (selected, evidence))
        chosen = {choose_opening(samples, latest, seed)[0]['match_id'] for seed in range(30)}
        self.assertEqual(chosen, {sample['match_id'] for sample in samples})

    def test_export_records_same_selection_and_execution_seed(self):
        model = build_current(dataset(), 'PlayerA', 'Zerg', 'Protoss', seed=17)
        with tempfile.TemporaryDirectory() as root:
            file = Path(root) / 'persona.ini'
            export_current(model, file)
            self.assertIn('seed=17\n', file.read_text())
        self.assertEqual(model['context']['seed'], 17)


if __name__ == '__main__':
    unittest.main()
