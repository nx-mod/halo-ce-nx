/*
GUEST_SOFTFLOAT_STUBS.C

musl's printf float formatting (vfprintf.c's fmt_fp) does real 128-bit
("tf"/quad) arithmetic on `long double`, which AArch64 genuinely is
(128-bit IEEE quad - not a choice, sizeof(long double) == 16 on this
hardware regardless of what bits/float.h claims, unlike Apple's arm64_32
where long double really is 64-bit). Porting libgcc's/compiler-rt's quad
soft-float routines is real work for a path this game never exercises:
its MSVC heritage means `long double` is always just `double` - nothing
here ever constructs a real 128-bit value, so these should never
actually run.

Stubs instead of the real routines, on purpose: if one of these DOES
fire, that assumption was wrong, and this needs to become a real port of
libgcc's quad-math (or a vfprintf.c patch to avoid the long-double path)
rather than silently producing wrong numbers from a half-right
implementation.
*/

#include <stdint.h>

extern void host_log(const char *text);

static void unexpected(const char *name)
{
	host_log(name);
	host_log("called - was \"the game never uses real long double\" wrong? "
		"see guest_softfloat_stubs.c");
	__builtin_trap();
}

typedef struct
{
	uint64_t lo, hi;
} tf_bits; /* stand-in for the 128-bit return/argument type */

tf_bits __addtf3(tf_bits a, tf_bits b) { unexpected("__addtf3"); return a; }
tf_bits __subtf3(tf_bits a, tf_bits b) { unexpected("__subtf3"); return a; }
tf_bits __multf3(tf_bits a, tf_bits b) { unexpected("__multf3"); return a; }
tf_bits __divtf3(tf_bits a, tf_bits b) { unexpected("__divtf3"); return a; }
int __netf2(tf_bits a, tf_bits b) { unexpected("__netf2"); return 1; }
int __eqtf2(tf_bits a, tf_bits b) { unexpected("__eqtf2"); return 1; }
int __getf2(tf_bits a, tf_bits b) { unexpected("__getf2"); return -1; }
int __letf2(tf_bits a, tf_bits b) { unexpected("__letf2"); return 1; }
tf_bits __extenddftf2(double a) { unexpected("__extenddftf2"); return (tf_bits){0, 0}; }
tf_bits __extendsftf2(float a) { unexpected("__extendsftf2"); return (tf_bits){0, 0}; }
double __trunctfdf2(tf_bits a) { unexpected("__trunctfdf2"); return 0; }
float __trunctfsf2(tf_bits a) { unexpected("__trunctfsf2"); return 0; }
unsigned __fixunstfsi(tf_bits a) { unexpected("__fixunstfsi"); return 0; }
tf_bits __floatunsitf(unsigned a) { unexpected("__floatunsitf"); return (tf_bits){0, 0}; }
int __fixtfsi(tf_bits a) { unexpected("__fixtfsi"); return 0; }
tf_bits __floatsitf(int a) { unexpected("__floatsitf"); return (tf_bits){0, 0}; }
