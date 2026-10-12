"""Read command evidence, not simulated game state, from screp JSON."""

from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from hashlib import sha256
from datetime import datetime
import json
from pathlib import Path
import re
import subprocess

UNIT_TYPES = json.loads(Path(__file__).with_name("unit_types.json").read_text(encoding="utf-8"))


def clean_text(value):
    return re.sub(r"[\x00-\x1f\x7f]", "", str(value))


def summarize(parsed, digest, sources):
    header = parsed["Header"]
    computed = parsed.get("Computed") or {}
    descriptions = {p["PlayerID"]: p for p in computed.get("PlayerDescs", [])}
    commands = (parsed.get("Commands") or {}).get("Cmds") or []
    players = []
    for player in header.get("Players", []):
        if player.get("Observer") or player.get("Type", {}).get("ID") != 2:
            continue
        pid = player["ID"]  # SlotID is not the command's PlayerID.
        stats = descriptions.get(pid, {})
        first_build = {}
        production = Counter()
        events = []
        seen_builds = set()
        seen_tech = set()
        building_counts = Counter()
        start = stats.get("StartLocation")
        attack_frames = []
        for command in commands:
            if command.get("PlayerID") != pid:
                continue
            # screp marks redundant/ineffective commands; even unmarked inputs
            # do NOT prove the requested action completed in the game.
            if command.get("IneffKind", 0):
                continue
            kind = command.get("Type", {}).get("Name")
            unit = command.get("Unit", {})
            if kind in ("Build", "Building Morph") and "ID" in unit:
                key = str(unit["ID"])
                first_build.setdefault(key, {
                    "name": unit["Name"], "frame": command["Frame"]})
                position = command.get("Pos", {})
                signature = (key, position.get("X"), position.get("Y"))
                if key in UNIT_TYPES and signature not in seen_builds:
                    seen_builds.add(signature)
                    building_counts[key] += 1
                    initial = int(UNIT_TYPES[key]["depot"] and kind != "Building Morph")
                    events.append({"frame": command["Frame"], "kind": "build", "id": unit["ID"],
                                   "count": building_counts[key] + initial})
            if kind in ("Train", "Unit Morph") and "ID" in unit:
                production[str(unit["ID"])] += 1
                if str(unit["ID"]) in UNIT_TYPES:
                    events.append({"frame": command["Frame"], "kind": "train", "id": unit["ID"]})
            for command_name, field in (("Tech", "Tech"), ("Upgrade", "Upgrade")):
                if kind == command_name and field in command:
                    signature = (kind.lower(), command[field]["ID"])
                    if signature not in seen_tech:
                        seen_tech.add(signature)
                        events.append({"frame": command["Frame"], "kind": kind.lower(),
                                       "id": command[field]["ID"], "count": 1})
            # This is an input proxy, not a detected battle or a unit-count estimate.
            if kind == "Targeted Order" and start and command.get("Order", {}).get("Name") in ("AttackMove", "AttackUnit", "Attack1"):
                position = command.get("Pos", {})
                if "X" in position and (position["X"] - start["X"]) ** 2 + (position["Y"] - start["Y"]) ** 2 > 640 ** 2:
                    attack_frames.append(command["Frame"])
        players.append({
            "name": clean_text(player["Name"]), "player_id": pid,
            "race": player["Race"]["Name"], "team": player.get("Team"),
            "apm": stats.get("APM"), "eapm": stats.get("EAPM"),
            "first_build_commands": first_build,
            "production_commands": dict(production),
            "events": events,
            "first_distant_attack_command_frame": min(attack_frames) if attack_frames else None,
        })
    active = [p for p in header.get("Players", []) if not p.get("Observer") and p.get("Type", {}).get("ID") in (1, 2)]
    date = header.get("StartTime")
    stamp = datetime.fromisoformat(date.replace("Z", "+00:00")).timestamp() if date else None
    identity = [stamp, clean_text(header.get("Map", "")), header.get("MapWidth"), header.get("MapHeight"),
                sorted((p["name"], p["race"], p["team"]) for p in players)]
    return {
        "sha256": digest, "sources": sources,
        "match_id": sha256(json.dumps(identity, ensure_ascii=False).encode()).hexdigest() if stamp else digest,
        "start_time": header.get("StartTime"), "frames": header["Frames"],
        "map": clean_text(header.get("Map", "")),
        "game_type": header.get("Type", {}).get("Name"),
        "version": header.get("Version"), "players": players,
        "two_human_opponents": (len(players) == 2 and len(active) == 2 and
                                players[0]["team"] != players[1]["team"]),
    }


def scan(root, screp, output, jobs=4):
    root, screp, output = Path(root).resolve(), Path(screp).resolve(), Path(output)
    if not root.is_dir():
        raise ValueError(f"Replay directory does not exist: {root}")
    if not screp.is_file():
        raise ValueError(f"screp is missing: {screp}; run scripts/bootstrap.py")
    if jobs < 1 or jobs > 16:
        raise ValueError("jobs must be between 1 and 16")
    groups, errors = {}, []
    paths = sorted(p for p in root.rglob("*") if p.suffix.lower() == ".rep")
    for path in paths:
        try:
            digest = sha256(path.read_bytes()).hexdigest()
            groups.setdefault(digest, []).append(str(path.relative_to(root)))
        except OSError as error:
            errors.append({"source": str(path.relative_to(root)), "error": str(error)})

    def parse(item):
        digest, sources = item
        try:
            result = subprocess.run(
                [str(screp), "-cmds", "-indent=false", str(root / sources[0])],
                capture_output=True, timeout=90, check=True)
            return summarize(json.loads(result.stdout), digest, sources), None
        except (OSError, subprocess.SubprocessError, ValueError, KeyError, TypeError) as error:
            return None, {"source": sources[0], "error": str(error)}

    games = []
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        for game, error in pool.map(parse, groups.items()):
            if error:
                errors.append(error)
            else:
                games.append(game)
    matches = {}
    for game in games:
        previous = matches.get(game["match_id"])
        if previous is None:
            matches[game["match_id"]] = game
            continue
        best = game if game["frames"] > previous["frames"] else previous
        best["sources"] = sorted(set(previous["sources"] + game["sources"]))
        matches[game["match_id"]] = best
    document = {"schema_version": 2, "source_files": len(paths),
                "byte_duplicates": sum(len(v) - 1 for v in groups.values()),
                "match_duplicates": len(games) - len(matches),
                "games": list(matches.values()), "errors": errors}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(document, ensure_ascii=False, indent=2), encoding="utf-8")
    return document


def catalog(document, anchor="Lavaa"):
    counts = {}
    for game in document["games"]:
        names = {p["name"] for p in game["players"]}
        for player in game["players"]:
            row = counts.setdefault(player["name"], {
                "games": 0, "with_anchor": 0, "races": Counter(), "latest": ""})
            row["games"] += 1
            row["with_anchor"] += anchor in names and player["name"] != anchor
            row["races"][player["race"]] += 1
            row["latest"] = max(row["latest"], game["start_time"] or "")
    return sorted(({"name": name, **row, "races": dict(row["races"])}
                   for name, row in counts.items()),
                  key=lambda row: (-row["with_anchor"], -row["games"], row["name"]))
