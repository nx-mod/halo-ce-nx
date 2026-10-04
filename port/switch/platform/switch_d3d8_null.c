/*
SWITCH_D3D8_NULL.C

The "headless boot" D3D8 backend (PORTING.md): every Direct3D entry point
the game calls, implemented as a safe no-op or a failure return rather
than real GLES3/NVK rendering. Gets the game ticking without a real GPU
backend yet - the next step after this links is port/switch/platform's
real GLES3 translation (modeled on port/vita/platform/d3d8_gxm.c).

PLACEHOLDER, NOT REVIEWED FOR CORRECTNESS: every Create- or Lock-style
function here returns 0 (success) with its output pointer left
untouched - the caller will dereference that garbage pointer next. This
is fine for reaching a clean *link*, which is as far as this went before
stopping deliberately short of hardware testing (see PORTING.md) - it
will need real review (most likely: real failure returns so the game's
own error paths trigger instead) before the game is actually run.

Mechanically generated from port/include/xdk/xdk_pdb.h's declarations
(tools/switch_build.py doesn't run this - it was a one-time script, see
PORTING.md's milestone 7) - every signature below is real, not guessed.
*/

#include "platform.h"

struct Direct3D *__stdcall Direct3DCreate8(unsigned int sdk_version)
{
	(void)sdk_version;
	return 0;
}

long __stdcall Direct3D_CreateDevice(unsigned int adapter, enum _D3DDEVTYPE device_type, void *window,
	unsigned long behavior_flags, struct _D3DPRESENT_PARAMETERS_ *presentation_parameters, struct D3DDevice **device)
{
	(void)adapter; (void)device_type; (void)window; (void)behavior_flags; (void)presentation_parameters; (void)device;
	return 0;
}

void __stdcall Direct3D_SetPushBufferSize(unsigned long size, unsigned long count)
{
	(void)size; (void)count;
}

/* ---------- state tables the inline D3DDevice_SetRenderState/
GetRenderState/SetTextureStageState/GetTextureStageState etc in
xdk_d3d8.h read and write directly (extern DWORD[], no function call) */
DWORD D3D__RenderState[D3DRS_MAX];
DWORD D3D__TextureState[D3DTSS_MAXSTAGES][D3DTSS_MAX];
WORD *D3D__IndexData;

long __stdcall D3DXGetErrorStringA(long error_result, char *buffer, unsigned long buffer_length)
{
	(void)error_result; (void)buffer_length;
	if (buffer && buffer_length)
		buffer[0] = '\0';
	return 0;
}

/* source/main/d3d_intimacy.cpp - a .cpp file, not built for Switch (no
C++ in this guest toolchain yet). No hardware flip-count register to
read regardless, so NULL ("not available") is the honest answer either
way, not a stand-in for a real implementation. */
volatile unsigned int *d3d_find_flipcount(void)
{
	return 0;
}

/* source/rasterizer/xbox/rasterizer_xbox_decals.c - real D3D resource
contiguous-memory tricks the null backend has no equivalent for */
void *halo_d3d_contiguous_alloc(unsigned long size)
{
	(void)size;
	return 0;
}

void *halo_d3d_resource_pointer(const void *resource)
{
	(void)resource;
	return 0;
}

void halo_d3d_stream_attribute(long reg, long stream)
{
	(void)reg; (void)stream;
}

/* port/linux/include/halo_linux_source_fixups.h / halo_ui_pointer.h -
display/menu-pointer hooks the real GLES3 backend will answer for real */
long halo_screen_width(void)
{
	return 640;
}

long halo_screen_commit(void)
{
	return 640;
}

void halo_screen_ui_offset(unsigned char centered)
{
	(void)centered;
}

int halo_interpolation_enabled(void)
{
	return 0;
}

struct halo_ui_pointer;
int halo_ui_pointer_update(int menus_active, struct halo_ui_pointer *pointer)
{
	(void)menus_active; (void)pointer;
	return 0;
}

/* source/game/player_control.c - no real gamepad-as-mouse backend yet
(PORTING.md: input is libnx hid, not written yet either) */
int halo_linux_mouse_look(short gamepad_index, float *yaw, float *pitch)
{
	(void)gamepad_index;
	if (yaw) *yaw = 0.0f;
	if (pitch) *pitch = 0.0f;
	return 0;
}

/* source/render/render.c - debug draw-call counters, always zero here */
void halo_render_draw_counts(unsigned long *stream, unsigned long *immediate)
{
	if (stream) *stream = 0;
	if (immediate) *immediate = 0;
}

/* source/main.c and friends: a generation counter bumped whenever a
display/audio/input setting changes, so callers can re-read it once.
config_boolean/real/string (guest_platform_stubs.c) never report a
change, so this never needs to move either. */
volatile unsigned long halo_settings_generation;

/* ---------- generated D3DDevice_, D3DResource_, D3DSurface_, D3DTexture_,
D3DVertexBuffer_, D3DPalette_, D3DCubeTexture_, D3DVolumeTexture_,
Direct3D and D3DX stubs (xdk_pdb.h's exact declarations + a no-op/0 body) */

void __stdcall D3DCubeTexture_LockRect(struct D3DCubeTexture *, enum _D3DCUBEMAP_FACES, unsigned int, struct _D3DLOCKED_RECT *, const struct tagRECT *, unsigned long)
{
	
}

void __stdcall D3DDevice_Begin(enum _D3DPRIMITIVETYPE)
{
	
}

void __stdcall D3DDevice_BeginVisibilityTest(void)
{
	
}

void __stdcall D3DDevice_BlockUntilVerticalBlank(void)
{
	
}

void __stdcall D3DDevice_Clear(unsigned long, const struct _D3DRECT *, unsigned long, unsigned long, float, unsigned long)
{
	
}

long __stdcall D3DDevice_CreateCubeTexture(unsigned int, unsigned int, unsigned long, enum _D3DFORMAT, unsigned long, struct D3DCubeTexture **)
{
	return 0;
}

long __stdcall D3DDevice_CreateIndexBuffer(unsigned int, unsigned long, enum _D3DFORMAT, unsigned long, struct D3DIndexBuffer **)
{
	return 0;
}

long __stdcall D3DDevice_CreatePalette(enum _D3DPALETTESIZE, struct D3DPalette **)
{
	return 0;
}

long __stdcall D3DDevice_CreateTexture(unsigned int, unsigned int, unsigned int, unsigned long, enum _D3DFORMAT, unsigned long, struct D3DTexture **)
{
	return 0;
}

long __stdcall D3DDevice_CreateVertexBuffer(unsigned int, unsigned long, unsigned long, unsigned long, struct D3DVertexBuffer **)
{
	return 0;
}

long __stdcall D3DDevice_CreateVertexShader(const unsigned long *, const unsigned long *, unsigned long *, unsigned long)
{
	return 0;
}

long __stdcall D3DDevice_CreateVolumeTexture(unsigned int, unsigned int, unsigned int, unsigned int, unsigned long, enum _D3DFORMAT, unsigned long, struct D3DVolumeTexture **)
{
	return 0;
}

void __stdcall D3DDevice_DeleteVertexShader(unsigned long)
{
	
}

void __stdcall D3DDevice_DrawIndexedVertices(enum _D3DPRIMITIVETYPE, unsigned int, const unsigned short *)
{
	
}

void __stdcall D3DDevice_DrawVertices(enum _D3DPRIMITIVETYPE, unsigned int, unsigned int)
{
	
}

void __stdcall D3DDevice_End(void)
{
	
}

long __stdcall D3DDevice_EndVisibilityTest(unsigned long)
{
	return 0;
}

void __stdcall D3DDevice_GetBackBuffer(int, unsigned long, struct D3DSurface **)
{
	
}

long __stdcall D3DDevice_GetDepthStencilSurface(struct D3DSurface **)
{
	return 0;
}

void __stdcall D3DDevice_GetDeviceCaps(struct _D3DCAPS8 *)
{
	
}

void __stdcall D3DDevice_GetTransform(enum _D3DTRANSFORMSTATETYPE, struct _D3DMATRIX *)
{
	
}

void __stdcall D3DDevice_GetVertexShaderSize(unsigned long, unsigned int *)
{
	
}

long __stdcall D3DDevice_GetVisibilityTestResult(unsigned long, unsigned int *, unsigned __int64 *)
{
	return 0;
}

void __stdcall D3DDevice_InsertCallback(enum _D3DCALLBACKTYPE, void (*)(unsigned long), unsigned long)
{
	
}

int __stdcall D3DDevice_IsBusy(void)
{
	return 0;
}

void __stdcall D3DDevice_KickPushBuffer(void)
{
	
}

void __stdcall D3DDevice_LoadVertexShader(unsigned long, unsigned long)
{
	
}

long __stdcall D3DDevice_PersistDisplay(void)
{
	return 0;
}

void __stdcall D3DDevice_Present(const struct tagRECT *, const struct tagRECT *, void *, void *)
{
	
}

unsigned long __stdcall D3DDevice_Release(void)
{
	return 0;
}

void __stdcall D3DDevice_SelectVertexShader(unsigned long, unsigned long)
{
	
}

void __stdcall D3DDevice_SetFlickerFilter(unsigned long)
{
	
}

void __stdcall D3DDevice_SetIndices(struct D3DIndexBuffer *, unsigned int)
{
	
}

void __stdcall D3DDevice_SetPalette(unsigned long, struct D3DPalette *)
{
	
}

void __stdcall D3DDevice_SetPixelShaderProgram(struct _D3DPixelShaderDef *)
{
	
}

void __stdcall D3DDevice_SetRenderState_BackFillMode(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_CullMode(unsigned long)
{
	
}

void __fastcall D3DDevice_SetRenderState_Deferred(enum _D3DRENDERSTATETYPE, unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_DoNotCullUncompressed(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_Dxt1NoiseEnable(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_EdgeAntiAlias(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_FillMode(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_FogColor(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_FrontFace(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_LineWidth(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_LogicOp(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_MultiSampleAntiAlias(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_MultiSampleMask(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_MultiSampleType(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_NormalizeNormals(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderStateNotInline(enum _D3DRENDERSTATETYPE, unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_OcclusionCullEnable(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_PSTextureModes(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_RopZCmpAlwaysRead(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_RopZRead(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_ShadowFunc(unsigned long)
{
	
}

void __fastcall D3DDevice_SetRenderState_Simple(unsigned long, unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_StencilCullEnable(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_StencilEnable(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_StencilFail(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_TextureFactor(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_TwoSidedLighting(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_VertexBlend(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_YuvEnable(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_ZBias(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderState_ZEnable(unsigned long)
{
	
}

void __stdcall D3DDevice_SetRenderTarget(struct D3DSurface *, struct D3DSurface *)
{
	
}

void __stdcall D3DDevice_SetShaderConstantMode(unsigned long)
{
	
}

void __stdcall D3DDevice_SetSoftDisplayFilter(int)
{
	
}

void __stdcall D3DDevice_SetStreamSource(unsigned int, struct D3DVertexBuffer *, unsigned int)
{
	
}

void __stdcall D3DDevice_SetTexture(unsigned long, struct D3DBaseTexture *)
{
	
}

void __stdcall D3DDevice_SetTextureState_BorderColor(unsigned long, unsigned long)
{
	
}

void __stdcall D3DDevice_SetTextureState_BumpEnv(unsigned long, enum _D3DTEXTURESTAGESTATETYPE, unsigned long)
{
	
}

void __stdcall D3DDevice_SetTextureState_ColorKeyColor(unsigned long, unsigned long)
{
	
}

void __fastcall D3DDevice_SetTextureState_Deferred(unsigned long, enum _D3DTEXTURESTAGESTATETYPE, unsigned long)
{
	
}

void __stdcall D3DDevice_SetTextureState_TexCoordIndex(unsigned long, unsigned long)
{
	
}

void __stdcall D3DDevice_SetTransform(enum _D3DTRANSFORMSTATETYPE, const struct _D3DMATRIX *)
{
	
}

void __stdcall D3DDevice_SetVertexData2f(int, float, float)
{
	
}

void __stdcall D3DDevice_SetVertexData2s(int, short, short)
{
	
}

void __stdcall D3DDevice_SetVertexData4f(int, float, float, float, float)
{
	
}

void __stdcall D3DDevice_SetVertexData4ub(int, unsigned char, unsigned char, unsigned char, unsigned char)
{
	
}

void __stdcall D3DDevice_SetVertexDataColor(int, unsigned long)
{
	
}

void __stdcall D3DDevice_SetVertexShader(unsigned long)
{
	
}

void __stdcall D3DDevice_SetVertexShaderConstant(int, const void *, unsigned long)
{
	
}

void __stdcall D3DDevice_SetVerticalBlankCallback(void (*)(unsigned long))
{
	
}

void __stdcall D3DDevice_SetViewport(const struct _D3DVIEWPORT8 *)
{
	
}

void __stdcall D3DPalette_Lock(struct D3DPalette *, unsigned long **, unsigned long)
{
	
}

void __stdcall D3DResource_BlockUntilNotBusy(struct D3DResource *)
{
	
}

int __stdcall D3DResource_IsBusy(struct D3DResource *)
{
	return 0;
}

void __stdcall D3DResource_Register(struct D3DResource *, void *)
{
	
}

unsigned long __stdcall D3DResource_Release(struct D3DResource *)
{
	return 0;
}

void __stdcall D3DSurface_GetDesc(struct D3DSurface *, struct _D3DSURFACE_DESC *)
{
	
}

void __stdcall D3DSurface_LockRect(struct D3DSurface *, struct _D3DLOCKED_RECT *, const struct tagRECT *, unsigned long)
{
	
}

void __stdcall D3DTexture_GetLevelDesc(struct D3DTexture *, unsigned int, struct _D3DSURFACE_DESC *)
{
	
}

long __stdcall D3DTexture_GetSurfaceLevel(struct D3DTexture *, unsigned int, struct D3DSurface **)
{
	return 0;
}

void __stdcall D3DTexture_LockRect(struct D3DTexture *, unsigned int, struct _D3DLOCKED_RECT *, const struct tagRECT *, unsigned long)
{
	
}

void __stdcall D3DVertexBuffer_Lock(struct D3DVertexBuffer *, unsigned int, unsigned int, unsigned char **, unsigned long)
{
	
}

void __stdcall D3DVolumeTexture_LockBox(struct D3DVolumeTexture *, unsigned int, struct _D3DLOCKED_BOX *, const struct _D3DBOX *, unsigned long)
{
	
}

D3DXMATRIX *__stdcall D3DXMatrixOrthoLH(D3DXMATRIX *, float, float, float, float)
{
	return 0;
}

D3DXMATRIX *__stdcall D3DXMatrixPerspectiveLH(D3DXMATRIX *, float, float, float, float)
{
	return 0;
}

struct D3DXVECTOR4 *__stdcall D3DXVec4Transform(struct D3DXVECTOR4 *, const struct D3DXVECTOR4 *, const D3DXMATRIX *)
{
	return 0;
}
