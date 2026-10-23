#!/usr/bin/env python3
"""Build the runtime's x86_64 library dependencies from source into one staging prefix.

Sources: the CrossOver 26.3 source release (work/crossover-sources/sources, from fetch_sources.py: gmp, nettle, gnutls,
FreeType, GLib 2.78, the GStreamer 1.24.4 monorepo) and official archives in
downloads/ (pcre2, libffi, proxy-libintl, SDL2, nettle, GLib; hashes pinned below). Each
source is copied into work/deps/src before configuring, so no source tree is modified.

The compiler is Apple clang for x86_64 with macOS 14 as the minimum. pkg-config sees only the
staging prefix, so Homebrew's arm64 libraries cannot leak in. Configure tests run under Rosetta.
After all packages are installed, every dylib and GStreamer plugin gets an @rpath install name
and @rpath references, for the runtime layout lib64/ and lib64/gstreamer-1.0/.

usage: build_msfs24_deps.py [--only NAME ...] [--from NAME] [--jobs N] [--fixup-only]
"""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, subprocess, sys, tarfile, time

ROOT = Path(__file__).resolve().parent  # runtime/
CW = ROOT / 'work/crossover-sources/sources'  # extracted by fetch_sources.py
DL = ROOT / 'downloads'
WORK = ROOT / 'work/deps'
SRC, BLD, PREFIX, LOGS = WORK / 'src', WORK / 'build', WORK / 'prefix', WORK / 'logs'
ARCHIVES = {
    'pcre2-10.42.tar.bz2': '8d36cd8cb6ea2a4c2bb358ff6411b0c788633a2a45dabbf1aeb4b701d1b5e840',
    'libffi-3.4.6.tar.gz': 'b0dea9df23c863a7a50e825440f3ebffabd65df1497108e5d437747843895a4e',
    'proxy-libintl-0.4.tar.gz': '13ef3eea0a3bc0df55293be368dfbcff5a8dd5f4759280f28e030d1494a5dffb',
    'SDL2-2.30.11.tar.gz': '8b8d4aef2038533da814965220f88f77d60dfa0f32685f80ead65e501337da7f',
    # CrossOver's nettle is a Git export without Makefile.in/configure; every file the two share is identical.
    'nettle-3.10.tar.gz': 'b4c518adb174e484cb4acea54118f02380c7133771e7e9beb98a0787194ee47c',
    # CrossOver's GLib lacks the gvdb submodule; the release adds only subprojects/gvdb (all 2071 shared files equal).
    'glib-2.78.0.tar.xz': '44eaab8b720877ce303c5540b657b126f12dc94972d9880b52959f43fb537b30',
}
MIN = '-mmacosx-version-min=14.0'
CC = f'/usr/bin/clang -arch x86_64 {MIN}'
CXX = f'/usr/bin/clang++ -arch x86_64 {MIN}'
HOST = 'x86_64-apple-darwin'
GST = CW / 'gstreamer/subprojects'


def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def env():
    e = {k: v for k, v in os.environ.items() if not k.startswith(('PKG_CONFIG', 'CFLAGS', 'LDFLAGS', 'CPPFLAGS', 'CC', 'CXX'))}
    # Homebrew's bison (keg-only) because GStreamer needs >= 2.4; macOS ships 2.3. Build-time tool only.
    e.update(PATH=f'{PREFIX}/bin:/opt/homebrew/opt/bison/bin:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin', CC=CC, CXX=CXX, OBJC=CC,
             CFLAGS='-O2', CXXFLAGS='-O2', CPPFLAGS=f'-I{PREFIX}/include',
             LDFLAGS=f'-L{PREFIX}/lib -Wl,-headerpad_max_install_names',
             PKG_CONFIG_LIBDIR=f'{PREFIX}/lib/pkgconfig:{PREFIX}/share/pkgconfig', PKG_CONFIG_PATH='',
             MACOSX_DEPLOYMENT_TARGET='14.0',
             # Build-time x86_64 tools (glib-compile-resources, ...) find staged libraries whose ids are @rpath.
             DYLD_FALLBACK_LIBRARY_PATH=f'{PREFIX}/lib')
    return e


def cross_file():
    path = WORK / 'x86_64-darwin.ini'
    path.write_text(f"""[binaries]
c = ['/usr/bin/clang', '-arch', 'x86_64']
cpp = ['/usr/bin/clang++', '-arch', 'x86_64']
objc = ['/usr/bin/clang', '-arch', 'x86_64']
objcpp = ['/usr/bin/clang++', '-arch', 'x86_64']
ar = '/usr/bin/ar'
strip = '/usr/bin/strip'
pkg-config = '/opt/homebrew/bin/pkg-config'

[built-in options]
c_args = ['{MIN}', '-I{PREFIX}/include']
cpp_args = ['{MIN}', '-I{PREFIX}/include']
objc_args = ['{MIN}', '-I{PREFIX}/include']
c_link_args = ['{MIN}', '-L{PREFIX}/lib', '-Wl,-headerpad_max_install_names']
cpp_link_args = ['{MIN}', '-L{PREFIX}/lib', '-Wl,-headerpad_max_install_names']
objc_link_args = ['{MIN}', '-L{PREFIX}/lib', '-Wl,-headerpad_max_install_names']
pkg_config_path = '{PREFIX}/lib/pkgconfig'

[properties]
needs_exe_wrapper = false
pkg_config_libdir = '{PREFIX}/lib/pkgconfig'

[host_machine]
system = 'darwin'
subsystem = 'macos'
kernel = 'xnu'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
""")
    return path


def run(name, cmd, cwd):
    LOGS.mkdir(parents=True, exist_ok=True)
    with (LOGS / f'{name}.log').open('a') as log:
        log.write('\n$ ' + ' '.join(map(str, cmd)) + f'\n  (cwd {cwd})\n'); log.flush()
        r = subprocess.run(list(map(str, cmd)), cwd=cwd, env=env(), stdout=log, stderr=subprocess.STDOUT)
    if r.returncode:
        raise SystemExit(f'{name}: command failed ({r.returncode}); see {LOGS / (name + ".log")}')


def fresh_copy(name, source):
    dest = SRC / name
    if dest.exists(): shutil.rmtree(dest)
    SRC.mkdir(parents=True, exist_ok=True)
    if source.is_dir():
        subprocess.run(['/bin/cp', '-cR', str(source), str(dest)], check=True)
    else:
        assert sha(source) == ARCHIVES[source.name], 'archive hash mismatch: ' + source.name
        tmp = SRC / (name + '.extract')
        if tmp.exists(): shutil.rmtree(tmp)
        tmp.mkdir()
        with tarfile.open(source) as t: t.extractall(tmp, filter='data')
        (inner,) = list(tmp.iterdir()); inner.rename(dest); tmp.rmdir()
    return dest


def autotools(name, source, args, bootstrap=None, make_targets=('install',), prepare=None):
    src = fresh_copy(name, source)
    if prepare: prepare(src)
    if bootstrap: run(name, bootstrap, src)
    build = BLD / name
    if build.exists(): shutil.rmtree(build)
    build.mkdir(parents=True)
    run(name, [src / 'configure', f'--prefix={PREFIX}', f'--host={HOST}', '--enable-shared', '--disable-static'] + args, build)
    run(name, ['make', f'-j{JOBS}'], build)
    for t in make_targets: run(name, ['make', t], build)


def cmake(name, source, options):
    src = fresh_copy(name, source)
    build = BLD / name
    if build.exists(): shutil.rmtree(build)
    run(name, ['cmake', '-S', src, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_INSTALL_PREFIX={PREFIX}',
               '-DCMAKE_INSTALL_LIBDIR=lib', '-DCMAKE_OSX_ARCHITECTURES=x86_64', '-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0',
               f'-DCMAKE_PREFIX_PATH={PREFIX}', '-DCMAKE_FIND_FRAMEWORK=LAST', '-DBUILD_SHARED_LIBS=ON',
               '-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew'] + options, WORK)
    run(name, ['cmake', '--build', build, '-j', str(JOBS)], WORK)
    run(name, ['cmake', '--install', build], WORK)


def meson(name, source, options, subprojects=(), prepare=None):
    src = fresh_copy(name, source)
    if prepare: prepare(src)
    for sub in subprojects:  # local subproject directories, used instead of downloading wraps
        target = src / 'subprojects' / sub.name
        if target.exists(): shutil.rmtree(target)
        subprocess.run(['/bin/cp', '-cR', str(sub), str(target)], check=True)
    build = BLD / name
    if build.exists(): shutil.rmtree(build)
    run(name, ['meson', 'setup', build, src, '--cross-file', cross_file(), f'--prefix={PREFIX}', '--libdir=lib',
               '--buildtype=release', '--wrap-mode=nodownload', '-Ddefault_library=shared', '-Db_ndebug=true'] + options, WORK)
    run(name, ['meson', 'compile', '-C', build, '-j', str(JOBS)], WORK)
    run(name, ['meson', 'install', '-C', build], WORK)


def gst_disabled_except(source, enabled):
    """Turn every feature option of a GStreamer module off, then enable the listed ones."""
    import re
    text = next(p for p in (source / 'meson.options', source / 'meson_options.txt') if p.exists()).read_text()
    names = set(re.findall(r"option\(\s*'([^']+)'\s*,\s*type\s*:\s*'feature'", text))
    return ['-Dauto_features=disabled'] + [f'-D{n}=enabled' for n in enabled if n in names] + \
           [f'-D{n}=disabled' for n in ('tests', 'examples', 'nls', 'doc', 'introspection', 'orc') if n in names]


PACKAGES = {}
def package(fn): PACKAGES[fn.__name__] = fn; return fn


@package
def gmp(): autotools('gmp', CW / 'gnutls/gmp', ['--enable-fat'])
@package
def nettle(): autotools('nettle', DL / 'nettle-3.10.tar.gz', ['--disable-documentation', '--disable-openssl', f'--with-include-path={PREFIX}/include', f'--with-lib-path={PREFIX}/lib'])
@package
def gnutls(): autotools('gnutls', CW / 'gnutls/gnutls', ['--with-included-libtasn1', '--with-included-unistring', '--without-p11-kit', '--without-idn', '--without-brotli',
                                                        '--without-zstd', '--without-tpm', '--without-tpm2', '--disable-doc', '--disable-tests', '--disable-tools',
                                                        '--disable-nls', '--disable-cxx', '--disable-guile', '--without-zlib',
                                                        # gnutls 3.8.3's bundled GOST code redefines streebog, which nettle 3.10 now provides.
                                                        '--disable-gost',
                                                        # CrossOver's configure does not query pkg-config for these; it expects them given.
                                                        # It tests `x$NETTLE_LIBS` unquoted, so the values must not contain spaces;
                                                        # LDFLAGS already carries -L for the prefix.
                                                        f'NETTLE_CFLAGS=-I{PREFIX}/include', 'NETTLE_LIBS=-lnettle',
                                                        f'HOGWEED_CFLAGS=-I{PREFIX}/include', 'HOGWEED_LIBS=-lhogweed',
                                                        f'GMP_CFLAGS=-I{PREFIX}/include', 'GMP_LIBS=-lgmp'])
@package
def freetype():
    # CrossOver's FreeType is a Git export without builds/unix/configure (autogen.sh needs automake),
    # so it is built with its own CMakeLists.txt.
    cmake('freetype', CW / 'freetype', ['-DFT_DISABLE_ZLIB=TRUE', '-DFT_DISABLE_BZIP2=TRUE', '-DFT_DISABLE_PNG=TRUE',
                                         '-DFT_DISABLE_HARFBUZZ=TRUE', '-DFT_DISABLE_BROTLI=TRUE'])
@package
def libffi(): autotools('libffi', DL / 'libffi-3.4.6.tar.gz', ['--disable-docs', '--disable-multi-os-directory'])
@package
def pcre2(): autotools('pcre2', DL / 'pcre2-10.42.tar.bz2', ['--enable-pcre2-8', '--disable-pcre2-16', '--disable-pcre2-32', '--disable-pcre2grep-libz', '--disable-pcre2grep-libbz2'])
@package
def proxy_libintl(): meson('proxy-libintl', DL / 'proxy-libintl-0.4.tar.gz', [])
def glib_without_distutils(src):
    """gdbus-codegen sorts doc 'since' versions with distutils.version.LooseVersion; Python 3.12+ has no
    distutils. Replace it with the same numeric/alphabetic ordering (the generated code is unaffected)."""
    path = src / 'gio/gdbus-2.0/codegen/utils.py'
    s = path.read_text()
    assert s.count('import distutils.version\n') == 1 and s.count('distutils.version.LooseVersion(v)') == 1
    s = s.replace('import distutils.version\n', 'import re\n').replace(
        'distutils.version.LooseVersion(v)',
        "[(0, int(c), '') if c.isdigit() else (1, 0, c) for c in re.findall(r'\\d+|[a-z]+', v.lower())]")
    path.write_text(s)


@package
def glib(): meson('glib', DL / 'glib-2.78.0.tar.xz', prepare=glib_without_distutils, options=['-Dtests=false', '-Dnls=disabled', '-Dselinux=disabled', '-Dxattr=false', '-Dlibmount=disabled', '-Ddtrace=false',
                                       '-Dsystemtap=false', '-Dsysprof=disabled', '-Dman=false', '-Dgtk_doc=false', '-Dlibelf=disabled', '-Doss_fuzz=disabled',
                                       '-Dbsymbolic_functions=false', '-Dglib_debug=disabled'])
@package
def gstreamer(): meson('gstreamer', GST / 'gstreamer', ['-Dtests=disabled', '-Dexamples=disabled', '-Dbenchmarks=disabled', '-Dtools=enabled', '-Dintrospection=disabled',
                                                       '-Dnls=disabled', '-Dlibunwind=disabled', '-Dlibdw=disabled', '-Ddbghelp=disabled', '-Dbash-completion=disabled',
                                                       '-Dcheck=enabled', '-Dptp-helper=disabled', '-Ddoc=disabled', '-Dcoretracers=disabled', '-Dgst_debug=true'])
@package
def gst_plugins_base(): meson('gst-plugins-base', GST / 'gst-plugins-base',
                              gst_disabled_except(GST / 'gst-plugins-base', ['app', 'audioconvert', 'audioresample', 'playback', 'typefind', 'videoconvertscale', 'videorate', 'volume', 'audiotestsrc', 'videotestsrc', 'gl'])
                              # GstGL: applemedia (VideoToolbox decode) in -bad requires it.
                              + ['-Dgl_api=opengl', '-Dgl_platform=cgl', '-Dgl_winsys=cocoa'])
@package
def gst_plugins_good(): meson('gst-plugins-good', GST / 'gst-plugins-good',
                              gst_disabled_except(GST / 'gst-plugins-good', ['isomp4', 'id3demux', 'wavparse', 'avi', 'audioparsers', 'deinterlace', 'videofilter', 'apetag', 'icydemux']))
@package
def gst_plugins_bad(): meson('gst-plugins-bad', GST / 'gst-plugins-bad',
                             gst_disabled_except(GST / 'gst-plugins-bad', ['videoparsers', 'applemedia', 'id3tag', 'gl']))
@package
def gst_plugins_ugly(): meson('gst-plugins-ugly', GST / 'gst-plugins-ugly', gst_disabled_except(GST / 'gst-plugins-ugly', ['asfdemux']))
@package
def sdl2(): autotools('SDL2', DL / 'SDL2-2.30.11.tar.gz', ['--disable-video-x11', '--disable-video-wayland', '--disable-video-opengles', '--disable-audio', '--disable-render',
                                                         '--enable-joystick', '--enable-haptic', '--enable-hidapi', '--enable-sensor'])


def fixup():
    """@rpath ids and references for every Mach-O library in lib/ and lib/gstreamer-1.0/."""
    libs = [p for p in (PREFIX / 'lib').glob('*.dylib') if not p.is_symlink()] + list((PREFIX / 'lib/gstreamer-1.0').glob('*.dylib'))
    prefix_lib = str(PREFIX / 'lib') + '/'
    changed = 0
    for lib in libs:
        out = subprocess.run(['otool', '-L', str(lib)], capture_output=True, text=True, check=True).stdout.splitlines()[1:]
        plugin = lib.parent.name == 'gstreamer-1.0'
        cmd = ['install_name_tool', '-id', '@rpath/' + ('gstreamer-1.0/' if plugin else '') + lib.name]
        for line in out[1:] if out and out[0].strip().startswith(str(lib)) else out:
            dep = line.strip().split(' (')[0]
            if dep.startswith(prefix_lib): cmd += ['-change', dep, '@rpath/' + Path(dep).name]
        loads = subprocess.run(['otool', '-l', str(lib)], capture_output=True, text=True, check=True).stdout
        rpaths = re.findall(r'cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (.+?) \(offset', loads)
        for r in rpaths:
            if r.startswith(str(PREFIX)): cmd += ['-delete_rpath', r]
        wanted = '@loader_path/..' if plugin else '@loader_path'
        if wanted not in rpaths: cmd += ['-add_rpath', wanted]
        if len(cmd) > 1:
            subprocess.run(cmd + [str(lib)], check=True, stderr=subprocess.DEVNULL); changed += 1
        subprocess.run(['codesign', '--force', '-s', '-', str(lib)], check=True, capture_output=True)
    return changed


def receipt():
    files = {}
    for p in sorted(PREFIX.rglob('*')):
        if p.is_file() and not p.is_symlink() and (p.suffix == '.dylib' or p.parent.name == 'gstreamer-1.0'):
            info = subprocess.run(['lipo', '-archs', str(p)], capture_output=True, text=True).stdout.strip()
            files[str(p.relative_to(PREFIX))] = dict(sha256=sha(p), archs=info)
    data = dict(created_unix=time.time(), prefix=str(PREFIX), archives=ARCHIVES, crossover_sources=str(CW), compiler=CC,
                builder_sha256=sha(Path(__file__)), libraries=files)
    (WORK / 'receipt.json').write_text(json.dumps(data, indent=2) + '\n')
    return data


def main():
    global JOBS
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--only', nargs='*'); ap.add_argument('--from', dest='start'); ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--fixup-only', action='store_true')
    args = ap.parse_args(); JOBS = args.jobs
    names = list(PACKAGES)
    if args.only: names = [n for n in names if n in args.only]
    if args.start: names = names[names.index(args.start):]
    if not args.fixup_only:
        for n in names:
            t = time.time(); print('building', n, flush=True); PACKAGES[n]()
            print(f'  done in {time.time() - t:.0f} s', flush=True)
    partial = (args.only or args.start) and not args.fixup_only
    if partial and names[-1] != list(PACKAGES)[-1]:
        print('partial build: @rpath fixup deferred until the last package'); return 0
    print('fixed up', fixup(), 'libraries')
    r = receipt()
    bad = [k for k, v in r['libraries'].items() if v['archs'] != 'x86_64']
    print(len(r['libraries']), 'libraries;', 'non-x86_64:', bad or 'none')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
