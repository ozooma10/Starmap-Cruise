[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version = '0.1.0',
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$releaseData = [System.IO.Path]::GetFullPath((Join-Path $projectRoot 'release\Data'))
$distRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot 'dist'))
$packageWork = [System.IO.Path]::GetFullPath((Join-Path $projectRoot 'build\release-package'))

function Assert-ProjectChildPath {
    param([Parameter(Mandatory)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $prefix = $projectRoot.TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside the project: $fullPath"
    }
    return $fullPath
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory)][string]$Command,
        [Parameter(Mandatory)][string[]]$Arguments
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed with exit code $LASTEXITCODE"
    }
}

if (Get-Process -Name Starfield -ErrorAction SilentlyContinue) {
    throw 'Starfield must be closed before building or packaging the plugin.'
}

if (-not $SkipBuild) {
    $verifiedReleaseData = Assert-ProjectChildPath $releaseData
    if (Test-Path -LiteralPath $verifiedReleaseData) {
        Remove-Item -LiteralPath $verifiedReleaseData -Recurse -Force
    }

    $savedModsPath = [Environment]::GetEnvironmentVariable('XSE_SF_MODS_PATH', 'Process')
    $savedGamePath = [Environment]::GetEnvironmentVariable('XSE_SF_GAME_PATH', 'Process')
    try {
        [Environment]::SetEnvironmentVariable('XSE_SF_MODS_PATH', $null, 'Process')
        [Environment]::SetEnvironmentVariable('XSE_SF_GAME_PATH', $null, 'Process')
        Push-Location $projectRoot
        try {
            Invoke-Checked 'xmake' @('f', '-m', 'releasedbg', '-y')
            Invoke-Checked 'xmake' @('-y')
            Invoke-Checked 'xmake' @('install', '-o', 'release/Data', '-y')
        }
        finally {
            Pop-Location
        }
    }
    finally {
        [Environment]::SetEnvironmentVariable('XSE_SF_MODS_PATH', $savedModsPath, 'Process')
        [Environment]::SetEnvironmentVariable('XSE_SF_GAME_PATH', $savedGamePath, 'Process')
    }
}

$pluginRoot = Join-Path $releaseData 'SFSE\Plugins'
$expectedRelativeFiles = @(
    'SFSE/Plugins/CruiseFromStarmap.dll',
    'SFSE/Plugins/CruiseFromStarmap.ini',
    'SFSE/Plugins/CruiseFromStarmap.pdb',
    'SFSE/Plugins/CruiseFromStarmapCustom.ini.example'
)
$actualRelativeFiles = @(
    Get-ChildItem -LiteralPath $releaseData -File -Recurse -ErrorAction Stop |
        ForEach-Object {
            $_.FullName.Substring($releaseData.Length + 1).Replace('\', '/')
        } |
        Sort-Object
)
$allowlistDifference = Compare-Object ($expectedRelativeFiles | Sort-Object) $actualRelativeFiles
if ($allowlistDifference) {
    $detail = $allowlistDifference | Out-String
    throw "Release payload differs from the four-file allowlist:`n$detail"
}

$defaultIni = Join-Path $pluginRoot 'CruiseFromStarmap.ini'
$activeKeys = @(
    Get-Content -LiteralPath $defaultIni |
        Where-Object { $_ -match '^\s*[^;#\[\s][^=]*=' } |
        ForEach-Object { ($_ -split '=', 2)[0].Trim() }
)
if ($activeKeys.Count -ne 1 -or $activeKeys[0] -ne 'bVerboseLog') {
    throw "Default INI exposes unexpected keys: $($activeKeys -join ', ')"
}
if ((Get-Content -LiteralPath $defaultIni) -notcontains 'bVerboseLog=true') {
    throw 'Default INI must ship with bVerboseLog=true until release smoke validation passes.'
}

$builtDll = Join-Path $projectRoot 'build\windows\x64\releasedbg\CruiseFromStarmap.dll'
$builtPdb = Join-Path $projectRoot 'build\windows\x64\releasedbg\CruiseFromStarmap.pdb'
$stagedDll = Join-Path $pluginRoot 'CruiseFromStarmap.dll'
$stagedPdb = Join-Path $pluginRoot 'CruiseFromStarmap.pdb'
$sourceIni = Join-Path $projectRoot 'CruiseFromStarmap.ini'
$sourceExample = Join-Path $projectRoot 'CruiseFromStarmapCustom.ini.example'
$stagedExample = Join-Path $pluginRoot 'CruiseFromStarmapCustom.ini.example'

$hashPairs = @(
    @($builtDll, $stagedDll, 'DLL'),
    @($builtPdb, $stagedPdb, 'PDB'),
    @($sourceIni, $defaultIni, 'default INI'),
    @($sourceExample, $stagedExample, 'custom INI example')
)
foreach ($pair in $hashPairs) {
    $leftHash = (Get-FileHash -LiteralPath $pair[0] -Algorithm SHA256).Hash
    $rightHash = (Get-FileHash -LiteralPath $pair[1] -Algorithm SHA256).Hash
    if ($leftHash -ne $rightHash) {
        throw "$($pair[2]) build/source and release hashes do not match."
    }
}

$removedTokens = @(
    'CruiseFromStarmapMarker',
    'bShowMarker',
    'bShowDestinationName',
    'bShowTargetStatus',
    'sMode',
    'CRUISE TARGET:'
)
$rg = Get-Command rg -ErrorAction SilentlyContinue
if ($rg) {
    $arguments = @('-a', '-n')
    foreach ($token in $removedTokens) {
        $arguments += @('-e', $token)
    }
    $arguments += $builtDll
    $staleStrings = & $rg.Source @arguments 2>$null
    if ($LASTEXITCODE -gt 1) {
        throw "rg failed while scanning the built DLL (exit $LASTEXITCODE)."
    }
    if ($staleStrings) {
        throw "Built DLL contains retired feature strings:`n$($staleStrings -join [Environment]::NewLine)"
    }
}

$verifiedPackageWork = Assert-ProjectChildPath $packageWork
if (Test-Path -LiteralPath $verifiedPackageWork) {
    Remove-Item -LiteralPath $verifiedPackageWork -Recurse -Force
}
$mainRoot = Join-Path $verifiedPackageWork 'main'
$symbolsRoot = Join-Path $verifiedPackageWork 'symbols'
$mainPlugins = Join-Path $mainRoot 'Data\SFSE\Plugins'
$symbolsPlugins = Join-Path $symbolsRoot 'Data\SFSE\Plugins'
New-Item -ItemType Directory -Path $mainPlugins -Force | Out-Null
New-Item -ItemType Directory -Path $symbolsPlugins -Force | Out-Null

foreach ($name in @('CruiseFromStarmap.dll', 'CruiseFromStarmap.ini',
        'CruiseFromStarmapCustom.ini.example')) {
    Copy-Item -LiteralPath (Join-Path $pluginRoot $name) -Destination $mainPlugins -Force
}
Copy-Item -LiteralPath $stagedPdb -Destination $symbolsPlugins -Force

New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
$mainArchive = Join-Path $distRoot "CruiseFromStarmap-$Version.zip"
$symbolsArchive = Join-Path $distRoot "CruiseFromStarmap-$Version-symbols.zip"
foreach ($archive in @($mainArchive, $symbolsArchive)) {
    if (Test-Path -LiteralPath $archive) {
        Remove-Item -LiteralPath (Assert-ProjectChildPath $archive) -Force
    }
}
Compress-Archive -LiteralPath (Join-Path $mainRoot 'Data') -DestinationPath $mainArchive
Compress-Archive -LiteralPath (Join-Path $symbolsRoot 'Data') -DestinationPath $symbolsArchive

$hashRows = @(
    Get-FileHash -LiteralPath $stagedDll, $stagedPdb, $defaultIni,
        $stagedExample, $mainArchive, $symbolsArchive -Algorithm SHA256
)
$checksumPath = Join-Path $distRoot 'SHA256SUMS.txt'
$checksumLines = $hashRows | ForEach-Object {
    '{0}  {1}' -f $_.Hash, (Split-Path -Leaf $_.Path)
}
Set-Content -LiteralPath $checksumPath -Value $checksumLines -Encoding utf8NoBOM

$hashRows | Select-Object @{Name='File';Expression={ Split-Path -Leaf $_.Path }}, Hash
