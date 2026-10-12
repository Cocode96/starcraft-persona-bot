"""Fit transparent command-timing summaries, without claiming neural training."""

from collections import Counter, defaultdict
import json
from pathlib import Path
from statistics import median


def fit_profile(document, player_name, race="Zerg", opponent_race=None):
    samples = []
    for game in document["games"]:
        # UMS observer maps may still have two active human opponents. Keep
        # this heuristic explicit; it does not certify standard melee rules.
        if not game.get("two_human_opponents"):
            continue
        for player in game["players"]:
            if player["name"] != player_name or player["race"] != race:
                continue
            opponent = next(p for p in game["players"] if p is not player)
            if opponent_race and opponent["race"] != opponent_race:
                continue
            samples.append((game, player))
    if not samples:
        raise ValueError(f"No eligible {race} games for exact nickname {player_name!r}")
    timings, production = defaultdict(list), Counter()
    apms = []
    for game, player in samples:
        for unit_id, event in player["first_build_commands"].items():
            timings[unit_id].append(event["frame"])
        production.update(player["production_commands"])
        if player["apm"] is not None:
            apms.append(player["apm"])
    return {
        "schema_version": 1, "name": player_name, "race": race,
        "opponent_race": opponent_race, "sample_games": len(samples),
        "method": "median_first_command_frames",
        "limitations": ["Commands are requests, not successful construction events.",
                        "Two-human-opponent filter includes UMS observer maps.",
                        "No held-out imitation accuracy or in-game strength measured."],
        "median_apm": median(apms) if apms else None,
        "first_build_frames": {key: {"median": round(median(values)), "samples": len(values)}
                               for key, values in sorted(timings.items())},
        "production_command_counts": dict(production),
        "source_hashes": [game["sha256"] for game, _ in samples],
    }


def save_profile(profile, output):
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(profile, ensure_ascii=False, indent=2), encoding="utf-8")


def export_bot(profile, output):
    if profile["race"] != "Zerg":
        raise ValueError("The current BWAPI module supports Zerg only")
    if not profile.get("sample_games"):
        raise ValueError("Cannot export a profile without replay evidence")
    frames = profile["first_build_frames"]
    def timing(unit, default):
        return frames.get(str(unit), {}).get("median", default)
    # Attack timing and worker/army caps are baseline rules, not inferred skill.
    rows = {
        "schema_version": 1, "name": profile["name"],
        "sample_games": profile["sample_games"],
        "pool_frame": timing(142, 1800), "hatchery_frame": timing(131, 3200),
        "extractor_frame": timing(149, 5000), "hydra_den_frame": timing(135, 7000),
        "attack_units": 12, "worker_target": 24,
    }
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("# Replay command medians + explicit baseline rules\n" +
                      "\n".join(f"{key}={value}" for key, value in rows.items()) + "\n",
                      encoding="utf-8")
