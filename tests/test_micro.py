import unittest
from persona.micro import caster_evidence


class CasterEvidenceTests(unittest.TestCase):
    def test_tracks_selection_not_target_tag_and_excludes_other_players(self):
        data = {'Header': {'Players': [{'ID': 1, 'Name': 'Test'}]}, 'Commands': {'Cmds': [
            {'PlayerID': 1, 'Frame': 0, 'Type': {'Name': 'Select'}, 'UnitTags': [100]},
            {'PlayerID': 1, 'Frame': 10, 'Type': {'Name': 'Targeted Order'}, 'Order': {'Name': 'CastPsionicStorm'}, 'UnitTag': 999},
            {'PlayerID': 2, 'Frame': 11, 'Type': {'Name': 'Right Click'}, 'UnitTags': [100]},
            {'PlayerID': 1, 'Frame': 30, 'Type': {'Name': 'Right Click'}, 'Pos': {'X': 100, 'Y': 150}},
            {'PlayerID': 1, 'Frame': 31, 'Type': {'Name': 'Select'}, 'UnitTags': [200]},
            {'PlayerID': 1, 'Frame': 40, 'Type': {'Name': 'Right Click'}, 'Pos': {'X': 80, 'Y': 120}},
        ]}}
        report = caster_evidence(data, 'Test')
        self.assertEqual(report['singleton_caster_selection_tags'], [100])
        following = report['windows'][0]['following_inputs_240_frames']
        self.assertEqual([c['frame'] for c in following], [30])
        self.assertNotIn('survived', report)


if __name__ == '__main__':
    unittest.main()
