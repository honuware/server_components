<#
.SYNOPSIS
  Regenerates .vs\launch.vs.json for a CMake Open Folder project from the CMake
  file API, replacing VS 2026's broken "Debug and Launch Settings" command.

.DESCRIPTION
  VS 2026's Debug > Debug and Launch Settings command adds an entry for the
  currently selected target, one at a time. This enumerates EVERY executable
  target from the CMake file API reply and writes them all in one pass.

  Existing entries are preserved (matched on projectTarget); new targets are
  appended. Imported targets (Git::Git and friends) are skipped.

.PARAMETER RepoPath
  Repo root containing CMakeLists.txt and out\build\<config>.

.PARAMETER Config
  Build configuration under out\build. Defaults to the most recently modified
  one present - NOT to a fixed name. Pass it explicitly if more than one exists.

.PARAMETER Defaults
  JSON file supplying args/env to stamp onto the generated entries. Defaults to
  <RepoPath>\tools\launch_defaults.local.json, then <RepoPath>\launch_defaults.local.json.

  .vs\ is gitignored, so anything you hand-edit into launch.vs.json is lost the
  moment that folder is cleared. Keeping args/env in a durable file and letting
  this script stamp them back is the point.

  Shape - "all" applies to every target, "targets" keys match the projectTarget
  label either exactly or as a wildcard:

    {
      "all":     { "env": { "HONUWARE_DB_HOST": "localhost" } },
      "targets": {
        "*tests.exe*":            { "args": ["--gtest_filter=Foo.*"] },
        "honuware_test_runner.exe": { "env": { "HONUWARE_DB_SSLMODE": "disable" } }
      }
    }

  Values from the defaults file win over what is already in launch.vs.json, since
  that file is disposable. Keys the defaults file does not mention are preserved.

.PARAMETER WhatIf
  Print the resulting launch.vs.json to the console and exit without writing it.
#>
param(
    [Parameter(Mandatory = $true)][string]$RepoPath,
    [string]$Config,
    [string]$Defaults,
    [switch]$WhatIf
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $RepoPath)) { throw "Repo path not found: $RepoPath" }

$buildRoot = Join-Path $RepoPath 'out\build'
if (-not (Test-Path $buildRoot)) {
    throw "No build tree at $buildRoot - configure the project first (cmake --preset <name>)."
}

if ($Config) {
    $configDir = Join-Path $buildRoot $Config
    if (-not (Test-Path $configDir)) { throw "Config not found: $configDir" }
}
else {
    $configDir = Get-ChildItem $buildRoot -Directory |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $configDir) { throw "No configurations under $buildRoot" }
}

$replyDir = Join-Path $configDir '.cmake\api\v1\reply'
if (-not (Test-Path $replyDir)) {
    # CMake only emits a reply when a client has registered a query. VS registers
    # its own, but a build tree configured outside VS (or a freshly deleted one)
    # has none. Register a stateless codemodel query so the next configure emits
    # a reply, rather than dead-ending here.
    $queryDir = Join-Path $configDir '.cmake\api\v1\query'
    New-Item -ItemType Directory -Force -Path $queryDir | Out-Null
    New-Item -ItemType File -Force -Path (Join-Path $queryDir 'codemodel-v2') | Out-Null
    throw @"
No CMake file API reply at $replyDir

A codemodel-v2 query has now been registered for you. Reconfigure the project,
then re-run this script:

    cmake --preset <name>      (from a VS developer prompt)
    - or just open the folder in Visual Studio and let it configure -
"@
}

Write-Host "Repo:   $RepoPath"
Write-Host "Config: $(Split-Path $configDir -Leaf)"

# Enumerate executable targets. Skip imported targets: they carry '::' in the
# name and/or an absolute artifact path outside the build tree.
$targets = @{}
foreach ($file in Get-ChildItem $replyDir -Filter 'target-*.json') {
    $json = Get-Content $file.FullName -Raw | ConvertFrom-Json
    if ($json.type -ne 'EXECUTABLE') { continue }
    if (-not $json.artifacts) { continue }
    if ($json.name -like '*::*') { continue }

    $artifact = $json.artifacts | Select-Object -First 1 -ExpandProperty path
    if ([System.IO.Path]::IsPathRooted($artifact)) { continue }

    # VS convention: bare filename at build root, "file.exe (sub\dir\file.exe)"
    # when nested. Backslash-separated.
    $windowsPath = $artifact -replace '/', '\'
    $exeName = Split-Path $windowsPath -Leaf
    $label = if ($windowsPath -eq $exeName) { $exeName } else { "$exeName ($windowsPath)" }

    $targets[$label] = $true
}

if ($targets.Count -eq 0) { throw "No executable targets found in $replyDir" }

$vsDir = Join-Path $RepoPath '.vs'
$launchPath = Join-Path $vsDir 'launch.vs.json'

# Preserve existing entries so hand-tuned args/env survive.
$existing = @()
$pruned = @()
if (Test-Path $launchPath) {
    $existing = (Get-Content $launchPath -Raw | ConvertFrom-Json).configurations
    if ($null -eq $existing) { $existing = @() }

    # The "Add Debug Configuration" dialog on the root CMakeLists.txt always
    # appends a skeleton with an empty projectTarget (documented behavior - you
    # are meant to fill it in). Invoked repeatedly it stacks up "CMakeLists.txt",
    # "CMakeLists.txt(1)", ... none of which launch anything. Drop them; the real
    # targets below replace what they were meant to become.
    $pruned = @($existing | Where-Object { [string]::IsNullOrWhiteSpace($_.projectTarget) })
    $existing = @($existing | Where-Object { -not [string]::IsNullOrWhiteSpace($_.projectTarget) })
}
$existingKeys = @($existing | ForEach-Object { $_.projectTarget })

$configurations = [System.Collections.ArrayList]::new()
foreach ($e in $existing) { [void]$configurations.Add($e) }

$added = @()
foreach ($label in ($targets.Keys | Sort-Object)) {
    if ($existingKeys -contains $label) { continue }
    [void]$configurations.Add([pscustomobject]@{
        type          = 'default'
        project       = 'CMakeLists.txt'
        projectTarget = $label
        name          = $label
    })
    $added += $label
}

# ---- Stamp args/env from the durable defaults file ---------------------------
# .vs\ is gitignored, so launch.vs.json is disposable and anything hand-edited
# into it is lost whenever that folder is cleared. The defaults file is the
# durable source for args/env; it wins over whatever is currently in the file.
function Merge-LaunchSpec {
    param($Entry, $Spec)

    $touched = @()
    $specNames = $Spec.PSObject.Properties.Name

    if ($specNames -contains 'args' -and $null -ne $Spec.args) {
        $Entry | Add-Member -NotePropertyName 'args' -NotePropertyValue @($Spec.args) -Force
        $touched += 'args'
    }

    if ($specNames -contains 'env' -and $null -ne $Spec.env) {
        $entryNames = $Entry.PSObject.Properties.Name
        $env = if ($entryNames -contains 'env' -and $Entry.env) { $Entry.env } else { [pscustomobject]@{} }
        foreach ($p in $Spec.env.PSObject.Properties) {
            $env | Add-Member -NotePropertyName $p.Name -NotePropertyValue $p.Value -Force
        }
        $Entry | Add-Member -NotePropertyName 'env' -NotePropertyValue $env -Force
        $touched += 'env'
    }

    return $touched
}

if (-not $Defaults) {
    foreach ($candidate in @(
            (Join-Path $RepoPath 'tools\launch_defaults.local.json'),
            (Join-Path $RepoPath 'launch_defaults.local.json'))) {
        if (Test-Path $candidate) { $Defaults = $candidate; break }
    }
}

$stamped = @()
if ($Defaults) {
    if (-not (Test-Path $Defaults)) { throw "Defaults file not found: $Defaults" }
    $def = Get-Content $Defaults -Raw | ConvertFrom-Json
    $defNames = $def.PSObject.Properties.Name

    foreach ($entry in $configurations) {
        $touched = @()

        if ($defNames -contains 'all' -and $def.all) {
            $touched += Merge-LaunchSpec -Entry $entry -Spec $def.all
        }

        if ($defNames -contains 'targets' -and $def.targets) {
            foreach ($p in $def.targets.PSObject.Properties) {
                # Exact label match, or wildcard - so "*tests.exe*" catches the
                # nested "name.exe (test\name.exe)" form without retyping it.
                if ($entry.projectTarget -eq $p.Name -or $entry.projectTarget -like $p.Name) {
                    $touched += Merge-LaunchSpec -Entry $entry -Spec $p.Value
                }
            }
        }

        if ($touched.Count -gt 0) {
            $stamped += "$($entry.projectTarget) [$(($touched | Sort-Object -Unique) -join ', ')]"
        }
    }
}

$doc = [pscustomobject]@{
    version        = '0.2.1'
    defaults       = [pscustomobject]@{}
    configurations = @($configurations)
}

$outJson = $doc | ConvertTo-Json -Depth 10

Write-Host ""
Write-Host "Targets found ($($targets.Count)):"
$targets.Keys | Sort-Object | ForEach-Object { Write-Host "  $_" }
Write-Host ""
if ($pruned.Count -gt 0) {
    Write-Host "Pruning ($($pruned.Count)) entries with empty projectTarget:"
    $pruned | ForEach-Object { Write-Host "  - $($_.name)" }
    Write-Host ""
}
if ($added.Count -gt 0) {
    Write-Host "Adding ($($added.Count)):"
    $added | ForEach-Object { Write-Host "  + $_" }
}
else {
    Write-Host "Nothing to add - all targets already present."
}

if ($Defaults) {
    Write-Host ""
    Write-Host "Defaults: $Defaults"
    if ($stamped.Count -gt 0) {
        $stamped | ForEach-Object { Write-Host "  * $_" }
    }
    else {
        Write-Host "  (no target matched)"
    }
}
else {
    Write-Host ""
    Write-Host "Defaults: none found (looked for tools\launch_defaults.local.json)"
}

if ($WhatIf) {
    Write-Host ""
    Write-Host "--- WhatIf: not written ---"
    Write-Host $outJson
    return
}

if (-not (Test-Path $vsDir)) { New-Item -ItemType Directory -Path $vsDir | Out-Null }
Set-Content -Path $launchPath -Value $outJson -Encoding utf8
Write-Host ""
Write-Host "Wrote $launchPath ($((Get-Item $launchPath).Length) bytes)"
