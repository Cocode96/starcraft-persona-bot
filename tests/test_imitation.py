import copy
from pathlib import Path
import tempfile
import unittest

from persona.imitation import distribution, export_model, phase, samples_for, train
from persona.replays import summarize
from test_replays import fixture


def dataset():
    games = []
    for i in range(10):
        source = fixture()
        source["Header"]["StartTime"] = f"2026-01-{i+1:02}T20:00:00+09:00"
        for j in range(10):
            source["Commands"]["Cmds"].append({"PlayerID": 1, "Frame": 5000 + j,
                "Type": {"Name": "Unit Morph"}, "Unit": {"ID": 38 if i < 8 else 37, "Name": "Fixture unit"}})
        games.append(summarize(source, str(i), []))
    return {"schema_version": 2, "games": games}


class ImitationTests(unittest.TestCase):
    def test_latest_matches_held_out_without_opponent_record_leakage(self):
        data = dataset()
        model = train(data, "PlayerA", "Zerg")
        report = model["evaluation"]
        self.assertEqual((report["train_games"], report["test_games"]), (8, 2))
        self.assertFalse(set(model["training_match_ids"]) & set(report["test_match_ids"]))
        self.assertFalse(set(report["baseline_training_match_ids"]) & set(report["test_match_ids"]))
        # Held-out games switch to zerglings; they must not improve training counts.
        self.assertGreater(model["policies"]["Protoss"][1]["38"], 0.9)
        self.assertEqual(report["personal"]["combat_top1_accuracy"], 0)

    def test_repeated_match_id_not_counted_twice(self):
        data = dataset()
        data["games"].append(copy.deepcopy(data["games"][0]))
        self.assertEqual(len(samples_for(data, "PlayerA", "Zerg")), 10)

    def test_each_game_has_equal_weight_despite_spam(self):
        samples = samples_for(dataset(), "PlayerA", "Zerg")[:2]
        samples[0]["player"]["events"] = [{"kind": "train", "id": 38, "frame": 5000}] * 1000
        samples[1]["player"]["events"] = [{"kind": "train", "id": 37, "frame": 5000}]
        probabilities = distribution(samples, "Zerg", 1)
        self.assertAlmostEqual(probabilities["38"], probabilities["37"])
        self.assertAlmostEqual(sum(probabilities.values()), 1)

    def test_export_keeps_one_real_training_sequence(self):
        model = train(dataset(), "PlayerA", "Zerg")
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory) / "persona.ini"
            chosen = export_model(model, file, "Protoss", 42)
            text = file.read_text()
            self.assertIn(chosen, model["training_match_ids"])
            self.assertIn("step=1800,0,142,1", text)
            self.assertIn("race=Zerg", text)
            self.assertEqual(chosen, export_model(model, file, "Protoss", 42))

    def test_phase_boundaries(self):
        self.assertEqual([phase(f) for f in (0, 4319, 4320, 8639, 8640, 14400)], [0, 0, 1, 1, 2, 3])

    def test_insufficient_data_and_old_schema_rejected(self):
        data = dataset()
        data["games"] = data["games"][:4]
        with self.assertRaises(ValueError):
            train(data, "PlayerA", "Zerg")
        data["schema_version"] = 1
        with self.assertRaises(ValueError):
            samples_for(data, "PlayerA", "Zerg")

    def test_computer_player_excludes_false_one_v_one(self):
        source = fixture()
        source["Header"]["Players"].append({"ID": 3, "Type": {"ID": 1}, "Name": "Computer",
                                            "Race": {"Name": "Zerg"}, "Team": 3})
        self.assertFalse(summarize(source, "x", [])["two_human_opponents"])

    def test_repeated_build_input_does_not_add_building_count(self):
        source = fixture()
        base = {"PlayerID": 1, "Frame": 2500, "Type": {"Name": "Build"},
                "Unit": {"ID": 131, "Name": "Hatchery"}, "Pos": {"X": 30, "Y": 30}}
        source["Commands"]["Cmds"].extend([base, dict(base, Frame=2501)])
        events = summarize(source, "x", [])["players"][0]["events"]
        hatcheries = [e for e in events if e["kind"] == "build" and e["id"] == 131]
        self.assertEqual(hatcheries, [{"frame": 2500, "kind": "build", "id": 131, "count": 2, "tile": [30, 30]}])


if __name__ == "__main__":
    unittest.main()
