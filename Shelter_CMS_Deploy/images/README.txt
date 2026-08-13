# Smart Shelter CMS — 배포용 Docker 이미지 (tar)

이 폴더에는 **사전 빌드된 Docker 이미지**가 들어 있습니다.  
소스 코드는 포함되지 않습니다.

| 파일 | 이미지 | 용도 |
|------|--------|------|
| `shelter-mosquitto.tar` | `shelter-mosquitto-local:2.0` | MQTT 브로커 (1883) |
| `shelter-web.tar` | `shelter-web-local:2.0` | CMS 웹 서버 (3000) |

## 제조사 — 이미지 생성 방법

개발 PC(`Web_server_mosquitto` 원본)에서:

```powershell
cd Web_server_mosquitto
powershell -ExecutionPolicy Bypass -File _build_deploy_package.ps1
```

또는 수동:

```powershell
docker compose build
docker save shelter-mosquitto-local:2.0 -o ..\Shelter_CMS_Deploy\images\shelter-mosquitto.tar
docker save shelter-web-local:2.0 -o ..\Shelter_CMS_Deploy\images\shelter-web.tar
```

생성 후 `Shelter_CMS_Deploy` 폴더 전체를 USB/압축으로 현장에 배포합니다.

## 현장 — tar 파일 없을 때

`install.bat` 실행 시 tar 없음 오류가 납니다.  
제조사에 **images 폴더 포함 전체 패키지**를 다시 요청하세요.
