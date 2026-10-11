# Puts the camcorder into a ReSkate checkout: copies the model and the ReSkate-side files into
# Extension/Camcorder and applies reskate.patch to ReSkate's own files.
#
#   pwsh integration/reskate/install.ps1 -ReSkate C:\src\ReSkate
#
# The patch is made against the ReSkate commit in reskate.commit; check that one out first
# (git -C C:\src\ReSkate checkout <commit>), or expect the patch to need fixing by hand.
param(
    [Parameter(Mandatory = $true)][string]$ReSkate
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Resolve-Path (Join-Path $here '..\..')
$target = Resolve-Path $ReSkate

if (-not (Test-Path (Join-Path $target 'Extension\HallOfMeat'))) {
    throw "$target does not look like a ReSkate checkout."
}
$expected = (Get-Content (Join-Path $here 'reskate.commit') -Raw).Trim()
$actual = (git -C $target rev-parse HEAD).Trim()
if ($actual -ne $expected) {
    Write-Warning "ReSkate is at $actual; the patch was made against $expected."
}

$model = Join-Path $target 'Extension\Camcorder\Model\camcorder'
New-Item -ItemType Directory -Force -Path $model | Out-Null
Copy-Item -Force (Join-Path $repo 'src\camcorder\*') $model
Copy-Item -Force (Join-Path $here 'Extension\Camcorder\*') (Join-Path $target 'Extension\Camcorder')

git -C $target apply --check --ignore-whitespace (Join-Path $here 'reskate.patch')
if ($LASTEXITCODE -ne 0) { throw 'reskate.patch does not apply (already applied, or a different ReSkate commit).' }
git -C $target apply --ignore-whitespace (Join-Path $here 'reskate.patch')
if ($LASTEXITCODE -ne 0) { throw 'Applying reskate.patch failed.' }
Write-Host "Camcorder installed into $target. Build with (crash uploads off: this is not ReSkate's own build):"
Write-Host '  cmake --preset vs2022-x64 "-DDINGOSDK_BACKTRACE_URL="'
Write-Host "  cmake --build --preset release --parallel 4 --target dingosdk_runtime dingosdk_launcher"
