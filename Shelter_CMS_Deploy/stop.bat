@echo off
chcp 65001 > nul
cd /d "%~dp0"
echo Smart Shelter CMS 서비스 중지...
docker compose down
echo 완료.
pause
