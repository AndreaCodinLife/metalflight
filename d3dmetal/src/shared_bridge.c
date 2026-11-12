/* Legacy shared-texture bridge for the D3DMetal research candidate.
 *
 * MSFS decodes its menu/intro video into a D3D11 texture created with D3D11_RESOURCE_MISC_SHARED,
 * calls IDXGIResource::GetSharedHandle and opens the handle with ID3D12Device::OpenSharedHandle.
 * D3DMetal implements neither (E_NOTIMPL). This bridge, active only where D3DMetal refuses:
 *   GetSharedHandle  registers the single-mip, single-slice, single-sample 2D texture and returns a
 *                    private legacy-style handle (bit 30 set, < 2^32);
 *   OpenSharedHandle for a registered handle creates one D3D12 texture with the same size, format and
 *                    (render-target) usage in a default heap, flags ALLOW_SIMULTANEOUS_ACCESS;
 *                    unknown handles fail with a null output (D3DMetal's own result kept);
 *   ID3D11DeviceContext::Flush after the original Flush, for each registered texture of that device
 *                    with an opened D3D12 texture: CopyResource to a staging texture, Map(READ), a linear
 *                    memcpy into one of two persistently mapped D3D12 upload buffers, and a
 *                    CopyTextureRegion on the bridge's own D3D12 queue (the GPU writes the tiled layout).
 *                    v4 (default, asynchronous): the Flush hook only queues CopyResource into one of two
 *                    staging textures, submits it and wakes a worker thread, which maps, copies and runs
 *                    the D3D12 upload under ID3D10Multithread, enabling the device's multithread protection
 *                    if it was off. If the worker is
 *                    behind, older frames are superseded (dropped), so the producer is never slowed.
 *                    MSFS24_D3DMETAL_SHARED_BRIDGE=sync keeps v3's synchronous contract (Flush returns after
 *                    the D3D12 copy completed) for exact-pixel controls.
 *                    v2 used WriteToSubresource, which D3DMetal implements as a CPU replaceRegion into the
 *                    tiled texture (97% of the 4K video thread in MSFS). The patched media engine flushes (and waits)
 *                    after every frame it writes to a shared texture (MSFS24_WINE_MEDIA_SHARED_FLUSH/WAIT).
 * It is a copy, not shared memory: D3D12 sees the D3D11 contents as of the last Flush, and a consumer
 * reading during a copy can see a partially updated frame. MSFS24_D3DMETAL_SHARED_BRIDGE=0 disables it.
 * Hooks are installed by patching one slot in each D3DMetal vtable, found through objects created on a
 * private D3D11 device; no D3DMetal code is modified. NT handles (IDXGIResource1) are not bridged.
 *
 * v7 direct path: MSFS24BridgeSubmitFrame (exported) lets the patched Wine media engine
 * (MSFS24_WINE_MEDIA_D3DMETAL_DIRECT=1) hand a decoded CPU frame for a bridged texture of the same size and
 * format. The frame is copied into one of two persistently mapped D3D12 upload buffers and a CopyTextureRegion
 * is submitted on the game's first DIRECT queue of that device (captured by a CreateCommandQueue hook), so
 * the game's own later work on that queue sees the completed write. No D3D11 upload, draw, flush, staging
 * readback or device lock is involved. A texture used this way is skipped by the Flush copy path.
 * MSFS24_D3DMETAL_BRIDGE_QUEUE=own uses a bridge-owned queue instead of the game's. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#include <stdio.h>
#include "shared_bridge.h"

#define MAX_ENTRIES 16

struct entry {
  HANDLE handle;
  ID3D11Texture2D *tex11;      /* strong ref: the producer may release its copy first */
  ID3D11Device *dev11;         /* strong ref */
  ID3D11Texture2D *staging[2]; /* created on first copy */
  volatile LONG slot_state[2]; /* 0 free, 1 copied and pending, 2 being read by the worker */
  volatile LONG pending;       /* staging index for the worker, -1 none */
  LONG next_slot, dropped;
  HANDLE work_event, worker;
  ID3D10Multithread *mt;
  LONGLONG hook_sum; LONG hook_n;
  ULONGLONG rate_tick; LONG rate_copies, rate_hooks; LONG hooks;
  ID3D12Resource *res12;       /* strong ref after OpenSharedHandle */
  ID3D12Device *dev12;         /* strong ref after OpenSharedHandle */
  D3D11_TEXTURE2D_DESC desc;
  LONG copies;
  /* GPU upload path, created on first copy */
  ID3D12CommandQueue *queue;
  ID3D12CommandAllocator *allocator[2];
  ID3D12GraphicsCommandList *list;
  ID3D12Fence *fence;
  HANDLE event;
  ID3D12Resource *upload[2];
  BYTE *upload_ptr[2];
  UINT64 slot_value[2], fence_value;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
  UINT rows;
  UINT64 row_bytes;
  BOOL broken;
  LONGLONG sum_map, sum_copy, sum_gpu; LONG timed;
  /* v7 direct path (separate D3D12 objects, never shared with the Flush worker) */
  volatile LONG direct;
  ID3D12CommandQueue *dqueue;
  ID3D12CommandAllocator *dalloc[2];
  ID3D12GraphicsCommandList *dlist;
  ID3D12Fence *dfence;
  HANDLE devent;
  ID3D12Resource *dupload[2];
  BYTE *dupload_ptr[2];
  UINT64 dslot_value[2], dfence_value;
  BOOL dbroken, dgame_queue;
  LONG dframes, dwaits; ULONGLONG drate_tick; LONG drate_frames; LONGLONG dcopy_sum; LONG dcopy_n;
};

static struct entry entries[MAX_ENTRIES];
static unsigned entry_count;
static SRWLOCK lock = SRWLOCK_INIT;
static DWORD reentry_tls = TLS_OUT_OF_INDEXES;

typedef HRESULT (STDMETHODCALLTYPE *get_shared_handle_fn)(IDXGIResource *, HANDLE *);
typedef HRESULT (STDMETHODCALLTYPE *open_shared_handle_fn)(ID3D12Device *, HANDLE, REFIID, void **);
typedef void (STDMETHODCALLTYPE *flush_fn)(ID3D11DeviceContext *);
static get_shared_handle_fn real_get_shared_handle;
static open_shared_handle_fn real_open_shared_handle;
static flush_fn real_flush;
typedef HRESULT (STDMETHODCALLTYPE *create_queue_fn)(ID3D12Device *, const D3D12_COMMAND_QUEUE_DESC *, REFIID, void **);
static create_queue_fn real_create_queue;
/* First DIRECT queue the game created on each D3D12 device (strong refs). */
static struct { ID3D12Device *device; ID3D12CommandQueue *queue; } game_queues[4];
static LONG queue_logs = 8;
static LONG log_budget = 64;

static void bridge_log_always(const char *fmt, ...) {
  char line[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  OutputDebugStringA(line);
}

static void bridge_log(const char *fmt, ...) {
  char line[256];
  va_list args;
  if (InterlockedDecrement(&log_budget) < 0) return;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  OutputDebugStringA(line);
}

static BOOL bridge_sync(void) {
  char value[8];
  DWORD n = GetEnvironmentVariableA("MSFS24_D3DMETAL_SHARED_BRIDGE", value, sizeof(value));
  return n == 4 && !memcmp(value, "sync", 4);
}

static BOOL bridge_enabled(void) {
  char value[4];
  DWORD n = GetEnvironmentVariableA("MSFS24_D3DMETAL_SHARED_BRIDGE", value, sizeof(value));
  return !(n == 1 && value[0] == '0');
}

static BOOL patch_slot(void **slot, void *hook, void **original) {
  DWORD old;
  if (*slot == hook) return TRUE;  /* already installed */
  if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return FALSE;
  *original = *slot;
  *slot = hook;
  VirtualProtect(slot, sizeof(void *), old, &old);
  FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void *));
  return TRUE;
}

static struct entry *find_handle(HANDLE handle) {
  for (unsigned i = 0; i < entry_count; i++)
    if (entries[i].handle == handle) return &entries[i];
  return NULL;
}

static HRESULT STDMETHODCALLTYPE hook_get_shared_handle(IDXGIResource *resource, HANDLE *out) {
  HRESULT hr = real_get_shared_handle(resource, out);
  ID3D11Texture2D *tex = NULL;
  D3D11_TEXTURE2D_DESC desc;
  if (SUCCEEDED(hr) || !out) return hr;
  if (FAILED(IDXGIResource_QueryInterface(resource, &IID_ID3D11Texture2D, (void **)&tex))) return hr;
  ID3D11Texture2D_GetDesc(tex, &desc);
  if (!(desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED) || (desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) ||
      desc.MipLevels != 1 || desc.ArraySize != 1 || desc.SampleDesc.Count != 1) {
    ID3D11Texture2D_Release(tex);
    return hr;
  }
  AcquireSRWLockExclusive(&lock);
  for (unsigned i = 0; i < entry_count; i++)
    if (entries[i].tex11 == tex) {
      *out = entries[i].handle;
      ReleaseSRWLockExclusive(&lock);
      ID3D11Texture2D_Release(tex);
      return S_OK;
    }
  if (entry_count == MAX_ENTRIES) {
    ReleaseSRWLockExclusive(&lock);
    ID3D11Texture2D_Release(tex);
    bridge_log("[D3DMETAL BRIDGE] GetSharedHandle table full\n");
    return hr;
  }
  struct entry *e = &entries[entry_count];
  e->handle = (HANDLE)(ULONG_PTR)(0x40000000u | ((entry_count + 1) << 4));
  e->tex11 = tex;  /* keep the QI reference */
  ID3D11Texture2D_GetDevice(tex, &e->dev11);
  e->desc = desc;
  entry_count++;
  *out = e->handle;
  ReleaseSRWLockExclusive(&lock);
  bridge_log("[D3DMETAL BRIDGE] GetSharedHandle bridged %ux%u format=%u bind=%x handle=%p (D3DMetal hr=%08lx)\n",
             desc.Width, desc.Height, desc.Format, desc.BindFlags, *out, (unsigned long)hr);
  return S_OK;
}

static HRESULT STDMETHODCALLTYPE hook_open_shared_handle(ID3D12Device *device, HANDLE handle, REFIID riid, void **out) {
  struct entry *e;
  HRESULT hr;
  if (out) *out = NULL;
  AcquireSRWLockExclusive(&lock);
  e = find_handle(handle);
  if (!e || !out) {
    ReleaseSRWLockExclusive(&lock);
    hr = real_open_shared_handle(device, handle, riid, out);
    if (FAILED(hr) && out) *out = NULL;
    return hr;
  }
  if (!e->res12) {
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0};
    D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, e->desc.Width, e->desc.Height, 1, 1, e->desc.Format,
                                {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS};
    if (e->desc.BindFlags & D3D11_BIND_RENDER_TARGET) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
                                              NULL, &IID_ID3D12Resource, (void **)&e->res12);
    bridge_log("[D3DMETAL BRIDGE] OpenSharedHandle handle=%p create=%08lx\n", handle, (unsigned long)hr);
    if (FAILED(hr)) { e->res12 = NULL; ReleaseSRWLockExclusive(&lock); return hr; }
    e->dev12 = device;
    ID3D12Device_AddRef(device);
  }
  hr = ID3D12Resource_QueryInterface(e->res12, riid, out);
  ReleaseSRWLockExclusive(&lock);
  return hr;
}

static BOOL create_upload_path(struct entry *e) {
  D3D12_RESOURCE_DESC tex = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, e->desc.Width, e->desc.Height, 1, 1, e->desc.Format,
                             {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE};
  D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
  D3D12_HEAP_PROPERTIES up = {D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0};
  UINT64 total = 0;
  HRESULT hr;
  ID3D12Device_GetCopyableFootprints(e->dev12, &tex, 0, 1, 0, &e->footprint, &e->rows, &e->row_bytes, &total);
  D3D12_RESOURCE_DESC buf = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, total, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                             D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
  if (FAILED(hr = ID3D12Device_CreateCommandQueue(e->dev12, &qd, &IID_ID3D12CommandQueue, (void **)&e->queue))) goto fail;
  for (int i = 0; i < 2; i++) {
    D3D12_RANGE none = {0, 0};
    if (FAILED(hr = ID3D12Device_CreateCommandAllocator(e->dev12, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator,
                                                         (void **)&e->allocator[i]))) goto fail;
    if (FAILED(hr = ID3D12Device_CreateCommittedResource(e->dev12, &up, D3D12_HEAP_FLAG_NONE, &buf, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                         NULL, &IID_ID3D12Resource, (void **)&e->upload[i]))) goto fail;
    if (FAILED(hr = ID3D12Resource_Map(e->upload[i], 0, &none, (void **)&e->upload_ptr[i]))) goto fail;
  }
  if (FAILED(hr = ID3D12Device_CreateCommandList(e->dev12, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, e->allocator[0], NULL,
                                                 &IID_ID3D12GraphicsCommandList, (void **)&e->list))) goto fail;
  ID3D12GraphicsCommandList_Close(e->list);
  if (FAILED(hr = ID3D12Device_CreateFence(e->dev12, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&e->fence))) goto fail;
  if (!(e->event = CreateEventA(NULL, FALSE, FALSE, NULL))) { hr = E_FAIL; goto fail; }
  bridge_log("[D3DMETAL BRIDGE] upload path ready rows=%u row_bytes=%llu pitch=%u total=%llu\n", e->rows,
             (unsigned long long)e->row_bytes, e->footprint.Footprint.RowPitch, (unsigned long long)total);
  return TRUE;
fail:
  bridge_log("[D3DMETAL BRIDGE] upload path failed %08lx\n", (unsigned long)hr);
  e->broken = TRUE;
  return FALSE;
}

static void upload_from_staging(ID3D11DeviceContext *context, struct entry *e, unsigned sidx) {
  D3D11_MAPPED_SUBRESOURCE mapped;
  LARGE_INTEGER t0, t1, freq;
  HRESULT hr;
  QueryPerformanceCounter(&t0);
  unsigned slot = (unsigned)(e->fence_value & 1);
  if (ID3D12Fence_GetCompletedValue(e->fence) < e->slot_value[slot]) {  /* GPU still reading this buffer */
    ID3D12Fence_SetEventOnCompletion(e->fence, e->slot_value[slot], e->event);
    if (WaitForSingleObject(e->event, 250) != WAIT_OBJECT_0) {
      bridge_log("[D3DMETAL BRIDGE] upload slot busy; frame skipped\n");
      return;
    }
  }
  LARGE_INTEGER t_map, t_copy;
  if (FAILED(hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)e->staging[sidx], 0, D3D11_MAP_READ, 0, &mapped))) {
    bridge_log("[D3DMETAL BRIDGE] staging map failed %08lx\n", (unsigned long)hr);
    return;
  }
  QueryPerformanceCounter(&t_map);
  BYTE *dst = e->upload_ptr[slot] + e->footprint.Offset;
  const BYTE *src = mapped.pData;
  UINT pitch = e->footprint.Footprint.RowPitch;
  if (pitch == mapped.RowPitch) memcpy(dst, src, (size_t)pitch * (e->rows - 1) + e->row_bytes);
  else for (UINT y = 0; y < e->rows; y++) memcpy(dst + (size_t)y * pitch, src + (size_t)y * mapped.RowPitch, e->row_bytes);
  ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)e->staging[sidx], 0);
  QueryPerformanceCounter(&t_copy);

  D3D12_TEXTURE_COPY_LOCATION to = {e->res12, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {.SubresourceIndex = 0}};
  D3D12_TEXTURE_COPY_LOCATION from = {e->upload[slot], D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {.PlacedFootprint = e->footprint}};
  ID3D12CommandAllocator_Reset(e->allocator[slot]);
  ID3D12GraphicsCommandList_Reset(e->list, e->allocator[slot], NULL);
  ID3D12GraphicsCommandList_CopyTextureRegion(e->list, &to, 0, 0, 0, &from, NULL);
  if (FAILED(hr = ID3D12GraphicsCommandList_Close(e->list))) {
    bridge_log("[D3DMETAL BRIDGE] copy list close failed %08lx\n", (unsigned long)hr);
    return;
  }
  ID3D12CommandList *lists[] = {(ID3D12CommandList *)e->list};
  ID3D12CommandQueue_ExecuteCommandLists(e->queue, 1, lists);
  e->slot_value[slot] = ++e->fence_value;
  hr = ID3D12CommandQueue_Signal(e->queue, e->fence, e->fence_value);
  /* Wait for the D3D12 copy: in sync mode this keeps v2's contract; in async mode it paces the worker. */
  if (SUCCEEDED(hr) && ID3D12Fence_GetCompletedValue(e->fence) < e->fence_value) {
    ID3D12Fence_SetEventOnCompletion(e->fence, e->fence_value, e->event);
    if (WaitForSingleObject(e->event, 250) != WAIT_OBJECT_0) bridge_log("[D3DMETAL BRIDGE] copy wait timed out\n");
  }
  QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&freq);
  LONG n = InterlockedIncrement(&e->copies);
  e->sum_map += t_map.QuadPart - t0.QuadPart; e->sum_copy += t_copy.QuadPart - t_map.QuadPart; e->sum_gpu += t1.QuadPart - t_copy.QuadPart;
  if (++e->timed == 60) {  /* averages: wait+CopyResource+Map | memcpy+Unmap | GPU copy submit+wait */
    ULONGLONG now = GetTickCount64();
    if (now - e->rate_tick >= 10000) {  /* every >=10 s, never budget-limited: published and requested frames per second */
      if (e->rate_tick)
        bridge_log_always("[D3DMETAL BRIDGE] rate t=%llu published_fps=%.1f flushes_fps=%.1f dropped_total=%ld\n", now,
                          (n - e->rate_copies) * 1000.0 / (now - e->rate_tick), (e->hooks - e->rate_hooks) * 1000.0 / (now - e->rate_tick), e->dropped);
      e->rate_tick = now; e->rate_copies = n; e->rate_hooks = e->hooks;
    }
    bridge_log("[D3DMETAL BRIDGE] avg60 n=%ld map_us=%lld memcpy_us=%lld gpu_us=%lld hook_us=%lld dropped=%ld\n", n,
               e->sum_map * 1000000 / freq.QuadPart / 60, e->sum_copy * 1000000 / freq.QuadPart / 60,
               e->sum_gpu * 1000000 / freq.QuadPart / 60,
               e->hook_n ? e->hook_sum * 1000000 / freq.QuadPart / e->hook_n : 0, e->dropped);
    e->sum_map = e->sum_copy = e->sum_gpu = 0; e->timed = 0; e->hook_sum = 0; e->hook_n = 0;
  }
  if (FAILED(hr) || n <= 3 || n % 600 == 0)
    bridge_log("[D3DMETAL BRIDGE] copy handle=%p n=%ld cpu_us=%lld hr=%08lx\n", e->handle, n,
               (long long)((t1.QuadPart - t0.QuadPart) * 1000000 / freq.QuadPart), (unsigned long)hr);
}

static BOOL prepare_entry(struct entry *e) {
  HRESULT hr;
  if (e->broken) return FALSE;
  if (!e->queue && !create_upload_path(e)) return FALSE;
  for (int i = 0; i < 2; i++) {
    if (e->staging[i]) continue;
    D3D11_TEXTURE2D_DESC sd = e->desc;
    sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.MiscFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(hr = ID3D11Device_CreateTexture2D(e->dev11, &sd, NULL, &e->staging[i]))) {
      e->staging[i] = NULL; e->broken = TRUE;
      bridge_log("[D3DMETAL BRIDGE] staging create failed %08lx\n", (unsigned long)hr);
      return FALSE;
    }
  }
  return TRUE;
}

static void copy_entry_sync(ID3D11DeviceContext *context, struct entry *e) {
  if (!prepare_entry(e)) return;
  ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)e->staging[0], (ID3D11Resource *)e->tex11);
  upload_from_staging(context, e, 0);
}

static DWORD WINAPI worker_main(void *param) {
  struct entry *e = param;
  ID3D11DeviceContext *context = NULL;
  ID3D11Device_GetImmediateContext(e->dev11, &context);
  for (;;) {
    WaitForSingleObject(e->work_event, INFINITE);
    LONG s = InterlockedExchange(&e->pending, -1);
    if (s < 0) continue;
    if (InterlockedCompareExchange(&e->slot_state[s], 2, 1) != 1) continue;  /* superseded */
    if (e->direct) { InterlockedExchange(&e->slot_state[s], 0); continue; }  /* v7: the direct path owns this texture */
    if (e->mt) ID3D10Multithread_Enter(e->mt);
    upload_from_staging(context, e, (unsigned)s);
    if (e->mt) ID3D10Multithread_Leave(e->mt);
    InterlockedExchange(&e->slot_state[s], 0);
  }
  return 0;
}

static void queue_entry_async(ID3D11DeviceContext *context, struct entry *e) {
  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);
  InterlockedIncrement(&e->hooks);
  if (!prepare_entry(e)) return;
  if (!e->worker) {
    if (FAILED(ID3D11Device_QueryInterface(e->dev11, &IID_ID3D10Multithread, (void **)&e->mt))) e->mt = NULL;
    /* The worker uses the immediate context from a second thread: make D3D11 serialize calls on this device. */
    BOOL was_protected = e->mt ? ID3D10Multithread_GetMultithreadProtected(e->mt) : FALSE;
    if (e->mt && !was_protected) ID3D10Multithread_SetMultithreadProtected(e->mt, TRUE);
    e->pending = -1;
    e->work_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    e->worker = e->work_event ? CreateThread(NULL, 0, worker_main, e, 0, NULL) : NULL;
    bridge_log("[D3DMETAL BRIDGE] async worker=%d multithread=%d was_protected=%d now=%d\n", e->worker != NULL, e->mt != NULL,
               was_protected, e->mt ? ID3D10Multithread_GetMultithreadProtected(e->mt) : -1);
    if (!e->worker) { e->broken = TRUE; return; }
  }
  /* One worker reads at most one slot (state 2). Use the other slot if needed; a slot still pending
     (state 1, an older frame the worker has not taken) is superseded and reused. */
  LONG s = e->next_slot;
  if (e->slot_state[s] == 2) s ^= 1;
  if (InterlockedCompareExchange(&e->slot_state[s], 0, 1) == 1) e->dropped++;
  if (e->slot_state[s] != 0) { e->dropped++; return; }  /* raced with the worker taking it: drop this frame */
  ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)e->staging[s], (ID3D11Resource *)e->tex11);
  real_flush(context);
  InterlockedExchange(&e->slot_state[s], 1);
  LONG old = InterlockedExchange(&e->pending, s);
  if (old >= 0 && old != s && InterlockedCompareExchange(&e->slot_state[old], 0, 1) == 1) e->dropped++;
  e->next_slot = s ^ 1;
  SetEvent(e->work_event);
  QueryPerformanceCounter(&t1);
  e->hook_sum += t1.QuadPart - t0.QuadPart; e->hook_n++;
}

static void STDMETHODCALLTYPE hook_flush(ID3D11DeviceContext *context) {
  ID3D11Device *device = NULL;
  real_flush(context);
  if (TlsGetValue(reentry_tls)) return;
  TlsSetValue(reentry_tls, (void *)1);
  BOOL sync = bridge_sync();
  ID3D11DeviceContext_GetDevice(context, &device);
  AcquireSRWLockExclusive(&lock);
  for (unsigned i = 0; i < entry_count; i++)
    if (entries[i].dev11 == device && entries[i].res12 && !entries[i].direct) {
      if (sync) copy_entry_sync(context, &entries[i]);
      else queue_entry_async(context, &entries[i]);
    }
  ReleaseSRWLockExclusive(&lock);
  if (device) ID3D11Device_Release(device);
  TlsSetValue(reentry_tls, NULL);
}

static BOOL install_d3d11_hooks(void) {
  typedef HRESULT (WINAPI *create11_fn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT,
                                        UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
  HMODULE d3d11 = LoadLibraryA("d3d11.dll");
  create11_fn create = d3d11 ? (create11_fn)(void *)GetProcAddress(d3d11, "D3D11CreateDevice") : NULL;
  ID3D11Device *device = NULL; ID3D11DeviceContext *context = NULL; ID3D11Texture2D *tex = NULL; IDXGIResource *res = NULL;
  D3D11_TEXTURE2D_DESC td = {4, 4, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                             D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_SHARED};
  BOOL ok = FALSE;
  if (!create || FAILED(create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                               D3D11_SDK_VERSION, &device, NULL, &context))) goto done;
  if (FAILED(ID3D11Device_CreateTexture2D(device, &td, NULL, &tex))) goto done;
  if (FAILED(ID3D11Texture2D_QueryInterface(tex, &IID_IDXGIResource, (void **)&res))) goto done;
  ok = patch_slot((void **)&res->lpVtbl->GetSharedHandle, (void *)hook_get_shared_handle, (void **)&real_get_shared_handle) &&
       patch_slot((void **)&context->lpVtbl->Flush, (void *)hook_flush, (void **)&real_flush);
done:
  if (res) IDXGIResource_Release(res);
  if (tex) ID3D11Texture2D_Release(tex);
  if (context) ID3D11DeviceContext_Release(context);
  if (device) ID3D11Device_Release(device);
  return ok;
}

static HRESULT STDMETHODCALLTYPE hook_create_queue(ID3D12Device *device, const D3D12_COMMAND_QUEUE_DESC *desc, REFIID riid, void **out) {
  HRESULT hr = real_create_queue(device, desc, riid, out);
  if (FAILED(hr) || !out || !*out || !desc || desc->Type != D3D12_COMMAND_LIST_TYPE_DIRECT || TlsGetValue(reentry_tls)) return hr;
  ID3D12CommandQueue *queue = NULL;
  if (FAILED(IUnknown_QueryInterface((IUnknown *)*out, &IID_ID3D12CommandQueue, (void **)&queue))) return hr;
  BOOL kept = FALSE;
  AcquireSRWLockExclusive(&lock);
  unsigned i;
  for (i = 0; i < 4 && game_queues[i].device && game_queues[i].device != device; i++) {}
  if (i < 4 && !game_queues[i].device) {
    game_queues[i].device = device; ID3D12Device_AddRef(device);
    game_queues[i].queue = queue; kept = TRUE;
  }
  ReleaseSRWLockExclusive(&lock);
  if (InterlockedDecrement(&queue_logs) >= 0)
    bridge_log_always("[D3DMETAL BRIDGE] game DIRECT queue device=%p queue=%p priority=%d flags=%x kept=%d\n", device, queue,
                      desc->Priority, desc->Flags, kept);
  if (!kept) ID3D12CommandQueue_Release(queue);
  return hr;
}

static BOOL bridge_own_queue(void) {
  char value[8];
  DWORD n = GetEnvironmentVariableA("MSFS24_D3DMETAL_BRIDGE_QUEUE", value, sizeof(value));
  return n == 3 && !memcmp(value, "own", 3);
}

/* Called with the bridge lock held. */
static BOOL create_direct_path(struct entry *e) {
  D3D12_RESOURCE_DESC tex = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, e->desc.Width, e->desc.Height, 1, 1, e->desc.Format,
                             {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE};
  D3D12_HEAP_PROPERTIES up = {D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0};
  UINT64 total = 0;
  HRESULT hr = S_OK;
  ID3D12Device_GetCopyableFootprints(e->dev12, &tex, 0, 1, 0, &e->footprint, &e->rows, &e->row_bytes, &total);
  D3D12_RESOURCE_DESC buf = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, total, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                             D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
  if (!bridge_own_queue())
    for (unsigned i = 0; i < 4; i++)
      if (game_queues[i].device == e->dev12) { e->dqueue = game_queues[i].queue; ID3D12CommandQueue_AddRef(e->dqueue); e->dgame_queue = TRUE; }
  if (!e->dqueue) {
    D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
    TlsSetValue(reentry_tls, (void *)1);
    hr = ID3D12Device_CreateCommandQueue(e->dev12, &qd, &IID_ID3D12CommandQueue, (void **)&e->dqueue);
    TlsSetValue(reentry_tls, NULL);
    if (FAILED(hr)) goto fail;
  }
  for (int i = 0; i < 2; i++) {
    D3D12_RANGE none = {0, 0};
    if (FAILED(hr = ID3D12Device_CreateCommandAllocator(e->dev12, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator,
                                                         (void **)&e->dalloc[i]))) goto fail;
    if (FAILED(hr = ID3D12Device_CreateCommittedResource(e->dev12, &up, D3D12_HEAP_FLAG_NONE, &buf, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                         NULL, &IID_ID3D12Resource, (void **)&e->dupload[i]))) goto fail;
    if (FAILED(hr = ID3D12Resource_Map(e->dupload[i], 0, &none, (void **)&e->dupload_ptr[i]))) goto fail;
  }
  if (FAILED(hr = ID3D12Device_CreateCommandList(e->dev12, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, e->dalloc[0], NULL,
                                                 &IID_ID3D12GraphicsCommandList, (void **)&e->dlist))) goto fail;
  ID3D12GraphicsCommandList_Close(e->dlist);
  if (FAILED(hr = ID3D12Device_CreateFence(e->dev12, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&e->dfence))) goto fail;
  if (!(e->devent = CreateEventA(NULL, FALSE, FALSE, NULL))) { hr = E_FAIL; goto fail; }
  bridge_log_always("[D3DMETAL BRIDGE] direct path ready handle=%p queue=%s rows=%u row_bytes=%llu pitch=%u total=%llu\n", e->handle,
                    e->dgame_queue ? "game" : "own", e->rows, (unsigned long long)e->row_bytes, e->footprint.Footprint.RowPitch,
                    (unsigned long long)total);
  return TRUE;
fail:
  bridge_log_always("[D3DMETAL BRIDGE] direct path failed %08lx\n", (unsigned long)hr);
  e->dbroken = TRUE;
  return FALSE;
}

/* Exported for the patched Wine media engine. Returns TRUE when the frame was submitted to the D3D12 texture
 * opened for dst; FALSE (nothing done) for any texture or frame it cannot take, so the caller draws as before. */
__declspec(dllexport) BOOL WINAPI MSFS24BridgeSubmitFrame(ID3D11Texture2D *dst, const void *data, UINT pitch, UINT width,
                                                         UINT height, DXGI_FORMAT format) {
  struct entry *e = NULL;
  LARGE_INTEGER t0, t1;
  HRESULT hr;
  if (!dst || !data || !bridge_enabled()) return FALSE;
  AcquireSRWLockExclusive(&lock);
  for (unsigned i = 0; i < entry_count; i++)
    if (entries[i].tex11 == dst) { e = &entries[i]; break; }
  if (!e || !e->res12 || e->dbroken || e->desc.Width != width || e->desc.Height != height || e->desc.Format != format ||
      (format != DXGI_FORMAT_B8G8R8A8_UNORM && format != DXGI_FORMAT_B8G8R8X8_UNORM) || pitch < width * 4 ||
      (!e->dlist && !create_direct_path(e))) {
    ReleaseSRWLockExclusive(&lock);
    return FALSE;
  }
  QueryPerformanceCounter(&t0);
  if (InterlockedExchange(&e->direct, 1) == 0)
    bridge_log_always("[D3DMETAL BRIDGE] direct frames start handle=%p %ux%u format=%u\n", e->handle, width, height, format);
  unsigned slot = (unsigned)(e->dfence_value & 1);
  if (ID3D12Fence_GetCompletedValue(e->dfence) < e->dslot_value[slot]) {  /* GPU still copying from this buffer */
    e->dwaits++;
    ID3D12Fence_SetEventOnCompletion(e->dfence, e->dslot_value[slot], e->devent);
    if (WaitForSingleObject(e->devent, 500) != WAIT_OBJECT_0) {
      ReleaseSRWLockExclusive(&lock);
      bridge_log("[D3DMETAL BRIDGE] direct upload buffer busy; frame skipped\n");
      return TRUE;  /* the texture keeps the previous frame */
    }
  }
  BYTE *out = e->dupload_ptr[slot] + e->footprint.Offset;
  UINT rp = e->footprint.Footprint.RowPitch;
  if (rp == pitch) memcpy(out, data, (size_t)pitch * (height - 1) + (size_t)width * 4);
  else for (UINT y = 0; y < height; y++) memcpy(out + (size_t)y * rp, (const BYTE *)data + (size_t)y * pitch, (size_t)width * 4);
  D3D12_TEXTURE_COPY_LOCATION to = {e->res12, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {.SubresourceIndex = 0}};
  D3D12_TEXTURE_COPY_LOCATION from = {e->dupload[slot], D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {.PlacedFootprint = e->footprint}};
  ID3D12CommandAllocator_Reset(e->dalloc[slot]);
  ID3D12GraphicsCommandList_Reset(e->dlist, e->dalloc[slot], NULL);
  ID3D12GraphicsCommandList_CopyTextureRegion(e->dlist, &to, 0, 0, 0, &from, NULL);
  if (FAILED(hr = ID3D12GraphicsCommandList_Close(e->dlist))) {
    e->dbroken = TRUE;
    ReleaseSRWLockExclusive(&lock);
    bridge_log_always("[D3DMETAL BRIDGE] direct list close failed %08lx\n", (unsigned long)hr);
    return FALSE;
  }
  ID3D12CommandList *lists[] = {(ID3D12CommandList *)e->dlist};
  ID3D12CommandQueue_ExecuteCommandLists(e->dqueue, 1, lists);
  e->dslot_value[slot] = ++e->dfence_value;
  ID3D12CommandQueue_Signal(e->dqueue, e->dfence, e->dfence_value);
  QueryPerformanceCounter(&t1);
  e->dcopy_sum += t1.QuadPart - t0.QuadPart; e->dcopy_n++;
  LONG n = ++e->dframes;
  ULONGLONG now = GetTickCount64();
  if (now - e->drate_tick >= 10000) {
    LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);
    if (e->drate_tick)
      bridge_log_always("[D3DMETAL BRIDGE] direct rate t=%llu frames_fps=%.1f submit_us=%lld buffer_waits=%ld queue=%s\n", now,
                        (n - e->drate_frames) * 1000.0 / (now - e->drate_tick),
                        e->dcopy_n ? e->dcopy_sum * 1000000 / freq.QuadPart / e->dcopy_n : 0, e->dwaits, e->dgame_queue ? "game" : "own");
    e->drate_tick = now; e->drate_frames = n; e->dcopy_sum = 0; e->dcopy_n = 0;
  }
  ReleaseSRWLockExclusive(&lock);
  return TRUE;
}

void shared_bridge_install(ID3D12Device *device12) {
  static LONG d3d11_state;  /* 0 = not tried, 1 = installed, 2 = failed */
  if (!device12 || !bridge_enabled()) return;
  if (reentry_tls == TLS_OUT_OF_INDEXES) reentry_tls = TlsAlloc();
  if (!patch_slot((void **)&device12->lpVtbl->OpenSharedHandle, (void *)hook_open_shared_handle,
                  (void **)&real_open_shared_handle)) {
    bridge_log("[D3DMETAL BRIDGE] OpenSharedHandle hook failed\n");
    return;
  }
  if (!patch_slot((void **)&device12->lpVtbl->CreateCommandQueue, (void *)hook_create_queue, (void **)&real_create_queue))
    bridge_log_always("[D3DMETAL BRIDGE] CreateCommandQueue hook failed\n");
  if (InterlockedCompareExchange(&d3d11_state, 3, 0) == 0) {
    BOOL ok = install_d3d11_hooks();
    InterlockedExchange(&d3d11_state, ok ? 1 : 2);
    bridge_log("[D3DMETAL BRIDGE] installed d3d12=1 d3d11=%d\n", ok);
  }
}
