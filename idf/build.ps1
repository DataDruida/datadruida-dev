# Compila una app de idf/apps.   .\build.ps1 dd-reles   [flash COMx]
param([Parameter(Mandatory)][string]$App, [string]$Action = "build", [string]$Port = "")
. C:\esp\esp-idf\export.ps1 *> $null
Set-Location (Join-Path $PSScriptRoot "apps\$App")
if (-not (Test-Path build)) { idf.py set-target esp32s3 2>&1 | Out-File -Encoding utf8 build-log.txt }
if ($Action -eq "flash") { idf.py -p $Port flash 2>&1 | Out-File -Encoding utf8 -Append build-log.txt }
else { idf.py build 2>&1 | Out-File -Encoding utf8 -Append build-log.txt }
"exit=$LASTEXITCODE"
