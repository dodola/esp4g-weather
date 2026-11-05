param(
    [Parameter(Position=0)]
    [ValidateSet('build', 'menuconfig', 'flash', 'monitor', 'size', 'reconfigure')]
    [string]$Action = 'build',
    [string]$Port,
    [string]$IdfPath = $env:IDF_PATH,
    [string]$ToolsPath = $env:IDF_TOOLS_PATH
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent

# Explicit SDK paths take precedence over the installer configuration.
if (-not $IdfPath) {
    $installFile = 'C:\Espressif\tools\eim_idf.json'
    if (Test-Path -LiteralPath $installFile) {
        $installs = Get-Content -LiteralPath $installFile -Raw | ConvertFrom-Json
        $selected = $installs.idfInstalled | Where-Object { $_.id -eq $installs.idfSelectedId } | Select-Object -First 1
        if ($selected) {
            $IdfPath = $selected.path
            if (-not $PSBoundParameters.ContainsKey('ToolsPath')) { $ToolsPath = $selected.idfToolsPath }
            $env:IDF_PYTHON_ENV_PATH = Split-Path (Split-Path $selected.python -Parent) -Parent
            $activationScript = $selected.activationScript
        }
    }
}
if (-not $IdfPath -or -not (Test-Path -LiteralPath (Join-Path $IdfPath 'export.ps1'))) {
    throw 'ESP-IDF >=5.5.2 is required. Set IDF_PATH or pass -IdfPath.'
}
$env:IDF_PATH = $IdfPath
if ($ToolsPath) { $env:IDF_TOOLS_PATH = $ToolsPath }
if ($activationScript -and (Test-Path -LiteralPath $activationScript)) {
    . $activationScript
} else {
    . (Join-Path $IdfPath 'export.ps1')
}
if (-not $?) { throw 'ESP-IDF environment activation failed' }
$idfArgs = @('-C', $projectRoot)
if ($Port) { $idfArgs += @('-p', $Port) }
if ($Action -eq 'flash' -or $Action -eq 'monitor') {
    if (-not $Port) { throw 'Specify the ESP32 USB port with -Port COMxx.' }
}
$idfArgs += $Action
$pythonExe = if ($env:IDF_PYTHON_ENV_PATH) { Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe' } else { 'python' }
& $pythonExe (Join-Path $IdfPath 'tools\idf.py') @idfArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
