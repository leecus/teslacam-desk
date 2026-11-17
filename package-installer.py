"""Build the per-user NSIS installer from a verified, versioned release folder."""
from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path, PurePosixPath

from package_release_support import digest

PROJECT = Path(__file__).resolve().parent


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', required=True)
    parser.add_argument('--makensis', type=Path, default=PROJECT / '.deps/nsis/nsis-3.11/makensis.exe')
    args = parser.parse_args()
    if not re.fullmatch(r'\d+\.\d+\.\d+', args.version):
        parser.error('Installer version must have three numeric components.')
    stage = PROJECT / f'dist/TeslaCamDesk-v{args.version}-windows-x64'
    output = PROJECT / f'dist/TeslaCamDesk-v{args.version}-setup.exe'
    if output.exists():
        raise FileExistsError('An installer with this version already exists.')
    manifest = json.loads((stage / 'manifest.json').read_text(encoding='utf-8'))
    if manifest['version'] != args.version:
        raise ValueError('Release folder version does not match the installer.')
    files = {'manifest.json'}
    for entry in manifest['files']:
        name = entry['path']
        relative = PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts or any(c in name for c in ':\\"\r\n'):
            raise ValueError(f'Unsafe release path: {name}')
        path = stage / name
        if path.stat().st_size != entry['bytes'] or digest(path) != entry['sha256']:
            raise ValueError(f'Release manifest mismatch: {name}')
        files.add(name)
    if {p.relative_to(stage).as_posix() for p in stage.rglob('*') if p.is_file()} != files:
        raise ValueError('Unlisted files in the release folder.')
    # Delete only files owned by this package; a custom installation folder may
    # contain unrelated user files. Non-recursive RMDir preserves those files.
    def escaped(name: str) -> str:
        return name.replace('$', '$$').replace('/', '\\')
    lines = [f'Delete "$INSTDIR\\{escaped(name)}"' for name in sorted(files)]
    directories = {p for name in files for p in PurePosixPath(name).parents if str(p) != '.'}
    lines += [f'RMDir "$INSTDIR\\{escaped(str(p))}"' for p in sorted(directories, key=lambda p: (-len(p.parts), str(p)))]
    build = PROJECT / f'.verification/installer-v{args.version}'
    build.mkdir(parents=True, exist_ok=True)
    include = build / 'uninstall-files.nsh'
    include.write_text('\n'.join(lines) + '\n', encoding='utf-8-sig')
    subprocess.run([str(args.makensis.resolve()), f'-DVERSION={args.version}', f'-DSOURCE_DIR={stage}',
                    f'-DOUTFILE={output}', f'-DUNINSTALL_FILES={include}',
                    str(PROJECT / 'installer/teslacam-setup.nsi')], check=True)
    with (PROJECT / f'dist/TeslaCamDesk-v{args.version}-SHA256SUMS.txt').open('a', encoding='ascii') as sums:
        sums.write(f'{digest(output)}  {output.name}\n')
    print(json.dumps({'installer': str(output), 'bytes': output.stat().st_size, 'sha256': digest(output)}, indent=2))


if __name__ == '__main__':
    main()
