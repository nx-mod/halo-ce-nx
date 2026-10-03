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
fi

mkdir -p obj/include/bits "$OBJDIR"
sed -f "$MUSL/tools/mkalltypes.sed" "$ARCH/bits/alltypes.h.in" "$MUSL/include/alltypes.h.in" > obj/include/bits/alltypes.h
cp "$ARCH/bits/syscall.h.in" obj/include/bits/syscall.h
sed -n -e 's/__NR_/SYS_/p' < "$ARCH/bits/syscall.h.in" >> obj/include/bits/syscall.h

CFLAGS="-mabi=ilp32 -O2 -nostdinc -fno-builtin -ffreestanding -fno-stack-protector \
	-Iobj/include -I$ARCH -Ilibc/src_include -I$MUSL/src/internal -I$MUSL/src/include -I$MUSL/include \
	-D__linux__=1 -D__unix__=1"

DIRS="conf ctype dirent env errno exit fcntl internal locale malloc malloc/mallocng math mman multibyte prng sched select signal stat stdio stdlib string time unistd"

EXCLUDE="dirent/alphasort.c dirent/closedir.c dirent/dirfd.c dirent/fdopendir.c dirent/opendir.c
dirent/readdir.c dirent/readdir_r.c dirent/rewinddir.c dirent/scandir.c dirent/seekdir.c
dirent/telldir.c dirent/versionsort.c env/__libc_start_main.c internal/emulate_wait4.c
internal/vdso.c internal/version.c select/poll.c select/ppoll.c stat/statvfs.c
stdio/__fdopen.c stdio/fopencookie.c stdio/freopen.c stdio/pclose.c stdio/__stdio_seek.c
stdio/__stdout_write.c time/__tz.c unistd/faccessat.c unistd/isatty.c unistd/nice.c
unistd/tcgetpgrp.c unistd/tcsetpgrp.c conf/sysconf.c
env/__init_tls.c env/__stack_chk.c env/__reset_tls.c malloc/oldmalloc thread/pthread_create.c
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
