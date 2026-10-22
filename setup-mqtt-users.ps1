$ErrorActionPreference = 'Stop'

$configDir = Join-Path $PSScriptRoot 'mosquitto'
$configFile = Join-Path $configDir 'mosquitto.conf'
$aclFile = Join-Path $configDir 'aclfile'

if (-not (Test-Path $configFile) -or -not (Test-Path $aclFile)) {
    throw 'Khong tim thay mosquitto.conf hoac aclfile trong thu muc mosquitto.'
}

Write-Host 'Tao tai khoan MQTT esp32. Nhap mat khau khi Docker hoi.' -ForegroundColor Cyan
$mountSpec = "type=bind,source=$configDir,target=/mosquitto/config"
docker run --rm -it --user 0 --mount $mountSpec --entrypoint mosquitto_passwd eclipse-mosquitto:2 -c /mosquitto/config/passwordfile esp32
if ($LASTEXITCODE -ne 0) { throw 'Khong tao duoc tai khoan esp32.' }

Write-Host 'Tao tai khoan MQTT nodered. Nhap mat khau khi Docker hoi.' -ForegroundColor Cyan
docker run --rm -it --user 0 --mount $mountSpec --entrypoint mosquitto_passwd eclipse-mosquitto:2 /mosquitto/config/passwordfile nodered
if ($LASTEXITCODE -ne 0) { throw 'Khong tao duoc tai khoan nodered.' }
if (-not (Test-Path (Join-Path $configDir 'passwordfile'))) {
    throw 'Khong tim thay mosquitto/passwordfile sau khi tao.'
}

Write-Host 'Da tao passwordfile dang bam. Hay luu lai hai mat khau de cau hinh Node-RED va ESP32.' -ForegroundColor Green
