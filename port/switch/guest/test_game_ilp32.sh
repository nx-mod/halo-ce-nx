#!/bin/bash
# Batch-compiles a representative sample of the real decompiled game
# source for the Switch ILP32 target, to find the real friction rate
# before attempting the whole 462K-line tree.
set -u
cd ~/switch/halo-ce-nx
export PATH=/opt/devkitpro/devkitA64/bin:$PATH
GUEST=port/switch/guest
MUSL=$GUEST/third_party/musl-1.2.5
ARCH=$GUEST/libc/arch/aarch64_ilp32

SWITCH_ABI_FLAGS="-mabi=ilp32 -fno-math-errno -DHALO_SWITCH=1 -DHALO_RELOCATABLE_TAG_CACHE=1 \
-fms-extensions -fshort-wchar -fno-short-enums -fsigned-char \
-fcommon -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks -fno-omit-frame-pointer \
-ffp-contract=off -ffunction-sections -fdata-sections -O2 -g \
-fno-builtin-wcslen -fno-builtin-wcsnlen -fno-builtin-wcschr -fno-builtin-wcsrchr -fno-builtin-wcscmp \
-fno-builtin-wcsncmp -fno-builtin-wcscpy -fno-builtin-wcsncpy -fno-builtin-wcscat -fno-builtin-wcsncat \
-fno-builtin-wmemchr -fno-builtin-wmemcmp -fno-builtin-wmemcpy -fno-builtin-wmemmove -fno-builtin-wmemset"

GAME_FLAGS="-std=gnu89 -D__STRICT_ANSI__ -Drestrict=__restrict__ -w -Wno-error=incompatible-pointer-types \
-Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=return-type"

INCLUDES="-Iport/linux/include -Isource -Isource/main -Isource/cseries -Isource/sound -Isource/bink \
-I\"source/saved films\" -I\"source/saved games\" -Isource/cache -Isource/units -Isource/text \
-Isource/tag_files -Isource/structures -Isource/shell -Isource/shaders -Isource/scenario -Isource/render \
-Isource/rasterizer -Isource/physics -Isource/objects -Isource/objects/widgets -Isource/networking \
-Isource/models -Isource/memory -Isource/memory/zlib -Isource/math -Isource/tool -Isource/items \
-Isource/interface -Isource/input -Isource/hs -Isource/game -Isource/effects -Isource/editor \
-Isource/dialogs -Isource/devices -Isource/cutscene -Isource/camera -Isource/bungie_net -Isource/bitmaps \
-Isource/ai -idirafter port/include/xdk"

MUSL_INCLUDES="-nostdinc -I$GUEST/obj/include -I$ARCH -I$GUEST/libc/src_include -I$MUSL/src/internal -I$MUSL/src/include -I$MUSL/include"

DIRS="source/math source/cseries source/memory source/ai"

ok=0; fail=0
mkdir -p /tmp/game_test_obj
for d in $DIRS; do
	for f in "$d"/*.c; do
		[ -f "$f" ] || continue
		out=$(eval aarch64-none-elf-gcc $SWITCH_ABI_FLAGS $GAME_FLAGS \
			-include port/linux/include/halo_linux_prefix.h -include build/switch/halo_msvc_semantics_switch.h \
			-DDEBUG -Dxbox $INCLUDES $MUSL_INCLUDES \
			-c "$f" -o /tmp/game_test_obj/out.o 2>&1)
		if [ $? -eq 0 ]; then
			ok=$((ok+1))
		else
			fail=$((fail+1))
			echo "FAIL: $f"
			echo "$out" | grep "error:" | sort -u | head -3 | sed 's/^/    /'
		fi
	done
done
echo
echo "$ok OK, $fail FAILED"
