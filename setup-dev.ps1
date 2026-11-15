$ErrorActionPreference = 'Stop'
$python = Join-Path $PSScriptRoot '.deps\python\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { & python -m venv (Join-Path $PSScriptRoot '.deps\python') }
if ($LASTEXITCODE -ne 0) { throw 'Python virtual environment setup failed.' }
& $python -m pip install aqtinstall imageio-ffmpeg
if ($LASTEXITCODE -ne 0) { throw 'Dependency tool setup failed.' }
& $python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O (Join-Path $PSScriptRoot '.deps\Qt') -m qtmultimedia --archives qtbase qtsvg qttools
if ($LASTEXITCODE -ne 0) { throw 'Qt SDK download failed.' }
Write-Output 'Development dependencies are ready. Run .\build.ps1 next.'
