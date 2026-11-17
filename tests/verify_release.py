"""Validate a Windows release ZIP and execute it outside the development tree."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import subprocess
import tempfile
import zipfile
from pathlib import Path


def digest(path: Path) -> str:
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def imports(path: Path) -> set[str]:
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError(f'Not a PE file: {path.name}')
    machine, count = struct.unpack_from('<HH', data, pe + 4)
    if machine != 0x8664:
        raise ValueError(f'Not an x64 executable: {path.name}')
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    if struct.unpack_from('<H', data, optional)[0] != 0x20B:
        raise ValueError(f'Not PE32+: {path.name}')
    sections = []
    for i in range(count):
        location = optional + optional_size + i * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from('<IIII', data, location + 8)
        sections.append((virtual_address, max(virtual_size, raw_size), raw_offset))

    def offset(rva: int) -> int:
        for address, size, raw in sections:
            if address <= rva < address + size:
                return raw + rva - address
        raise ValueError(f'Invalid RVA in {path.name}: {rva:x}')

    def string(rva: int) -> str:
        start = offset(rva)
        return data[start:data.index(0, start)].decode('ascii').lower()

    result = set()
    import_rva = struct.unpack_from('<I', data, optional + 112 + 8)[0]
    if import_rva:
        position = offset(import_rva)
        while any(data[position:position + 20]):
            name = struct.unpack_from('<I', data, position + 12)[0]
            result.add(string(name))
            position += 20
    # Also check delay-loaded libraries rather than relying solely on the normal import table.
    delay_rva = struct.unpack_from('<I', data, optional + 112 + 13 * 8)[0]
    image_base = struct.unpack_from('<Q', data, optional + 24)[0]
    if delay_rva:
        position = offset(delay_rva)
        while any(data[position:position + 32]):
            attributes, name = struct.unpack_from('<II', data, position)
            result.add(string(name if attributes & 1 else name - image_base))
            position += 32
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    args = parser.parse_args()
    workspace = Path(tempfile.mkdtemp(prefix='TeslaCam release 中文 '))
    with zipfile.ZipFile(args.archive) as archive:
        corrupt = archive.testzip()
        if corrupt:
            raise ValueError(f'ZIP CRC failed: {corrupt}')
        for name in archive.namelist():
            parts = Path(name).parts
            if Path(name).is_absolute() or '..' in parts or ':' in name:
                raise ValueError(f'Unsafe ZIP member: {name}')
        roots = {Path(name).parts[0] for name in archive.namelist()}
        if len(roots) != 1:
            raise ValueError('The release must have exactly one root folder.')
        archive.extractall(workspace)
    root = workspace / roots.pop()
    manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
    expected = {'manifest.json'}
    for entry in manifest['files']:
        path = root / entry['path']
        if not path.is_file() or path.stat().st_size != entry['bytes'] or digest(path) != entry['sha256']:
            raise ValueError(f'Manifest mismatch: {entry["path"]}')
        expected.add(entry['path'])
    actual = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
    if actual != expected:
        raise ValueError('The package contains unlisted files.')
    if any(Path(name).suffix.lower() in {'.mp4', '.sqlite', '.db', '.pdb', '.obj'} for name in actual):
        raise ValueError('Recordings, indexes, or development artifacts must not be packaged.')
    for name in ['vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'platforms/qwindows.dll',
                 'sqldrivers/qsqlite.dll', 'multimedia/ffmpegmediaplugin.dll', 'qt.conf']:
        if not (root / name).is_file():
            raise ValueError(f'Missing required runtime file: {name}')
    system = Path(os.environ['SystemRoot']) / 'System32'
    local = {p.name.lower() for p in root.glob('*.dll')}
    pe_files = list(root.rglob('*.dll')) + list(root.glob('*.exe'))
    missing = []
    for binary in pe_files:
        for dependency in imports(binary):
            if dependency.startswith(('api-ms-', 'ext-ms-')):
                continue
            if dependency not in local and not (system / dependency).exists():
                missing.append(f'{binary.relative_to(root)} -> {dependency}')
    if missing:
        raise ValueError('Unresolved DLL imports:\n' + '\n'.join(sorted(missing)))
    env = os.environ.copy()
    env['PATH'] = str(system) + os.pathsep + os.environ['SystemRoot']
    for key in list(env):
        if key.startswith(('QT_', 'QML', 'TESLACAM_')):
            del env[key]
    env['TESLACAM_DATA_DIR'] = str(workspace / 'isolated index')
    env['QT_DEBUG_PLUGINS'] = '1'
    flags = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
    result = subprocess.run([str(root / 'TeslaCamManager.exe'), '--smoke-test'], cwd=workspace,
                            env=env, capture_output=True, text=True, errors='replace', timeout=40,
                            creationflags=flags)
    if result.returncode:
        raise RuntimeError(f'Packaged application startup failed: {result.returncode}\n{result.stderr}')
    if '.deps' in result.stderr:
        raise RuntimeError('The packaged app searched the development SDK for plugins.')
    if not (workspace / 'isolated index/index.sqlite').exists():
        raise RuntimeError('The packaged SQLite plugin did not create an index.')
    video = workspace / 'synthetic verification.mp4'
    command = [str(root / 'ffmpeg.exe'), '-hide_banner', '-loglevel', 'error', '-nostdin', '-f', 'lavfi',
               '-i', 'testsrc2=size=320x180:rate=15', '-t', '2', '-c:v', 'libx264', str(video)]
    subprocess.run(command, cwd=workspace, env=env, check=True, capture_output=True, timeout=40, creationflags=flags)
    # Decode verification mirrors the app's actual thumbnail path: H.264 decode + scale + MJPEG encode.
    thumbnail = workspace / 'synthetic verification.jpg'
    subprocess.run([str(root / 'ffmpeg.exe'), '-hide_banner', '-loglevel', 'error', '-nostdin', '-y', '-i',
                    str(video), '-frames:v', '1', '-vf', 'scale=240:-2', str(thumbnail)], cwd=workspace,
                   env=env, check=True, capture_output=True, timeout=40, creationflags=flags)
    if not thumbnail.is_file() or thumbnail.stat().st_size == 0:
        raise RuntimeError('The packaged FFmpeg did not produce a thumbnail.')
    print(json.dumps({'archive': str(args.archive.resolve()), 'sha256': digest(args.archive),
                      'verified_files': len(actual), 'verified_pe_files': len(pe_files),
                      'startup_exit': result.returncode, 'ffmpeg_roundtrip': 'passed',
                      'extracted_root': str(root)}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
