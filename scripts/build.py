#!/usr/bin/env python3
"""Build Juicy and its pinned open-source dependencies in a local directory."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PINS = {
    'ogg': ('https://github.com/xiph/ogg.git', 'be05b13e98b048f0b5a0f5fa8ce514d56db5f822'),
    'vorbis': ('https://github.com/xiph/vorbis.git', '0657aee69dec8508a0011f47f3b69d7538e9d262'),
    'flac': ('https://github.com/xiph/flac.git', '1507800de4b70e21be71f38caa0d9079d0bc6e45'),
    'opus': ('https://github.com/xiph/opus.git', 'ddbe48383984d56acd9e1ab6a090c54ca6b735a6'),
    'sndfile': ('https://github.com/libsndfile/libsndfile.git', '72f6af15e8f85157bd622ed45b979025828b7001'),
    'fluidsynth': ('https://github.com/FluidSynth/fluidsynth.git', 'c3b244ddfd094489a5490beee013c621a504adf2'),
    'signalsmith-linear': ('https://github.com/Signalsmith-Audio/linear.git', '5668673560146a9cfe38c25315071e3fd68c8317'),
    'signalsmith-dsp': ('https://github.com/Signalsmith-Audio/dsp.git', '2d20161915e733f117545c6be8cd3275a739a1e3'),
    'signalsmith-hilbert': ('https://github.com/Signalsmith-Audio/hilbert-iir.git', '9e42e09714c8215d51eaa1b52a3d54ae8dd2a3a3'),
    'JUCE': ('https://github.com/juce-framework/JUCE.git', 'd60b7a78030661574a615b586d9325ba7a7fa7dd'),
}

def run(*args, **kwargs):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kwargs)

def source(name, directory):
    url, sha = PINS[name]
    path = directory / name
    if not path.exists():
        path.mkdir(parents=True)
        run('git', 'init', path)
        run('git', '-C', path, 'remote', 'add', 'origin', url)
        run('git', '-C', path, 'fetch', '--depth=1', 'origin', sha)
        run('git', '-C', path, 'checkout', '--detach', 'FETCH_HEAD')
    actual = clean_revision(path)
    if actual != sha:
        raise RuntimeError(f'{name}: expected {sha}, got {actual}; use a separate source directory')
    if name == 'fluidsynth':
        if (path / '.git').exists():
            run('git', '-C', path, 'submodule', 'update', '--init', '--depth=1', '--', 'gcem', 'signalsmith-audio-basics')
        else:
            for child in ['gcem', 'signalsmith-audio-basics']:
                if not clean_revision(path / child):
                    raise RuntimeError(f'Modified exported submodule: {child}')
    return path

def exported_revision(repository):
    """Verify recorded files in our own exported bundle, without Git metadata."""
    repository = Path(repository).resolve()
    bundle = ROOT.resolve().parent
    manifest_path = bundle / 'source-manifest.json'
    if not manifest_path.is_file():
        raise RuntimeError('No Git checkout or source-manifest.json; use a clone or an exported source bundle')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if manifest.get('format') != 1:
        raise RuntimeError('Unsupported source manifest format')
    try:
        name = repository.relative_to(bundle).as_posix()
        entry = manifest['repositories'][name]
    except (ValueError, KeyError) as error:
        raise RuntimeError(f'Source repository is not in the export manifest: {repository}') from error
    for relative, expected in entry['files'].items():
        path = repository / relative
        # The manifest is a path/hash index, never permission to escape the bundle.
        if Path(relative).is_absolute() or '..' in Path(relative).parts:
            raise RuntimeError('Unsafe path in source manifest')
        if 'symlink' in expected:
            if not path.is_symlink() or os.readlink(path) != expected['symlink']:
                return None
        elif not path.is_file() or path.is_symlink():
            return None
        else:
            digest = hashlib.sha256()
            with path.open('rb') as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b''):
                    digest.update(block)
            if digest.hexdigest() != expected['sha256']:
                return None
    return entry['revision']


def clean_revision(repository):
    if not (Path(repository) / '.git').exists():
        return exported_revision(repository)
    revision = subprocess.check_output(
        ['git', '-C', str(repository), 'rev-parse', 'HEAD'], text=True).strip()
    if subprocess.check_output(
        ['git', '-C', str(repository), 'status', '--porcelain', '--untracked-files=no'],
        text=True).strip():
        return None
    return revision


def desktop_products(build, system):
    artifacts = build / 'plugin' / 'JuicySFPlugin_artefacts' / 'Release'
    standalone_suffix = '.app' if system == 'Darwin' else '.exe'
    return (artifacts / 'VST3' / 'Fluid_Juicy_SF2.vst3',
            artifacts / 'Standalone' / ('Fluid_Juicy_SF2' + standalone_suffix))


def product_manifest(product):
    if not product.exists():
        raise RuntimeError(f'Missing build product: {product}')
    entries = product.rglob('*') if product.is_dir() else (product,)
    manifest = {}
    for entry in entries:
        name = entry.relative_to(product).as_posix()
        if entry.is_symlink():
            manifest[name] = {'symlink': os.readlink(entry)}
        elif entry.is_file():
            digest = hashlib.sha256()
            with entry.open('rb') as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                    digest.update(chunk)
            manifest[name] = {'sha256': digest.hexdigest()}
    if not manifest:
        raise RuntimeError(f'Empty build product: {product}')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=['arm64', 'x86_64'], default=None)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--source-dir', type=Path, default=None)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--generator', default=None)
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 2, 8))
    parser.add_argument('--verify-sources-only', action='store_true',
                        help='Validate the checkout/export and every pinned dependency without compiling')
    args = parser.parse_args()
    mac = platform.system() == 'Darwin'
    arch = args.arch or ('arm64' if mac else 'x86_64')
    if not mac and arch != 'x86_64':
        parser.error('This update targets macOS arm64; Windows validation is x64 only')
    build = args.build_dir.resolve() / arch
    record_path = build / 'build-record.json'
    if not args.verify_sources_only:
        record_path.unlink(missing_ok=True)
    revision = clean_revision(ROOT)
    prefix = build / 'prefix'
    exported_sources = ROOT.parent / 'dependencies'
    default_sources = exported_sources if (ROOT.parent / 'source-manifest.json').is_file() else args.build_dir.resolve() / 'sources'
    sources = (args.source_dir or default_sources).resolve()
    if args.verify_sources_only:
        if revision is None:
            raise RuntimeError('Modified plugin sources')
        for name in PINS:
            source(name, sources)
        if 'fluidsynth' in PINS and not (ROOT / '.git').exists():
            for name in ['gcem', 'signalsmith-audio-basics']:
                if not clean_revision(sources / 'fluidsynth' / name):
                    raise RuntimeError(f'Modified exported submodule: {name}')
        print('Plugin and all pinned dependency sources verified.')
        return
    common = ['-DCMAKE_BUILD_TYPE=Release', '-DBUILD_SHARED_LIBS=OFF',
              '-DCMAKE_POSITION_INDEPENDENT_CODE=ON', '-DCMAKE_INSTALL_LIBDIR=lib',
              f'-DCMAKE_INSTALL_PREFIX={prefix}', f'-DCMAKE_PREFIX_PATH={prefix}',
              '-DBUILD_TESTING=OFF', '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
              '-DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON']
    generator = args.generator or ('Unix Makefiles' if mac else 'Visual Studio 17 2022')
    gen = ['-G', generator]
    if generator.startswith('Visual Studio'):
        gen += ['-A', 'x64']
    if mac:
        common += [f'-DCMAKE_OSX_ARCHITECTURES={arch}', '-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0']
    paths = {}

    def dependency(name, options):
        paths[name] = source(name, sources)
        folder = build / name
        run(args.cmake, '-S', paths[name], '-B', folder, *gen, *common, *options)
        run(args.cmake, '--build', folder, '--config', 'Release', '--parallel', args.jobs)
        run(args.cmake, '--install', folder, '--config', 'Release')

    dependency('ogg', ['-DINSTALL_DOCS=OFF', '-DINSTALL_PKG_CONFIG_MODULE=ON'])
    dependency('vorbis', ['-DINSTALL_DOCS=OFF'])
    dependency('flac', ['-DBUILD_PROGRAMS=OFF', '-DBUILD_EXAMPLES=OFF', '-DBUILD_DOCS=OFF', '-DBUILD_CXXLIBS=OFF', '-DINSTALL_MANPAGES=OFF'])
    dependency('opus', ['-DOPUS_BUILD_PROGRAMS=OFF', '-DOPUS_BUILD_TESTING=OFF'])
    dependency('sndfile', ['-DBUILD_PROGRAMS=OFF', '-DBUILD_EXAMPLES=OFF',
                          f'-DCMAKE_PROJECT_libsndfile_INCLUDE={ROOT / "cmake/DependencyThreads.cmake"}',
                          '-DENABLE_EXTERNAL_LIBS=ON', '-DENABLE_MPEG=OFF',
                          '-DENABLE_CPACK=OFF', '-DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON', '-DCMAKE_DISABLE_FIND_PACKAGE_FLAC=OFF'])
    disabled = ['alsa', 'aufile', 'dbus', 'ipv6', 'jack', 'ladspa', 'midishare',
                'network', 'oss', 'dsound', 'wasapi', 'waveout', 'winmidi', 'sdl3',
                'pulseaudio', 'pipewire', 'readline', 'openmp', 'coreaudio', 'coremidi',
                'framework', 'systemd', 'portaudio', 'opensles', 'oboe']
    signal_options = []
    for name in ['signalsmith-linear', 'signalsmith-dsp', 'signalsmith-hilbert']:
        paths[name] = source(name, sources)
        signal_options.append(f'-DFETCHCONTENT_SOURCE_DIR_{name.upper()}={paths[name]}')
    dependency('fluidsynth', [f'-Denable-{x}=OFF' for x in disabled] +
               ['-Denable-libsndfile=ON', '-Denable-signalsmith=ON', '-Denable-threads=ON'] + signal_options)
    paths['JUCE'] = source('JUCE', sources)
    plugin = build / 'plugin'
    run(args.cmake, '-S', ROOT, '-B', plugin, *gen, *common,
        f'-DJUICY_JUCE_SOURCE={paths["JUCE"]}', '-DJUICY_BUILD_TESTS=ON',
        '-DJUICY_COPY_PLUGIN_AFTER_BUILD=OFF')
    run(args.cmake, '--build', plugin, '--config', 'Release', '--parallel', args.jobs,
        '--target', 'JuicySFPlugin_VST3', 'JuicySFPlugin_Standalone', 'JuicyRegressionTests')
    ctest = Path(shutil.which(args.cmake) or args.cmake).resolve().with_name('ctest.exe' if os.name == 'nt' else 'ctest')
    run(ctest, '--test-dir', plugin, '-C', 'Release', '--output-on-failure')
    record = {'pins': PINS, 'arch': arch, 'platform': platform.platform(),
              'cmake': subprocess.check_output([args.cmake, '--version'], text=True),
              'status': 'build and processor regression tests passed; DAW acceptance not tested'}
    if revision is not None and clean_revision(ROOT) == revision:
        record['source_commit'] = revision
        record['products'] = {p.name: product_manifest(p)
                              for p in desktop_products(build, platform.system())}
        temporary_record = record_path.with_suffix('.tmp')
        temporary_record.write_text(json.dumps(record, indent=2), encoding='utf-8')
        temporary_record.replace(record_path)
    else:
        print('Build has local source changes. A clean, committed build is required for packaging.')
    print(f'Build completed in {build}. Nothing installed into plugin folders.')

if __name__ == '__main__':
    main()
