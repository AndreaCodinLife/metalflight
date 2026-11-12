/* Mullion-compatible IDXGIOutput::WaitForVBlank for the D3DMetal research candidate.
 *
 * MSFS's menu-video thread (METhread) waits for a vertical blank before each video tick. Mullion's DXGI returns
 * from WaitForVBlank immediately (owned probe vblank_probe: 0.00 ms back to back, 8.0 ms with 8 ms work), and MSFS
 * has been qualified with that behaviour. D3DMetal sleeps to a display refresh (9.7 ms back to back on the 120 Hz
 * panel; 17.7 ms with 8 ms work), so any per-frame work makes the video loop miss refreshes and the 59.94 fps menu
 * video plays at about half speed (Wine's frame sink never skips). This hook makes D3DMetal's WaitForVBlank return
 * S_OK immediately, as Mullion's does. MSFS24_D3DMETAL_VBLANK=wait keeps D3DMetal's own wait.
 * Installed once, by patching the WaitForVBlank slot of D3DMetal's IDXGIOutput vtable found through a private
 * factory; no D3DMetal code is modified. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <dxgi.h>
#include <stdio.h>
#include "vblank.h"

typedef HRESULT (STDMETHODCALLTYPE *wait_vblank_fn)(IDXGIOutput *);
static wait_vblank_fn real_wait_vblank;
static volatile LONG calls;

static HRESULT STDMETHODCALLTYPE hook_wait_vblank(IDXGIOutput *output) {
  (void)output;
  if (InterlockedIncrement(&calls) == 1) OutputDebugStringA("[D3DMETAL VBLANK] first WaitForVBlank returned immediately (Mullion behaviour)\n");
  return S_OK;
}

void vblank_install(void) {
  static LONG state;
  char value[8];
  if (InterlockedCompareExchange(&state, 1, 0) != 0) return;
  DWORD n = GetEnvironmentVariableA("MSFS24_D3DMETAL_VBLANK", value, sizeof(value));
  if (n == 4 && !memcmp(value, "wait", 4)) { OutputDebugStringA("[D3DMETAL VBLANK] hook disabled (wait)\n"); return; }
  typedef HRESULT (WINAPI *create_factory_fn)(REFIID, void **);
  HMODULE dxgi = LoadLibraryA("dxgi.dll");
  create_factory_fn create = dxgi ? (create_factory_fn)(void *)GetProcAddress(dxgi, "CreateDXGIFactory1") : NULL;
  IDXGIFactory1 *factory = NULL; IDXGIAdapter *adapter = NULL; IDXGIOutput *output = NULL;
  BOOL ok = FALSE;
  if (create && SUCCEEDED(create(&IID_IDXGIFactory1, (void **)&factory)) &&
      SUCCEEDED(IDXGIFactory1_EnumAdapters(factory, 0, &adapter)) && SUCCEEDED(IDXGIAdapter_EnumOutputs(adapter, 0, &output))) {
    void **slot = (void **)&output->lpVtbl->WaitForVBlank;
    DWORD old;
    if (*slot == (void *)hook_wait_vblank) ok = TRUE;
    else if (VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) {
      real_wait_vblank = (wait_vblank_fn)*slot;
      *slot = (void *)hook_wait_vblank;
      VirtualProtect(slot, sizeof(void *), old, &old);
      ok = TRUE;
    }
  }
  if (output) IDXGIOutput_Release(output);
  if (adapter) IDXGIAdapter_Release(adapter);
  if (factory) IDXGIFactory1_Release(factory);
  OutputDebugStringA(ok ? "[D3DMETAL VBLANK] installed\n" : "[D3DMETAL VBLANK] install failed\n");
}
