"""Ninja rules for the PS5 build (``ninja ps5``; port/ps5/PS5.md, phase 1).

The PS5 port runs the x32 guest image of ``ninja linux_x86_64`` (the game
compiled as 32-bit code for x86-64, tools/linux_guest_build.py) in a host
built as a PS5 title with PS5_Vulkan's toolchain. This graph builds, into
build/ps5:

- the guest image, halo_guest_vk.elf: the linux_x86_64 guest from the same
  sources, with the Xbox window movable (HALO_MOVABLE_WINDOW: a title has a
  system module at 0x80000000) and the Vulkan renderer
  (port/android/VULKAN.md), the console's only one;
- the host, host/eboot.elf: port/ps5/host compiled with PS5_Vulkan's
  compiler wrapper over its payload SDK fork, and linked as PS5_Vulkan's
  titles that link RADV are (tools/ps5_title.sh), with RADV's release
  archive, glslang (built here with the fork's CMake toolchain) and pacbrew's
  SDL2 in the link;
- the title, title/PPSA99500: eboot.bin (the host, signed), sce_sys/
  (param.json and icon0.png, port/ps5/sce_sys), sce_module/libc.prx
  (PS5_Vulkan's), halo_guest.elf and NOTICE.txt (what the build is made of,
  and where to get it), which tools/ps5_deploy.py installs on the console.

None of PS5_Vulkan, its SDK fork, PS5_Mesa, pacbrew or glslang is copied
into this repository: they are GPL (glslang is not, but is fetched all the
same), and they are used where they are, at the commits pinned below, which
NOTICE.txt names. Their checkouts are configure.py's --ps5-vulkan (whose
.deps/native holds the SDK fork, RADV's archive and zlib, set up by its
tools/setup-native-dependencies.sh and tools/build-radv.sh) and --ps5-mesa.

The build is generated only when asked for (configure.py --ps5 or
--ps5-vulkan, or once its sources are fetched under build/ps5/third_party);
otherwise, and when it cannot be generated, ``ninja ps5`` is a target that
stops with the reason. A checkout that is not at its pin is a warning, not
a stop. The PS5's own rules name their files by absolute path, since
tools/ps5_title.sh and CMake run them from other folders; the guest's rules
are the shared generator's (tools/android_build.py, generate_guest_image),
which names them from the repository, as the other ports' do.
"""

import io
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

from .android_build import GLSLANG_DIR as ANDROID_GLSLANG_DIR
from .android_build import GLSLANG_TAG, GLSLANG_URL, MUSL_VERSION, fetch_third_party, generate_guest_image
from .guest_arch import X32
from .linux_build import updater_defines
from .ninja_syntax import Writer, escape

# the pins: what phase 0 B of port/ps5/PS5.md built and ran on the console
PS5_VULKAN_COMMIT = "8aee3afb37bcfad06f0c7d263b901040f08ebbd0"
PS5_VULKAN_URL = "https://github.com/mpereiraesaa/PS5_Vulkan"
# (the fork of the payload SDK that PS5_Vulkan's setup exports by commit:
# its .deps/native/ps5-payload-sdk/.ps5-sdk-revision)
PS5_PAYLOAD_SDK_REVISION = "95c08f27386fc698f6bbe21dde3030140a41d10b"
PS5_PAYLOAD_SDK_URL = "https://github.com/mihawk-99/PS5_PayloadSDK"
# (RADV's release archive, built from it, and Vulkan's headers)
PS5_MESA_COMMIT = "01d70579b8d410d6a482e36c6f184798ae4c83ad"
PS5_MESA_URL = "https://github.com/mpereiraesaa/PS5_Mesa"
# (SDL2 2.30.12 and what it needs, as PS5_Vulkan's
# tools/setup-pacbrew-dependencies.sh fetches and checks it)
PACBREW_VERSION = "v0.40.2"
PACBREW_SHA256 = "a85f65de418a8e6a898c6c3e3c870d50fff7618a200e4dd59ea9692af6ecec4d"
PACBREW_URL = f"https://github.com/ps5-payload-dev/pacbrew-repo/releases/download/{PACBREW_VERSION}/ps5-payload-dev.tar.gz"

TITLE_ID = "PPSA99500"
PORT_DIR = Path("port/ps5")
ANDROID_DIR = Path("port/android")
LINUX_DIR = Path("port/linux")
LINUX_HOST_DIR = LINUX_DIR / "host"
TOMLC17_DIR = Path("port/third_party/tomlc17")
# the Android host library's files the PS5's host uses as they are
# (port/ps5/PS5.md, phase 2); the rest are replaced by port/ps5/host's
SHARED_HOST_FILES = ("host_arch.c", "host_common.c", "host_loader.c", "host_thread.c", "host_watch_hash.c")
# and the Vulkan renderer's host half (port/ps5/PS5.md, phase 4), less
# host_vk_driver.c (port/ps5/host/host_vk_driver_ps5.c is the console's) and
# the probe, host_vk_probe.c
SHARED_RENDERER_FILES = ("host_vk.c", "host_vk_data.c", "host_vk_draw.c", "host_vk_post.c", "host_vk_present.c",
                         "host_vk_render.c", "host_vk_shaders.c", "host_vk_texture.c", "host_vk_visibility.c")
HOST_WRAPS = ("--wrap=mmap", "--wrap=SDL_ShowSimpleMessageBox")
BUILD = Path("build/ps5")
THIRD_PARTY = BUILD / "third_party"
GLSLANG_DIR = THIRD_PARTY / "glslang"
# the system's OpenGL headers: the guest's desktop OpenGL entry points come
# from GL/glcorearb.h, as linux_x86_64's do
GL_HEADERS = Path("/usr/include")
GL_HEADER_NAMES = ("GL/glcorearb.h", "GLES3/gl32.h", "GLES2/gl2ext.h", "KHR/khrplatform.h")

DEFAULT_VULKAN = Path.home() / "projects" / "ps5-deps" / "PS5_Vulkan"
DEFAULT_LLVM_CONFIG = Path("/usr/bin/llvm-config-22")
DEFAULT_HOSTDEPS = Path.home() / "projects" / "ps5-deps" / "hostdeps" / "env.sh"

# what of the PS5_Vulkan checkout the build uses, besides .deps/native (the
# SDK fork, RADV's archive, zlib) and runtime/libc.prx
NATIVE_TOOL_SOURCES = ("native_app_builder.cpp", "self_container.cpp", "elf_object.cpp", "sce_module_writer.cpp")
VULKAN_FILES = (
    "tooling/prospero-clang18", "tools/radv-link.sh", "tools/setup-pacbrew-dependencies.sh",
    "tooling/native/app_crt.cpp", "tooling/native/app_cpp_runtime.cpp", "tooling/native/app-symbols.map",
    "tooling/psbc/ps5-pie-unwind.ld", "runtime/libc.prx.sha256",
    *(f"tooling/native/{name}" for name in NATIVE_TOOL_SOURCES),
)
# the AGC stub libraries that name the driver's imports, compiled from
# PS5_Vulkan's own stub sources as its tools/build-radv-title.sh does
AGC_STUBS = (("libSceAgc", "vendor/ps5/sdk/stubs/agc_canary_link_stub.c"),
             ("libSceAgcDriver", "vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c"))


def ps5_configure_inputs() -> List[Path]:
    return [Path(__file__), PORT_DIR / "host", PORT_DIR / "sce_sys"]


def requested(sln: Any, third_party: Path = THIRD_PARTY) -> bool:
    """whether configure.py is to generate the build, and so fetch its
    sources: asked for (--ps5, --ps5-vulkan), or its sources there already"""
    return (bool(getattr(sln, "ps5", False)) or bool(getattr(sln, "ps5_vulkan", None))
            or ((third_party / f"musl-{MUSL_VERSION}").is_dir() and (third_party / "SDL3").is_dir()))


def _unavailable(n: Writer, reason: str, quiet: bool = False) -> None:
    """a ps5 target that stops with the reason it cannot be built
    (linux_guest_build's pattern); configure.py says it too unless quiet"""
    message = f"PS5 build (ps5) unavailable: {reason}"
    if not quiet:
        print(message, file=sys.stderr)
    n.comment(message)
    n.rule(name="ps5_unavailable", command=escape(f"echo {shlex.quote(message)} >&2 && exit 1"),
           description="PS5 UNAVAILABLE")
    n.build(outputs="ps5", rule="ps5_unavailable")
    n.newline()


def generate_ps5_build(n: Writer, sln: Any) -> None:
    config_path = LINUX_DIR / "port.json"
    if not config_path.is_file() or not (PORT_DIR / "host").is_dir():
        return
    if not requested(sln):
        _unavailable(n, "run python3 configure.py --ps5 first (it downloads musl, SDL3 and glslang for it)",
                     quiet=True)
        return
    # (written aside, so that a build that fails to generate halfway leaves
    # nothing of itself but its unavailable target)
    output = io.StringIO()
    try:
        _generate(Writer(output, n.width), sln, config_path)
    except (subprocess.CalledProcessError, OSError, RuntimeError) as error:
        _unavailable(n, str(error))
        return
    n.output.write(output.getvalue())


# ---------- the checkouts and their pins

def _git_head(path: Path) -> Optional[str]:
    result = subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None


def _warn_pin(what: str, path: Path, actual: Optional[str], pin: str) -> None:
    if actual != pin:
        print(f"PS5 build: warning: {what} at {path} is {actual or 'not a git checkout'}, not the pinned {pin} "
              "(tools/ps5_build.py)", file=sys.stderr)


def check_pins(vulkan: Path, sdk: Path, mesa: Path, glslang: Path) -> None:
    """warns of each checkout that is not at its pin"""
    _warn_pin("PS5_Vulkan", vulkan, _git_head(vulkan), PS5_VULKAN_COMMIT)
    revision = (sdk / ".ps5-sdk-revision").read_text(encoding="utf-8").strip()
    _warn_pin("the SDK fork", sdk, revision, PS5_PAYLOAD_SDK_REVISION)
    _warn_pin("PS5_Mesa", mesa, _git_head(mesa), PS5_MESA_COMMIT)
    script = (vulkan / "tools" / "setup-pacbrew-dependencies.sh").read_text(encoding="utf-8")
    pinned = f"version={PACBREW_VERSION}\n" in script and f"hash={PACBREW_SHA256}\n" in script
    _warn_pin("pacbrew", vulkan / "tools" / "setup-pacbrew-dependencies.sh",
              f"{PACBREW_VERSION} {PACBREW_SHA256}" if pinned else "another release",
              f"{PACBREW_VERSION} {PACBREW_SHA256}")
    tag = subprocess.run(["git", "-C", str(glslang), "describe", "--tags"], capture_output=True, text=True)
    _warn_pin("glslang", glslang, tag.stdout.strip() if tag.returncode == 0 else None, GLSLANG_TAG)


def fetch_glslang() -> Path:
    """glslang's source at GLSLANG_TAG (configure time, once): the Android
    build's clone when it is there at that tag, else a clone of its own"""
    describe = subprocess.run(["git", "-C", str(ANDROID_GLSLANG_DIR), "describe", "--tags"], capture_output=True,
                              text=True)
    if not GLSLANG_DIR.is_dir() and describe.returncode == 0 and describe.stdout.strip() == GLSLANG_TAG:
        return ANDROID_GLSLANG_DIR
    if not GLSLANG_DIR.is_dir():
        print(f"Cloning glslang {GLSLANG_TAG}")
        GLSLANG_DIR.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "clone", "-q", "--depth", "1", "--branch", GLSLANG_TAG, GLSLANG_URL, str(GLSLANG_DIR)],
                       check=True)
    return GLSLANG_DIR


def resolve_pacbrew(vulkan: Path) -> Dict[str, Any]:
    """pacbrew's SDL2, fetched once (346 MB, into PS5_Vulkan's .deps/pacbrew)
    and checked by PS5_Vulkan's script: its include folders and defines
    ("cflags") and its archives and system libraries in link order ("libs")"""
    result = subprocess.run(["bash", str(vulkan / "tools" / "setup-pacbrew-dependencies.sh"), "--resolve", "sdl2"],
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"pacbrew's SDL2 could not be resolved: {result.stderr.strip() or result.stdout.strip()}")
    return json.loads(result.stdout)


def notice_text() -> str:
    """NOTICE.txt: what the title is built from, and where its source is"""
    return f"""Halo: Combat Evolved for the PS5 ({TITLE_ID})

This title is built from this repository and from the following, at the
commits named. They are GPL-3.0-or-later (glslang is under its own licences,
named in its repository); the title is a GPL work as a whole, and its source
is all of them at these commits.

- PS5_Vulkan {PS5_VULKAN_COMMIT}
  {PS5_VULKAN_URL}
  (its C runtime, linker script, title tools, AGC stub sources, link recipe
  and runtime/libc.prx)
- PS5_PayloadSDK (PS5_Vulkan's fork of the payload SDK) {PS5_PAYLOAD_SDK_REVISION}
  {PS5_PAYLOAD_SDK_URL}
  (the platform layer, libc++ and the system's stub libraries)
- PS5_Mesa {PS5_MESA_COMMIT}
  {PS5_MESA_URL}
  (RADV, the Vulkan driver, and Vulkan's headers)
- pacbrew {PACBREW_VERSION}, SHA-256 {PACBREW_SHA256}
  {PACBREW_URL}
  (SDL2 2.30.12 and the libraries it brings)
- glslang {GLSLANG_TAG}
  {GLSLANG_URL}
"""


def pins_text() -> str:
    """the pins in one line, for the title's log"""
    return (f"PS5_Vulkan {PS5_VULKAN_COMMIT[:12]}, SDK fork {PS5_PAYLOAD_SDK_REVISION[:7]}, "
            f"PS5_Mesa {PS5_MESA_COMMIT[:12]}, pacbrew {PACBREW_VERSION}, glslang {GLSLANG_TAG}")


def radv_pins_text() -> str:
    """what RADV's archive is built from, for the renderer's log line"""
    return f"PS5_Mesa {PS5_MESA_COMMIT[:12]}, built by PS5_Vulkan {PS5_VULKAN_COMMIT[:12]}"


def _clang_of(llvm_config: Path) -> Path:
    """the clang of an llvm-config (clang 22's, for the host)"""
    try:
        bindir = subprocess.run([str(llvm_config), "--bindir"], capture_output=True, text=True, check=True).stdout.strip()
    except (subprocess.CalledProcessError, OSError) as error:
        raise RuntimeError(f"{llvm_config} does not run ({error}): --ps5-llvm-config names clang 22's") from error
    clang = Path(bindir) / "clang"
    if not clang.is_file():
        raise RuntimeError(f"no clang in {bindir} ({llvm_config} --bindir)")
    return clang


def _guest_tools(guest_cc: str) -> List[str]:
    """llvm-ar, ld.lld and llvm-objdump of the guest compiler's version: those
    of its suffix where a distribution installs several (clang-22's are
    llvm-ar-22 and the rest, as linux_x86_64's), else those beside it (an
    unversioned clang is a link into LLVM's own folder, which has them all
    when the system's path does not)"""
    name = Path(guest_cc).name
    if name.startswith("clang-") and "/" not in guest_cc:
        suffix = name[len("clang"):]
        return [f"llvm-ar{suffix}", f"ld.lld{suffix}", f"llvm-objdump{suffix}"]
    found = shutil.which(guest_cc)
    folder = Path(found).resolve().parent if found else None
    return [str(folder / tool) if folder and (folder / tool).is_file() else tool
            for tool in ("llvm-ar", "ld.lld", "llvm-objdump")]


# ---------- the build

def _generate(n: Writer, sln: Any, config_path: Path) -> None:
    """writes the build to n; raises RuntimeError with the reason it cannot
    be built"""
    root = Path.cwd().resolve()
    vulkan = Path(getattr(sln, "ps5_vulkan", None) or os.environ.get("PS5_VULKAN") or DEFAULT_VULKAN)
    vulkan = vulkan.expanduser().resolve()
    mesa = Path(getattr(sln, "ps5_mesa", None) or vulkan.parent / "PS5_Mesa").expanduser().resolve()
    llvm_config = Path(getattr(sln, "ps5_llvm_config", None) or DEFAULT_LLVM_CONFIG).expanduser()
    hostdeps = Path(getattr(sln, "ps5_hostdeps", None) or DEFAULT_HOSTDEPS).expanduser().resolve()

    if not vulkan.is_dir():
        raise RuntimeError(f"no PS5_Vulkan checkout at {vulkan} (configure.py --ps5-vulkan, or PS5_VULKAN)")
    sdk = vulkan / ".deps" / "native" / "ps5-payload-sdk"
    if not (sdk / ".ps5-sdk-revision").is_file():
        raise RuntimeError(f"no SDK fork in {sdk}: run bash {vulkan / 'tools' / 'setup-native-dependencies.sh'}")
    missing = [name for name in VULKAN_FILES if not (vulkan / name).is_file()]
    missing += [source for _, source in AGC_STUBS if not (vulkan / source).is_file()]
    if missing:
        raise RuntimeError(f"the PS5_Vulkan checkout at {vulkan} has no {', '.join(missing)} "
                           f"(it is pinned at {PS5_VULKAN_COMMIT[:12]})")
    zlib_root = vulkan / ".deps" / "native" / "zlib" / "root"
    zlib = next(iter(sorted(zlib_root.rglob("libz.a"))), None) if zlib_root.is_dir() else None
    if zlib is None:
        raise RuntimeError(f"no zlib in {zlib_root}: run bash {vulkan / 'tools' / 'setup-native-dependencies.sh'}")
    if not (mesa / "include" / "vulkan" / "vulkan.h").is_file():
        raise RuntimeError(f"no PS5_Mesa checkout at {mesa} (configure.py --ps5-mesa): its include/vulkan "
                           "is Vulkan's headers")
    archive = vulkan / ".deps" / "native" / "radv-release" / "lib" / "libvulkan_radeon.ps5.a"
    if not archive.is_file():
        raise RuntimeError(f"no RADV release archive at {archive}: run port/ps5/probe/vk/build.sh, which builds it, "
                           f"or LLVM_CONFIG={llvm_config} bash {vulkan / 'tools' / 'build-radv.sh'} release (clang 22)")
    if not hostdeps.is_file():
        raise RuntimeError(f"no hostdeps environment at {hostdeps} (configure.py --ps5-hostdeps): the script that "
                           "puts meson's and LLVM's tools on the path")
    missing = [name for name in GL_HEADER_NAMES if not (GL_HEADERS / name).is_file()]
    if missing:
        raise RuntimeError(f"no {', '.join(missing)} in {GL_HEADERS} (libgl-dev, libgles-dev): the desktop guest's "
                           "OpenGL stubs need them, as linux_x86_64's do")
    clang = _clang_of(llvm_config)
    # (the guest too, by default: clang 22, the compiler of the profiles in
    # pgo/, with its LLVM tools beside it)
    guest_cc = getattr(sln, "ps5_guest_cc", None) or str(clang)

    try:
        fetch_third_party(THIRD_PARTY)
    except (subprocess.CalledProcessError, OSError, RuntimeError) as error:
        raise RuntimeError(f"cannot fetch musl or SDL3 ({error})") from error
    try:
        glslang_source = fetch_glslang()
    except (subprocess.CalledProcessError, OSError) as error:
        raise RuntimeError(f"cannot fetch glslang {GLSLANG_TAG} ({error})") from error
    check_pins(vulkan, sdk, mesa, glslang_source)
    sdl2 = resolve_pacbrew(vulkan)
    glslang_source = (root / glslang_source).resolve()
    config: Dict[str, Any] = json.loads(config_path.read_text(encoding="utf-8"))

    n.comment("PS5 build (ninja ps5); see port/ps5/README.md")
    build = root / BUILD
    host_dir = build / "host"
    obj_dir = host_dir / "obj"
    title = build / "title" / TITLE_ID
    python = "$python"

    # ---------- the guest image: linux_x86_64's, movable, with the Vulkan renderer

    # (HALO_PS5 for the platform layer's helpers that only the deko3d and
    # Vulkan renderers call, hud_hires.h, text_hires.h and menu_files.h:
    # Android and the Switch have them by their own names; and the Vulkan
    # renderer's host calls, host_imports_vk.list, in the one import table of
    # both images, as Android's)

    n.variable("ps5_guest_cc", guest_cc)
    ar, ld, objdump = _guest_tools(guest_cc)
    guest = generate_guest_image(
        n, sln, config, arch=X32, prefix="ps5", label="PS5", build=BUILD, third_party=THIRD_PARTY,
        guest_cc=guest_cc, gl_headers=GL_HEADERS, ar=ar, ld=ld, objdump=objdump, builtins_cc="$ps5_guest_cc",
        environment="gnu", platform_defines=["-DHALO_MOVABLE_WINDOW=1", "-DHALO_PS5=1"],
        extra_runtime=[LINUX_HOST_DIR / "guest_desktop.c"], extra_imports=[LINUX_HOST_DIR / "host_imports.list", ANDROID_DIR / "host_imports_vk.list"],
        updater_cflags=updater_defines(getattr(sln, "port_release", False)), desktop_gl=True, vulkan=True)

    # ---------- the toolchain

    n.newline()
    n.variable("ps5_sdk", str(sdk))
    n.variable("ps5_vulkan", str(vulkan))
    # Clang 22 compiles the host, as it compiled RADV's archive: with clang
    # 18 the PS5 target leaves Mesa's weak entry points at default
    # visibility (PS5_FINDINGS.md, "Building the Vulkan driver"). PS5_Vulkan's
    # compiler wrapper takes its clang from PS5_CLANG, and the SDK's own
    # wrappers from LLVM_CONFIG, so both are set.
    n.variable("ps5_clang", str(clang))
    n.variable("ps5_llvm_config", str(llvm_config))
    ps5_cc = "PS5_PAYLOAD_SDK=$ps5_sdk PS5_CLANG=$ps5_clang LLVM_CONFIG=$ps5_llvm_config sh $ps5_vulkan/tooling/prospero-clang18"

    # glslang, static, with the SDK fork's CMake toolchain, as
    # port/ps5/probe/glslang/build-extra.sh builds it: the options of
    # port/android/probe/glslang/CMakeLists.txt (no optimizer, no HLSL, no
    # binaries, no tests), and the clang the fork's bin/prospero-clang finds
    # when LLVM_CONFIG is unset (clang 18 here), which the probe ran
    glslang_build = build / "glslang"
    glslang_libraries = [glslang_build / "glslang" / "libglslang.a",
                         glslang_build / "glslang" / "libglslang-default-resource-limits.a"]
    n.rule(
        name="ps5_glslang",
        command=(f"(test -f {glslang_build}/build.ninja || env -u LLVM_CONFIG cmake -S {glslang_source} "
                 f"-B {glslang_build} -G Ninja -DCMAKE_TOOLCHAIN_FILE={sdk}/toolchain/prospero.cmake "
                 "-DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY "
                 "-DENABLE_OPT=OFF -DENABLE_HLSL=OFF -DENABLE_GLSLANG_BINARIES=OFF -DENABLE_GLSLANG_JS=OFF "
                 "-DGLSLANG_TESTS=OFF -DBUILD_EXTERNAL=OFF -DBUILD_SHARED_LIBS=OFF "
                 f"> {build}/glslang.configure.log) && "
                 f"env -u LLVM_CONFIG ninja -C {glslang_build} glslang glslang-default-resource-limits "
                 f"> {build}/glslang.build.log || "
                 f"(echo 'glslang did not build: see {build}/glslang.configure.log and {build}/glslang.build.log' >&2; "
                 "exit 1)"),
        description=f"PS5 GLSLANG {GLSLANG_TAG}",
    )
    n.build(outputs=glslang_libraries, rule="ps5_glslang",
            implicit=[glslang_source / "CMakeLists.txt", sdk / "toolchain" / "prospero.cmake"])

    # the title tool (ELF to title ELF, and the fake signature), built from
    # PS5_Vulkan's sources with the system's clang++, as its tools/build.sh does
    native = vulkan / "tooling" / "native"
    native_tool = build / "tools" / "ps5-native-tool"
    n.rule(
        name="ps5_native_tool",
        command=f"clang++ -std=c++20 -O2 -Wall -Wextra -Werror -I {zlib.parent.parent}/include $in {zlib} -o $out",
        description="PS5 TOOL $out",
    )
    n.build(outputs=native_tool, rule="ps5_native_tool", inputs=[native / name for name in NATIVE_TOOL_SOURCES],
            implicit=[*sorted(native.glob("*.hpp")), zlib])

    n.rule(name="ps5_stub_cc", command=f"{ps5_cc} -std=c11 -O2 -fPIC -c $in -o $out", description="PS5 STUB CC $out")
    n.rule(name="ps5_stub_so", command="$ps5_sdk/bin/prospero-lld --shared -soname $soname -o $out $in",
           description="PS5 STUB $out")
    stubs: List[Path] = []
    for library, source in AGC_STUBS:
        stub_obj = host_dir / "stubs" / f"{library}_stub.o"
        stub = host_dir / "stubs" / f"{library}.so"
        n.build(outputs=stub_obj, rule="ps5_stub_cc", inputs=vulkan / source)
        n.build(outputs=stub, rule="ps5_stub_so", inputs=stub_obj, variables={"soname": f"{library}.prx"})
        stubs.append(stub)

    # ---------- the host

    n.newline()
    n.rule(
        name="ps5_host_cc",
        command=f"{ps5_cc} -MMD -MF $out.d $cflags -c $in -o $out",
        description="PS5 HOST CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    # the commit the title is built from, written at every build and restat,
    # as the Android host's (tools/android_build_stamp.py: HALO_PROBE_BUILD)
    build_stamp = host_dir / "gen" / "ps5_build.h"
    n.rule(name="ps5_build_stamp", command=f"{python} tools/android_build_stamp.py $out",
           description="PS5 BUILD STAMP $out", restat=True)
    n.build(outputs="ps5_always", rule="phony")
    n.build(outputs=build_stamp, rule="ps5_build_stamp", implicit=["ps5_always"])
    # a weak function for every name of the host's import table, each ending
    # the run with its name and arguments (host_fatal): a real definition in
    # any host object overrides it, so the host links while it is
    # incomplete, and the stubs still standing are the imports nothing
    # answers (llvm-nm lists them as weak; port/ps5/PS5.md, phase 2)
    import_stubs = host_dir / "gen" / "host_import_stubs.c"
    n.rule(name="ps5_import_stubs", command=f"{python} -m tools.ps5_build stubs $in $out",
           description="PS5 IMPORT STUBS $out")
    n.build(outputs=import_stubs, rule="ps5_import_stubs", inputs=guest["host_table_c"],
            implicit=[root / "tools" / "ps5_build.py"])

    sdl2_flags = [f"-isystem {flag[2:]}" if flag.startswith("-I") else flag for flag in sdl2["cflags"]]
    host_cflags = " ".join([
        "-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-ffunction-sections", "-fdata-sections",
        "-DHALO_PS5=1",
        f"-I{root / PORT_DIR / 'host'}", f"-I{root / ANDROID_DIR / 'include'}", f"-I{root / ANDROID_DIR / 'host'}",
        f"-I{root / LINUX_DIR / 'src'}", f"-I{root / TOMLC17_DIR}", f"-I{host_dir / 'gen'}",
        # (the renderer's command stream and shader layouts, which both of its
        # halves include; and the folder of its SPIR-V cache is named for the
        # compiler, as Android's: host_vk_shaders.c)
        f"-I{root / ANDROID_DIR / 'guest'}", shlex.quote(f'-DHOST_VK_GLSLANG_TAG="{GLSLANG_TAG}"'),
        # (Vulkan's headers, SDL2's and SDL3's are not the host's to keep free
        # of warnings; SDL3's give the guest's constants and layouts, and its
        # calls in the shared files bind to SDL2's of the same names)
        f"-isystem {mesa / 'include'}", *sdl2_flags, f"-isystem {root / THIRD_PARTY / 'SDL3' / 'include'}",
        f"-I{glslang_source}",
    ])
    cxxflags = "-std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections"
    # the C runtime first, as PS5_Vulkan's titles link it
    objects = [obj_dir / "app_crt.o", obj_dir / "app_cpp_runtime.o"]
    n.build(outputs=objects[0], rule="ps5_host_cc", inputs=native / "app_crt.cpp", variables={"cflags": cxxflags})
    n.build(outputs=objects[1], rule="ps5_host_cc", inputs=native / "app_cpp_runtime.cpp",
            variables={"cflags": cxxflags})
    # the PS5's own files, the Android host library's that are used as they
    # are (its host_debug.c is the PS5's host_debug_ps5.c), the desktop's
    # file helpers (posix.h's, which the game imports), config.toml's
    # parser, the import table and the stubs
    host_sources = [*sorted((root / PORT_DIR / "host").glob("*.c")),
                    *(root / ANDROID_DIR / "host" / name for name in SHARED_HOST_FILES + SHARED_RENDERER_FILES),
                    root / LINUX_DIR / "src" / "posix_files.c", root / TOMLC17_DIR / "tomlc17.c",
                    guest["host_table_c"], import_stubs]
    for source in host_sources:
        obj = obj_dir / (source.name + ".o")
        cflags = host_cflags
        if source.name == "host_main.c":
            cflags += " " + shlex.quote(f'-DHALO_PS5_PINS="{pins_text()}"')
        if source.name == "host_vk_driver_ps5.c":
            cflags += " " + shlex.quote(f'-DHALO_PS5_RADV_PINS="{radv_pins_text()}"')
        if source.name == "tomlc17.c":
            # (the SDK's math.h: its isnan's _Generic names volatile types,
            # which an lvalue never has; the warning is the header's, in a
            # file that is not this repository's to change)
            cflags += " -Wno-unreachable-code-generic-assoc"
        if source.name in SHARED_RENDERER_FILES or source.name == "host_vk_screenshot_ps5.c":
            # (Vulkan's idiom, a structure given its sType and nothing else,
            # is what -Wextra's missing-field-initializers names: the shared
            # files use it throughout, as the screenshot does after them, and
            # Android compiles them without -Wextra; and host_vk_render.c's
            # clears' self-test puts a clear command, which ends in its
            # rectangles, before the rectangles it sends, which clang 22
            # names a GNU extension)
            cflags += " -Wno-missing-field-initializers -Wno-gnu-variable-sized-type-not-at-end"
        n.build(outputs=obj, rule="ps5_host_cc", inputs=source, variables={"cflags": cflags},
                implicit=[build_stamp])
        objects.append(obj)

    # the link (tools/ps5_title.sh link): RADV's link recipe, glslang's two
    # archives whole, as the glslang probe links them, and SDL2's archives
    # and system libraries; eboot.elf is the linker's (with the symbols, and
    # eboot.elf.map), eboot.title.elf ps5-native-tool's, which is signed
    eboot_elf = host_dir / "eboot.elf"
    title_elf = host_dir / "eboot.title.elf"
    # (and the wraps: mmap, whose anonymous MAP_FIXED mappings of the guest's
    # ranges host_memory.c answers in their direct memory, and the message
    # box host_common.c's host_fatal shows, a notification instead)
    link_inputs = ["--whole-archive", *map(str, glslang_libraries), "--no-whole-archive",
                   "-L", str(sdk / "target" / "lib"), *sdl2["libs"], *HOST_WRAPS,
                   "--version-script", str(root / PORT_DIR / "host" / "host_local.map")]
    n.rule(
        name="ps5_link",
        command=(f"PS5_CLANG=$ps5_clang bash {root / 'tools' / 'ps5_title.sh'} link --vulkan $ps5_vulkan "
                 f"--sdk $ps5_sdk --archive {archive} --tool {native_tool} --out {eboot_elf} --title-elf {title_elf} "
                 + " ".join(f"--stub {stub}" for stub in stubs) + " $in -- $link_inputs"),
        description="PS5 LINK $out",
    )
    n.build(outputs=[eboot_elf, title_elf], rule="ps5_link", inputs=objects,
            implicit=[*stubs, *glslang_libraries, native_tool, archive, sdk / "target" / "lib" / "libps5platform.a",
                      root / "tools" / "ps5_title.sh", vulkan / "tools" / "radv-link.sh",
                      root / PORT_DIR / "host" / "host_local.map"],
            implicit_outputs=[Path(f"{eboot_elf}.map")],
            variables={"link_inputs": " ".join(shlex.quote(item) for item in link_inputs)})

    # ---------- the title

    n.newline()
    eboot = title / "eboot.bin"
    n.rule(name="ps5_self",
           command=f"bash {root / 'tools' / 'ps5_title.sh'} self --tool {native_tool} --in $in --out $out",
           description="PS5 SELF $out")
    n.build(outputs=eboot, rule="ps5_self", inputs=title_elf, implicit=[native_tool, root / "tools" / "ps5_title.sh"])
    n.rule(name="ps5_copy", command="mkdir -p $$(dirname $out) && cp $in $out", description="PS5 COPY $out")
    param = title / "sce_sys" / "param.json"
    icon = title / "sce_sys" / "icon0.png"
    # The icon is made once from the Android master, and committed, as the
    # Switch's icon.jpg is:
    #   python3 -c "from PIL import Image; Image.open('port/assets/icon/opence-icon.png').convert('RGBA')
    #       .resize((512, 512), Image.LANCZOS).save('port/ps5/sce_sys/icon0.png', optimize=True)"
    n.build(outputs=param, rule="ps5_copy", inputs=root / PORT_DIR / "sce_sys" / "param.json")
    n.build(outputs=icon, rule="ps5_copy", inputs=root / PORT_DIR / "sce_sys" / "icon0.png")
    # PS5_Vulkan's libc.prx shim, after its checksum (and rebuilt by its
    # tools/rebuild-libc.sh, which reproduces the recorded file, if absent)
    libc = title / "sce_module" / "libc.prx"
    runtime = vulkan / "runtime"
    n.rule(
        name="ps5_libc",
        command=(f"(test -f {runtime}/libc.prx || (. {hostdeps} && bash {vulkan}/tools/rebuild-libc.sh)) && "
                 f"(cd {runtime} && sha256sum --check --strict --quiet libc.prx.sha256) && "
                 f"mkdir -p $$(dirname $out) && cp {runtime}/libc.prx $out"),
        description="PS5 LIBC $out",
    )
    n.build(outputs=libc, rule="ps5_libc", inputs=runtime / "libc.prx.sha256")
    guest_image = title / "halo_guest.elf"
    n.build(outputs=guest_image, rule="ps5_copy", inputs=guest["vk_image"], implicit=[guest["vk_checked"]])
    notice = title / "NOTICE.txt"
    n.rule(name="ps5_notice", command=f"{python} -m tools.ps5_build notice $out", description="PS5 NOTICE $out")
    n.build(outputs=notice, rule="ps5_notice", implicit=[root / "tools" / "ps5_build.py"])

    n.build(outputs="ps5", rule="phony", inputs=[eboot, param, icon, libc, guest_image, notice])
    n.newline()


def import_stubs_text(table: str) -> str:
    """host_import_stubs.c for a host import table (tools/android_imports.py's
    host_import_table.c): one weak function for each of its functions,
    which ends the run naming the import and its first arguments"""
    entries = re.findall(r'\{ "([A-Za-z0-9_]+)", \(void \*\)([A-Za-z0-9_]+) \}', table)
    if not entries:
        raise ValueError("no imports in the host's import table")
    lines = ["/* generated by tools/ps5_build.py from the host's import table; do not edit",
             "",
             "Every import a weak function: a real definition in any host object",
             "overrides it (port/ps5/PS5.md, phase 2). */",
             "",
             "void host_fatal(const char *format, ...) __attribute__((format(printf, 1, 2), noreturn));",
             ""]
    for name, symbol in entries:
        lines += [f"long {symbol}(long a, long b, long c, long d, long e, long f);",
                  f"__attribute__((weak)) long {symbol}(long a, long b, long c, long d, long e, long f)",
                  "{",
                  "\t(void)e;",
                  "\t(void)f;",
                  f'\thost_fatal("import %s is not answered (%lx, %lx, %lx, %lx)", "{name}", a, b, c, d);',
                  "}",
                  ""]
    return "\n".join(lines)


if __name__ == "__main__":
    # python3 -m tools.ps5_build notice <file>: NOTICE.txt, from the pins above
    if len(sys.argv) == 3 and sys.argv[1] == "notice":
        Path(sys.argv[2]).parent.mkdir(parents=True, exist_ok=True)
        Path(sys.argv[2]).write_text(notice_text(), encoding="utf-8")
    # python3 -m tools.ps5_build stubs <host_import_table.c> <file>: the weak import stubs
    elif len(sys.argv) == 4 and sys.argv[1] == "stubs":
        Path(sys.argv[3]).parent.mkdir(parents=True, exist_ok=True)
        Path(sys.argv[3]).write_text(import_stubs_text(Path(sys.argv[2]).read_text(encoding="utf-8")),
                                     encoding="utf-8")
    else:
        sys.exit("usage: python3 -m tools.ps5_build notice <file>, or stubs <host_import_table.c> <file>")
