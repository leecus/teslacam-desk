"""Build a versioned Windows ZIP from verified local output. Never overwrites a release."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tarfile
import zipfile
from pathlib import Path

PROJECT = Path(__file__).resolve().parent
SOURCES = {
    f'{module}-everywhere-src-6.8.3.tar.xz':
    f'https://download.qt.io/archive/qt/6.8/6.8.3/submodules/{module}-everywhere-src-6.8.3.tar.xz'
    for module in ('qtbase', 'qtsvg', 'qtmultimedia')
}
SOURCES.update({
    'ffmpeg-7.1.tar.xz': 'https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz',
    'ffmpeg-9.0.2.tar.xz': 'https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz',
})
PLUGINS = {
    'platforms': ['qwindows.dll'],
    'sqldrivers': ['qsqlite.dll'],
    'multimedia': ['ffmpegmediaplugin.dll', 'windowsmediaplugin.dll'],
    'imageformats': ['qgif.dll', 'qico.dll', 'qjpeg.dll', 'qsvg.dll'],
    'iconengines': ['qsvgicon.dll'],
    'networkinformation': ['qnetworklistmanager.dll'],
    'styles': ['qmodernwindowsstyle.dll'],
    'tls': ['qcertonlybackend.dll', 'qschannelbackend.dll'],
}


def digest(path: Path) -> str:
    with path.open('rb') as handle:
        return hashlib.file_digest(handle, 'sha256').hexdigest()


def copy(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise FileNotFoundError(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def collect_licenses(archive: Path, destination: Path) -> None:
    with tarfile.open(archive, 'r:xz') as tar:
        for item in tar:
            parts = Path(item.name).parts
            if not item.isfile() or len(parts) < 2 or '..' in parts:
                continue
            name = parts[-1].lower()
            if 'LICENSES' not in parts and not name.startswith(('license', 'copying', 'copyright', 'notice')):
                continue
            data = tar.extractfile(item)
            if data is None:
                continue
            output = destination.joinpath(*parts[1:])
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(data.read())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', default='0.1.0')
    parser.add_argument('--sources', type=Path, default=PROJECT / '.deps/release-sources')
    parser.add_argument('--runtime', type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?', args.version):
        parser.error('Version must be a safe semantic version.')
    version = args.version
    name = f'TeslaCamDesk-v{version}-windows-x64'
    output = PROJECT / 'dist'
    stage = output / name
    archive = output / f'{name}.zip'
    if stage.exists() or archive.exists():
        raise FileExistsError('A release with this version already exists; it will not be overwritten.')
    binary = PROJECT / 'bin'
    for required in ['TeslaCamManager.exe', 'ffmpeg.exe', 'Qt6Core.dll']:
        if not (binary / required).is_file():
            raise FileNotFoundError(f'Run build.ps1 first: missing {required}')
    source_root = args.sources.resolve()
    for filename in [*SOURCES, 'ffmpeg-release-essentials.zip', 'DXC-LICENSE.txt', 'DXC-ThirdPartyNotices.txt']:
        if not (source_root / filename).is_file():
            raise FileNotFoundError(f'Missing third-party release material: {filename}')
    runtime = args.runtime
    if runtime is None:
        roots = Path('C:/Program Files/Microsoft Visual Studio/18/Community/VC/Redist/MSVC')
        candidates = list(roots.glob('*/x64/Microsoft.VC*.CRT'))
        candidates = [p for p in candidates if (p / 'vcruntime140.dll').is_file()]
        if not candidates:
            raise FileNotFoundError('Supply --runtime with an x64 MSVC redistributable CRT directory.')
        runtime = sorted(candidates, key=lambda p: p.as_posix())[-1]
    if not (runtime / 'vcruntime140.dll').is_file():
        raise FileNotFoundError('The runtime directory must contain vcruntime140.dll.')
    # Verify the CLI is the exact upstream binary whose notices we distribute.
    with zipfile.ZipFile(source_root / 'ffmpeg-release-essentials.zip') as upstream:
        member = next(n for n in upstream.namelist() if n.endswith('/bin/ffmpeg.exe'))
        if hashlib.sha256(upstream.read(member)).hexdigest() != digest(binary / 'ffmpeg.exe'):
            raise RuntimeError('FFmpeg binary differs from the documented upstream distribution.')
        stage.mkdir(parents=True)
        for filename in ['TeslaCamManager.exe', 'ffmpeg.exe']:
            copy(binary / filename, stage / filename)
        for dll in binary.glob('*.dll'):
            copy(dll, stage / dll.name)
        for directory, files in PLUGINS.items():
            for filename in files:
                copy(binary / directory / filename, stage / directory / filename)
        for dll in runtime.glob('*.dll'):
            copy(dll, stage / dll.name)
        for filename in upstream.namelist():
            if filename.endswith('/LICENSE') or filename.endswith('/README.txt'):
                target = stage / 'licenses/FFmpeg-CLI' / Path(filename).name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(upstream.read(filename))
    (stage / 'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=.\n', encoding='utf-8')
    for filename in SOURCES:
        collect_licenses(source_root / filename, stage / 'licenses' / filename.removesuffix('.tar.xz'))
    for filename in ['DXC-LICENSE.txt', 'DXC-ThirdPartyNotices.txt']:
        copy(source_root / filename, stage / 'licenses/DirectXShaderCompiler' / filename)
    qt = PROJECT / '.deps/Qt/6.8.3/msvc2022_64'
    for module in ('qtbase', 'qtsvg', 'qtmultimedia'):
        copy(qt / f'sbom/{module}-6.8.3.spdx.json', stage / f'licenses/SBOM/{module}-6.8.3.spdx.json')
    readme = f'''TeslaCam Desk {version} — Windows x64 预览版

完整解压此文件夹，双击 TeslaCamManager.exe。请保留同目录的 DLL、插件和许可文件。
选择 U 盘或 TeslaCam 录像目录，再点击“扫描建索引”。
扫描、播放、原片备份、裁剪及清理均在本地进行。

永久删除不经过回收站，无法撤销。收藏事件受保护，建议先使用“备份后删除”。
索引默认保存到 %LOCALAPPDATA%\\LocalTools\\TeslaCamManager，不写入录像 U 盘。

Windows 10/11 x64。macOS、Linux 尚未提供发行包。真实车型录像格式仍需进一步验证。
主页：https://github.com/leecus/teslacam-desk
第三方许可与来源：THIRD-PARTY-NOTICES.txt、licenses/、third-party-sources.json。
'''
    (stage / 'README.txt').write_text(readme, encoding='utf-8-sig')
    config = subprocess.run([str(binary / 'ffmpeg.exe'), '-version'], capture_output=True, text=True, errors='replace', check=True)
    notices = '''THIRD-PARTY SOFTWARE

Qt 6.8.3 (qtbase, qtsvg, qtmultimedia): Copyright The Qt Company Ltd. and contributors.
These libraries are dynamically linked and distributed under LGPL-3.0.
Qt's third-party notices and license texts are in licenses/; shipped module SBOMs are in licenses/SBOM/.
You may replace/relink these shared libraries with compatible modified versions.
Reverse engineering for debugging modifications to the LGPL libraries is not restricted by this distribution.
Source archives for the shipped Qt modules are offered alongside this ZIP on the same GitHub Release.
Qt source trees include the sources/notices for bundled SQLite, image codecs, zlib, font libraries and other components.
Qt project: https://www.qt.io/ ; source: https://code.qt.io/qt/

Qt multimedia FFmpeg shared libraries: FFmpeg 7.1, LGPL-2.1-or-later.
Unmodified official Qt MSVC binaries. Corresponding FFmpeg 7.1 source archive accompanies the release.
Build configuration: --prefix=installed --disable-programs --disable-doc --disable-debug
--enable-network --disable-lzma --enable-pic --disable-vulkan --disable-v4l2-m2m
--disable-decoder=truemotion1 --toolchain=msvc --enable-shared --disable-static

FFmpeg command-line program: 9.0.2 essentials static build by Gyan Doshi, GPL-3.0-or-later.
This is a separate executable, invoked as a child process. Its upstream README and GPL text are preserved
in licenses/FFmpeg-CLI/. Source: https://github.com/FFmpeg/FFmpeg/commit/946fcce07b
Full upstream distribution/build information: https://www.gyan.dev/ffmpeg/builds/
Sources and build configuration are identified here and in third-party-sources.json.
FFmpeg source archives include the project notices; external codec/library copyrights and licenses remain applicable.

DirectX Shader Compiler / DXIL: Microsoft Corporation and contributors.
Notices and applicable University of Illinois, LLVM and MIT terms are in licenses/DirectXShaderCompiler/.
Source: https://github.com/microsoft/DirectXShaderCompiler

Microsoft Visual C++ Runtime (x64): Copyright Microsoft Corporation.
Unmodified redistributable CRT DLLs are copied from the installed Visual Studio redistributable directory.
https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist

FFmpeg CLI version and configuration:
'''
    (stage / 'THIRD-PARTY-NOTICES.txt').write_text(notices + config.stdout + config.stderr, encoding='utf-8')
    source_manifest = {
        'release': f'https://github.com/leecus/teslacam-desk/releases/tag/v{version}',
        'archives': [{'file': file, 'url': url, 'sha256': digest(source_root / file)} for file, url in SOURCES.items()],
        'ffmpeg_cli_upstream': 'https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip',
        'ffmpeg_cli_source_commit': 'https://github.com/FFmpeg/FFmpeg/commit/946fcce07b',
    }
    (stage / 'third-party-sources.json').write_text(json.dumps(source_manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    manifest = [{'path': str(p.relative_to(stage)).replace('\\', '/'), 'bytes': p.stat().st_size, 'sha256': digest(p)}
                for p in sorted(stage.rglob('*')) if p.is_file()]
    (stage / 'manifest.json').write_text(json.dumps({'version': version, 'files': manifest}, indent=2) + '\n', encoding='utf-8')
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in sorted(stage.rglob('*')):
            if p.is_file():
                z.write(p, str(p.relative_to(output)).replace('\\', '/'))
    sums = [f'{digest(archive)}  {archive.name}'] + [f'{digest(source_root / file)}  {file}' for file in SOURCES]
    (output / f'TeslaCamDesk-v{version}-SHA256SUMS.txt').write_text('\n'.join(sums) + '\n', encoding='ascii')
    print(json.dumps({'zip': str(archive), 'bytes': archive.stat().st_size, 'sha256': digest(archive), 'files': len(manifest)}, indent=2))


if __name__ == '__main__':
    main()
