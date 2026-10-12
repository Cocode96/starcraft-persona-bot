"""Build/test the Windows module and package a selected replay profile."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

from bootstrap import ROOT, find_cmake


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", default="data/active-persona.ini")
    parser.add_argument("--roster", help="Package all trained people from this roster directory")
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("The BWAPI DLL requires Windows; use CMake with BUILD_BWAPI_BOT=OFF for portable tests")
    cmake = find_cmake()
    subprocess.run([cmake, "-S", str(ROOT), "-B", str(ROOT / "build"), "-A", "Win32"], check=True)
    subprocess.run([cmake, "--build", str(ROOT / "build"), "--config", "Release", "--parallel", "4"], check=True)
    ctest = str(Path(cmake).with_name("ctest.exe"))
    subprocess.run([ctest, "--test-dir", str(ROOT / "build"), "-C", "Release", "--output-on-failure"], check=True)
    if args.roster:
        source = (ROOT / args.roster).resolve()
        manifest = json.loads((source / "roster.json").read_text(encoding="utf-8"))
        for person in manifest["players"]:
            name = person["directory"]
            if Path(name).name != name or name in (".", ".."):
                raise ValueError("Invalid roster directory")
            profiles = [source / name / file for file in ("persona.ini", "persona-Zerg.ini", "persona-Terran.ini", "persona-Protoss.ini")]
            for profile in profiles:
                subprocess.run([str(ROOT / "build/Release/model_contract_tests.exe"), str(profile)], check=True)
            destination = ROOT / "dist/players" / name / "bwapi-data"
            (destination / "AI").mkdir(parents=True, exist_ok=True)
            (destination / "read").mkdir(exist_ok=True)
            (destination / "write").mkdir(exist_ok=True)
            shutil.copy2(ROOT / "build/Release/PersonaBot.dll", destination / "AI/PersonaBot.dll")
            for profile in profiles:
                shutil.copy2(profile, destination / "read" / profile.name)
        shutil.copy2(source / "roster.json", ROOT / "dist/players/roster.json")
        print(f"Packaged {len(manifest['players'])} people into dist/players; in-game verification is pending")
        return
    profile = (ROOT / args.profile).resolve()
    # Use the actual C++ parser to validate the Python-generated profile.
    subprocess.run([str(ROOT / "build/Release/config_tests.exe"), str(profile)], check=True)
    destination = ROOT / "dist/bwapi-data"
    (destination / "AI").mkdir(parents=True, exist_ok=True)
    (destination / "read").mkdir(exist_ok=True)
    (destination / "write").mkdir(exist_ok=True)
    shutil.copy2(ROOT / "build/Release/PersonaBot.dll", destination / "AI/PersonaBot.dll")
    shutil.copy2(profile, destination / "read/persona.ini")
    print(f"Prepared {destination}; game installation has not been modified")


if __name__ == "__main__":
    main()
