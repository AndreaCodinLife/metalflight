/* d3d12.dll shim for the D3DMetal research candidate.
 *
 * Mullion's D3D12CreateDevice loads the CPU split-push helper named by
 * MULLION_DIAGNOSTIC_HELPER_DLL once, on the first device creation, after the
 * game's Windows imports are resolved (sources/metalflight-mullion/src/d3d12/d3d12.cpp).
 * D3DMetal has no such hook, so this shim keeps exactly that load point and then
 * calls D3DMetal's own D3D12CreateDevice. D3DMetal's d3d12.dll is installed
 * unchanged as the builtin "d3dm12.dll"; every other export is a PE forwarder to it.
 * v2 adds the legacy shared-texture bridge (shared_bridge.c); v5 adds a Mullion-compatible WaitForVBlank
 * (vblank.c); v7 adds the direct CPU-frame path (shared_bridge.c, MSFS24BridgeSubmitFrame). Capability answers are unchanged. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <unknwn.h>
#include <stdio.h>
#include <d3d12.h>
#include "shared_bridge.h"
#include "vblank.h"

typedef HRESULT (WINAPI *create_device_fn)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);

static INIT_ONCE helper_once = INIT_ONCE_STATIC_INIT;
static INIT_ONCE real_once = INIT_ONCE_STATIC_INIT;
static create_device_fn real_create;

static void note(const char *text) { OutputDebugStringA(text); }

static BOOL CALLBACK load_helper(PINIT_ONCE once, PVOID param, PVOID *context) {
  char path[MAX_PATH], line[MAX_PATH + 96];
  DWORD length = GetEnvironmentVariableA("MULLION_DIAGNOSTIC_HELPER_DLL", path, sizeof(path));
  (void)once; (void)param; (void)context;
  if (!length || length >= sizeof(path)) { note("[D3DMETAL SHIM] helper=none\n"); return TRUE; }
  HMODULE module = LoadLibraryA(path);
  snprintf(line, sizeof(line), "[D3DMETAL SHIM] helper=%s loaded=%d error=%lu\n", path, module != NULL,
           module ? 0ul : (unsigned long)GetLastError());
  note(line);
  return TRUE;
}

static BOOL CALLBACK resolve_real(PINIT_ONCE once, PVOID param, PVOID *context) {
  (void)once; (void)param; (void)context;
  HMODULE d3dm = LoadLibraryA("d3dm12.dll");
  real_create = d3dm ? (create_device_fn)(void *)GetProcAddress(d3dm, "D3D12CreateDevice") : NULL;
  if (!real_create) note("[D3DMETAL SHIM] d3dm12.dll D3D12CreateDevice unavailable\n");
  return TRUE;
}

HRESULT WINAPI D3D12CreateDevice(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_level, REFIID riid, void **device) {
  InitOnceExecuteOnce(&helper_once, load_helper, NULL, NULL);
  InitOnceExecuteOnce(&real_once, resolve_real, NULL, NULL);
  if (!real_create) return E_FAIL;
  HRESULT hr = real_create(adapter, minimum_level, riid, device);
  if (SUCCEEDED(hr)) vblank_install();  /* v5: Mullion-compatible WaitForVBlank (vblank.c) */
  if (SUCCEEDED(hr) && device && *device) {  /* v2: legacy shared-texture bridge (shared_bridge.c) */
    ID3D12Device *d12 = NULL;
    if (SUCCEEDED(IUnknown_QueryInterface((IUnknown *)*device, &IID_ID3D12Device, (void **)&d12))) {
      shared_bridge_install(d12);
      ID3D12Device_Release(d12);
    }
  }
  return hr;
}
