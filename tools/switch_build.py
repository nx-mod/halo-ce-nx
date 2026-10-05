"""Ninja rules for the Switch guest build (``ninja switch_guest``).

Compiles the game against the ILP32 AArch64 toolchain (devkitA64's
`-mabi=ilp32`, a real GCC ELF target - no Mach-O conversion needed,
unlike Android's guest) for the first time across the whole source
tree, not just a sample. See port/switch/PORTING.md for the story and
every fix below's reasoning.

Also compiles the slice of port/linux/src (SWITCH_PLATFORM_FILES) and
port/third_party/musl-math this guest can reuse as pure, portable
compute - no host import needed; port/switch/platform's null backend
for what's left (D3D8 is now real, see below - DirectSound/XInput/XNet/
Win32 file API/save-system are still null, PORTING.md's milestone 7);
and now the real D3D8->GLES3 renderer (SWITCH_D3D8_FILES: d3d8_gl.c,
d3d8_resources.c, gl_functions.c, nv2a_vsh.c, nv2a_psh.c - the same
files Linux uses, Android's existing HALO_ANDROID GLES3 branches
extended to HALO_SWITCH too, not a separate implementation) plus a
generated guest/host import bridge, since the ILP32 guest cannot link
against devkitPro's real (64-bit) GLES3 libraries directly:
tools/android_gl_stubs.py (unmodified - reads gl.h's existing Android
function list, writes guest-side wrappers around hostgl_<name> imports)
and tools/android_imports.py (unmodified - turns host_imports.list +
the generated GL import list into assembly trampolines through a
host-filled table, mirrors guest_syscall.c-style host_log/host_write
but generated now that there are >100 instead of 2). Still no ninja
link rule: that step is done by hand against a manually-built guest_*.o
runtime set for now (see PORTING.md's milestone 5/6/9).
"""

import json
import os
import shutil
from pathlib import Path
from typing import Any, Dict, List

from .ninja_syntax import Writer
from .linux_build import (
    GAME_FLAGS, PLATFORM_FLAGS, XDK_INCLUDE, MUSL_MATH_DIR, xdk_headers, musl_math_sources, _quote,
)

LINUX_DIR = Path("port/linux")
SWITCH_DIR = Path("port/switch")
GUEST_DIR = SWITCH_DIR / "guest"
BUILD = Path("build/switch")

# the MSVC-ABI flags, on AArch64 ILP32. Modeled on VITA_ABI_FLAGS
# (tools/vita_build.py) - the closest precedent (32-bit ARM, same MSVC-ABI
# concerns), not LINUX_ABI_FLAGS (x86-specific: -malign-double and
# -freg-struct-return don't exist on ARM and aren't needed there - AArch64's
# natural alignment already matches what those flags force on x86).
SWITCH_ABI_FLAGS = [
    "-mabi=ilp32",
    "-fno-math-errno",
    "-DHALO_SWITCH=1",
    # tags only need to be real pointers within this process, which they
    # are (see PORTING.md's "resolved" section) - same reasoning as Vita.
    "-DHALO_RELOCATABLE_TAG_CACHE=1",
    "-fms-extensions",
    "-fshort-wchar",
    "-fno-short-enums",
    "-fsigned-char",
    "-fcommon",
    "-fno-strict-aliasing",
    "-fwrapv",
    "-fno-delete-null-pointer-checks",
    "-fno-omit-frame-pointer",
    "-ffp-contract=off",
    "-ffunction-sections",
    "-fdata-sections",
    "-O2",
    "-g",
    *(f"-fno-builtin-{name}" for name in (
        "wcslen", "wcsnlen", "wcschr", "wcsrchr", "wcscmp", "wcsncmp", "wcscpy",
        "wcsncpy", "wcscat", "wcsncat", "wmemchr", "wmemcmp", "wmemcpy",
        "wmemmove", "wmemset",
    )),
]

# GCC (devkitA64) has no -Wincompatible-function-pointer-types - that's a
# clang-only warning name, folded into -Wincompatible-pointer-types there.
# -Drestrict=__restrict__: restrict isn't a keyword under this project's
# -std=gnu89 (deliberate, every platform, unrelated to Switch), but musl's
# own headers (written assuming C99+) use it as one - GCC's __restrict__
# is a keyword unconditionally, regardless of -std (PORTING.md).
SWITCH_GAME_FLAGS = [f for f in GAME_FLAGS if f != "-Wno-error=incompatible-function-pointer-types"] + [
    "-Drestrict=__restrict__",
]

# The subset of port/linux/src this port's guest can reuse as-is: pure
# compute/data, no real file/thread/GPU/audio access, so no host import
# needed - just the same ILP32 ABI as game code. Everything else there
# (xbox_kernel.c, xnet.c, dsound_sdl.c, xinput_sdl.c, d3d8_gl.c, ...) is
# either SDL-tied (wrong for Switch - see PORTING.md's platform-layer
# plan) or needs a real host_* import that doesn't exist yet.
SWITCH_PLATFORM_FILES = [
    "halo_linker_common.c", "msvc_crt.c", "msvc_wide.c", "bink_null.c",
    "tag_relocate.c", "frame_timing.c",
    # the Win32-file-API-over-POSIX layer (CreateFileA/ReadFile/...,
    # platform_translate_path, platform_data_root/save_root) - no SDL,
    # no Android/Linux-specific code at all, straight POSIX open/read/
    # write/close/stat/opendir calls only. Needs real posix_* host
    # imports (host_posix_files.c) and real open/read/write/close/lseek
    # guest syscalls (guest_syscall.c) - PORTING.md's "real file I/O"
    # milestone.
    "xbox_files.c",
    # the real DirectSound mixer (ADPCM decode, resampling, 3D rolloff/
    # panning) - reused exactly as proven on Linux/Vita; only its own
    # HALO_SWITCH branches replace the SDL-based "feed PCM to the
    # speaker" bottom layer with libnx audout (host_audio.c) fed from a
    # dedicated real guest thread (switch_xbox_threads.c's CreateThread).
    # Needs real pthread_mutex_t (guest_pthread_stubs.c) now too, for
    # its own mixer_lock/parameter_lock between the tick and audio
    # threads - PORTING.md's "wire in audio/controls" milestone.
    "dsound_sdl.c",
]

# The real D3D8 device: Linux's own GLES3-over-OpenGL-4.5 renderer, whose
# existing HALO_ANDROID branches (same GLES3.2 API, same guest/host split
# shape) now also cover HALO_SWITCH - not a new implementation.
SWITCH_D3D8_FILES = [
    "d3d8_gl.c", "d3d8_resources.c", "gl_functions.c", "nv2a_vsh.c", "nv2a_psh.c", "xbox_textures.c",
    "xgpu_text.c",
]

DEVKITPRO = Path(os.environ.get("DEVKITPRO", "/opt/devkitpro"))
SWITCH_PORTLIBS_INCLUDE = DEVKITPRO / "portlibs" / "switch" / "include"

MUSL_VERSION = "1.2.5"
MUSL = GUEST_DIR / "third_party" / f"musl-{MUSL_VERSION}"
MUSL_ARCH = GUEST_DIR / "libc" / "arch" / "aarch64_ilp32"
MUSL_SRC_INCLUDE = GUEST_DIR / "libc" / "src_include"
MUSL_OBJ_INCLUDE = GUEST_DIR / "obj" / "include"
MUSL_LIB = GUEST_DIR / "build" / "musl" / "libc.a"


def generate_switch_build(n: Writer, sln: Any) -> None:
    config_path = LINUX_DIR / "port.json"
    if not config_path.is_file():
        return
    if not shutil.which("aarch64-none-elf-gcc"):
        n.comment("Switch build: no devkitA64 (aarch64-none-elf-gcc) on PATH")
        return
    config: Dict[str, Any] = json.loads(config_path.read_text(encoding="utf-8"))
    linux_semantics_header = Path("build/linux/halo_msvc_semantics.h")
    switch_semantics_header = BUILD / "halo_msvc_semantics_switch.h"
    linux_platform_semantics_header = Path("build/linux/platform_msvc_semantics.h")
    switch_platform_semantics_header = BUILD / "platform_msvc_semantics_switch.h"
    prefix_header = LINUX_DIR / "include" / "halo_linux_prefix.h"
    obj_dir = BUILD / "obj"

    n.comment("Switch guest build (ninja switch_guest): compiles the game against the ILP32 "
              "toolchain. No link step yet - see port/switch/PORTING.md.")

    n.rule(
        name="switch_cc",
        command="aarch64-none-elf-gcc -MMD -MF $out.d $cflags -c $in -o $out",
        description="SWITCH CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    n.rule(
        name="switch_as",
        command="aarch64-none-elf-gcc -mabi=ilp32 -c $in -o $out",
        description="SWITCH AS $out",
    )
    # unmodified Android generators (tools/android_gl_stubs.py,
    # tools/android_imports.py) - see this file's own header comment.
    # gl_stubs reads gl.h's existing Android function list against
    # devkitPro's real GLES3 headers (same Khronos-registry format
    # Android's sysroot headers use) and writes the guest-side wrappers
    # plus the hostgl_* import names those wrappers call through.
    gen_dir = BUILD / "gen"
    guest_gl_c = gen_dir / "guest_gl.c"
    gl_imports_list = gen_dir / "gl_imports.list"
    n.rule(
        name="switch_gl_stubs",
        command=f"$python tools/android_gl_stubs.py $in {SWITCH_PORTLIBS_INCLUDE}/GLES3/gl32.h "
                f"{SWITCH_PORTLIBS_INCLUDE}/GLES2/gl2ext.h {guest_gl_c} {gl_imports_list}",
        description="SWITCH GL STUBS $out",
    )
    n.build(outputs=[guest_gl_c, gl_imports_list], rule="switch_gl_stubs", inputs=LINUX_DIR / "src" / "gl.h")
    # imports merges host_imports.list (host_log/host_write/the six
    # host_gl_* bridge functions) with the generated hostgl_* list into
    # one assembly file of trampolines through a host-filled table -
    # replaces the old hand-written guest_imports.S now that there are
    # >100 imports instead of 2.
    host_imports_list = SWITCH_DIR / "host_imports.list"
    guest_imports_s = gen_dir / "guest_imports.s"
    n.rule(
        name="switch_imports",
        command=f"$python tools/android_imports.py $out $in",
        description="SWITCH IMPORTS $out",
    )
    n.build(outputs=guest_imports_s, rule="switch_imports", inputs=[host_imports_list, gl_imports_list])
    # the generated halo_msvc_semantics.h's "#pragma weak NAME" lines
    # hard-error under GCC for any NAME that also ends up static - see
    # PORTING.md. Every ordinary game TU gives these names static linkage
    # (halo_linux_prefix.h's plain `__inline` -> `static __inline__`), so
    # the pragma is illegal there and this strips it. msvc_comdat.c is the
    # one exception - it needs the *unstripped* header instead, since its
    # own copies are never static (see generate_switch_build's comdat_* vars).
    n.rule(
        name="switch_strip_weak_pragmas",
        command="grep -v '^#pragma weak' $in > $out",
        description="SWITCH MSVC SEMANTICS (no pragma weak) $out",
    )
    n.build(outputs=switch_semantics_header, rule="switch_strip_weak_pragmas", inputs=linux_semantics_header)
    n.build(outputs=switch_platform_semantics_header, rule="switch_strip_weak_pragmas",
            inputs=linux_platform_semantics_header)

    # musl: not yet real ninja rules (build_musl.sh does its own up-to-date
    # checking in bash) - good enough to unblock game compiles; formalize
    # once this needs to be fast rather than just correct.
    n.rule(
        name="switch_musl",
        command=f"cd {GUEST_DIR} && ./build_musl.sh",
        description="SWITCH MUSL (devkitA64 -mabi=ilp32) $out",
    )
    n.build(outputs=MUSL_LIB, rule="switch_musl", implicit=[GUEST_DIR / "build_musl.sh"])

    abi = " ".join(SWITCH_ABI_FLAGS)
    port_include = LINUX_DIR / "include"
    sdk_flags = f"-idirafter {XDK_INCLUDE}"
    # game code sees only the PUBLIC musl headers, same as any client of an
    # installed libc would (musl's own `make install-headers` never exposes
    # src/include or src/internal). -I src/include once leaked in here too
    # (copied from build_musl.sh's own, legitimately different, needs) and
    # caused a real, nasty class of bug: src/include/features.h defines
    # `weak`/`hidden` as bare-word macros meant only for musl's own
    # implementation files, which then silently mis-expand any ordinary
    # use of those two common English words in GAME code too, wherever a
    # musl public header's chain happened to reach that internal one.
    musl_includes = " ".join([
        "-nostdinc",
        f"-I{MUSL_OBJ_INCLUDE}", f"-I{MUSL_ARCH}", f"-I{MUSL}/include",
    ])
    excluded = set(config.get("exclude_sources", []))
    objects: List[Path] = []
    implicit = [*xdk_headers(), prefix_header, switch_semantics_header, MUSL_LIB]
    # msvc_comdat.c's own copies of header inline functions must stay
    # `#pragma weak` (the unstripped header), not stripped like every other
    # TU: in every *other* TU, halo_linux_prefix.h's __inline macro makes
    # these names static, so GCC hard-errors on "#pragma weak" for them
    # (hence switch_strip_weak_pragmas). But msvc_comdat.c itself redefines
    # __inline to plain, external linkage before including the same headers
    # (see its own comment) - so the pragma is always legal there, and it's
    # the thing that makes this unit's copy pick-any instead of a hard
    # conflict with the handful of names (dot_product4d, limit2d, ...) that
    # a specific game file also defines for real, under the same name, via
    # its own "#define NAME NAME_inline ... #undef" rename trick. Stripping
    # the pragma here too made both copies strong -> "multiple definition".
    comdat_implicit = [*xdk_headers(), prefix_header, linux_semantics_header, MUSL_LIB]

    def add(source: Path, cflags: str, extra_implicit: List[Path] = None) -> None:
        obj = obj_dir / source.with_suffix(".o")
        objects.append(obj)
        n.build(outputs=obj, rule="switch_cc", inputs=source, implicit=extra_implicit or implicit,
                variables={"cflags": cflags})

    for proj in sln.projects:
        if proj.name not in config["projects"]:
            continue
        options = proj.options
        defines = " ".join(f"-D{d}" for d in options.get("defines") or [])
        includes = " ".join(
            f"-I{_quote(d)}" for d in options.get("include_dirs") or [] if Path(d) != Path("xbox/include")
        )
        game_cflags = " ".join([
            abi, " ".join(SWITCH_GAME_FLAGS), f"-include {prefix_header}", f"-include {switch_semantics_header}",
            defines, f"-I{port_include}", includes, sdk_flags, musl_includes,
        ])
        comdat_cflags = " ".join([
            abi, " ".join(SWITCH_GAME_FLAGS), f"-include {prefix_header}", f"-include {linux_semantics_header}",
            defines, f"-I{port_include}", includes, sdk_flags, musl_includes,
        ])
        for obj in proj.objects:
            name = str(obj.file_path).replace(os.sep, "/")
            if obj.status.name == "Missing" or name in excluded or obj.file_path.suffix.lower() != ".c":
                continue
            add(obj.file_path, game_cflags)
        for source in sorted(Path(config["game_sources"]).glob("*.c")):
            if source.name == "msvc_comdat.c":
                add(source, comdat_cflags, extra_implicit=comdat_implicit)
            else:
                add(source, game_cflags)

    # the reusable slice of port/linux/src (SWITCH_PLATFORM_FILES, see its
    # comment) - not game code, so no gnu89/MSVC-inline concern: compiled
    # gnu11 like Linux/Vita's own platform layer, and sees only the XDK's
    # inline functions (platform files don't include game headers at all).
    platform_implicit = [*xdk_headers(), prefix_header, switch_platform_semantics_header, MUSL_LIB]
    platform_cflags = " ".join([
        abi, " ".join(PLATFORM_FLAGS), f"-include {prefix_header}", f"-include {switch_platform_semantics_header}",
        "-DDEBUG", "-Dxbox", f"-I{port_include}", f"-I{LINUX_DIR}/src", sdk_flags, musl_includes,
    ])
    for name in SWITCH_PLATFORM_FILES:
        source = LINUX_DIR / "src" / name
        obj = obj_dir / source.with_suffix(".o")
        objects.append(obj)
        n.build(outputs=obj, rule="switch_cc", inputs=source, implicit=platform_implicit,
                variables={"cflags": platform_cflags})

    # the Switch-only "headless boot" null backend (PORTING.md's milestone
    # 7): D3D8/DirectSound/XInput/XNet/Win32/posix, as safe no-ops/failures
    # rather than real rendering/audio/input/networking/file-I/O. Same
    # platform-layer treatment as SWITCH_PLATFORM_FILES above.
    for source in sorted((SWITCH_DIR / "platform").glob("*.c")):
        obj = obj_dir / source.with_suffix(".o")
        objects.append(obj)
        n.build(outputs=obj, rule="switch_cc", inputs=source, implicit=platform_implicit,
                variables={"cflags": platform_cflags})

    # the real D3D8 device (SWITCH_D3D8_FILES, see its comment) - same
    # platform-layer treatment as SWITCH_PLATFORM_FILES, plus devkitPro's
    # real GLES3/EGL headers (gl.h's HALO_SWITCH branch reads them).
    d3d8_cflags = platform_cflags + f" -I{SWITCH_PORTLIBS_INCLUDE}"
    d3d8_implicit = platform_implicit + [guest_gl_c]
    for name in SWITCH_D3D8_FILES:
        source = LINUX_DIR / "src" / name
        obj = obj_dir / source.with_suffix(".o")
        objects.append(obj)
        n.build(outputs=obj, rule="switch_cc", inputs=source, implicit=d3d8_implicit,
                variables={"cflags": d3d8_cflags})

    # the generated guest-side GL wrappers (guest_gl_c) and import
    # trampolines (guest_imports_s) - plain ILP32 C/asm, no game ABI and
    # no platform-layer winsock dance, just devkitPro's real GLES3/GLES2
    # headers for the former's own prototypes.
    guest_gl_cflags = " ".join([abi, "-std=gnu11", "-w", f"-I{SWITCH_PORTLIBS_INCLUDE}", musl_includes])
    guest_gl_obj = obj_dir / "guest_gl.o"
    objects.append(guest_gl_obj)
    n.build(outputs=guest_gl_obj, rule="switch_cc", inputs=guest_gl_c, implicit=[MUSL_LIB],
            variables={"cflags": guest_gl_cflags})
    guest_imports_obj = obj_dir / "guest_imports.o"
    objects.append(guest_imports_obj)
    n.build(outputs=guest_imports_obj, rule="switch_as", inputs=guest_imports_s)

    # halo_math.h's deterministic, lockstep-safe halo_sin/halo_cos/... -
    # musl's own math source, built the same way on every native port
    # (see tools/linux_build.py's musl_math_sources). Pure computation,
    # same gnu11/no-game-ABI treatment as the platform files above.
    musl_math_cflags = " ".join([
        abi, "-std=gnu11", "-w", f"-I{MUSL_MATH_DIR}/include", f"-include {MUSL_MATH_DIR}/include/libm.h",
    ])
    for source in musl_math_sources():
        obj = obj_dir / "musl_math" / source.name.replace(".c", ".o")
        objects.append(obj)
        n.build(outputs=obj, rule="switch_cc", inputs=source, implicit=[MUSL_LIB],
                variables={"cflags": musl_math_cflags})

    n.build(outputs="switch_guest", rule="phony", inputs=objects)
    n.newline()
