# Smart Shelter CMS — 배포 패키지용 Docker 이미지 빌드·export (개발자 전용)
# Web_server_mosquitto 원본에서 실행 → Shelter_CMS_Deploy/images/*.tar 생성

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Deploy = Join-Path (Split-Path -Parent $Root) "Shelter_CMS_Deploy"
$Images = Join-Path $Deploy "images"

Write-Host "=== Smart Shelter CMS Deploy Package Builder ===" -ForegroundColor Cyan
Write-Host "Source: $Root"
Write-Host "Deploy: $Deploy"

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
  Write-Host "[ERROR] Docker not found" -ForegroundColor Red
  exit 1
}

Push-Location $Root
try {
  Write-Host "`n[1/4] docker compose build..." -ForegroundColor Yellow
  docker compose build
  if ($LASTEXITCODE -ne 0) { throw "docker compose build failed" }

  New-Item -ItemType Directory -Force -Path $Images | Out-Null

  Write-Host "`n[2/4] docker save shelter-mosquitto-local:2.0..." -ForegroundColor Yellow
  docker save shelter-mosquitto-local:2.0 -o (Join-Path $Images "shelter-mosquitto.tar")

  Write-Host "`n[3/4] docker save shelter-web-local:2.0..." -ForegroundColor Yellow
  docker save shelter-web-local:2.0 -o (Join-Path $Images "shelter-web.tar")

  Write-Host "`n[4/4] Copy compose helpers..." -ForegroundColor Yellow
  Copy-Item (Join-Path $Root "detect-host-ip.ps1") (Join-Path $Deploy "detect-host-ip.ps1") -Force

  Write-Host "`n=== Done ===" -ForegroundColor Green
  Write-Host "Deploy folder: $Deploy"
  Get-ChildItem $Images -Filter *.tar | ForEach-Object {
    Write-Host "  $($_.Name)  $([math]::Round($_.Length/1MB, 1)) MB"
  }
  Write-Host "`nZip Shelter_CMS_Deploy and ship to site (no Web_server_mosquitto source)."
}
finally {
  Pop-Location
}
