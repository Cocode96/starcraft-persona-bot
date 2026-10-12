"""Read command evidence, not simulated game state, from screp JSON."""

from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess


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
            if kind in ("Train", "Unit Morph") and "ID" in unit:
                production[str(unit["ID"])] += 1
        players.append({
            "name": clean_text(player["Name"]), "player_id": pid,
            "race": player["Race"]["Name"], "team": player.get("Team"),
            "apm": stats.get("APM"), "eapm": stats.get("EAPM"),
            "first_build_commands": first_build,
            "production_commands": dict(production),
        })
    return {
        "sha256": digest, "sources": sources,
        "start_time": header.get("StartTime"), "frames": header["Frames"],
        "map": clean_text(header.get("Map", "")),
        "game_type": header.get("Type", {}).get("Name"),
        "version": header.get("Version"), "players": players,
        "two_human_opponents": (len(players) == 2 and
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
    document = {"schema_version": 1, "source_files": len(paths),
                "byte_duplicates": sum(len(v) - 1 for v in groups.values()),
                "games": games, "errors": errors}
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
