#!/usr/bin/env python3
"""Collects what the local builds left into release/, ready to upload to a
GitHub release:

    python3 tools/release.py 110                # configures build 110, builds, packs
    python3 tools/release.py                    # the flavor build.ninja builds
    python3 tools/release.py --flavor release   # named so, whatever it builds

Given a build number, it first runs configure.py as a release goes out:
HALO_BUILD_NUMBER=<n> python3 configure.py --release --portable
--windows-cross, against the MSVC toolchain xwin left in ~/xwin (--xwin; its
newest VC/Tools/MSVC version).

    release/halo-switch-<flavor>.zip    halo.nro and halo_guest.elf
    release/halo-windows-<flavor>.zip   halo.exe and SDL3.dll
    release/halo-web-<flavor>.zip       build/web/site, whole
    release/halo-android-<flavor>.apk   what ninja android_apk assembled
    release/halo-android-<flavor>.zip   the same APK, for the app's updater
    release/halo-android-debug.zip      the same zip, for the apps up to build 108

It runs ninja switch, windows, web and android_apk first, so that nothing
older than build.ninja's build number ships (an up-to-date tree builds
nothing), and refuses a build.ninja without HALO_BUILD_NUMBER, whose builds
would never update. The zips carry what tools/ci_build.py's dist/ folders
carry (its OUTPUTS, and the licences and brokers.txt of copy_notices), flat,
so that a zip unpacks into the game's folder. Android's updater downloads the zip, not the APK, and
installs the .apk it finds in it (port/android/.../Updater.java). The
Switch's is stored, not compressed, as its self-updater reads it while it
downloads (port/switch/host/host_update.c).
Each file's date is printed, so that a stale build is seen before it ships.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile
from pathlib import Path

from ci_build import APKS, OUTPUTS, ROOT, copy_notices

RELEASE = ROOT / "release"
SITE = ROOT / "build/web/site"
TARGETS = ["switch", "windows", "web", "android_apk"]


def built_flavor() -> str:
    # (configure.py's --release, as the port's updaters see it)
    try:
        match = re.search(r'HALO_BUILD_FLAVOR=\\"(\w+)\\"', (ROOT / "build.ninja").read_text())
    except OSError:
        match = None
    return match.group(1) if match else "debug"


def built_number() -> int:
    # (configure.py's HALO_BUILD_NUMBER, which the updaters compare with the
    # latest release's: 0, and they never look for one)
    try:
        match = re.search(r"-DHALO_BUILD_NUMBER=(\d+)", (ROOT / "build.ninja").read_text())
    except OSError:
        match = None
    return int(match.group(1)) if match else 0


def configure(number: int, xwin: Path) -> bool:
    # (xwin splat's layout: VC/Tools/MSVC/<version> and Windows Kits/10)
    versions = sorted((xwin / "VC/Tools/MSVC").glob("*"),
                      key=lambda path: [int(part) if part.isdigit() else 0
                                        for part in path.name.split(".")])
    sdk = xwin / "Windows Kits/10"
    if not versions or not sdk.is_dir():
        print(f"no MSVC toolchain in {xwin} (VC/Tools/MSVC/<version> and Windows Kits/10)",
              file=sys.stderr)
        return False
    command = [sys.executable, "configure.py", "--release", "--portable", "--windows-cross",
               "--windows-vctools-dir", str(versions[-1]), "--windows-sdk-dir", str(sdk)]
    print(f"HALO_BUILD_NUMBER={number} {' '.join(command[1:])}")
    environment = dict(os.environ, HALO_BUILD_NUMBER=str(number))
    if subprocess.run(command, cwd=ROOT, env=environment).returncode != 0:
        print("configure.py failed: no release", file=sys.stderr)
        return False
    return True


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
    parser.add_argument("number", type=int, nargs="?",
                        help="the build number to configure for first (default: build.ninja's, "
                        "configured already)")
    parser.add_argument("--xwin", type=Path, default=Path.home() / "xwin",
                        help="the MSVC toolchain the Windows build cross-compiles against "
                        "(default: ~/xwin)")
    args = parser.parse_args()
    if args.number is not None:
        if args.number <= 0:
            print("the build number must be above 0, or the builds never update",
                  file=sys.stderr)
            return 1
        if not configure(args.number, args.xwin):
            return 1
    flavor = args.flavor or built_flavor()

    number = built_number()
    if number <= 0:
        print("build.ninja has no build number, so the release's builds would never update: "
              "HALO_BUILD_NUMBER=<n> python3 configure.py ...", file=sys.stderr)
        return 1
    # (what is not built for this number yet; ninja -n cannot say, as Android's
    # commit stamp runs at every build and is only known unchanged after it)
    if subprocess.run(["ninja", *TARGETS], cwd=ROOT).returncode != 0:
        print(f"ninja {' '.join(TARGETS)} failed: no release", file=sys.stderr)
        return 1

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

    print(f"build {number}, {flavor} release into {RELEASE.relative_to(ROOT)}/")
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
    # (the APK is Gradle's debug build, and the apps up to build 108 ask for
    # the zip of their own kind, halo-android-debug.zip, by name: it too, so
    # that they find the update)
    if flavor != "debug":
        legacy = RELEASE / "halo-android-debug.zip"
        shutil.copy2(RELEASE / f"halo-android-{flavor}.zip", legacy)
        print(f"-> {legacy.relative_to(ROOT)} (the same, for the apps up to build 108)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
