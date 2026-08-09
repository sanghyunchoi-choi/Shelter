FROM node:20-alpine

WORKDIR /app

# 1. 의존성 파일만 먼저 레이어로 복사
COPY package.json ./

# 2. 로컬 찌꺼기와 상관없이 리눅스 컨테이너 전용으로 클린 갱신 설치
RUN rm -rf node_modules package-lock.json && npm install

# 3. 그 다음 실제 소스코드 파일들만 선별 복사
COPY server.js ./
COPY public/ ./public/

EXPOSE 3000

CMD ["node", "server.js"]
