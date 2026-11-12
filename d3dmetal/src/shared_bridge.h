#pragma once
#include <d3d12.h>
/* Installs the legacy shared-texture bridge (see shared_bridge.c) on a device D3DMetal created. */
void shared_bridge_install(ID3D12Device *device);
