"""Select a packaged person into dist/bwapi-data without modifying a game install."""

import argparse
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("name", nargs="?", help="Exact replay nickname or confirmed display name")
    args = parser.parse_args()
    roster = json.loads((ROOT / "dist/players/roster.json").read_text(encoding="utf-8"))
    if args.name is None:
        for person in roster["players"]:
            print(f"{person['nickname']:16} {person['display_name']:8} {person['race']}")
        return
    matches = [person for person in roster["players"] if args.name in (person["nickname"], person["display_name"])]
    if len(matches) != 1:
        parser.error("Select exactly one listed nickname or display name")
    person = matches[0]
    folder = person["directory"]
    if Path(folder).name != folder or folder in (".", ".."):
        parser.error("Invalid roster folder")
    source = ROOT / "dist/players" / folder / "bwapi-data"
    files = [Path("AI/PersonaBot.dll")] + [Path("read") / file for file in
             ("persona.ini", "persona-Zerg.ini", "persona-Terran.ini", "persona-Protoss.ini")]
    if not all((source / file).is_file() for file in files):
        parser.error("Package is incomplete; rebuild with --roster first")
    for file in files:
        destination = ROOT / "dist/bwapi-data" / file
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / file, destination)
    (ROOT / "dist/bwapi-data/write").mkdir(exist_ok=True)
    print(f"Selected {person['display_name']} ({person['nickname']}) / {person['race']}; prepared dist/bwapi-data")
    print("No game installed or launched. Select the matching race when testing in BWAPI 4.4.0.")


if __name__ == "__main__":
    main()
