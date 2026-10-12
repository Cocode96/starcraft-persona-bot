"""Install pinned public tools into this repository only."""

import argparse
import hashlib
import io
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SCREP_VERSION = "v1.13.7"
SCREP_HASHES = {
    "windows-amd64.zip": "7441a2b9a45f87f650bcda5e73a8ff5b710d90000dfad4c1db03ed74833a9661",
    "linux-amd64.tar.gz": "e03637d4bd8aa242b7347a09b9423051baf89cc595398e4da775792535c7572a",
}
BWAPI_SHA256 = "e6d1abeff2a9b31886d9ade15c5c783a32f7e973f1f0e7137911f3d083fa72e2"


def download(url, expected_hash):
    request = urllib.request.Request(url, headers={"User-Agent": "starcraft-persona-bot/0.1"})
    with urllib.request.urlopen(request, timeout=90) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != expected_hash:
        raise RuntimeError(f"Checksum mismatch: {url}")
    return data


def find_cmake():
    if shutil.which("cmake"):
        return shutil.which("cmake")
    vswhere = Path("C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe")
    if vswhere.is_file():
        install = subprocess.check_output(
            [str(vswhere), "-latest", "-products", "*", "-property", "installationPath"],
            text=True).strip()
        candidate = Path(install) / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
        if candidate.is_file():
            return str(candidate)
    raise RuntimeError("CMake is missing; install Visual Studio C++ CMake tools")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bwapi", action="store_true", help="Also unpack BWAPI 4.4.0 SDK")
    args = parser.parse_args()
    system = platform.system().lower()
    if system not in ("windows", "linux") or platform.machine().lower() not in ("amd64", "x86_64"):
        raise RuntimeError("Bootstrap currently supports Windows/Linux x86-64")
    suffix = f"{system}-amd64." + ("zip" if system == "windows" else "tar.gz")
    archive_name = f"screp-{SCREP_VERSION}-{suffix}"
    archive = download(f"https://github.com/icza/screp/releases/download/{SCREP_VERSION}/{archive_name}",
                       SCREP_HASHES[suffix])
    destination = ROOT / "tools/screp"
    destination.mkdir(parents=True, exist_ok=True)
    filename = "screp.exe" if system == "windows" else "screp"
    if system == "windows":
        with zipfile.ZipFile(io.BytesIO(archive)) as package:
            payload = package.read(filename)
    else:
        with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as package:
            member = next(m for m in package.getmembers() if Path(m.name).name == filename and m.isfile())
            payload = package.extractfile(member).read()
    executable = destination / filename
    executable.write_bytes(payload)
    executable.chmod(0o755)
    subprocess.run([str(executable), "-version"], check=True)
    if args.bwapi:
        if system != "windows":
            raise RuntimeError("BWAPI DLL builds require Windows")
        work = ROOT / "work"
        work.mkdir(exist_ok=True)
        package = work / "BWAPI.7z"
        if not package.is_file() or hashlib.sha256(package.read_bytes()).hexdigest() != BWAPI_SHA256:
            package.write_bytes(download("https://github.com/bwapi/bwapi/releases/download/v4.4.0/BWAPI.7z",
                                         BWAPI_SHA256))
        sdk = ROOT / "tools/bwapi"
        sdk.mkdir(parents=True, exist_ok=True)
        subprocess.run([find_cmake(), "-E", "tar", "xf", str(package)], cwd=sdk, check=True)
        print(f"BWAPI SDK: {sdk}")


if __name__ == "__main__":
    main()
