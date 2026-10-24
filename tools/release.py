#!/usr/bin/env python3
"""Collects what the local builds left into release/, ready to upload to a
GitHub release:

    python3 tools/release.py                    # the flavor build.ninja builds
    python3 tools/release.py --flavor release   # named so, whatever it builds

    release/halo-switch-<flavor>.zip    halo.nro and halo_guest.elf
    release/halo-windows-<flavor>.zip   halo.exe and SDL3.dll
    release/halo-web-<flavor>.zip       build/web/site, whole
    release/halo-android-<flavor>.apk   what ninja android_apk assembled
    release/halo-android-<flavor>.zip   the same APK, for the app's updater

It builds nothing: run ninja switch, windows, web and android_apk first. The
zips carry what tools/ci_build.py's dist/ folders carry (its OUTPUTS, and the
licences and brokers.txt of copy_notices), flat, so that a zip unpacks into
the game's folder. Android's updater downloads the zip, not the APK, and
installs the .apk it finds in it (port/android/.../Updater.java). The
Switch's is stored, not compressed, as its self-updater reads it while it
downloads (port/switch/host/host_update.c).
Each file's date is printed, so that a stale build is seen before it ships.
"""

import argparse
import re
import shutil
import sys
import tempfile
import time
import zipfile
from pathlib import Path

from ci_build import APKS, OUTPUTS, ROOT, copy_notices

RELEASE = ROOT / "release"
SITE = ROOT / "build/web/site"


def built_flavor() -> str:
    # (configure.py's --release, as the port's updaters see it)
    try:
        match = re.search(r'HALO_BUILD_FLAVOR=\\"(\w+)\\"', (ROOT / "build.ninja").read_text())
    except OSError:
        match = None
    return match.group(1) if match else "debug"


def describe(path: Path) -> str:
    stamp = time.strftime("%Y-%m-%d %H:%M", time.localtime(path.stat().st_mtime))
    return f"{path.relative_to(ROOT)} ({stamp})"


def write_zip(archive: Path, folder: Path, compression: int) -> None:
    with zipfile.ZipFile(archive, "w", compression) as zip_file:
        for path in sorted(folder.rglob("*")):
            if path.is_file() and path.name != ".DS_Store":
                zip_file.write(path, path.relative_to(folder).as_posix())


def zip_port(platform: str, flavor: str, compression: int, outputs=None) -> None:
    outputs = outputs or [ROOT / output for output in OUTPUTS[platform]]
    with tempfile.TemporaryDirectory() as staging:
        staging = Path(staging)
        for output in outputs:
            shutil.copy2(output, staging)
            print(f"  {describe(output)}")
        copy_notices(platform, staging)
        archive = RELEASE / f"halo-{platform}-{flavor}.zip"
        write_zip(archive, staging, compression)
    print(f"-> {archive.relative_to(ROOT)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--flavor", choices=["debug", "release"], default=None,
                        help="the name the files take (default: what build.ninja builds)")
    args = parser.parse_args()
    flavor = args.flavor or built_flavor()

    apk = ROOT / APKS["debug"]  # (ninja android_apk's, release or not)
    needed = [ROOT / output for platform in ("switch", "windows") for output in OUTPUTS[platform]]
    needed += [SITE / "index.html", apk]
    missing = [path for path in needed if not path.exists()]
    if missing:
        for path in missing:
            print(f"missing {path.relative_to(ROOT)}", file=sys.stderr)
        return 1

    if RELEASE.exists():
        shutil.rmtree(RELEASE)
    RELEASE.mkdir()

    print(f"{flavor} release into {RELEASE.relative_to(ROOT)}/")
    zip_port("switch", flavor, zipfile.ZIP_STORED)
    zip_port("windows", flavor, zipfile.ZIP_DEFLATED)

    print(f"  {describe(SITE)}")
    archive = RELEASE / f"halo-web-{flavor}.zip"
    write_zip(archive, SITE, zipfile.ZIP_DEFLATED)
    print(f"-> {archive.relative_to(ROOT)}")

    print(f"  {describe(apk)}")
    target = RELEASE / f"halo-android-{flavor}.apk"
    shutil.copy2(apk, target)
    print(f"-> {target.relative_to(ROOT)}")
    # (the APK is compressed already)
    zip_port("android", flavor, zipfile.ZIP_STORED, [target])
    return 0


if __name__ == "__main__":
    sys.exit(main())
