#!/usr/bin/env python3
"""Package a MetalFlight runtime release: metalflight-runtime-<version>.tar.xz + metalflight-manifest.json.

Inputs (all explicit; nothing is downloaded):
  --wine DIR      an assembled from-source Wine runtime (bin/, lib/wine/, lib64/, share/wine/)
  --inputs DIR    staged extras:
                    mullion/lib/wine/x86_64-windows/*.dll, mullion/lib/wine/x86_64-unix/winemetal.so
                    mullion/system32/*.dll           (copied into the prefix's system32 at launch)
                    helper/split_push_helper.dll     (portable CPU helper)
                    probes/steam_session.exe         (Steam SDK readiness probe)
                    licenses/<component>/...         (licence texts of everything shipped)
  --profile FILE  runtime/profile.json
The runtime drops Wine's developer tools and import libraries, adds the inputs under
share/metalflight/, writes a per-file SHA-256 inventory, and archives the directory as a single
top-level folder. The manifest records the archive URL (--base-url + file name), size and SHA-256.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, sys, time

KEEP_BIN = {'wine', 'wineserver', 'wine-preloader', 'wine64-preloader'}


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 22), b''): h.update(block)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--wine', type=Path, required=True); ap.add_argument('--inputs', type=Path, required=True)
    ap.add_argument('--profile', type=Path, required=True); ap.add_argument('--version', required=True)
    ap.add_argument('--app-version', default='0.1.0'); ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--base-url', required=True, help='e.g. https://github.com/<owner>/metalflight/releases/download/<tag>/')
    ap.add_argument('--strip-debug', action='store_true', help='Strip DWARF from Unix modules and PE files (smaller download)')
    ap.add_argument('--source-lock', type=Path, help='Corresponding source repository commits for this build')
    a = ap.parse_args()
    name = f'metalflight-runtime-{a.version}'
    a.output.mkdir(parents=True, exist_ok=True)
    root = a.output / name
    if root.exists(): shutil.rmtree(root)
    subprocess.run(['/bin/cp', '-R', str(a.wine), str(root)], check=True)

    for p in (root / 'bin').iterdir():
        if p.name not in KEEP_BIN: p.unlink()
    for arch in ('x86_64-windows', 'i386-windows'):
        for p in (root / 'lib/wine' / arch).glob('*.a'): p.unlink()
    for extra in ('runtime-manifest.json', 'verify.json'):
        (root / extra).unlink(missing_ok=True)
    shutil.rmtree(root / 'share/man', ignore_errors=True); shutil.rmtree(root / 'include', ignore_errors=True)

    mullion = a.inputs / 'mullion'
    for p in (mullion / 'lib/wine').rglob('*'):
        if p.is_file(): shutil.copy2(p, root / 'lib/wine' / p.relative_to(mullion / 'lib/wine'))
    share = root / 'share/metalflight'; share.mkdir(parents=True)
    shutil.copytree(mullion / 'system32', share / 'system32')
    shutil.copytree(a.inputs / 'helper', share / 'helper')
    shutil.copytree(a.inputs / 'probes', share / 'probes')
    shutil.copytree(a.inputs / 'licenses', share / 'licenses')
    profile = json.loads(a.profile.read_text())
    for rel in profile['system32'] + [profile['helper'], profile['readinessProbe']]:
        if not (root / rel).is_file(): raise SystemExit(f'profile references a missing file: {rel}')
    shutil.copy2(a.profile, share / 'profile.json')
    if a.source_lock:
        shutil.copy2(a.source_lock, share / 'sources.lock.json')

    if a.strip_debug:
        for p in (root / 'lib/wine/x86_64-unix').glob('*.so'): subprocess.run(['strip', '-S', str(p)], check=True)
        for arch in ('x86_64-windows', 'i386-windows'):
            for p in (root / 'lib/wine' / arch).iterdir():
                if p.suffix in ('.dll', '.exe', '.drv', '.sys', '.ocx', '.acm', '.ax', '.cpl', '.tlb', '.ds', '.com'):
                    subprocess.run(['x86_64-w64-mingw32-strip' if arch.startswith('x86_64') else 'i686-w64-mingw32-strip', '--strip-debug', str(p)],
                                   check=False, capture_output=True)

    files = {str(p.relative_to(root)): sha(p) for p in sorted(root.rglob('*')) if p.is_file() and not p.is_symlink()}
    (share / 'inventory.json').write_text(json.dumps(dict(version=a.version, created_unix=int(time.time()), files=files), indent=2) + '\n')

    archive = a.output / f'{name}.tar.xz'
    archive.unlink(missing_ok=True)
    env = dict(os.environ, XZ_OPT='-6 -T0', COPYFILE_DISABLE='1')
    subprocess.run(['/usr/bin/tar', '--no-mac-metadata', '-cJf', str(archive), '-C', str(a.output), name], check=True, env=env)
    manifest = dict(schema=1, appVersion=a.app_version,
                    runtime=dict(version=a.version, url=a.base_url.rstrip('/') + '/' + archive.name, sha256=sha(archive), size=archive.stat().st_size))
    (a.output / 'metalflight-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'{archive.name}: {archive.stat().st_size / 1e6:.0f} MB, {len(files)} files')
    return 0


if __name__ == '__main__':
    sys.exit(main())
