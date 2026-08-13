@echo off
chcp 65001 > nul
cd /d "%~dp0"
echo Smart Shelter CMS 재시작...
docker compose restart
echo 완료.
pause
