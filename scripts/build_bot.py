"""Build/test the Windows module and package a selected replay profile."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys

from bootstrap import ROOT, find_cmake


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", default="data/active-persona.ini")
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("The BWAPI DLL requires Windows; use CMake with BUILD_BWAPI_BOT=OFF for portable tests")
    cmake = find_cmake()
    subprocess.run([cmake, "-S", str(ROOT), "-B", str(ROOT / "build"), "-A", "Win32"], check=True)
    subprocess.run([cmake, "--build", str(ROOT / "build"), "--config", "Release", "--parallel", "4"], check=True)
    ctest = str(Path(cmake).with_name("ctest.exe"))
    subprocess.run([ctest, "--test-dir", str(ROOT / "build"), "-C", "Release", "--output-on-failure"], check=True)
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
