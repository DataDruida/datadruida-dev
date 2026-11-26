# Compila / graba una app de idf/apps.
#   .\build.ps1 dd-reles
#   .\build.ps1 dd-pantalla flash COM79
#   .\build.ps1 dd-reles -Program examples\02_termostato.c    # con un programa
# -Program copia ese archivo como main/app.c (lo mismo que hará el servicio de
# compilación de /developers) y al terminar deja el app.c vacío de fábrica.
param([Parameter(Mandatory)][string]$App, [string]$Action = "build", [string]$Port = "", [string]$Program = "")
. C:\esp\esp-idf\export.ps1 *> $null
$dir = Join-Path $PSScriptRoot "apps\$App"
$appc = Join-Path $dir "main\app.c"
$backup = $null
if ($Program) {
    $src = if ([System.IO.Path]::IsPathRooted($Program)) { $Program } else { Join-Path $PSScriptRoot $Program }
    $backup = Get-Content -Raw -Encoding utf8 $appc
    Copy-Item -Force $src $appc
    # ⚠ Copy-Item conserva la fecha del original: si es más vieja que la última
    # compilación, ninja no recompila app.c y el binario sale con el programa
    # anterior (pasó: la 4.3B quedó con un programa vacío).
    (Get-Item $appc).LastWriteTime = Get-Date
}
try {
    Set-Location $dir
    if (-not (Test-Path build)) { idf.py set-target esp32s3 2>&1 | Out-File -Encoding utf8 build-log.txt }
    if ($Action -eq "flash") { idf.py -p $Port flash 2>&1 | Out-File -Encoding utf8 -Append build-log.txt }
    else { idf.py build 2>&1 | Out-File -Encoding utf8 -Append build-log.txt }
    "exit=$LASTEXITCODE"
} finally {
    if ($null -ne $backup) { [System.IO.File]::WriteAllText($appc, $backup, (New-Object System.Text.UTF8Encoding $false)) }
}
