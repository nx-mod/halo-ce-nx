/*
SWITCH_XNET_NULL.C

The "headless boot" networking backend. No guest-side socket syscalls
exist yet (build_musl.sh's own comment: network/* needs bits/socket.h,
deferred along with real file I/O), so every winsock call fails rather
than pretending to succeed - matching real BSD/Winsock convention
(SOCKET_ERROR/INVALID_SOCKET is -1, not 0, unlike most of this port's
other null stubs). XNetStartup/XNetCleanup/XNetRegisterKey and friends
report success: "the Xbox Live network layer initialized" is a
different claim than "a socket call succeeded", and degrading
gracefully there (init succeeds, every actual connection attempt then
fails) is closer to a real environment with no network hardware than
failing startup outright. See switch_d3d8_null.c's header comment for
the generation method and caveats.
*/

#include <string.h>

#include "platform.h"

int WSAAPI WSAStartup(WORD version_requested, LPWSADATA data)
{
	(void)version_requested;
	if (data)
		memset(data, 0, sizeof(*data));
	return 0;
}

int WSAAPI WSACleanup(void)
{
	return 0;
}

int WSAAPI WSAGetLastError(void)
{
	return 0;
}

unsigned int __stdcall halo_ws_accept(unsigned int, struct sockaddr *, int *)
{
	return -1;
}

int __stdcall halo_ws_bind(unsigned int, const struct sockaddr *, int)
{
	return -1;
}

int __stdcall halo_ws_closesocket(unsigned int)
{
	return -1;
}

int __stdcall halo_ws_connect(unsigned int, const struct sockaddr *, int)
{
	return -1;
}

int __stdcall halo_ws_getpeername(unsigned int, struct sockaddr *, int *)
{
	return -1;
}

int __stdcall halo_ws_getsockname(unsigned int, struct sockaddr *, int *)
{
	return -1;
}

int __stdcall halo_ws_getsockopt(unsigned int, int, int, char *, int *)
{
	return -1;
}

int __stdcall halo_ws_ioctlsocket(unsigned int, long, unsigned long *)
{
	return -1;
}

int __stdcall halo_ws_listen(unsigned int, int)
{
	return -1;
}

int __stdcall halo_ws_recv(unsigned int, char *, int, int)
{
	return -1;
}

int __stdcall halo_ws_recvfrom(unsigned int, char *, int, int, struct sockaddr *, int *)
{
	return -1;
}

int __stdcall halo_ws_select(int, struct halo_ws_fd_set *, struct halo_ws_fd_set *, struct halo_ws_fd_set *, const struct halo_ws_timeval *)
{
	return 0;
}

int __stdcall halo_ws_send(unsigned int, const char *, int, int)
{
	return -1;
}

int __stdcall halo_ws_sendto(unsigned int, const char *, int, int, const struct sockaddr *, int)
{
	return -1;
}

int __stdcall halo_ws_setsockopt(unsigned int, int, int, const char *, int)
{
	return -1;
}

unsigned int __stdcall halo_ws_socket(int, int, int)
{
	return -1;
}

int __stdcall XNetCleanup(void)
{
	return 0;
}

int __stdcall XNetCreateKey(struct XNKID *, struct XNKEY *)
{
	return 0;
}

unsigned long __stdcall XNetGetEthernetLinkStatus(void)
{
	return 0;
}

unsigned long __stdcall XNetGetTitleXnAddr(struct XNADDR *)
{
	return 0;
}

int __stdcall XNetRandom(unsigned char *, unsigned int)
{
	return 0;
}

int __stdcall XNetRegisterKey(const struct XNKID *, const struct XNKEY *)
{
	return 0;
}

int __stdcall XNetStartup(const struct XNetStartupParams *)
{
	return 0;
}

int __stdcall XNetUnregisterKey(const struct XNKID *)
{
	return 0;
}

int __stdcall XNetXnAddrToInAddr(const struct XNADDR *, const struct XNKID *, struct in_addr *)
{
	return 0;
}

int __stdcall __WSAFDIsSet(unsigned int, struct halo_ws_fd_set *)
{
	return -1;
}
