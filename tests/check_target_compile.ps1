<#
.SYNOPSIS
Compile the application against the installed ARM and FreeRTOS headers.
.DESCRIPTION
This produces objects only; it does not link a Vitis application or test hardware.
The default BSP headers are the read-only reference extracted from the supplied ZIP.
After copying the package elsewhere, pass -BspInclude with a generated Vitis BSP's
include directory. Both runs use tests/target_config/FreeRTOSConfig.h to exercise
static-allocation support disabled and enabled without modifying that BSP.
#>
param(
    [string]$Compiler = 'C:\AMDDesignTools\2025.2\Vitis\gnu\aarch32\nt\gcc-arm-none-eabi\bin\arm-none-eabi-gcc.exe',
    [string]$BspInclude = '',
    [string]$FreeRtosRoot = 'C:\AMDDesignTools\2025.2\Vitis\data\embeddedsw\ThirdParty\bsp\freertos10_xilinx_v1_18'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$package = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BspInclude)) {
    $workspace = Split-Path -Parent $package
    $BspInclude = Join-Path $workspace 'reference_review\legacy_audit\bsp_include'
}
$sourceDirectory = Join-Path $package 'src'
$fixtureDirectory = Join-Path $PSScriptRoot 'target_config'
$kernelInclude = Join-Path $FreeRtosRoot 'src\Source\include'
$portInclude = Join-Path $FreeRtosRoot 'src\Source\portable\GCC\ARM_CA9'
$requiredFiles = @(
    (Join-Path $BspInclude 'xparameters.h'),
    (Join-Path $BspInclude 'xil_types.h'),
    (Join-Path $kernelInclude 'FreeRTOS.h'),
    (Join-Path $portInclude 'portmacro.h'),
    (Join-Path $fixtureDirectory 'FreeRTOSConfig.h')
)
foreach ($required in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required header is missing: $required. Supply -BspInclude or -FreeRtosRoot for your installation."
    }
}
$compilerExecutable = (Get-Command -Name $Compiler -CommandType Application -ErrorAction Stop).Source
$sources = @(Get-ChildItem -LiteralPath $sourceDirectory -Filter '*.c' -File | Sort-Object Name)
if ($sources.Count -eq 0) { throw "No application C sources found in $sourceDirectory." }

# Vendor headers are system includes; warnings in application code remain errors.
$commonArguments = @(
    '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-Wconversion',
    '-Wshadow', '-Wstrict-prototypes', '-fno-common', '-O2',
    '-DSDT', '-mcpu=cortex-a9', '-mfpu=vfpv3', '-mfloat-abi=hard',
    '-I', $sourceDirectory, '-I', $fixtureDirectory,
    '-isystem', $kernelInclude, '-isystem', $portInclude, '-isystem', $BspInclude
)
foreach ($staticAllocation in @(0, 1)) {
  foreach ($hardwareEnabled in @(0, 1)) {
    $buildDirectory = Join-Path $package "build\target-headers\static-$staticAllocation-hardware-$hardwareEnabled"
    New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null
    foreach ($source in $sources) {
        $object = Join-Path $buildDirectory ($source.BaseName + '.o')
        & $compilerExecutable @commonArguments "-DSONAR_CHECK_STATIC_ALLOCATION=$staticAllocation" `
            '-DSONAR_MIC_ENABLE_HARDWARE=0' "-DSONAR_MOTOR_ENABLE_HARDWARE=$hardwareEnabled" `
            '-c' $source.FullName '-o' $object
        if ($LASTEXITCODE -ne 0) {
            throw "ARM header compilation failed: $($source.Name), static allocation $staticAllocation."
        }
    }
    $objects = @($sources | ForEach-Object { Join-Path $buildDirectory ($_.BaseName + '.o') })
    $combined = Join-Path $buildDirectory 'application_combined.o'
    & $compilerExecutable -nostdlib -r @objects -o $combined
    if ($LASTEXITCODE -ne 0) { throw 'Application object linking failed.' }
    $nm = Join-Path (Split-Path -Parent $compilerExecutable) 'arm-none-eabi-nm.exe'
    $unresolved = @(& $nm -u $combined)
    if ($LASTEXITCODE -ne 0) { throw 'Could not inspect combined application symbols.' }
    if ($unresolved | Where-Object { $_ -match '\bU\s+sonar_' }) {
        throw 'An application module has an unresolved sonar_ symbol.'
    }
    Write-Output "PASS: $($sources.Count) ARM objects and internal symbols; static=$staticAllocation motor=$hardwareEnabled PL=0."
  }
}
# A command-line override must not activate the inherited Stage 2 GPIO adapter.
$blockedObject = Join-Path $package 'build\target-headers\blocked.o'
$rejection = & $compilerExecutable @commonArguments '-DSONAR_CHECK_STATIC_ALLOCATION=0' `
    '-DSONAR_MIC_ENABLE_HARDWARE=1' '-c' (Join-Path $sourceDirectory 'main.c') '-o' $blockedObject 2>&1
if ($LASTEXITCODE -eq 0 -or ($rejection -join "`n") -notmatch 'Stage 3 hardware integration pending') {
    throw 'Unverified hardware activation was not rejected as expected.'
}
Write-Output 'PASS: unverified hardware activation rejected at compile time.'
Write-Output 'Header and internal-symbol checks only. Final Vitis ELF linking and board acceptance remain separate.'
