#pragma once
/* Makes D3DMetal IDXGIOutput::WaitForVBlank return immediately, as Mullion does (see vblank.c). */
void vblank_install(void);
