/*
HALO_LINUX_PREFIX.H

Force-included ahead of every translation unit in the native Linux build
(clang -include). It reproduces the handful of MSVC/XDK environment
assumptions that the game source relies on, so the source itself can stay
byte-for-byte identical to what the matching MSVC build compiles.
*/

#ifndef __HALO_LINUX_PREFIX_H
#define __HALO_LINUX_PREFIX_H

#if !defined(__i386__) && !defined(HALO_ANDROID) && !defined(HALO_SWITCH) && \
	!(defined(__arm__) && __SIZEOF_POINTER__ == 4)
#error the Linux port targets 32-bit x86 or 32-bit ARM: game data structures assume 32-bit pointers
#endif
#if defined(HALO_SWITCH) && __SIZEOF_POINTER__ != 4
#error HALO_SWITCH needs -mabi=ilp32 (see port/switch/PORTING.md) - this game's data structures assume 32-bit pointers
#endif

#define HALO_LINUX 1
/* the handheld ports (Android, Vita, Switch): no desktop updater, disc
image import, invite hand-off or environment; settings live with the
game data */
#if defined(HALO_ANDROID) || defined(HALO_VITA) || defined(HALO_SWITCH)
#define HALO_NOT_DESKTOP 1
#endif

/* ---------- XDK architecture selection (MSVC predefines these) */

#define _X86_ 1
#define _M_IX86 600
#define _STDCALL_SUPPORTED 1
#define _INTEGRAL_MAX_BITS 64
#define _WCHAR_T_DEFINED

#if defined(HALO_SWITCH)
/* clang recognizes __stdcall/__cdecl/__fastcall as real (no-op outside
x86) MSVC-compat keywords on every target, which is why Vita (also
ARM, also clang) never needed this - GCC only understands them
natively on x86, so AArch64 sees "__stdcall" as a bare, meaningless
identifier and chokes on the resulting "HRESULT __stdcall name(...)"
as two consecutive identifiers. There is only one calling convention
on AArch64 anyway. */
#define __stdcall
#define __cdecl
#define __fastcall

/* same story as __stdcall above: clang recognizes MSVC's __intN
type keywords on every target, GCC doesn't recognize them at all. */
#define __int64 long long
#define __int32 int
#define __int16 short
#define __int8 char

/* GCC's -fms-extensions understands __declspec(...) exists but not
any particular argument - "unknown type name 'align'" - clang
apparently has built-in knowledge of the handful of declspec
attributes this source actually uses. The usual portable trick:
## only pastes "__declspec_" onto align/naked/selectany's own first
token, leaving a following "(4)" alone to reach __declspec_align. */
#define __declspec(x) __declspec_##x
#define __declspec_align(n) __attribute__((aligned(n)))
#define __declspec_selectany __attribute__((__weak__))
#define __declspec_noreturn __attribute__((__noreturn__))
/* DLL import/export linkage - meaningless on ELF, there's only one
kind of symbol visibility that matters here */
#define __declspec_dllexport
#define __declspec_dllimport
/* no AArch64 equivalent (x86 prologue/epilogue control) - every use
is inside an __asm block, which doesn't exist on this target either */
#define __declspec_naked
#endif
#define _USE_MATH_DEFINES
/* the XDK's COM headers decorate methods with __export when _WIN32 is unset */
#define __export

/* ---------- MSVC inline semantics

MSVC gives C `__inline` functions COMDAT (pick-any) linkage. ELF C has no
equivalent, so every translation unit gets its own private copy instead.
Clang only warns about the resulting `static static`. */

/* Same macro as every platform - see the long history in PORTING.md
for the two dead ends tried first and why they were wrong:

1. `#pragma weak NAME` for a NAME that already has internal (static)
   linkage is a GCC hard error ("weak declaration of NAME must be
   public"), unconditionally, regardless of pragma placement - not
   just a warning like it is under clang. The generated
   halo_msvc_semantics.h's whole "COMDAT inline functions" section is
   exactly this situation, for every name in it - skipped for Switch
   (`switch_strip_weak_pragmas` in tools/switch_build.py).
2. Tried putting `__attribute__((weak))` directly on the declaration
   instead (GCC only warns about that one, -Wattributes, silenced by
   -w) and dropping the forced `static` to avoid "static static"
   (plenty of the source already writes `static __inline` itself).
   Wrong: under -std=gnu89 specifically (required project-wide, not
   a Switch-only thing), GCC's old GNU89 inline semantics give a
   plain (non-static) `inline` function strong external linkage
   REGARDLESS of an attached weak attribute - confirmed directly
   (`nm`: `T`/`g`, not `W`/`w`). Silent at compile time, only visible
   at link time as "multiple definition" once two translation units
   both include a header with such a function's body - which is
   exactly why milestone 3's compile-only testing never caught it.

The actual fix doesn't touch this macro at all: `static` is what
every platform already correctly relies on to avoid the multiple-
definition case (each TU's copy becomes link-invisible, no clash -
weak-via-attribute was never needed for that, and doesn't work under
gnu89 anyway). The narrow opposite case - a name declared `__inline`
with no body in most units, needing to resolve against the one real
body elsewhere - is already handled, on every platform including this
one, by `port/linux/game/msvc_comdat.c` (`#undef`s `__inline`/
`__forceinline` itself, first, so its own inclusion of the relevant
headers gets real external definitions) and `tools/linux_link_check.py`.
Nothing Switch-specific needed there either.

The only genuine Switch-only wrinkle is the literal duplicate keyword:
`static` (from this macro) + `static` (already in the source, for
functions that generally are both in MSVC terms) is a hard GCC error
("duplicate 'static'"), not just a clang warning. Can't fix from
inside a macro (no way to detect what already precedes the expansion
point) - fixed instead by removing the now-redundant explicit `static`
from the ~20 affected lines directly (`static __inline` -> `__inline`;
the macro already supplies it) - zero behavior change on any platform,
since the expansion is identical either way once there's only one. */
#define __inline static __inline__
#define _inline static __inline__
#define __forceinline static __inline__ __attribute__((always_inline))

/* An inline function that also has an ordinary prototype keeps external
linkage; the generated halo_msvc_semantics.h marks every inline function
name `#pragma weak` (HALO_SWITCH: the attribute above instead), making
those definitions pick-any like a COMDAT. */

/* glibc spells its own extern-inline helpers with __inline; keep it from
emitting them so the redefinition above cannot reach them. */
#define __NO_INLINE__ 1

/* ---------- __declspec(selectany) data (XDK D3DCONST tables) */

/* __weak__, not weak: musl's own #define weak __attribute__((__weak__))
(src/include/features.h, only relevant for HALO_SWITCH's musl libc)
would otherwise expand the bare word again inside this attribute
list - same issue as __inline's below, fixed the same way. */
#define DECLSPEC_SELECTANY __attribute__((__weak__))

/* ---------- MSVC intrinsics

Clang predeclares the MSVC _Interlocked* builtins with prototypes that
conflict with the XDK's WINAPI declarations; route the XDK names to the
platform layer instead. */

#define _InterlockedCompareExchange halo_linux_InterlockedCompareExchange
#define _InterlockedDecrement halo_linux_InterlockedDecrement
#define _InterlockedExchange halo_linux_InterlockedExchange
#define _InterlockedExchangeAdd halo_linux_InterlockedExchangeAdd
#define _InterlockedIncrement halo_linux_InterlockedIncrement

/* ---------- structured exception handling

Only the top-level crash handler in main() uses SEH. POSIX has no equivalent
(the platform layer installs signal handlers instead), so the guarded block
always runs and the handler is compiled out. */

#define __try if (1)
#define __except(filter) else if (0)
#define __finally
#define __leave

/* ---------- multiplayer session limits of the native builds */

#include "halo_port_limits.h"

/* the Xbox Winsock headers' fd_set in game units (platform units see glibc's,
which is larger) */
#ifndef HALO_LINUX_PLATFORM_LAYER
#define FD_SETSIZE HALO_PORT_FD_SETSIZE
#endif

/* ---------- Winsock

Game code sees the XDK's Winsock under private names (see the header). The
platform layer includes the XDK headers itself, via platform.h. */

#ifndef HALO_LINUX_PLATFORM_LAYER
#ifdef HALO_VITA
/* newlib's stdio reaches its timeval and select declarations, which glibc's
does not: they are declared first, under their own names */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#endif
#include "halo_linux_winsock_names.h"
#include "halo_linux_source_fixups.h"
#endif

/* ---------- MSVC built-in types */

#include <stddef.h>

#endif /* __HALO_LINUX_PREFIX_H */
