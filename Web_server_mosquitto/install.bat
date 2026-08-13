@echo off
chcp 64901 > nul
title 스마트쉘터 CMS 통합관제 자동 설치기
cls

echo =================================================================
echo  [SYSTEM] 스마트쉘터 CMS 로컬 가동 인프라 자동 빌드 시퀀스 시작
echo =================================================================
echo.

:: 1. 기존 가동 중인 찌꺼기 컨테이너가 있다면 충돌 방지를 위해 선제적 초기화
echo [1/3] 기존 인프라 충돌 테스트 및 가상 포트 세션 청소 중...
docker compose down > nul 2>&1
docker rm -f shelter-web-local shelter-mosquitto > nul 2>&1
echo --- 완료.
echo.

:: 2. 호스트 PC LAN IP 자동 감지 (.env 생성 — Docker 컨테이너가 제어보드에 알려줄 브로커 IP)
echo [2/4] 호스트 PC LAN IP 자동 감지 중...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0detect-host-ip.ps1"
if errorlevel 1 (
  echo [WARN] LAN IP 자동 감지 실패. Web_server_mosquitto\.env 파일에 SERVER_LAN_IP=192.168.0.xxx 를 직접 입력하세요.
) else (
  type "%~dp0.env"
)
echo.

:: 3. 전달받은 완제품 바이너리 tar 이미지 강제 주입 (소스코드 은닉 핵심)
echo [3/4] 도커 완제품 패키지 이미지 파일 마운트 중... (약 15~30초 소요)
docker load -i shelter-web-image.tar
echo --- 이미지 안착 완료.
echo.

:: 4. 주입된 이미지를 기반으로 모스키토 브로커와 웹 관제 마스터 백그라운드 시동
echo [4/4] 통합 관제 마스터 엔진 및 모스키토 브로커 백그라운드 기동 시작...
docker compose up -d
echo --- 컨테이너 가동 바인딩 완료.
echo.

echo =================================================================
echo  🎉 스마트쉘터 CMS 인프라가 무결하게 최종 가동되었습니다!
echo.
echo  ▶ 관제 대시보드 주소 : http://localhost:3000
echo  ▶ 분전반/하드웨어 연동 포트 : 1883
echo =================================================================
echo.
pause
