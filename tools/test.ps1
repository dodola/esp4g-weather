param([string]$CompilerBin = 'C:\msys64\ucrt64\bin',
      [string]$IdfPath = 'C:\esp\v5.5.2\esp-idf',
      [string]$Fixture)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'tmp\tests'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$env:PATH = "$CompilerBin;$env:PATH"
$jsonDir = Join-Path $IdfPath 'components\json\cJSON'
& (Join-Path $CompilerBin 'gcc.exe') -c (Join-Path $jsonDir 'cJSON.c') -I $jsonDir -o (Join-Path $out 'cJSON.o')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $CompilerBin 'g++.exe') -std=c++23 -Wall -Wextra -Werror -finput-charset=UTF-8 -fexec-charset=UTF-8 -I (Join-Path $root 'main') -I $jsonDir (Join-Path $root 'tests\weather_data_test.cpp') (Join-Path $root 'main\weather_data.cpp') (Join-Path $out 'cJSON.o') -o (Join-Path $out 'weather_data_test.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Fixture) { & (Join-Path $out 'weather_data_test.exe') $Fixture }
else { & (Join-Path $out 'weather_data_test.exe') }
exit $LASTEXITCODE
