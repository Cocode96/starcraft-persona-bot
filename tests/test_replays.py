import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import subprocess

from persona.profiles import export_bot, fit_profile
from persona.replays import catalog, scan, summarize


def fixture():
    return {
        "Header": {"Frames": 20000, "StartTime": "2025-10-28T20:00:00+09:00",
                   "Map": "\x07Fixture", "Type": {"Name": "Use map settings"},
                   "Version": "1.21+", "Players": [
                       {"ID": 1, "SlotID": 0, "Type": {"ID": 2}, "Name": "PlayerA",
                        "Race": {"Name": "Zerg"}, "Team": 1},
                       {"ID": 0, "SlotID": 1, "Type": {"ID": 2}, "Name": "PlayerB",
                        "Race": {"Name": "Protoss"}, "Team": 2},
                       {"ID": 2, "Type": {"ID": 2}, "Name": "Observer",
                        "Race": {"Name": "Zerg"}, "Team": 3, "Observer": True}]},
        "Commands": {"Cmds": [
            {"PlayerID": 0, "Frame": 100, "Type": {"Name": "Build"}, "Unit": {"ID": 156, "Name": "Pylon"}},
            {"PlayerID": 1, "Frame": 500, "Type": {"Name": "Build"}, "Unit": {"ID": 142, "Name": "Pool"}, "IneffKind": 5},
            {"PlayerID": 1, "Frame": 1800, "Type": {"Name": "Build"}, "Unit": {"ID": 142, "Name": "Pool"}},
            {"PlayerID": 1, "Frame": 1900, "Type": {"Name": "Build"}, "Unit": {"ID": 142, "Name": "Pool"}}]},
        "Computed": {"PlayerDescs": [{"PlayerID": 1, "APM": 120}],
                     "ChatCmds": [{"Message": "Do not export chat"}]}}


class ReplayTests(unittest.TestCase):
    def test_hotkey_selection_and_build_coordinates_are_retained(self):
        data = fixture()
        data['Commands']['Cmds'] = [
            {'PlayerID': 1, 'Frame': 1, 'Type': {'Name': 'Select'}, 'UnitTags': [91], 'IneffKind': 4},
            {'PlayerID': 1, 'Frame': 2, 'Type': {'Name': 'Hotkey'}, 'HotkeyType': {'Name': 'Assign'}, 'Group': 2},
            {'PlayerID': 1, 'Frame': 3, 'Type': {'Name': 'Select'}, 'UnitTags': [92]},
            {'PlayerID': 1, 'Frame': 4, 'Type': {'Name': 'Hotkey'}, 'HotkeyType': {'Name': 'Select'}, 'Group': 2},
            {'PlayerID': 1, 'Frame': 5, 'Type': {'Name': 'Unit Morph'}, 'Unit': {'ID': 41}},
            {'PlayerID': 1, 'Frame': 6, 'Type': {'Name': 'Build'}, 'Unit': {'ID': 131, 'Name': 'Hatchery'}, 'Pos': {'X': 20, 'Y': 30}},
        ]
        events = summarize(data, 'test', [])['players'][0]['events']
        self.assertEqual(events[0]['producer_tags'], [91])
        self.assertEqual(events[1]['tile'], [20, 30])

    def test_scan_deduplicates_bytes_and_retains_source_names(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "one.rep").write_bytes(b"same replay")
            (root / "renamed.REP").write_bytes(b"same replay")
            tool = root / "screp.exe"
            tool.touch()
            result = subprocess.CompletedProcess([], 0, json.dumps(fixture()).encode(), b"")
            with patch("persona.replays.subprocess.run", return_value=result) as run:
                data = scan(root, tool, root / "catalog.json", jobs=1)
            self.assertEqual(run.call_count, 1)
            self.assertEqual(data["source_files"], 2)
            self.assertEqual(data["byte_duplicates"], 1)
            self.assertEqual(len(data["games"][0]["sources"]), 2)

    def test_scan_surfaces_corrupt_replays_without_losing_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "broken.rep").write_bytes(b"broken")
            tool = root / "screp.exe"
            tool.touch()
            result = subprocess.CompletedProcess([], 0, b"not JSON", b"")
            with patch("persona.replays.subprocess.run", return_value=result):
                data = scan(root, tool, root / "catalog.json", jobs=1)
            self.assertEqual(data["games"], [])
            self.assertEqual(data["errors"][0]["source"], "broken.rep")
            self.assertTrue((root / "catalog.json").is_file())

    def test_command_player_id_is_not_slot_id(self):
        game = summarize(fixture(), "a", ["sample.rep"])
        self.assertEqual(game["players"][0]["first_build_commands"], {"142": {"name": "Pool", "frame": 1800}})
        self.assertEqual(game["players"][1]["first_build_commands"]["156"]["frame"], 100)
        self.assertEqual(game["map"], "Fixture")
        self.assertNotIn("Do not export chat", json.dumps(game))
        self.assertEqual(len(game["players"]), 2)

    def test_team_games_not_eligible_for_profile(self):
        source = fixture()
        source["Header"]["Players"][1]["Team"] = 1
        game = summarize(source, "a", [])
        self.assertFalse(game["two_human_opponents"])
        with self.assertRaises(ValueError):
            fit_profile({"games": [game]}, "PlayerA")

    def test_exact_nickname_and_matchup(self):
        game = summarize(fixture(), "a", [])
        data = {"games": [game]}
        with self.assertRaises(ValueError):
            fit_profile(data, "playera")
        with self.assertRaises(ValueError):
            fit_profile(data, "PlayerA", opponent_race="Terran")
        profile = fit_profile(data, "PlayerA", opponent_race="Protoss")
        self.assertEqual(profile["sample_games"], 1)

    def test_median_only_observed_build_commands(self):
        first = summarize(fixture(), "a", [])
        second = copy.deepcopy(first)
        second["sha256"] = "b"
        second["players"][0]["first_build_commands"]["142"]["frame"] = 2200
        third = copy.deepcopy(first)
        third["sha256"] = "c"
        third["players"][0]["first_build_commands"] = {}
        profile = fit_profile({"games": [first, second, third]}, "PlayerA")
        self.assertEqual(profile["sample_games"], 3)
        self.assertEqual(profile["first_build_frames"]["142"], {"median": 2000, "samples": 2})
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "profile.ini"
            export_bot(profile, output)
            text = output.read_text()
            self.assertIn("pool_frame=2000", text)
            self.assertIn("sample_games=3", text)
            profile["race"] = "Protoss"
            with self.assertRaises(ValueError):
                export_bot(profile, output)

    def test_catalog_excludes_observers_and_uses_exact_anchor(self):
        rows = catalog({"games": [summarize(fixture(), "a", [])]}, "PlayerA")
        self.assertEqual(rows[0]["name"], "PlayerB")
        self.assertEqual(rows[0]["with_anchor"], 1)
        self.assertNotIn("Observer", [row["name"] for row in rows])


if __name__ == "__main__":
    unittest.main()
