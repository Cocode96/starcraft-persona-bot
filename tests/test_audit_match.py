import tempfile
import unittest
from pathlib import Path
from scripts.audit_match import audit_player


class RuntimeAuditTests(unittest.TestCase):
    def test_gas_balance_reports_observation_without_claiming_mining_output(self):
        with tempfile.TemporaryDirectory() as root:
            write = Path(root) / 'bwapi-data/write'
            write.mkdir(parents=True)
            (write / 'live-status.tsv').write_text(
                'frame\tworkers\tarmy\tbuildings\tminerals\tgas\tbuilder\tbuild_type\n'
                '9000\t20\t8\t4\t100\t2600\t-1\t-1\n'
                '9240\t20\t8\t4\t100\t1200\t-1\t-1\n')
            (write / 'gas-balance.tsv').write_text('9000\t1\t100\t2600\t9\n9240\t1\t100\t1200\t7\n')
            (write / 'gas-transfers.tsv').write_text('9024\t1\t10\t20\n9120\t2\t10\t20\n')
            report = audit_player(root)['gas_balance']
            self.assertEqual(report['paused_samples'], 2)
            self.assertEqual(report['accepted_mineral_transfers'], 2)
            self.assertEqual(report['max_sampled_gas'], 2600)
            self.assertEqual(report['excess_gas_low_mineral_samples'], 1)
            self.assertNotIn('mined_minerals', report)

    def test_capacity_request_needs_creation_and_live_training_evidence(self):
        with tempfile.TemporaryDirectory() as root:
            write = Path(root) / 'bwapi-data/write'
            write.mkdir(parents=True)
            (write / 'capacity-plans.tsv').write_text(
                '15000\t5\t5\t1800\t24\t10\t20\n'
                '18000\t6\t6\t1800\t24\t30\t40\n'
                '21000\t7\t7\t1800\t24\t50\t60\n')
            (write / 'structure-events.tsv').write_text(
                '15100\t10\t160\t10\t20\n'
                '20000\t11\t160\t30\t40\n'
                '21100\t12\t160\t50\t60\n')
            (write / 'unit-deaths.tsv').write_text('22000\t12\t160\t50\t60\n')
            (write / 'facilities.tsv').write_text(
                '16000\t10\t160\t1\t0\t1\t0\n'
                '16800\t10\t160\t1\t1\t0\t1\n'
                '22200\t12\t160\t1\t1\t0\t1\n')
            additions = audit_player(root)['capacity']['gateway_additions']
            self.assertEqual(additions[0]['created_frame'], 15100)
            self.assertEqual(additions[0]['first_training_sample'], 16800)
            self.assertIsNone(additions[1]['created_frame'])
            self.assertIsNone(additions[1]['first_training_sample'])
            self.assertEqual(additions[2]['created_frame'], 21100)
            self.assertIsNone(additions[2]['first_training_sample'])

    def test_supply_samples_exclude_opening_full_cap_and_elimination(self):
        with tempfile.TemporaryDirectory() as root:
            write = Path(root) / 'bwapi-data/write'
            write.mkdir(parents=True)
            (write / 'live-status.tsv').write_text(
                'frame\tworkers\tarmy\tbuildings\tminerals\tgas\tbuilder\tbuild_type\tsupply_used\tsupply_total\n'
                '6000\t20\t8\t4\t200\t100\t-1\t-1\t60\t60\n'
                '6240\t20\t8\t4\t200\t100\t-1\t-1\t60\t60\n'
                '6480\t20\t8\t4\t200\t100\t-1\t-1\t60\t60\n'
                '6720\t20\t8\t4\t200\t100\t-1\t-1\t60\t76\n'
                '6960\t20\t8\t4\t200\t100\t-1\t-1\t400\t400\n'
                '7200\t0\t8\t4\t200\t100\t-1\t-1\t60\t60\n')
            report = audit_player(root)['supply']
            self.assertEqual(report['samples'], 3)
            self.assertEqual(report['blocked_samples'], 2)
            self.assertEqual(report['longest_blocked_run_samples'], 2)

    def test_missing_runtime_never_passes(self):
        with tempfile.TemporaryDirectory() as root:
            report = audit_player(root)
            self.assertNotEqual(report['state'], 'checks_passed')
            self.assertIsNone(report['first_expansion_at_natural'])

    def test_home_macro_hatch_and_worker_swarm_are_detected(self):
        with tempfile.TemporaryDirectory() as root:
            write = Path(root) / 'bwapi-data/write'
            write.mkdir(parents=True)
            (write / 'live-status.tsv').write_text('frame\tworkers\tarmy\tbuildings\tminerals\tgas\tbuilder\tbuild_type\n9600\t20\t8\t4\t200\t100\t-1\t-1\n')
            (write / 'map-plan.tsv').write_text('home\t7\t6\nbase\t14\t38\n')
            (write / 'structure-events.tsv').write_text('0\t1\t131\t7\t6\n3000\t2\t131\t0\t16\n')
            (write / 'build-orders.tsv').write_text('2718\t3\t131\t0\t16\tbuild\n2724\t4\t131\t0\t16\tbuild\n')
            result = audit_player(root)
            self.assertFalse(result['first_expansion_at_natural'])
            self.assertEqual(len(result['worker_swarm_events']), 1)
            self.assertEqual(result['state'], 'issues_found')

    def test_live_game_stays_incomplete_until_end_callback(self):
        with tempfile.TemporaryDirectory() as root:
            write = Path(root) / 'bwapi-data/write'
            write.mkdir(parents=True)
            (write / 'live-status.tsv').write_text('frame\tworkers\tarmy\tbuildings\tminerals\tgas\tbuilder\tbuild_type\n9600\t20\t8\t4\t200\t100\t-1\t-1\n')
            (write / 'map-plan.tsv').write_text('home\t7\t6\nbase\t14\t38\n')
            (write / 'structure-events.tsv').write_text('0\t1\t131\t7\t6\n3000\t2\t131\t14\t38\n')
            (write / 'scout-orders.tsv').write_text('1800\t3\n')
            (write / 'rally-orders.tsv').write_text('1900\t1\n')
            (write / 'test-limit.txt').write_text('9700\n')
            self.assertEqual(audit_player(root)['state'], 'incomplete')
            (write / 'persona-results.txt').write_text('Test\ttest_limit\t9701\n')
            report = audit_player(root)
            self.assertEqual(report['state'], 'checks_passed')
            self.assertTrue(report['match_finished'])

    def test_caster_research_state_is_separate_from_spell_success(self):
        with tempfile.TemporaryDirectory() as root:
            write = Path(root) / 'bwapi-data/write'
            write.mkdir(parents=True)
            (write / 'caster-state.tsv').write_text(
                '14000\t0\t10\t200\t0\t1\t100\t4\t6\t100\t100\n'
                '16000\t0\t10\t200\t1\t0\t100\t4\t6\t100\t100\n')
            (write / 'micro-actions.tsv').write_text('16002\t10\tstorm\t4\n')
            report = audit_player(root)['casters']
            self.assertEqual(report['first_storm_researched_frame'], 16000)
            self.assertEqual(report['units_observed'], 1)
            self.assertEqual(report['researched_and_energy_ready_samples'], 1)
            self.assertEqual(report['accepted_storm_commands'], 1)
            self.assertNotIn('damage', report)

    def test_caster_escape_orders_are_not_counted_as_successful_survival(self):
        with tempfile.TemporaryDirectory() as root:
            write = Path(root) / 'bwapi-data/write'
            write.mkdir(parents=True)
            self.assertFalse(audit_player(root)['casters']['movement_log_present'])
            (write / 'caster-moves.tsv').write_text(
                '14000\t10\tevade\t0\t-20\t160\t100\t100\n'
                '14048\t10\tscreen\t3\t200\t240\t100\t100\n'
                '14096\t10\tevade\t0\t200\t180\t100\t100\n')
            report = audit_player(root)['casters']
            self.assertTrue(report['movement_log_present'])
            self.assertEqual(report['accepted_evade_moves'], 2)
            self.assertEqual(report['evade_destinations_with_more_clearance'], 1)
            self.assertNotIn('survival_rate', report)

    def test_caster_return_path_checks_include_normal_screen_moves(self):
        with tempfile.TemporaryDirectory() as root:
            write = Path(root) / 'bwapi-data/write'
            write.mkdir(parents=True)
            (write / 'caster-moves.tsv').write_text(
                '14000\t10\tscreen\t2\t120\t180\t100\t100\t300\t300\t-80\n'
                '14048\t10\tevade\t0\t-50\t160\t100\t100\t300\t300\t-50\n')
            report = audit_player(root)
            self.assertEqual(report['casters']['path_checked_moves'], 2)
            self.assertEqual(report['casters']['unsafe_sampled_paths'], 1)
            self.assertTrue(any('sampled path' in item for item in report['findings']))


if __name__ == '__main__':
    unittest.main()
