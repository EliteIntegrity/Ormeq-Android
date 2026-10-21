# Fetches the pinned third-party sources into third_party/ (not tracked in Git).
# Re-run after changing a pin; a checkout at a different revision is replaced.
# tools/fetch-deps.sh does the same on Linux and macOS.
param([switch]$Force)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$thirdParty = Join-Path $root 'third_party'

# Ormeq mixes its own sound: of SDL_mixer only its SDL-adapted stb_vorbis is
# used, for the Ogg music and ambience. SDL_ttf brings FreeType and HarfBuzz
# as submodules.
$deps = @(
    @{ Name = 'SDL';       Url = 'https://github.com/libsdl-org/SDL.git';       Tag = 'release-3.4.16'; Commit = 'fa2c02bb6e21974a89ea9824bc53c9932abe5f9c'; Submodules = $false },
    @{ Name = 'SDL_image'; Url = 'https://github.com/libsdl-org/SDL_image.git'; Tag = 'release-3.4.6';  Commit = 'f661fa1ad24ab1b81e43662532f9a6a9fcf67ea6'; Submodules = $false },
    @{ Name = 'SDL_ttf';   Url = 'https://github.com/libsdl-org/SDL_ttf.git';   Tag = 'release-3.2.2';  Commit = 'a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b'; Submodules = $true },
    @{ Name = 'SDL_mixer'; Url = 'https://github.com/libsdl-org/SDL_mixer.git'; Tag = 'release-3.2.4';  Commit = '72a81869b45e249e8e67102db4e98dd2441f05a1'; Submodules = $false }
)

New-Item -ItemType Directory -Force $thirdParty | Out-Null

function Get-Commit([string]$dir) {
    if (-not (Test-Path (Join-Path $dir '.git'))) { return $null }
    return (git -C $dir rev-parse HEAD 2>$null)
}

# git reports its progress on stderr, which Windows PowerShell turns into errors
# when the output is captured (as in a build service); its exit code is what
# says whether it worked.
function Invoke-Git([string[]]$GitArgs) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & git @GitArgs 2>&1 | ForEach-Object { Write-Host "$_" }
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
}

foreach ($dep in $deps) {
    $dir = Join-Path $thirdParty $dep.Name
    if ((Get-Commit $dir) -eq $dep.Commit -and -not $Force) {
        Write-Host "$($dep.Name): $($dep.Tag) already present"
        continue
    }
    if (Test-Path $dir) {
        Write-Host "$($dep.Name): replacing the existing checkout"
        Remove-Item -Recurse -Force -LiteralPath $dir
    }
    $cloneArgs = @('clone', '--quiet', '--depth', '1', '--branch', $dep.Tag, '-c', 'advice.detachedHead=false')
    if ($dep.Submodules) { $cloneArgs += @('--recurse-submodules', '--shallow-submodules') }
    $cloneArgs += @($dep.Url, $dir)
    Write-Host "$($dep.Name): cloning $($dep.Tag)"
    if ((Invoke-Git $cloneArgs) -ne 0) { throw "Clone failed for $($dep.Name)" }
    if ((Get-Commit $dir) -ne $dep.Commit) { throw "$($dep.Name): $($dep.Tag) is not the pinned commit $($dep.Commit)" }
}

Write-Host 'Third-party sources ready.'
