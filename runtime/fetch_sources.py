#!/usr/bin/env python3
"""Download the pinned source archives the runtime is built from, verify their SHA-256, and extract
CodeWeavers' CrossOver source release into work/crossover-sources.

All URLs are the projects' own release locations. The Wine tree itself is not here: clone
AndreaCodinLife/metalflight-wine into work/metalflight-wine (or set METALFLIGHT_WINE_TREE).
"""
from pathlib import Path
import hashlib, subprocess, sys, tarfile, urllib.request

ROOT = Path(__file__).resolve().parent
DL = ROOT / 'downloads'
ARCHIVES = {
    'crossover-sources-26.3.0.tar.gz': ('https://media.codeweavers.com/pub/crossover/source/crossover-sources-26.3.0.tar.gz',
                                        'ac99c8ca4b3848f3e81784135f023df266b61c2345726ea55a50b3e030dd6872'),
    'glib-2.78.0.tar.xz': ('https://download.gnome.org/sources/glib/2.78/glib-2.78.0.tar.xz',
                           '44eaab8b720877ce303c5540b657b126f12dc94972d9880b52959f43fb537b30'),
    'nettle-3.10.tar.gz': ('https://ftp.gnu.org/gnu/nettle/nettle-3.10.tar.gz',
                           'b4c518adb174e484cb4acea54118f02380c7133771e7e9beb98a0787194ee47c'),
    'pcre2-10.42.tar.bz2': ('https://github.com/PhilipHazel/pcre2/releases/download/pcre2-10.42/pcre2-10.42.tar.bz2',
                            '8d36cd8cb6ea2a4c2bb358ff6411b0c788633a2a45dabbf1aeb4b701d1b5e840'),
    'libffi-3.4.6.tar.gz': ('https://github.com/libffi/libffi/releases/download/v3.4.6/libffi-3.4.6.tar.gz',
                            'b0dea9df23c863a7a50e825440f3ebffabd65df1497108e5d437747843895a4e'),
    'proxy-libintl-0.4.tar.gz': ('https://github.com/frida/proxy-libintl/archive/refs/tags/0.4.tar.gz',
                                 '13ef3eea0a3bc0df55293be368dfbcff5a8dd5f4759280f28e030d1494a5dffb'),
    'SDL2-2.30.11.tar.gz': ('https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-2.30.11.tar.gz',
                            '8b8d4aef2038533da814965220f88f77d60dfa0f32685f80ead65e501337da7f'),
}


def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 22), b''): h.update(b)
    return h.hexdigest()


def main():
    DL.mkdir(exist_ok=True)
    for name, (url, digest) in ARCHIVES.items():
        path = DL / name
        if not (path.exists() and sha(path) == digest):
            print('downloading', url, flush=True)
            urllib.request.urlretrieve(url, path)
        if sha(path) != digest: raise SystemExit(f'{name}: SHA-256 mismatch')
        print('ok', name)
    target = ROOT / 'work/crossover-sources'
    if not (target / 'sources/glib').exists():
        target.mkdir(parents=True, exist_ok=True)
        with tarfile.open(DL / 'crossover-sources-26.3.0.tar.gz') as t: t.extractall(target, filter='data')
    print('CrossOver sources in', target)
    return 0


if __name__ == '__main__':
    sys.exit(main())
