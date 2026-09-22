param([string]$Compiler = 'gcc')
$ErrorActionPreference = 'Stop'
$package = Split-Path -Parent $PSScriptRoot
$build = Join-Path $package 'build\host'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$sources = @('sonar_config.c', 'sonar_health.c', 'sonar_profile.c', 'sonar_selftest.c', 'sonar_mic.c', 'sonar_mic_selftest.c', 'sonar_motor.c', 'sonar_motor_selftest.c', 'sonar_speaker.c', 'sonar_speaker_selftest.c', 'sonar_ps_i2c.c', 'sonar_tic.c', 'sonar_driver_selftest.c', 'sonar_codec_bus.c', 'sonar_codec_selftest.c', 'sonar_scan_config.c', 'sonar_scan.c', 'sonar_scan_command.c', 'sonar_scan_selftest.c', 'sonar_scan_sim.c') |
    ForEach-Object { Join-Path $package "src\$_" }
$exe = Join-Path $build 'sonar_tests.exe'
& $Compiler -std=c11 -Wall -Wextra -Werror -pedantic -Wconversion -Wshadow -O2 `
    '-I' (Join-Path $package 'src') @sources (Join-Path $PSScriptRoot 'test_main.c') -o $exe
if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Host tests failed.' }

