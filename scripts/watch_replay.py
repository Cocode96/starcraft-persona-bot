"""Open a saved local test replay at watchable speed, in an isolated runtime."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import shutil
import hashlib

from watch_match import ROOT, configuration, launch, prepare_runtime, require_no_game, copy_runtime


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('replay', nargs='?')
    parser.add_argument('--analyze', action='store_true', help='Bounded fast replay diagnostics, without unit control')
    parser.add_argument('--max-frames', type=int, default=24000)
    args = parser.parse_args()
    if not 1 <= args.max_frames <= 432000:
        parser.error('max-frames must be 1..432000')
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
    copy_runtime(prepare_runtime(), folder)
    (folder / 'maps/replays').mkdir(exist_ok=True)
    shutil.copy2(replay, folder / 'maps/replays/watch.rep')
    (folder / 'bwapi-data/AI').mkdir(exist_ok=True)
    (folder / 'bwapi-data/write').mkdir(exist_ok=True)
    shutil.copy2(ROOT / 'build/Release/PersonaBot.dll', folder / 'bwapi-data/AI/PersonaBot.dll')
    (folder / 'bwapi-data/bwapi.ini').write_text(configuration('Replay', 'Zerg', True, 0 if args.analyze else 42,
                                                          replay='maps/replays/watch.rep'), encoding='ascii')
    details = {'folder': str(folder), 'replay': str(replay),
               'replay_sha256': hashlib.sha256(replay.read_bytes()).hexdigest(),
               'dll_sha256': hashlib.sha256((folder / 'bwapi-data/AI/PersonaBot.dll').read_bytes()).hexdigest(),
               'analyze': args.analyze, 'max_frames': args.max_frames if args.analyze else 0}
    (folder / 'replay-viewer.json').write_text(json.dumps(details, indent=2), encoding='utf-8')
    (ROOT / 'dist/last-replay-viewer.json').write_text(json.dumps(details, indent=2), encoding='utf-8')
    launch(folder, max_frames=args.max_frames if args.analyze else 0, replay_analysis=args.analyze)
    print(f'Opened replay: {replay}')
    print(f'Viewer folder: {folder}')


if __name__ == '__main__':
    main()
