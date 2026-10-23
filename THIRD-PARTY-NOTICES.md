# Third-party notices

Each runtime release ships these licence texts in `share/metalflight/licenses/<component>/`.
Corresponding source:
- Wine and Mullion: their own repositories, tagged per runtime release.
- The libraries: the pinned upstream archives in `runtime/fetch_sources.py`, built by
  `runtime/build_deps.py` with the options recorded there.

| Component | Version | Licence |
| --- | --- | --- |
| Wine (CodeWeavers CrossOver 26.3.0 source release, with MetalFlight patches) | 11.0-based | LGPL-2.1-or-later |
| Mullion (fork of DXMT by Feifan He for CodeWeavers) | MetalFlight build | LGPL-2.1 |
| GLib | 2.78.0 | LGPL-2.1-or-later |
| GStreamer core, -base, -good, -bad, -ugly (selected plugins) | 1.24.4 | LGPL-2.1-or-later |
| GnuTLS (library) | 3.8.3 | LGPL-2.1-or-later |
| Nettle | 3.10 | LGPL-3.0-or-later or GPL-2.0-or-later |
| GMP | CrossOver 26.3 copy | LGPL-3.0-or-later or GPL-2.0-or-later |
| FreeType | CrossOver 26.3 copy | FTL or GPL-2.0 |
| libffi | 3.4.6 | MIT |
| PCRE2 | 10.42 | BSD-3-Clause |
| proxy-libintl | 0.4 | LGPL-2.1-or-later |
| SDL2 | 2.30.11 | zlib |

Not distributed:
- **Apple's `libd3dshared.dylib`** (Game Porting Toolkit). Each user downloads the toolkit from Apple
  under Apple's licence, and MetalFlight copies the file locally.
- **Steam**: Valve's installer, downloaded from Valve at setup.
- **Microsoft Flight Simulator 2024**: the user's own copy.
- **Microsoft Visual C++ runtime**: installed by the game's own setup.
