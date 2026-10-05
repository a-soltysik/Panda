[CmdletBinding()]
param(
    [ValidateSet(
        "gcc-development",
        "gcc-quality",
        "gcc-unity",
        "gcc-docs",
        "gcc-coverage",
        "gcc-sanitizers",
        "gcc-thread-sanitizer",
        "gcc-tests",
        "clang-tests"
    )]
    [string] $Preset = "gcc-development",
    [string[]] $Target = @(),
    [int] $Jobs = 0,
    [switch] $RunTests,
    [switch] $IncludeSystemTests,
    [string] $TestRegex,
    [switch] $SyncOnly,
    [string] $Distro = "Ubuntu"
)

$ErrorActionPreference = "Stop"
if ($Jobs -lt 0) {
    throw "-Jobs must be 0 (automatic) or a positive integer."
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$wslPathArgument = $repositoryRoot.Replace("\", "/")
$wslRepositoryRoot = [string](& wsl.exe -d $Distro -- wslpath -a -u $wslPathArgument)
if ($LASTEXITCODE -ne 0 -or -not $wslRepositoryRoot) {
    throw "Could not map the repository path into WSL distro '$Distro'."
}
$wslRepositoryRoot = $wslRepositoryRoot.Trim()

$wslScript = "$wslRepositoryRoot/scripts/wsl_build.py"
$arguments = @("--source", $wslRepositoryRoot, "--preset", $Preset)
if ($Jobs -gt 0) {
    $arguments += @("--jobs", "$Jobs")
}
foreach ($buildTarget in $Target) {
    $arguments += @("--target", $buildTarget)
}
if ($RunTests) {
    $arguments += "--run-tests"
}
if ($IncludeSystemTests) {
    $arguments += "--include-system-tests"
}
if ($TestRegex) {
    $arguments += @("--test-regex", $TestRegex)
}
if ($SyncOnly) {
    $arguments += "--sync-only"
}

& wsl.exe -d $Distro -- python3 -B $wslScript @arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
