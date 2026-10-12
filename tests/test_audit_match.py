import tempfile
import unittest
from pathlib import Path
from scripts.audit_match import audit_player


class RuntimeAuditTests(unittest.TestCase):
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


if __name__ == '__main__':
    unittest.main()
