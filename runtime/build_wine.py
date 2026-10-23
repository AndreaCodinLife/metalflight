#!/usr/bin/env python3
"""Full build of the metalflight-wine tree (METALFLIGHT_WINE_TREE, default work/metalflight-wine) (i386 + x86_64 PE, x86_64 Unix) and assembly of the runtime layout.

Compilers are the ones the verified components used: Apple clang for x86_64 Unix code and Homebrew's
mingw-w64 GCC (x86_64 and i686) for PE code, found by configure on PATH. Libraries come only from
work/deps/prefix (pkg-config is restricted to it). Unix modules get an extra rpath to the
runtime's lib64. After `make`, msfs24/ucrt_wcs_probe.py relinks x86_64 ucrtbase.dll with the
formatter probe, then `make install` runs into a staging root.

Runtime layout (work/runtime/<label>):
  bin/            wine, wineserver, wine-preloader...
  lib/wine/       x86_64-unix, x86_64-windows, i386-windows
  lib64/          the dependency dylibs; lib64/gstreamer-1.0/ the plugins
  share/wine/     wine.inf, nls, fonts
Nothing from CrossOver's binary distribution is copied. Steps can be selected with --steps.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, sys, time

ROOT = Path(__file__).resolve().parent  # runtime/
TREE = Path(os.environ.get('METALFLIGHT_WINE_TREE', ROOT / 'work/metalflight-wine'))  # clone of metalflight-wine
DEPS = ROOT / 'work/deps/prefix'
WORK = ROOT / 'work/wine'
BUILD, STAGE, LOGS = WORK / 'build', WORK / 'stage', WORK / 'logs'
SRC_ALIAS = '../sources/wine-platform'  # srcdir as seen by the verified build-platform build
STEPS = ('configure', 'make', 'probe', 'install', 'assemble', 'verify')


def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 22), b''): h.update(b)
    return h.hexdigest()


def env():
    e = {k: v for k, v in os.environ.items() if not k.startswith(('PKG_CONFIG', 'CFLAGS', 'LDFLAGS', 'CPPFLAGS', 'CC', 'CXX', 'DYLD_', 'WINE'))}
    e.update(PATH='/opt/homebrew/opt/bison/bin:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin',
             PKG_CONFIG_LIBDIR=f'{DEPS}/lib/pkgconfig', PKG_CONFIG_PATH='',
             DYLD_FALLBACK_LIBRARY_PATH=f'{DEPS}/lib', MACOSX_DEPLOYMENT_TARGET='14.0')
    return e


def run(step, cmd, cwd):
    LOGS.mkdir(parents=True, exist_ok=True)
    with (LOGS / f'{step}.log').open('a') as log:
        log.write('\n$ ' + ' '.join(map(str, cmd)) + '\n'); log.flush()
        r = subprocess.run(list(map(str, cmd)), cwd=cwd, env=env(), stdout=log, stderr=subprocess.STDOUT)
    if r.returncode: raise SystemExit(f'{step} failed ({r.returncode}); see {LOGS / (step + ".log")}')


def build_tool_lib64():
    """Build-time tools (sfnt2fon, ...) run from BUILD/tools/<tool>/ and resolve @loader_path/../../../lib64,
    i.e. WORK/lib64. macOS strips DYLD_* through /usr/bin/make, so give them that directory instead."""
    link = WORK / 'lib64'
    if not link.is_symlink(): link.symlink_to(DEPS / 'lib')


def configure(args):
    if BUILD.exists() and not args.keep_build: shutil.rmtree(BUILD)
    BUILD.mkdir(parents=True, exist_ok=True)
    build_tool_lib64()
    run('configure', [TREE / 'configure', '--prefix=/', '--enable-archs=i386,x86_64', '--host=x86_64-apple-darwin',
                      '--disable-tests', '--without-x', '--without-vulkan', '--without-wayland', '--without-capi',
                      '--without-sane', '--without-gphoto', '--without-v4l2', '--without-pulse', '--without-alsa',
                      '--without-oss', '--without-ffmpeg', '--without-usb', '--without-hwloc', '--without-unwind',
                      'CC=/usr/bin/clang -arch x86_64', 'CXX=/usr/bin/clang++ -arch x86_64',
                      # Source paths are embedded (__FILE__ in asserts and critical-section names). Map them to the
                      # path of the verified build so the layout is machine-independent and matches the
                      # verified modules, whose addresses the CPU helper's site tables depend on.
                      f'CFLAGS=-O2 -g -mmacosx-version-min=14.0 -ffile-prefix-map={TREE}={SRC_ALIAS}',
                      f'CROSSCFLAGS=-g -O2 -ffile-prefix-map={TREE}={SRC_ALIAS}',
                      f'LDFLAGS=-L{DEPS}/lib -Wl,-rpath,@loader_path/../../../lib64 -Wl,-headerpad_max_install_names',
                      'BISON=/opt/homebrew/opt/bison/bin/bison'], BUILD)
    summary = [l for l in (BUILD / 'config.log').read_text(errors='replace').splitlines() if l.startswith('configure: ') and ('not found' in l or "won't" in l)]
    (WORK / 'configure-notes.txt').write_text('\n'.join(summary) + '\n')


def make(args):
    build_tool_lib64()
    run('make', ['make', f'-j{args.jobs}'], BUILD)


def probe(args):
    run('probe', ['python3', TREE / 'msfs24/ucrt_wcs_probe.py', BUILD], BUILD)


def install(args):
    if STAGE.exists(): shutil.rmtree(STAGE)
    run('install', ['make', 'install', f'DESTDIR={STAGE}'], BUILD)


def assemble(args):
    out = ROOT / 'work/runtime' / args.label
    if out.exists(): shutil.rmtree(out)
    out.mkdir(parents=True)
    for d in ('bin', 'lib', 'share'):
        if (STAGE / d).exists(): subprocess.run(['/bin/cp', '-R', str(STAGE / d), str(out / d)], check=True)
    lib64 = out / 'lib64'; (lib64 / 'gstreamer-1.0').mkdir(parents=True)
    for p in (DEPS / 'lib').iterdir():
        if p.suffix == '.dylib':
            target = lib64 / p.name
            if p.is_symlink(): target.symlink_to(os.readlink(p))
            else: shutil.copy2(p, target)
    for p in (DEPS / 'lib/gstreamer-1.0').glob('*.dylib'): shutil.copy2(p, lib64 / 'gstreamer-1.0' / p.name)
    for unused in ('include', 'man'):
        if (out / 'share' / unused).exists(): shutil.rmtree(out / 'share' / unused)
    files = {str(p.relative_to(out)): sha(p) for p in sorted(out.rglob('*')) if p.is_file() and not p.is_symlink()}
    commit = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=TREE, capture_output=True, text=True, check=True).stdout.strip()
    deps = json.loads((ROOT / 'work/deps/receipt.json').read_text())
    (out / 'runtime-manifest.json').write_text(json.dumps(dict(
        label=args.label, created_unix=time.time(), wine_tree_commit=commit, deps_receipt_builder=deps['builder_sha256'],
        builder_sha256=sha(Path(__file__)), files=files), indent=2) + '\n')
    print('assembled', out, len(files), 'files')


def verify(args):
    out = ROOT / 'work/runtime' / args.label
    problems = []
    for p in list((out / 'lib/wine/x86_64-unix').glob('*.so')) + [out / 'bin/wineserver', out / 'bin/wine']:
        if not p.exists(): problems.append(f'missing {p.name}'); continue
        archs = subprocess.run(['lipo', '-archs', str(p)], capture_output=True, text=True).stdout.strip()
        if archs != 'x86_64': problems.append(f'{p.name}: {archs}')
        for line in subprocess.run(['otool', '-L', str(p)], capture_output=True, text=True).stdout.splitlines()[1:]:
            dep = line.strip().split(' (')[0]
            if dep.startswith('@rpath/') and dep != f'@rpath/{p.name}':
                leaf = dep[len('@rpath/'):]
                if not ((p.parent / leaf).exists() or (out / 'lib64' / leaf).exists()): problems.append(f'{p.name}: unresolved {dep}')
            elif dep.startswith('/') and not dep.startswith(('/usr/lib/', '/System/')): problems.append(f'{p.name}: absolute {dep}')
    for arch, sub in (('x86_64', 'x86_64-windows'), ('i386', 'i386-windows')):
        n = len(list((out / 'lib/wine' / sub).glob('*.dll')))
        if n < 500: problems.append(f'only {n} {arch} PE dlls')
    print('verify:', 'OK' if not problems else problems)
    (out / 'verify.json').write_text(json.dumps(dict(ok=not problems, problems=problems), indent=2) + '\n')
    return 0 if not problems else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--steps', nargs='*', default=list(STEPS), choices=STEPS)
    ap.add_argument('--jobs', type=int, default=8); ap.add_argument('--label', default='msfs24-wine-dev')
    ap.add_argument('--keep-build', action='store_true', help='Do not wipe an existing build directory on configure')
    args = ap.parse_args()
    assert not subprocess.run(['git', 'status', '--porcelain'], cwd=TREE, capture_output=True, text=True).stdout, 'msfs24-wine has uncommitted changes'
    for step in STEPS:
        if step in args.steps:
            t = time.time(); print('step', step, flush=True)
            r = globals()[step](args)
            print(f'  {step} done in {time.time() - t:.0f} s', flush=True)
            if r: return r
    return 0


if __name__ == '__main__':
    sys.exit(main())
