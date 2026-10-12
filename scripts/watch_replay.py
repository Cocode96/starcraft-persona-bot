"""Open a saved local test replay at watchable speed, in an isolated runtime."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import shutil

from watch_match import ROOT, configuration, launch, prepare_runtime, require_no_game


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('replay', nargs='?')
    args = parser.parse_args()
    if args.replay:
        replay = Path(args.replay).resolve()
    else:
        manifest = json.loads((ROOT / 'dist/last-match.json').read_text(encoding='utf-8'))
        candidates = sorted((Path(manifest['folders'][0]) / 'maps/replays').glob('*.rep'))
        if not candidates:
            parser.error('Latest match has no saved replay yet')
        replay = candidates[-1]
    if not replay.is_file() or replay.suffix.lower() != '.rep':
        parser.error('A local .rep file is required')
    require_no_game()
    folder = ROOT / 'dist/replay-viewers' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    shutil.copytree(prepare_runtime(), folder, ignore=shutil.ignore_patterns('*.rep', '*.log'))
    (folder / 'maps/replays').mkdir(exist_ok=True)
    shutil.copy2(replay, folder / 'maps/replays/watch.rep')
    (folder / 'bwapi-data/AI').mkdir(exist_ok=True)
    shutil.copy2(ROOT / 'build/Release/PersonaBot.dll', folder / 'bwapi-data/AI/PersonaBot.dll')
    (folder / 'bwapi-data/bwapi.ini').write_text(configuration('Replay', 'Zerg', True, 42,
                                                          replay='maps/replays/watch.rep'), encoding='ascii')
    launch(folder)
    print(f'Opened replay: {replay}')


if __name__ == '__main__':
    main()
