Smart Shelter CMS — 현장 배포 패키지 (소스 코드 미포함)
======================================================

[ 설치 ]
  1. Docker Desktop 설치 및 실행
  2. install.bat 실행
  3. http://localhost:3000

1. Docker Desktop 설치·실행
2. (권장) C:\ShelterCMS 등 짧은 경로에 폴더 복사
3. install.bat 실행
4. http://localhost:3000 (공무원: http://관제PC_IP:3000)

[ 문서 ]
  docs\설치_가이드.md       — 관제 PC 설치
  docs\운영_가이드_공무원.md — 공무원 브라우저 접속
  docs\네트워크_구성.md     — LAN·IP·포트

[ 엑셀 도식 ]
  ..\Protocol\Shelter_Ops_Deploy.xlsx (Protocol 폴더)
  ..\Protocol\Shelter_Ops_Deploy_v1.0.xlsx (rev 1.2 납품)
  ..\Protocol\Shelter_Protocol_v1.0.xlsx
  ..\Protocol\개발가이드_v1.0.docx

[ images 폴더 ]
  shelter-mosquitto.tar, shelter-web.tar 필수
  없으면 Web_server_mosquitto\_build_deploy_package.ps1 로 제조사에서 생성

==========================================================
[다른 PC에 설치시]
최소로 옮길 파일 
Shelter_CMS_Deploy/
├── install.bat
├── docker-compose.yml
├── detect-host-ip.ps1
├── images/
│   ├── shelter-mosquitto.tar    ← 필수
│   └── shelter-web.tar          ← 필수
└── .env.example                 ← 권장 (IP 자동 감지 실패 시만 사용)
>>>>>>>>>>>>>>
추가확인
방화벽 		TCP 3000, 1883 허용 (다른 PC·제어보드 접속 시)
제어보드		MQTT 브로커 IP = 이 관제 PC의 LAN IP (예: 192.168.0.107)
공무원 PC		별도 설치 없음 → http://관제PC_IP:3000

>>>>>>>>>>>>>>
Docker Desktop 설치
Docker Desktop 실행 (트레이 아이콘 Running 상태)
위 폴더를 PC에 복사 (가능하면 C:\ShelterCMS 같은 짧은 경로)
install.bat 더블클릭
브라우저: http://localhost:3000
→ 이때 Mosquitto(1883) + CMS(3000) 둘 다 같이 올라갑니다.