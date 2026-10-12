"""Launch two local BWAPI instances with separate files and saved replays."""

import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import zipfile
import csv

ROOT = Path(__file__).resolve().parents[1]
RUNTIME_URL = "https://davechurchill.ca/starcraft/files/startcraft/scbw_bwapi440.zip"
RUNTIME_SHA256 = "4546155ecfebd50f72dc407041ec0b65282aefdf083e58f96c29f55b75eb0c0e"
DEFAULT_MAP = "maps/BroodWar/cog/(4)FightingSpirit1.3.scx"


def require_no_game():
    listing = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq StarCraft.exe', '/FO', 'CSV', '/NH'],
                             capture_output=True, text=True, errors='replace', check=True,
                             creationflags=subprocess.CREATE_NO_WINDOW)
    if any(row and row[0].lower() == 'starcraft.exe' for row in csv.reader(listing.stdout.splitlines())):
        raise RuntimeError('StarCraft is already running. Close the previous game windows before launching a new match.')


def prepare_runtime():
    archive = ROOT / "work/scbw_bwapi440.zip"
    archive.parent.mkdir(exist_ok=True)
    if not archive.exists():
        subprocess.run(["curl.exe", "--fail", "--location", "--silent", "--show-error",
                        "--max-time", "300", RUNTIME_URL, "--output", str(archive)], check=True)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != RUNTIME_SHA256:
        raise ValueError("Unexpected game archive checksum; extraction stopped")
    runtime = ROOT / "tools/runtime"
    if not (runtime / "StarCraft.exe").exists():
        with zipfile.ZipFile(archive) as package:
            for entry in package.namelist():
                if not (runtime / entry).resolve().is_relative_to(runtime.resolve()):
                    raise ValueError("Unsafe archive path")
            package.extractall(runtime)
    return runtime


def configuration(name, race, host, speed, replay=None, map_path=DEFAULT_MAP):
    return f"""[ai]
ai = bwapi-data/AI/PersonaBot.dll
ai_dbg = bwapi-data/AI/PersonaBot.dll
tournament =
[auto_menu]
auto_menu = {'SINGLE_PLAYER' if replay else 'LAN'}
character_name = {name}
lan_mode = Local PC
auto_restart = OFF
map = {replay if replay else map_path if host else ''}
game = {name if host else 'JOIN_FIRST'}
race = {race}
game_type = MELEE
wait_for_min_players = 2
wait_for_max_players = 2
wait_for_time = 1000
save_replay = maps/replays/Persona_$Y$m$d_$H$M$S.rep
[config]
holiday = OFF
shared_memory = ON
console_attach_on_startup = FALSE
console_alloc_on_startup = FALSE
console_attach_auto = FALSE
console_alloc_auto = FALSE
[window]
windowed = ON
left = {40 if host else 710}
top = 60
width = 640
height = 480
[starcraft]
sound = {'ON' if host else 'OFF'}
speed_override = {speed}
drop_players = OFF
"""


def launch(folder, fast_test=False, max_frames=0):
    environment = dict(os.environ, PERSONA_AUTO_CAMERA="1")
    if fast_test:
        environment['PERSONA_FAST_TEST'] = '1'
    if max_frames:
        environment['PERSONA_TEST_MAX_FRAMES'] = str(max_frames)
    result = subprocess.run([str(folder / "injectory_x86.exe"), "--launch", str(folder / "StarCraft.exe"),
                             "--inject", str(folder / "bwapi-data/BWAPI.dll"),
                             "--print-pid"], cwd=folder, capture_output=True, text=True,
                            timeout=45, creationflags=subprocess.CREATE_NO_WINDOW, env=environment)
    (folder / "launch.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"Game launch failed; see {folder / 'launch.log'}")
    print(result.stdout.strip())
    return int(result.stdout.strip())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--left", default="Lavaa")
    parser.add_argument("--right", default="Doran_e")
    parser.add_argument("--speed", type=int, default=42, help="Milliseconds per frame; 42 is normal fastest speed")
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--legacy-profile", action="store_true", help="Use the old historical deployment instead of current-period data")
    parser.add_argument("--map", default=DEFAULT_MAP)
    parser.add_argument('--fast-test', action='store_true', help='Disable rendering and run as fast as possible')
    parser.add_argument('--max-frames', type=int, default=0, help='End the test match at this frame (0 = full match)')
    args = parser.parse_args()
    if not 0 <= args.speed <= 200:
        parser.error("speed must be 0..200")
    if not 0 <= args.max_frames <= 432000:
        parser.error('max-frames must be 0..432000')
    if not args.prepare_only:
        require_no_game()
    roster = json.loads((ROOT / "dist/players/roster.json").read_text(encoding="utf-8"))
    people = []
    for name in (args.left, args.right):
        matches = [p for p in roster["players"] if p["nickname"] == name or p["display_name"] == name]
        if len(matches) != 1:
            parser.error(f"Unknown or ambiguous person: {name}")
        people.append(matches[0])
    runtime = prepare_runtime()
    selected_map = (runtime / args.map).resolve()
    if not selected_map.is_relative_to(runtime.resolve()) or not selected_map.is_file():
        parser.error("Map must exist inside tools/runtime")
    session = ROOT / "dist/matches" / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    folders = []
    for index, person in enumerate(people):
        folder = session / ("left" if index == 0 else "right")
        shutil.copytree(runtime, folder, ignore=shutil.ignore_patterns('*.rep', '*.log'))
        package = (ROOT / "dist/players" / person["directory"] / "bwapi-data").resolve()
        if not package.is_relative_to((ROOT / "dist/players").resolve()):
            raise ValueError("Invalid person package path")
        shutil.copytree(package, folder / "bwapi-data", dirs_exist_ok=True)
        if not args.legacy_profile:
            # Import after bootstrap so direct script invocation works from any directory.
            import sys
            sys.path.insert(0, str(ROOT))
            from persona.temporal import build_current, export_current, export_placements
            document = json.loads((ROOT / "data/replays-v2.json").read_text(encoding="utf-8"))
            model = build_current(document, person['nickname'], person['race'], people[1-index]['race'],
                                  'Fighting Spirit' if 'FightingSpirit' in args.map else Path(args.map).stem)
            profile = folder / 'bwapi-data/read/persona.ini'
            export_current(model, profile)
            export_current(model, profile.with_name(f"persona-{people[1-index]['race']}.ini"))
            export_placements(document, model, profile.with_name('placements.tsv'))
            subprocess.run([str(ROOT / 'build/Release/model_contract_tests.exe'), str(profile)], check=True)
        (folder / "maps/replays").mkdir(exist_ok=True)
        (folder / "bwapi-data/bwapi.ini").write_text(
            configuration(person["nickname"], person["race"], index == 0, args.speed, map_path=args.map), encoding="ascii")
        folders.append(folder)
    manifest = {"session": str(session), "people": [p["nickname"] for p in people],
                "folders": [str(f) for f in folders], "speed": args.speed, "map": args.map,
                "fast_test": args.fast_test, "max_frames": args.max_frames,
                "dll_sha256": hashlib.sha256((folders[0] / 'bwapi-data/AI/PersonaBot.dll').read_bytes()).hexdigest()}
    (session / "match.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    (ROOT / "dist/last-match.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"Prepared: {session}")
    if args.prepare_only:
        return
    pids = [launch(folders[0], args.fast_test, args.max_frames)]
    time.sleep(2)
    pids.append(launch(folders[1], args.fast_test, args.max_frames))
    manifest['pids'] = pids
    (session / 'match.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    (ROOT / 'dist/last-match.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print("Two local game windows launched. Match startup is automatic; replay files stay in each maps/replays folder.")


if __name__ == "__main__":
    main()
