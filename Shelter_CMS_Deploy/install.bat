@echo off
chcp 65001 > nul
title Smart Shelter CMS 배포 설치
cls

echo =================================================================
echo  Smart Shelter CMS — 현장 관제 서버 설치 (배포판)
echo  Mosquitto 1883 + CMS 웹 3000
echo =================================================================
echo.

where docker > nul 2>&1
if errorlevel 1 (
  echo [ERROR] Docker Desktop 이 설치되어 있지 않습니다.
  echo         https://www.docker.com/products/docker-desktop/ 설치 후 다시 실행하세요.
  pause
  exit /b 1
)

docker info > nul 2>&1
if errorlevel 1 (
  echo [ERROR] Docker Desktop 이 실행 중이 아닙니다. Docker를 시작한 뒤 다시 실행하세요.
  pause
  exit /b 1
)

if not exist "%~dp0images\shelter-mosquitto.tar" (
  echo [ERROR] images\shelter-mosquitto.tar 파일이 없습니다.
  echo         제조사에 배포 패키지 전체(images 폴더 포함)를 요청하세요.
  pause
  exit /b 1
)
if not exist "%~dp0images\shelter-web.tar" (
  echo [ERROR] images\shelter-web.tar 파일이 없습니다.
  echo         제조사에 배포 패키지 전체(images 폴더 포함)를 요청하세요.
  pause
  exit /b 1
)

echo [1/5] 기존 컨테이너 정리...
docker compose -f "%~dp0docker-compose.yml" down > nul 2>&1
docker rm -f shelter-web-local shelter-mosquitto > nul 2>&1
echo --- 완료.
echo.

echo [2/5] 관제 PC LAN IP 자동 감지...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0detect-host-ip.ps1"
if errorlevel 1 (
  echo [WARN] LAN IP 자동 감지 실패.
  echo        .env.example 를 .env 로 복사한 뒤 SERVER_LAN_IP=실제IP 를 입력하세요.
  if not exist "%~dp0.env" copy /Y "%~dp0.env.example" "%~dp0.env" > nul
) else (
  if exist "%~dp0.env" type "%~dp0.env"
)
echo.

echo [3/5] Docker 이미지 로드 (Mosquitto)...
docker load -i "%~dp0images\shelter-mosquitto.tar"
if errorlevel 1 (
  echo [ERROR] shelter-mosquitto.tar 로드 실패
  pause
  exit /b 1
)
echo.

echo [4/5] Docker 이미지 로드 (CMS 웹)...
docker load -i "%~dp0images\shelter-web.tar"
if errorlevel 1 (
  echo [ERROR] shelter-web.tar 로드 실패
  pause
  exit /b 1
)
echo.

echo [5/5] 서비스 기동...
cd /d "%~dp0"
docker compose up -d
if errorlevel 1 (
  echo [ERROR] docker compose up 실패
  echo        경로에 특수문자가 있으면 C:\ShelterCMS 등 짧은 경로로 복사 후 재시도하세요.
  pause
  exit /b 1
)
echo.

echo =================================================================
echo  설치 완료
echo.
echo  ▶ 이 PC에서 CMS     : http://localhost:3000
echo  ▶ 다른 PC(공무원)   : http://[관제PC_LAN_IP]:3000
echo  ▶ MQTT 브로커       : [관제PC_LAN_IP]:1883  ^(제어보드 설정용^)
echo.
echo  제어보드 최초 설정 시 MQTT 브로커 IP = 위 LAN IP
echo  상세: docs\설치_가이드.md
echo =================================================================
echo.
pause
