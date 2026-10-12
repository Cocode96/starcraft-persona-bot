"""Empirical, matchup/phase-conditioned imitation of recorded command choices.

This model has no reconstructed resource state, rewards, or neural network.
Evaluation describes held-out command prediction, not in-game human likeness.
"""

from collections import Counter
from datetime import datetime
import json
import math
from pathlib import Path
import random
from statistics import median

from .replays import UNIT_TYPES

PHASE_ENDS = (4320, 8640, 14400)
SUPPORTED_UNITS = {
    "Zerg": {37, 38, 39, 41, 43, 44, 47, 62, 103},
    "Terran": {0, 2, 3, 5, 7, 8, 9, 12, 32, 34},
    "Protoss": {60, 61, 64, 65, 66, 67, 70, 72, 83, 84},
}
WORKERS = {"Zerg": 41, "Terran": 7, "Protoss": 64}


def phase(frame):
    return sum(frame >= end for end in PHASE_ENDS)


def timestamp(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()


def samples_for(document, name, race):
    if document.get("schema_version") != 2:
        raise ValueError("Re-scan replays with schema 2 before training")
    seen, samples = set(), []
    for game in document["games"]:
        key = game["match_id"]
        if key in seen or not game["two_human_opponents"] or game["frames"] < 1440:
            continue
        for player in game["players"]:
            if player["name"] == name and player["race"] == race:
                opponent = next(p for p in game["players"] if p["player_id"] != player["player_id"])
                if opponent["race"] not in SUPPORTED_UNITS:
                    continue
                seen.add(key)
                samples.append({"match_id": key, "sha256": game["sha256"], "map": game["map"],
                                "start_time": game["start_time"], "opponent_race": opponent["race"],
                                "player": player})
    return sorted(samples, key=lambda s: (timestamp(s["start_time"]), s["match_id"]))


def distribution(samples, race, phase_index=None, opponent_race=None):
    selected = [s for s in samples if opponent_race is None or s["opponent_race"] == opponent_race]
    if len(selected) < 3:
        selected = samples
    counts = Counter()
    games = 0
    for sample in selected:
        local = Counter(e["id"] for e in sample["player"]["events"]
                        if e["kind"] == "train" and e["id"] in SUPPORTED_UNITS[race]
                        and (phase_index is None or phase(e["frame"]) == phase_index))
        total = sum(local.values())
        if total:
            games += 1
            counts.update({key: count / total for key, count in local.items()})
    if not counts and phase_index is not None:
        return distribution(samples, race, None, opponent_race)
    # A small prior makes unseen-but-supported actions evaluable without log(0).
    denominator = games + 0.01 * len(SUPPORTED_UNITS[race])
    return {str(unit): (counts[unit] + 0.01) / denominator for unit in sorted(SUPPORTED_UNITS[race])}


def fit_model(samples, name, race, display_name=None):
    if not samples:
        raise ValueError(f"No eligible samples for {name}/{race}")
    production = Counter(e["id"] for s in samples for e in s["player"]["events"]
                         if e["kind"] == "train" and e["id"] != {"Zerg": 42}.get(race))
    unsupported = {str(key): value for key, value in production.items() if key not in SUPPORTED_UNITS[race]}
    openings = []
    worker_counts, attack_frames = [], []
    for sample in samples:
        player = sample["player"]
        # Distinct build locations are command evidence; cancellation is not reconstructed.
        steps = [e for e in player["events"] if e["kind"] in ("tech", "upgrade") or
                 (e["kind"] == "build" and UNIT_TYPES.get(str(e["id"]), {}).get("race") == race)][:96]
        openings.append({"match_id": sample["match_id"], "opponent_race": sample["opponent_race"],
                         "map": sample["map"], "steps": steps})
        worker_counts.append(4 + sum(e["kind"] == "train" and e["id"] == WORKERS[race] and e["frame"] <= 14400
                                     for e in player["events"]))
        if player.get("first_distant_attack_command_frame") is not None:
            attack_frames.append(player["first_distant_attack_command_frame"])
    return {
        "schema_version": 2, "name": name, "display_name": display_name or name, "race": race,
        "method": "empirical_matchup_phase_command_model", "sample_games": len(samples),
        "training_match_ids": [s["match_id"] for s in samples],
        "phase_end_frames": list(PHASE_ENDS),
        "policies": {opponent: [distribution(samples, race, i, None if opponent == "Any" else opponent)
                               for i in range(4)] for opponent in ("Any", "Zerg", "Terran", "Protoss")},
        "openings": openings,
        "worker_target_proxy": min(60, max(12, round(median(worker_counts)))),
        "first_distant_attack_frame_proxy": round(median(attack_frames)) if attack_frames else 12000,
        "attack_proxy_samples": len(attack_frames),
        "supported_command_fraction": (sum(production.values()) - sum(unsupported.values())) / max(1, sum(production.values())),
        "unsupported_production_commands": unsupported,
        "observed_unit_ids": sorted(unit for unit in production if unit in SUPPORTED_UNITS[race]),
        "limitations": ["Command attempts are not successful unit production or completed construction.",
                        "Policy observes time phase and matchup, not resource or tactical state.",
                        "Worker target and distant attack timing are proxies, not measured live unit counts or battles.",
                        "Repeated same-location buildings and repeated upgrades are not reconstructed.",
                        "UMS observer maps pass the two-human filter; map rules still need review.",
                        "Spell/micro execution and supply recovery use common rules, not learned personal controls."],
    }


def score(model, samples):
    events, correct, negative_log, combat_events, combat_correct = 0, 0, 0.0, 0, 0
    for sample in samples:
        policies = model["policies"].get(sample["opponent_race"], model["policies"]["Any"])
        for event in sample["player"]["events"]:
            if event["kind"] != "train" or event["id"] not in SUPPORTED_UNITS[model["race"]]:
                continue
            probabilities = policies[phase(event["frame"])]
            prediction = max(probabilities, key=probabilities.get)
            truth = str(event["id"])
            events += 1
            correct += prediction == truth
            negative_log -= math.log(probabilities[truth])
            if event["id"] != WORKERS[model["race"]]:
                combat_probs = {key: value for key, value in probabilities.items() if int(key) != WORKERS[model["race"]]}
                combat_events += 1
                combat_correct += max(combat_probs, key=combat_probs.get) == truth
    return {"production_commands": events, "top1_accuracy": correct / events if events else None,
            "mean_negative_log_likelihood": negative_log / events if events else None,
            "combat_commands": combat_events,
            "combat_top1_accuracy": combat_correct / combat_events if combat_events else None}


def train(document, name, race, display_name=None):
    samples = samples_for(document, name, race)
    if len(samples) < 5:
        raise ValueError(f"Need at least 5 eligible games for chronological evaluation: {name}/{race} has {len(samples)}")
    split = len(samples) - max(1, len(samples) // 5)
    training, held_out = samples[:split], samples[split:]
    model = fit_model(training, name, race, display_name)
    test_ids = {s["match_id"] for s in held_out}
    # Exclude every held-out match, including the other player's record, from baseline training.
    others = {p["name"] for g in document["games"] for p in g["players"] if p["race"] == race}
    baseline_samples = [s for other in sorted(others) for s in samples_for(document, other, race)
                        if s["match_id"] not in test_ids and timestamp(s["start_time"]) < timestamp(held_out[0]["start_time"])]
    baseline = fit_model(baseline_samples or training, "Pooled baseline", race)
    evaluation = {"split": "latest_20_percent_by_match", "train_games": len(training), "test_games": len(held_out),
                  "test_match_ids": sorted(test_ids), "personal": score(model, held_out),
                  "baseline_training_match_ids": sorted({s["match_id"] for s in baseline_samples or training}),
                  "pooled_race_baseline": score(baseline, held_out),
                  "meaning": "Held-out production-command prediction only; not bot skill or human-likeness."}
    model["evaluation"] = evaluation
    return model


def export_model(model, output, opponent="Any", seed=0):
    if model.get("schema_version") != 2 or model.get("method") != "empirical_matchup_phase_command_model":
        raise ValueError("Expected a schema-2 imitation model")
    if not 0 <= seed <= 432000 or opponent not in ("Any", "Zerg", "Terran", "Protoss"):
        raise ValueError("Invalid opponent or seed (expected 0..432000)")
    candidates = [o for o in model["openings"] if o["opponent_race"] == opponent]
    if not candidates:
        candidates = model["openings"]
    chosen = random.Random(seed).choice(candidates)
    rows = ["# Empirical imitation model; replay command attempts, not game-state reconstruction",
            "schema_version=2", f"name={model['name']}", f"race={model['race']}",
            f"sample_games={model['sample_games']}", f"seed={seed}",
            f"worker_target={model['worker_target_proxy']}",
            f"first_attack_frame={model['first_distant_attack_frame_proxy']}",
            f"# Opening source match: {chosen['match_id']}"]
    kinds = {"build": 0, "tech": 1, "upgrade": 2}
    for event in chosen["steps"]:
        rows.append(f"step={event['frame']},{kinds[event['kind']]},{event['id']},{event['count']}")
    for i, policy in enumerate(model["policies"][opponent]):
        for unit, weight in policy.items():
            if int(unit) in model["observed_unit_ids"]:
                rows.append(f"weight={i},{unit},{max(1, round(weight * 10000))}")
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(rows) + "\n", encoding="utf-8")
    return chosen["match_id"]


def train_roster(document, identities, output, seed=0):
    output = Path(output)
    players = [{"nickname": "Lavaa", "display_name": "Lavaa"}] + identities["players"]
    results = []
    for identity in players:
        name = identity["nickname"]
        race_counts = {race: len(samples_for(document, name, race)) for race in SUPPORTED_UNITS}
        race = max(race_counts, key=race_counts.get)
        model = train(document, name, race, identity["display_name"])
        directory = output / f"{name}-{race}"
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "model.json").write_text(json.dumps(model, ensure_ascii=False, indent=2), encoding="utf-8")
        for opponent in ("Any", "Zerg", "Terran", "Protoss"):
            filename = "persona.ini" if opponent == "Any" else f"persona-{opponent}.ini"
            export_model(model, directory / filename, opponent, seed)
        results.append({"nickname": name, "display_name": identity["display_name"], "race": race,
                        "race_counts": race_counts, "directory": directory.name,
                        "supported_command_fraction": model["supported_command_fraction"],
                        "evaluation": model["evaluation"]})
    manifest = {"schema_version": 2, "seed": seed, "players": results,
                "in_game_verified": False, "neural_network": False}
    (output / "roster.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    return manifest
