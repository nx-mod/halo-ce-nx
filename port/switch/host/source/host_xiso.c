/*
HOST_XISO.C

Pulls in the one real, shared port/linux/src/xiso.c (not a copy - same
file every port reads an xiso with) with HALO_SWITCH_HOST set first, so
its platform.h/posix.h includes route to switch_host_posix_shim.h
instead (see xiso.c's own comment). SOURCES only globs port/switch/host/
source, so this is simpler than adding port/linux/src wholesale (which
would also try to build every other file there, wrong target entirely).
*/

#define HALO_SWITCH_HOST
#include "../../../linux/src/xiso.c"
