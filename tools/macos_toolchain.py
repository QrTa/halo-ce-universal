"""The tools a build on macOS needs that are not where the build looks by
default: Homebrew's LLVM (Apple's clang in /usr/bin has no
-fno-define-target-os-macros, which the arm64_32 guest's musl passes, nor the
Windows targets), lld (Homebrew's llvm no longer has lld-link), the Android
SDK and the JDK Android Studio ships.

Each is found at configure time and written into build.ninja as an absolute
path, so a build does not depend on the PATH of the shell that runs ninja.
That matters because ninja reruns configure.py by itself whenever one of its
inputs changes, from whichever shell ran ninja. Elsewhere than on macOS (or
with nothing found) each returns None, and the builds keep their defaults."""

import os
import sys
from pathlib import Path
from typing import Optional

# Homebrew's prefixes: Apple Silicon's, then Intel's
HOMEBREW_PREFIXES = (Path("/opt/homebrew"), Path("/usr/local"))


def _homebrew_tool(formula: str, name: str) -> Optional[Path]:
    if sys.platform != "darwin":
        return None
    for prefix in HOMEBREW_PREFIXES:
        tool = prefix / "opt" / formula / "bin" / name
        if tool.is_file():
            return tool
    return None


def default_clang() -> str:
    """Homebrew LLVM's clang on macOS, or plain clang (the PATH's)"""
    clang = _homebrew_tool("llvm", "clang")
    return str(clang) if clang else "clang"


def lld_link() -> Optional[Path]:
    """Homebrew's lld-link on macOS (the lld formula's, or an older llvm's)"""
    return _homebrew_tool("lld", "lld-link") or _homebrew_tool("llvm", "lld-link")


def android_sdk() -> Optional[Path]:
    """ANDROID_HOME or ANDROID_SDK_ROOT, or the SDK Android Studio installs on
    macOS"""
    for variable in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        sdk = os.environ.get(variable)
        if sdk and Path(sdk).is_dir():
            return Path(sdk)
    if sys.platform == "darwin":
        sdk = Path.home() / "Library" / "Android" / "sdk"
        if sdk.is_dir():
            return sdk
    return None


def java_home() -> Optional[Path]:
    """JAVA_HOME, or on macOS the JDK inside Android Studio (the system's
    java may be an old browser plugin's, too old for Gradle)"""
    home = os.environ.get("JAVA_HOME")
    if home and Path(home).is_dir():
        return Path(home)
    if sys.platform == "darwin":
        for studio in (Path("/Applications/Android Studio.app"), Path.home() / "Applications" / "Android Studio.app"):
            jbr = studio / "Contents" / "jbr" / "Contents" / "Home"
            if jbr.is_dir():
                return jbr
    return None
