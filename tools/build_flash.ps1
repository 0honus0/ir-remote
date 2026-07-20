param(
  [string] $Port = "",
  [int] $Baud = 460800,
  [string] $Config = "configs/ir.yaml",
  [string] $Database = "",
  [string] $BinaryDir = "data/irext/bin",
  [string] $OutputDir = "generated/irext",
  [string] $CatalogOffset = "0x300000",
  [switch] $NoFlash,
  [switch] $EraseFlash
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $RepoRoot

function Resolve-RepoPath([string] $PathValue) {
  if ([System.IO.Path]::IsPathRooted($PathValue)) {
    return $PathValue
  }
  return Join-Path $RepoRoot $PathValue
}

function Invoke-Step([string] $Title, [string[]] $Command) {
  Write-Host ""
  Write-Host "==> $Title" -ForegroundColor Cyan
  Write-Host ($Command -join " ")
  $exe = $Command[0]
  $arguments = @()
  if ($Command.Count -gt 1) {
    $arguments = $Command[1..($Command.Count - 1)]
  }
  & $exe @arguments
  if ($LASTEXITCODE -ne 0) {
    throw "$Title failed with exit code $LASTEXITCODE"
  }
}

function Get-IrextDatabase() {
  if ($Database) {
    $databasePath = Resolve-RepoPath $Database
    if (!(Test-Path $databasePath)) {
      throw "Database not found: $databasePath"
    }
    return (Resolve-Path $databasePath).Path
  }

  $dbDir = Resolve-RepoPath "data/irext/db"
  $preferredDb = Join-Path $dbDir "irext_db_20260519_sqlite3.db"
  if ((Test-Path $preferredDb) -and ((Get-Item $preferredDb).Length -gt 0)) {
    return (Resolve-Path $preferredDb).Path
  }

  $zipPath = Join-Path $dbDir "irext_db_20260519_sqlite3.zip"
  if (!(Test-Path $zipPath)) {
    throw "No usable IRext database found under $dbDir"
  }

  Write-Host "==> Extract database"
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $archive = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
  try {
    $entry = $archive.Entries | Where-Object { $_.FullName -like "*.db" } | Select-Object -First 1
    if ($null -eq $entry) {
      throw "No .db file found in $zipPath"
    }
    $target = Join-Path $dbDir ([System.IO.Path]::GetFileName($entry.FullName))
    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
    return (Resolve-Path $target).Path
  } finally {
    $archive.Dispose()
  }
}

function Get-LatestFile([string] $Root, [string] $Filter) {
  $file = Get-ChildItem -Path $Root -Recurse -Filter $Filter |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
  if ($null -eq $file) {
    throw "Build output not found: $Filter under $Root"
  }
  return $file.FullName
}

function Test-MergedImage([string] $Firmware, [string] $Catalog, [string] $FullImage, [string] $Offset) {
  Write-Host ""
  Write-Host "==> Verify merged image" -ForegroundColor Cyan
  $firmwareBytes = [System.IO.File]::ReadAllBytes($Firmware)
  $catalogBytes = [System.IO.File]::ReadAllBytes($Catalog)
  $fullBytes = [System.IO.File]::ReadAllBytes($FullImage)
  $offsetText = $Offset
  if ($offsetText.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
    $offsetText = $offsetText.Substring(2)
  }
  $catalogOffsetValue = [Convert]::ToInt32($offsetText, 16)

  if ($fullBytes.Length -eq 0 -or $fullBytes[0] -ne 0xE9) {
    throw "full image does not start with ESP8266 image header 0xE9"
  }
  for ($index = 0; $index -lt $firmwareBytes.Length; $index++) {
    if ($fullBytes[$index] -ne $firmwareBytes[$index]) {
      throw "firmware prefix mismatch"
    }
  }
  for ($index = 0; $index -lt $catalogBytes.Length; $index++) {
    if ($fullBytes[$catalogOffsetValue + $index] -ne $catalogBytes[$index]) {
      throw "catalog payload mismatch at offset"
    }
  }

  Write-Host ("verified: firmware={0} full={1} catalog={2} offset=0x{3:X}" -f `
      $firmwareBytes.Length, $fullBytes.Length, $catalogBytes.Length, $catalogOffsetValue)
}

if (!$NoFlash -and [string]::IsNullOrWhiteSpace($Port)) {
  throw "Port is required for flashing. Example: .\tools\build_flash.ps1 -Port COM3"
}

$python = "python"
$configPath = Resolve-RepoPath $Config
$binaryDirPath = Resolve-RepoPath $BinaryDir
$outputDirPath = Resolve-RepoPath $OutputDir
$catalogPath = Join-Path $outputDirPath "catalog.bin"
$indexPath = Join-Path $outputDirPath "catalog_index.h"
$oldCatalogHeader = Join-Path $outputDirPath "catalog_data.h"
$databasePath = Get-IrextDatabase

if (!(Test-Path $configPath)) {
  throw "Config not found: $configPath"
}
if (!(Test-Path $binaryDirPath)) {
  throw "Binary dir not found: $binaryDirPath"
}

Invoke-Step "Generate IRext catalog index and bin" @(
  $python,
  "tools/export_irext_catalog.py",
  $databasePath,
  "--binary-dir", $binaryDirPath,
  "--output-dir", $outputDirPath,
  "--emit-header",
  "--allow-missing",
  "--raw-flash-offset", $CatalogOffset
)

if (Test-Path $oldCatalogHeader) {
  Remove-Item -LiteralPath $oldCatalogHeader -Force
}
if (!(Test-Path $catalogPath) -or !(Test-Path $indexPath)) {
  throw "Catalog generation did not produce catalog.bin and catalog_index.h"
}

Invoke-Step "Compile ESPHome firmware" @($python, "-m", "esphome", "compile", $configPath)

$buildRoot = Join-Path (Split-Path $configPath -Parent) ".esphome/build"
$firmwarePath = Get-LatestFile $buildRoot "firmware.bin"
$fullImagePath = Join-Path (Split-Path $firmwarePath -Parent) "firmware.with_irext.bin"

Invoke-Step "Merge firmware and IRext catalog" @(
  $python,
  "tools/merge_irext_catalog.py",
  $firmwarePath,
  $catalogPath,
  "--offset", $CatalogOffset,
  "--output", $fullImagePath
)

Test-MergedImage $firmwarePath $catalogPath $fullImagePath $CatalogOffset

if ($NoFlash) {
  Write-Host ""
  Write-Host "NoFlash enabled. Skip flashing." -ForegroundColor Yellow
  Write-Host "Full image: $fullImagePath"
  exit 0
}

if ($EraseFlash) {
  Invoke-Step "Erase flash" @($python, "-m", "esptool", "--chip", "esp8266", "--port", $Port, "--baud", "$Baud", "erase-flash")
}

Invoke-Step "Flash full image" @(
  $python,
  "-m", "esptool",
  "--chip", "esp8266",
  "--port", $Port,
  "--baud", "$Baud",
  "write-flash",
  "-z",
  "--flash-mode", "dout",
  "--flash-freq", "40m",
  "--flash-size", "4MB",
  "0x0", $fullImagePath
)

Write-Host ""
Write-Host "Done." -ForegroundColor Green
