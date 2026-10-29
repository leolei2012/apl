<#
Compare immutable Git baseline and working root core with the same trace harness.
Only writes to a new system temporary directory; no service, database or device.
Requires installed GCC/size/nm. Baseline is deliberate: never regenerate a golden
from the implementation under test. Resource measurements use -Os, default timings.
#>
param(
    [string]$BaselineRef = '749c6fc',
    [string]$Compiler = 'C:/Qt/Tools/mingw1310_64/bin/gcc.exe'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$checkDir = Join-Path ([System.IO.Path]::GetTempPath()) ('apl-output-trace-' + [guid]::NewGuid().ToString('N'))
$toolsDir = Split-Path -Parent $Compiler
$sizeTool = Join-Path $toolsDir 'size.exe'
$nmTool = Join-Path $toolsDir 'nm.exe'
foreach ($tool in @($Compiler, $sizeTool, $nmTool)) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Required installed tool missing: $tool" }
}
Push-Location -LiteralPath $root
try {
    $revision = git rev-parse --verify ($BaselineRef + '^{commit}')
    if ($LASTEXITCODE -ne 0) { throw 'Baseline revision unavailable' }
    # Support baselines from before/after the apl_lib directory move.
    $baselinePrefix = if (@(git ls-tree --name-only $revision apl_lib).Count) { 'apl_lib/' } else { '' }
    $libraryRoot = Join-Path $root 'apl_lib'
    $protected = @('include/apl.h','include/apl_core.h','include/apl_hmi.h','include/apl_type.h',
        'config/apl_core_config.h','config/apl_hmi_config.h','src/apl.c','src/apl_hmi.c')
    foreach ($path in $protected) {
        $expected = git rev-parse --verify ($revision + ':' + $baselinePrefix + $path)
        if ($LASTEXITCODE -ne 0) { throw "Baseline input missing: $path" }
        $actual = git hash-object -- (Join-Path $libraryRoot $path)
        if ($LASTEXITCODE -ne 0 -or $actual -ne $expected) { throw "Public input differs: $path" }
    }
    New-Item -ItemType Directory -Path $checkDir | Out-Null
    $oldCore = Join-Path $checkDir 'baseline_core.c'
    git show ($revision + ':' + $baselinePrefix + 'src/apl_core.c') | Set-Content -LiteralPath $oldCore -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw 'Could not export baseline core' }
    $modes = @(
        @{Name='both'; Defines=@()},
        @{Name='purify'; Defines=@('-DAPL_COMPILE_HUMIDIFY=0','-DAPL_DEFAULT_HUMIDIFY=0')},
        @{Name='humidify'; Defines=@('-DAPL_COMPILE_PURIFY=0','-DAPL_DEFAULT_PURIFY=0')}
    )
    $common = @('-std=c11','-Wall','-Wextra','-Werror','-finput-charset=UTF-8','-Os','-Iapl_lib/include','-Iapl_lib/config')
    $shortTimes = @('-DAPL_CORE_DRY_S=3','-DAPL_CORE_WATER_SHORTAGE_RUN_S=4','-DAPL_CORE_WATER_BLINK_S=2')
    $report = @()
    foreach ($mode in $modes) {
        $versions = @()
        foreach ($version in @('baseline','working')) {
            $core = if ($version -eq 'baseline') { $oldCore } else { Join-Path $libraryRoot 'src/apl_core.c' }
            $stem = Join-Path $checkDir ($mode.Name + '-' + $version)
            $exe = $stem + '.exe'
            & $Compiler @common @($mode.Defines) @shortTimes tests/test_output_trace.c $core -o $exe
            if ($LASTEXITCODE -ne 0) { throw "Trace compile failed: $stem" }
            & $exe | Set-Content -LiteralPath ($stem + '.trace') -Encoding utf8
            if ($LASTEXITCODE -ne 0) { throw "Trace execution failed: $stem" }
            # Default configuration for resource comparisons, independent of shortened trace timings.
            & $Compiler @common @($mode.Defines) -fstack-usage -c $core -o ($stem + '.o')
            if ($LASTEXITCODE -ne 0) { throw "Resource compile failed: $stem" }
            $sections = @{}
            $sectionRows = & $sizeTool -A ($stem + '.o')
            if ($LASTEXITCODE -ne 0) { throw 'size failed' }
            foreach ($line in $sectionRows) {
                if ($line -match '^(\.[\w.$]+)\s+(\d+)\s+') { $sections[$Matches[1]] = [int]$Matches[2] }
            }
            $symbols = @(& $nmTool --extern-only --defined-only ($stem + '.o') | ForEach-Object {
                if ($_ -match '\s([A-Za-z])\s+(\S+)$') { $Matches[1] + ' ' + $Matches[2] }
            })
            if ($LASTEXITCODE -ne 0) { throw 'nm failed' }
            $stack = @{}
            foreach ($line in Get-Content -LiteralPath ($stem + '.su')) {
                if ($line -match '^.*:\d+:\d+:(\S+)\s+(\d+)\s+(\S+)$') {
                    $stack[$Matches[1]] = [int]$Matches[2]
                }
            }
            $probe = $stem + '-sizes.c'
            $coreInclude = $core.Replace('\','/')
            @"
#include "apl.h"
#include <stdio.h>
#include "$coreInclude"
int main(void) {
    printf("%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n", sizeof(struct apl_core_state),
        _Alignof(struct apl_core_state), sizeof(struct apl_core), sizeof(struct apl_hmi),
        sizeof(struct apl), sizeof(struct apl_core_ops), sizeof(struct apl_link_snapshot),
        sizeof(struct apl_core_memory));
    return 0;
}
"@ | Set-Content -LiteralPath $probe -Encoding utf8
            & $Compiler @common @($mode.Defines) $probe -o ($stem + '-sizes.exe')
            if ($LASTEXITCODE -ne 0) { throw 'Size probe compile failed' }
            $objectSizes = & ($stem + '-sizes.exe')
            if ($LASTEXITCODE -ne 0) { throw 'Size probe failed' }
            $versions += [pscustomobject]@{
                Version=$version; TraceHash=(Get-FileHash -LiteralPath ($stem + '.trace')).Hash
                TracePath=($stem + '.trace'); Sections=$sections; Stack=$stack
                ObjectSizes=$objectSizes; Symbols=$symbols
            }
        }
        $before = $versions[0]; $after = $versions[1]
        if ($before.TraceHash -ne $after.TraceHash) {
            throw "TRACE MISMATCH: $($before.TracePath) <> $($after.TracePath). Inspect first differing record; do not update baseline."
        }
        if ($before.ObjectSizes -ne $after.ObjectSizes) { throw 'Object size/alignment changed' }
        if (Compare-Object $before.Symbols $after.Symbols) { throw 'Exported symbols changed' }
        $lines = (Get-Content -LiteralPath $after.TracePath | Measure-Object -Line).Lines
        Write-Output ("TRACE PASS {0}: {1} records; SHA256={2}" -f $mode.Name,$lines,$after.TraceHash)
        Write-Output ("SIZES {0}: {1} (private size,alignment; core,hmi,apl,ops,snapshot,memory)" -f $mode.Name,$after.ObjectSizes)
        Write-Output ("RESOURCE {0}: text {1}->{2}, data {3}->{4}, bss {5}->{6}; tick stack {7}->{8}" -f `
            $mode.Name,$before.Sections['.text'],$after.Sections['.text'],$before.Sections['.data'],`
            $after.Sections['.data'],$before.Sections['.bss'],$after.Sections['.bss'],`
            $before.Stack['apl_core_tick'],$after.Stack['apl_core_tick'])
        $report += [pscustomobject]@{Mode=$mode.Name; Records=$lines; Versions=$versions}
    }
    [pscustomobject]@{Baseline=$revision; Compiler=$Compiler; Modes=$report} |
        ConvertTo-Json -Depth 9 | Set-Content -LiteralPath (Join-Path $checkDir 'report.json') -Encoding utf8
    Write-Output ('TRACE_REPORT=' + (Join-Path $checkDir 'report.json'))
} finally { Pop-Location }
