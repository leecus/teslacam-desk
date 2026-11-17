param(
    [string]$QtRoot = (Join-Path $PSScriptRoot '.deps\Qt\6.8.3\msvc2022_64'),
    [string]$FfmpegPath,
    [switch]$SkipTests,
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$env:VSLANG = '1033'
$vsRoot = 'C:\Program Files\Microsoft Visual Studio\18\Community'
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
if (-not (Test-Path -LiteralPath $ninja)) { $ninja = (Get-Command ninja -ErrorAction Stop).Source }
if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'bin\qmake.exe'))) { throw 'Qt 6.8 SDK not found. See README.md.' }
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $devCmd = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path -LiteralPath $devCmd)) { throw 'Run from an x64 MSVC Developer PowerShell.' }
    $environmentLines = & cmd.exe /d /s /c "`"$devCmd`" -arch=x64 -host_arch=x64 >nul && set"
    foreach ($line in $environmentLines) {
        if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
    }
}
$env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
$env:VSLANG = '1033'
$buildDirectory = Join-Path $PSScriptRoot 'build'
# MSVC may only have Chinese language resources. Probe its actual prefix in PowerShell;
# CMake's own locale detection can misdecode it and silently lose header dependencies.
$probeDirectory = Join-Path $PSScriptRoot '.verification'
New-Item -ItemType Directory -Path $probeDirectory -Force | Out-Null
$probeSource = Join-Path $probeDirectory 'msvc-dependency-probe.cpp'
Set-Content -LiteralPath $probeSource -Value '#include <stddef.h>' -Encoding ascii
$probeOutput = & cl.exe /nologo /c /showIncludes $probeSource "/Fo$(Join-Path $probeDirectory 'msvc-dependency-probe.obj')" 2>&1
if ($LASTEXITCODE -ne 0) { throw 'MSVC dependency detection failed.' }
$dependencyPrefix = $null
foreach ($line in $probeOutput) {
    if ([string]$line -match '^(.*?)[A-Za-z]:[\\/].*stddef\.h\s*$') { $dependencyPrefix = $matches[1].TrimEnd(); break }
}
if (-not $dependencyPrefix) { throw 'Cannot determine the MSVC include prefix; refusing an unsafe incremental build.' }
$rulesPath = Join-Path $buildDirectory 'CMakeFiles\rules.ninja'
$needsClean = $Clean -or ((Test-Path -LiteralPath $rulesPath) -and -not ([IO.File]::ReadAllText($rulesPath).Contains("msvc_deps_prefix = $dependencyPrefix")))
& $cmake -S $PSScriptRoot -B $buildDirectory -G Ninja '-DCMAKE_BUILD_TYPE=Release' "-DTESLACAM_MSVC_DEPS_PREFIX=$dependencyPrefix" "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_PREFIX_PATH=$QtRoot"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
$buildArguments = @('--build', $buildDirectory, '--parallel', '4')
if ($needsClean) { $buildArguments += '--clean-first' }
& $cmake @buildArguments
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
# Prefer the reproducible minimal CLI, then an existing local CLI. Never replace
# either with the larger Python fallback on every incremental build.
if (-not $FfmpegPath) {
    $minimalFfmpeg = Join-Path $PSScriptRoot '.deps\ffmpeg-minimal\ffmpeg-9.0.2\ffmpeg.exe'
    $localFfmpeg = Join-Path $PSScriptRoot 'bin\ffmpeg.exe'
    if (Test-Path -LiteralPath $minimalFfmpeg) { $FfmpegPath = $minimalFfmpeg }
    elseif (Test-Path -LiteralPath $localFfmpeg) { $FfmpegPath = $localFfmpeg }
}
if (-not $FfmpegPath) {
    $ffmpegPython = Join-Path $PSScriptRoot '.deps\python\Scripts\python.exe'
    if (Test-Path -LiteralPath $ffmpegPython) {
        $FfmpegPath = & $ffmpegPython -c 'import imageio_ffmpeg; print(imageio_ffmpeg.get_ffmpeg_exe())'
        if ($LASTEXITCODE -ne 0) { throw 'Cannot locate the fallback FFmpeg CLI.' }
    }
}
if (-not $FfmpegPath -or -not (Test-Path -LiteralPath $FfmpegPath)) { throw 'Supply -FfmpegPath or build tools/ffmpeg-minimal/build.sh first.' }
$ffmpegSource = (Resolve-Path -LiteralPath $FfmpegPath).Path
$ffmpegTarget = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'bin\ffmpeg.exe'))
if ($ffmpegSource -ne $ffmpegTarget) { Copy-Item -LiteralPath $ffmpegSource -Destination $ffmpegTarget -Force }
if (-not $SkipTests) {
    $ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
    & $ctest --test-dir $buildDirectory --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Core safety tests failed.' }
}
$exe = Join-Path $PSScriptRoot 'bin\TeslaCamManager.exe'
# This is the local development output with its runtime dependencies, not a release package.
& (Join-Path $QtRoot 'bin\windeployqt.exe') --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime $exe
if ($LASTEXITCODE -ne 0) { throw 'Local Qt runtime setup failed.' }
Write-Output "Built and verified: $exe"
