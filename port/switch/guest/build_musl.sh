#!/bin/bash
# Builds a minimal musl 1.2.5 for the guest ABI (-mabi=ilp32, devkitA64's
# native AArch64 ILP32 - see PORTING.md). File list and exclusions are
# halo-ce-universal's curated set for their Android guest (same idea,
# same musl version), plus a handful this port excludes too: real-OS
# subsystems (dirent/poll/ioctl/termios/resource/statfs/link - the guest
# talks to the host through imports, not real POSIX syscalls for these),
# __libc_start_main.c (we have our own guest entry), and three stdio
# files (freopen/pclose/__stdio_seek) whose use of __syscall1 doesn't
# preprocess cleanly here yet - not needed for now, revisit if the game
# source actually calls them.
#
# libc/arch/aarch64_ilp32 is Android's arm64_32 arch directory, copied
# and adapted: weak_alias back to musl's stock GNU attribute (no Mach-O
# assembler), bits/errno.h and bits/float.h borrowed from musl's real
# aarch64 arch (errno numbers are universal; float.h because AArch64's
# long double is the native 128-bit IEEE quad even under -mabi=ilp32 -
# Android's copy assumed Apple's 64-bit long double, wrong for us), and
# one clang-only builtin (__builtin_arm_yield) replaced with plain asm.
set -eu
cd "$(dirname "$0")"

MUSL_VERSION=1.2.5
MUSL_URL="https://musl.libc.org/releases/musl-$MUSL_VERSION.tar.gz"
MUSL="third_party/musl-$MUSL_VERSION"
ARCH=libc/arch/aarch64_ilp32
OBJDIR=build/musl
export PATH=/opt/devkitpro/devkitA64/bin:$PATH

if [ ! -d "$MUSL" ]; then
	mkdir -p third_party
	echo "Downloading $MUSL_URL"
	curl -sSfL -o "third_party/musl-$MUSL_VERSION.tar.gz" "$MUSL_URL"
	tar xzf "third_party/musl-$MUSL_VERSION.tar.gz" -C third_party
	rm "third_party/musl-$MUSL_VERSION.tar.gz"

	# game code compiles with -D__STRICT_ANSI__ (deliberately: hides POSIX
	# names like random()/strnlen() so they can't collide with the game's
	# own - see port/linux/include/math.h's comment). musl's *internal*
	# headers (never visible to game code - every declared name starts
	# with __, or is "hidden") still need clockid_t/locale_t/etc., which
	# musl's public headers only typedef when _GNU_SOURCE or similar is
	# defined (include/time.h, include/pthread.h). Can't just define
	# _GNU_SOURCE for every game file - that unlocks the exact POSIX
	# declarations __STRICT_ANSI__ exists to hide. Scoped instead: define
	# it only around these two internal headers' own single #include
	# line, undefined again immediately after, so it never reaches any
	# later #include in the same translation unit.
	for f in src/include/time.h src/include/pthread.h; do
		python3 -c '
import re, sys
path = sys.argv[1]
text = open(path).read()
text = re.sub(
    r"^(#include \"\.\./\.\./include/.*\.h\")$",
    "#define _GNU_SOURCE 1\n\\1\n#undef _GNU_SOURCE",
    text, count=1, flags=re.MULTILINE)
open(path, "w").write(text)
' "$MUSL/$f"
	done

	# game code's own __inline -> "static __inline__" (MSVC comdat
	# emulation, halo_linux_prefix.h) collides with these musl *public*
	# headers' own "static __inline" the same way it collided with the
	# real math.h (port/linux/include/math.h already guards that one
	# with the same push_macro/pop_macro trick - these four have no
	# game-side wrapper header to do it in, so it happens here instead).
	# Whole-file scope (push right after the include guard, pop right
	# before its outermost #endif), not per-line: neutralizing __inline's
	# MSVC meaning is always safe for musl's own content, never needed
	# there, regardless of how many "static __inline"s a given file has.
	for f in include/ctype.h include/sched.h include/byteswap.h include/endian.h; do
		python3 -c '
import sys
path = sys.argv[1]
lines = open(path).read().split("\n")
for i, l in enumerate(lines):
    if l.startswith("#define") and l.rstrip().endswith("_H"):
        lines[i:i+1] = [l, "#pragma push_macro(\"__inline\")", "#undef __inline"]
        break
for i in range(len(lines) - 1, -1, -1):
    if lines[i].strip():
        lines[i:i] = ["#pragma pop_macro(\"__inline\")"]
        break
open(path, "w").write("\n".join(lines))
' "$MUSL/$f"
	done

	# include/unistd.h's "long syscall(long, ...);" is a real declaration,
	# but src/internal/syscall.h's own "#define syscall(...) ..." macro
	# (active in any file that includes both, stdio_impl.h's chain first)
	# makes the preprocessor read it as a call instead: "long" and "..."
	# become the macro's two arguments, mangling the whole line into
	# garbage (confirmed via -E: __fdopen.c and freopen.c both hit this -
	# the actual reason build_musl.sh used to exclude them, not a deeper
	# missing-feature gap). Same whole-file push_macro/pop_macro fix,
	# scoped to "syscall" instead of "__inline" - safe here too: the
	# declaration is unistd.h's only use of the name.
	python3 -c '
import sys
path = sys.argv[1]
lines = open(path).read().split("\n")
for i, l in enumerate(lines):
    if l.startswith("#define") and l.rstrip().endswith("_H"):
        lines[i:i+1] = [l, "#pragma push_macro(\"syscall\")", "#undef syscall"]
        break
for i in range(len(lines) - 1, -1, -1):
    if lines[i].strip():
        lines[i:i] = ["#pragma pop_macro(\"syscall\")"]
        break
open(path, "w").write("\n".join(lines))
' "$MUSL/include/unistd.h"

	# __fdopen.c's TIOCGWINSZ check (line-buffer a terminal automatically)
	# needs bits/ioctl.h, which this port doesn't have (same real-OS-
	# subsystem gap as the DIRS exclusions above) - but it's an
	# optional heuristic, not a correctness requirement: skipping it
	# just means every stream defaults to fully buffered instead of line
	# buffered on a TTY. Delete the #include and the "if" that uses it,
	# leaving "f->lbf = EOF;" (already fully buffered) as the only effect.
	python3 -c '
import re, sys
path = sys.argv[1]
text = open(path).read()
text = text.replace("#include <sys/ioctl.h>\n", "")
text = text.replace("\tstruct winsize wsz;\n", "")
text = re.sub(
    r"\tif \(!\(f->flags & F_NOWR\) && !__syscall\(SYS_ioctl, fd, TIOCGWINSZ, &wsz\)\)\n\t\tf->lbf = .\\n.;\n",
    "", text)
open(path, "w").write(text)
' "$MUSL/src/stdio/__fdopen.c"
fi

mkdir -p obj/include/bits "$OBJDIR"
sed -f "$MUSL/tools/mkalltypes.sed" "$ARCH/bits/alltypes.h.in" "$MUSL/include/alltypes.h.in" > obj/include/bits/alltypes.h
cp "$ARCH/bits/syscall.h.in" obj/include/bits/syscall.h
sed -n -e 's/__NR_/SYS_/p' < "$ARCH/bits/syscall.h.in" >> obj/include/bits/syscall.h

CFLAGS="-mabi=ilp32 -O2 -nostdinc -fno-builtin -ffreestanding -fno-stack-protector \
	-Iobj/include -I$ARCH -Ilibc/src_include -I$MUSL/src/internal -I$MUSL/src/include -I$MUSL/include \
	-D__linux__=1 -D__unix__=1"

# oldmalloc, not mallocng: mallocng's get_meta() asserts a "secret"
# value stored at allocation time still matches at free() time (a
# hardening check), which crashed on real hardware (Undefined
# Instruction / BRK) on the very first free() - not yet root-caused,
# and a simpler allocator better suited to this minimal a runtime
# anyway. Both fall back to mmap() (guest_syscall.c's bump allocator)
# the same way; __expand_heap tries SYS_brk first but our stub always
# fails that check, so it falls through to mmap() cleanly either way.
DIRS="conf ctype dirent env errno exit fcntl internal locale malloc malloc/oldmalloc math mman multibyte prng sched select signal stat stdio stdlib string time unistd"

EXCLUDE="dirent/alphasort.c dirent/closedir.c dirent/dirfd.c dirent/fdopendir.c dirent/opendir.c
dirent/readdir.c dirent/readdir_r.c dirent/rewinddir.c dirent/scandir.c dirent/seekdir.c
dirent/telldir.c dirent/versionsort.c env/__libc_start_main.c internal/emulate_wait4.c
internal/vdso.c internal/version.c select/poll.c select/ppoll.c stat/statvfs.c
stdio/fopencookie.c stdio/pclose.c stdio/__stdio_seek.c
stdio/__stdout_write.c time/__tz.c unistd/faccessat.c unistd/isatty.c unistd/nice.c
unistd/tcgetpgrp.c unistd/tcsetpgrp.c conf/sysconf.c
env/__init_tls.c env/__stack_chk.c env/__reset_tls.c malloc/mallocng thread/pthread_create.c
string/explicit_bzero.c"

objects=""
for d in $DIRS; do
	for f in "$MUSL/src/$d"/*.c; do
		[ -f "$f" ] || continue
		rel="${f#$MUSL/src/}"
		skip=0
		for e in $EXCLUDE; do [ "$rel" = "$e" ] && skip=1 && break; done
		[ "$skip" = 1 ] && continue
		obj="$OBJDIR/$(echo "$rel" | tr '/' '_' | sed 's/\.c$/.o/')"
		objects="$objects $obj"
		[ "$obj" -nt "$f" ] && continue
		echo "CC $rel"
		aarch64-none-elf-gcc $CFLAGS -c "$f" -o "$obj"
	done
done

# network/*, misc/ioctl.c and misc/getrlimit.c need bits/socket.h,
# bits/ioctl.h, bits/resource.h - real-OS headers we don't have (same
# category as the DIRS exclusions above). Real networking (actual
# socket syscalls through the host) is a separate, later task, not a
# header-porting one - deferred along with these.
for f in thread/__lock.c thread/__wait.c thread/__timedwait.c thread/vmlock.c \
	thread/pthread_self.c thread/pthread_equal.c thread/pthread_once.c \
	thread/pthread_setcancelstate.c thread/pthread_testcancel.c thread/default_attr.c \
	thread/lock_ptc.c misc/getauxval.c misc/basename.c misc/dirname.c misc/realpath.c \
	misc/uname.c misc/syscall.c; do
	obj="$OBJDIR/$(echo "$f" | tr '/' '_' | sed 's/\.c$/.o/')"
	objects="$objects $obj"
	[ -f "$MUSL/src/$f" ] || continue
	[ "$obj" -nt "$MUSL/src/$f" ] 2>/dev/null && continue
	echo "CC $f"
	aarch64-none-elf-gcc $CFLAGS -c "$MUSL/src/$f" -o "$obj"
done

aarch64-none-elf-ar rcs "$OBJDIR/libc.a" $objects
echo "built $OBJDIR/libc.a"
