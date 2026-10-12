import argparse
import json
from pathlib import Path
import sys

from .profiles import export_bot, fit_profile, save_profile
from .replays import catalog, scan
from .imitation import train, train_roster, export_model


def main():
    parser = argparse.ArgumentParser(description="Replay profiles for a BWAPI practice bot")
    commands = parser.add_subparsers(dest="command", required=True)
    scan_cmd = commands.add_parser("scan")
    scan_cmd.add_argument("root")
    scan_cmd.add_argument("--screp", default="tools/screp/screp.exe" if sys.platform == "win32" else "tools/screp/screp")
    scan_cmd.add_argument("--output", default="data/replays.json")
    scan_cmd.add_argument("--jobs", type=int, default=4)
    list_cmd = commands.add_parser("players")
    list_cmd.add_argument("--input", default="data/replays.json")
    list_cmd.add_argument("--anchor", default="Lavaa")
    list_cmd.add_argument("--limit", type=int, default=30)
    profile_cmd = commands.add_parser("profile")
    profile_cmd.add_argument("name")
    profile_cmd.add_argument("--race", choices=["Zerg", "Terran", "Protoss"], default="Zerg")
    profile_cmd.add_argument("--opponent-race", choices=["Zerg", "Terran", "Protoss"])
    profile_cmd.add_argument("--input", default="data/replays.json")
    profile_cmd.add_argument("--output", required=True)
    export_cmd = commands.add_parser("export-bot")
    export_cmd.add_argument("profile")
    export_cmd.add_argument("--output", default="data/active-persona.ini")
    train_cmd = commands.add_parser("train")
    train_cmd.add_argument("name")
    train_cmd.add_argument("--race", required=True, choices=["Zerg", "Terran", "Protoss"])
    train_cmd.add_argument("--input", default="data/replays-v2.json")
    train_cmd.add_argument("--output", required=True)
    roster_cmd = commands.add_parser("train-roster")
    roster_cmd.add_argument("--input", default="data/replays-v2.json")
    roster_cmd.add_argument("--identities", default="data/player-identities.json")
    roster_cmd.add_argument("--output", default="data/roster")
    roster_cmd.add_argument("--seed", type=int, default=0)
    model_cmd = commands.add_parser("export-model")
    model_cmd.add_argument("model")
    model_cmd.add_argument("--output", required=True)
    model_cmd.add_argument("--opponent", choices=["Any", "Zerg", "Terran", "Protoss"], default="Any")
    model_cmd.add_argument("--seed", type=int, default=0)
    args = parser.parse_args()
    try:
        if args.command == "scan":
            data = scan(args.root, args.screp, args.output, args.jobs)
            print(json.dumps({k: v for k, v in data.items() if k != "games"}, ensure_ascii=False))
            print(f"Parsed games: {len(data['games'])}; saved {args.output}")
            return 1 if data["errors"] else 0
        if args.command == "export-bot":
            export_bot(json.loads(Path(args.profile).read_text(encoding="utf-8")), args.output)
            print(args.output)
            return 0
        if args.command == "export-model":
            export_model(json.loads(Path(args.model).read_text(encoding="utf-8")), args.output, args.opponent, args.seed)
            print(args.output)
            return 0
        data = json.loads(Path(args.input).read_text(encoding="utf-8"))
        if args.command == "train-roster":
            identities = json.loads(Path(args.identities).read_text(encoding="utf-8"))
            roster = train_roster(data, identities, args.output, args.seed)
            for player in roster["players"]:
                evaluation = player["evaluation"]
                print(f"{player['nickname']} / {player['race']}: train {evaluation['train_games']}, test {evaluation['test_games']}")
        elif args.command == "train":
            save_profile(train(data, args.name, args.race), args.output)
            print(args.output)
        elif args.command == "players":
            print(json.dumps(catalog(data, args.anchor)[:args.limit], ensure_ascii=False, indent=2))
        else:
            profile = fit_profile(data, args.name, args.race, args.opponent_race)
            save_profile(profile, args.output)
            print(f"{profile['name']}: {profile['sample_games']} {profile['race']} games -> {args.output}")
    except (ValueError, OSError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
