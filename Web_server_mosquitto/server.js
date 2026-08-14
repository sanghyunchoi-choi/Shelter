const express = require('express'); 
const http = require('http'); 
const { Server } = require('socket.io'); 
const mqtt = require('mqtt'); 
const fs = require('fs');
const os = require('os');
const path = require('path'); 

const PORT = process.env.PORT || 3000; 
const MQTT_BROKER = process.env.MQTT_BROKER || 'mqtt://localhost:1883'; 
const DEVICE_TIMEOUT_MS = 5 * 60 * 1000; 

// [2026-08-14 신규] 처리 안 된 예외로 서버 프로세스 전체가 죽는 것을 방지.
// 지금까지는 이런 안전장치가 없어서, MQTT 메시지 하나라도 예상 못한 형식으로
// 오면(JSON.parse 실패 등) 서버 전체가 조용히 죽고 스케줄/웹UI가 전부 멈출 수
// 있었습니다. 죽이지 않고 로그만 남기도록 합니다.
process.on('uncaughtException', (err) => {
 console.error('[FATAL] Uncaught Exception (서버는 계속 실행됩니다):', err);
});
process.on('unhandledRejection', (reason) => {
 console.error('[FATAL] Unhandled Promise Rejection (서버는 계속 실행됩니다):', reason);
});

function isVirtualInterface(name) {
 const n = (name || '').toLowerCase();
 return /docker|veth|br-|vmware|virtualbox|vboxnet|hyper-v|vethernet|wsl|loopback|npcap|tap|tun|bluetooth|wireguard|zerotier|tailscale|hamachi|singbox|clash|meta|default switch|nat|vether/i.test(n);
}

function isLikelyVirtualIp(ip) {
 const p = ip.split('.').map(Number);
 if (p[0] === 127) return true;
 if (p[0] === 169 && p[1] === 254) return true;
 if (p[0] === 172 && p[1] >= 16 && p[1] <= 31) return true;
 if (p[0] === 192 && p[1] === 168 && p[2] === 56) return true;
 return false;
}

function scoreLanCandidate(ip, ifaceName) {
 let score = 0;
 const n = (ifaceName || '').toLowerCase();
 if (ip.startsWith('192.168.')) score += 100;
 else if (ip.startsWith('10.')) score += 40;
 else score += 10;
 if (/wi-?fi|wlan|wireless|무선/i.test(ifaceName || '')) score += 30;
 if (/ethernet|이더넷|lan|local area/i.test(ifaceName || '') && !/virtual|hyper|vmware|vbox/i.test(n)) score += 25;
 if (/virtual|hyper|vmware|vbox|docker|wsl|vether/i.test(n)) score -= 200;
 return score;
}

function getLocalServerIp() {
 if (process.env.SERVER_LAN_IP) return process.env.SERVER_LAN_IP.trim();

 const candidates = [];
 for (const [name, addrs] of Object.entries(os.networkInterfaces())) {
  if (isVirtualInterface(name)) continue;
  for (const net of addrs || []) {
   const family = net.family;
   if (family !== 'IPv4' && family !== 4) continue;
   if (net.internal) continue;
   if (isLikelyVirtualIp(net.address)) continue;
   candidates.push({ ip: net.address, score: scoreLanCandidate(net.address, name), name });
  }
 }
 candidates.sort((a, b) => b.score - a.score);
 if (candidates.length) {
  console.log(`[SERVER] LAN IP 자동 감지: ${candidates[0].ip} (${candidates[0].name})`);
  return candidates[0].ip;
 }

 const inDocker = fs.existsSync('/.dockerenv') || process.env.DOCKER === 'true';
 if (inDocker) {
  console.warn('[SERVER] Docker 컨테이너 내부 실행 — 컨테이너 IP(172.x)는 제어보드용이 아닙니다.');
  console.warn('[SERVER] docker-compose.yml 또는 .env 에 SERVER_LAN_IP=호스트PC_LAN_IP 를 설정하세요. (예: 192.168.0.107)');
 }
 return process.env.HOST_LAN_IP || '127.0.0.1';
}

const SERVER_LAN_IP = getLocalServerIp();

const TOPICS = { 
 TELE_STATE: 'dev/tele/state', TELE_DUST: 'dev/tele/dust', 
 TELE_TH_IN: 'dev/tele/th_in', TELE_TH_OUT: 'dev/tele/th_out', 
 TELE_AD: 'dev/tele/ad', TELE_FAN: 'dev/tele/fan', 
 TELE_PB: 'dev/tele/pb', TELE_AP: 'dev/tele/ap', TELE_AC: 'dev/tele/ac', 
 TELE_INPUT: 'dev/tele/input',
 CMD_AD: 'dev/cmnd/ad', CMD_FAN: 'dev/cmnd/fan', CMD_PB: 'dev/cmnd/pb', 
 CMD_AP: 'dev/cmnd/ap', CMD_AC: 'dev/cmnd/ac', CMD_DEV: 'dev/cmnd/dev',
 CMD_INPUT: 'dev/cmnd/input',
 STAT_AC: 'dev/stat/ac', STAT_AP: 'dev/stat/ap', STAT_FAN: 'dev/stat/fan',
 STAT_PB: 'dev/stat/pb', STAT_AD: 'dev/stat/ad', STAT_DEV: 'dev/stat/dev',
 STAT_INPUT: 'dev/stat/input'
}; 

const app = express(); 
const server = http.createServer(app); 
const io = new Server(server, { transports: ['websocket'], allowUpgrades: false }); 

app.use(express.json()); 

let activeSessions = new Set(); 
app.post('/api/login', (req, res) => {
 const { id, pw } = req.body;
 if (id === 'smart' && pw === 'smart') {
 const token = 'session_' + Math.random().toString(36).substring(2);
 activeSessions.add(token);
 return res.json({ success: true, token });
 }
 return res.json({ success: false, message: '인증 실패' });
}); 

let hourlyPowerStats = Array(24).fill(0); 
// [v2.0 신규] 실시간 개별 채널 전력 그래프용 롤링 히스토리
// PB tele(10초 주기)가 올 때마다 1개씩 추가, 최대 360개(1시간) 유지
const POWER_HISTORY_MAX = 360;
let powerHistory = [];
// 1분 단위 합산 전력 (약 10초 샘플 6건 → 분당 평균 합산 W)
const POWER_MINUTE_MAX = 120;
let powerMinuteHistory = [];
let powerMinuteBucket = { minuteKey: null, sum: 0, count: 0 };
let lastOfflineMinuteKey = null;
let lastPbTeleAt = 0;
let lastPbDataValid = false;
const PB_TELE_STALE_MS = 35000;
let mqttLogSession = []; 

// [2026-08-14 신규] 스케줄 설정을 파일로 저장 — 서버 재시작해도 설정이 유지되도록.
// 기존에는 순수 메모리 변수라 서버가 재시작되면(PC 재부팅, 크래시, 터미널 종료 등)
// 설정한 스케줄이 조용히 기본값으로 초기화되는 문제가 있었습니다.
const SCHEDULE_CONFIG_PATH = path.join(__dirname, 'schedule_config.json');

function loadScheduleConfig() {
 const defaults = {
  morningHour: 7, morningMin: 0,
  morningFlags: { ac: true, ap: true, ad: true, pb: true },
  morningAdChannels: Array(15).fill(1),
  morningPbChannels: Array(8).fill(1),
  nightHour: 20, nightMin: 0,
  nightFlags: { ac: true, ap: true, ad: true, pb: true },
  nightAdChannels: Array(15).fill(0),
  nightPbChannels: Array(8).fill(0)
 };
 try {
  if (fs.existsSync(SCHEDULE_CONFIG_PATH)) {
   const saved = JSON.parse(fs.readFileSync(SCHEDULE_CONFIG_PATH, 'utf8'));
   console.log(`[CONFIG_SYNC] 저장된 스케줄 설정 복원: 출근 ${saved.morningHour}:${saved.morningMin} / 퇴근 ${saved.nightHour}:${saved.nightMin}`);
   return { ...defaults, ...saved };
  }
 } catch (e) {
  console.error('[CONFIG_SYNC] 스케줄 설정 파일 로드 실패, 기본값 사용:', e.message);
 }
 return defaults;
}

function saveScheduleConfig() {
 try {
  fs.writeFileSync(SCHEDULE_CONFIG_PATH, JSON.stringify(globalScheduleConfig, null, 2), 'utf8');
 } catch (e) {
  console.error('[CONFIG_SYNC] 스케줄 설정 파일 저장 실패:', e.message);
 }
}

let globalScheduleConfig = loadScheduleConfig();

// 뼈대 마스터 상태 메모리
const deviceState = { 
 state: { status: 'OFFLINE', uid: '', ip: '', mode: '', conn: 0 }, 
 dust: { pm1_0: 0, pm2_5: 0, pm10: 0, conn: 0 }, 
 th_in: { temp: 0, humi: 0, conn: 0 }, 
 th_out: { temp: 0, humi: 0, conn: 0 }, 
 ad: { relays: Array(15).fill(0), conn: 0 }, 
 fan: { mode: 'AUTO', duty: 0, rpm: 0, conn: 0 }, 
 pb: { current_b_id: 0, channels: Array(8).fill(0).map((_,i)=>({b_id:0, ch:i+1, sw:0, w:0, c:0.0, v:220})), conn: 0 }, 
 ap: { pwr: 0, mode: 'AUTO', spd: 0, uv: 0, co2: 0, pm25: 0, pm10: 0, pm1_0: 0, temp: 0, temp_out: 0, humi: 0, tvoc: 0, filter: 100, conn: 0 }, 
 ac: { pwr: 0, mode: 'COOL', temp: 24, curr: 0, spd: 1, err: 0, conn: 0 },
 // [v2.0 신규] 외부 입력 4채널 (PD4~PD7)
 input: { p4: 0, p5: 0, p6: 0, p7: 0, cnt4: 0, cnt5: 0, cnt6: 0, cnt7: 0, conn: 0 }
}; 

let deviceTimeoutTimer = null;
function normalizeConn(payload, fallback = 0) {
 if (payload && payload.conn !== undefined) {
  return payload.conn === 1 || payload.conn === true ? 1 : 0;
 }
 return fallback;
}
function resetDeviceTimeout() {
 if (deviceTimeoutTimer) clearTimeout(deviceTimeoutTimer);
 /* 메인보드 MQTT 생존만 갱신 — 하위 장치 conn 은 각 tele/stat payload 값만 사용 */
 deviceState.state.status = 'ONLINE';
 deviceState.state.conn = 1;
 deviceTimeoutTimer = setTimeout(() => { setAllOffline(); }, DEVICE_TIMEOUT_MS);
}

function setAllOffline() {
 Object.keys(deviceState).forEach(k => { if (k !== 'pb') deviceState[k].conn = 0; });
 deviceState.dust = { pm1_0: 0, pm2_5: 0, pm10: 0, conn: 0 };
 deviceState.th_in = { temp: 0, humi: 0, conn: 0 }; 
 deviceState.th_out = { temp: 0, humi: 0, conn: 0 };
 deviceState.ad.relays = Array(15).fill(0);
 deviceState.state.status = 'OFFLINE'; deviceState.state.conn = 0;
 io.emit('device_update', { topic: TOPICS.TELE_STATE, data: { status: 'OFFLINE', conn: 0 } });
 io.emit('clear_ui_reset', { message: 'RESET_DATA' });
}

const TELE_KEY_MAP = { 
 'dev/stat/pb': 'pb', 
 [TOPICS.TELE_STATE]: 'state', [TOPICS.TELE_DUST]: 'dust', [TOPICS.TELE_TH_IN]: 'th_in', 
 [TOPICS.TELE_TH_OUT]: 'th_out', [TOPICS.TELE_AD]: 'ad', [TOPICS.TELE_FAN]: 'fan', 
 [TOPICS.TELE_PB]: 'pb', [TOPICS.TELE_AP]: 'ap', [TOPICS.TELE_AC]: 'ac',
 [TOPICS.TELE_INPUT]: 'input'
};

const STAT_KEY_MAP = {
 [TOPICS.STAT_AC]: 'ac', [TOPICS.STAT_AP]: 'ap', [TOPICS.STAT_FAN]: 'fan',
 [TOPICS.STAT_INPUT]: 'input', [TOPICS.STAT_AD]: 'ad'
};

const TELE_TOPIC_BY_KEY = {
 state: TOPICS.TELE_STATE, dust: TOPICS.TELE_DUST, th_in: TOPICS.TELE_TH_IN,
 th_out: TOPICS.TELE_TH_OUT, ad: TOPICS.TELE_AD, fan: TOPICS.TELE_FAN,
 pb: TOPICS.TELE_PB, ap: TOPICS.TELE_AP, ac: TOPICS.TELE_AC, input: TOPICS.TELE_INPUT
};

function publishRefreshPolls() {
 const activeBid = deviceState.pb.current_b_id;
 mqttClient.publish(TOPICS.CMD_AC, JSON.stringify({ get_ac: 'state' }), { qos: 1 });
 mqttClient.publish(TOPICS.CMD_AP, JSON.stringify({ get_ap: 'state' }), { qos: 1 });
 mqttClient.publish(TOPICS.CMD_FAN, JSON.stringify({ get_fan: 'state' }), { qos: 1 });
 mqttClient.publish(TOPICS.CMD_AD, JSON.stringify({ get_ad: 'state' }), { qos: 1 });
 mqttClient.publish(TOPICS.CMD_INPUT, JSON.stringify({ get_input: 'state' }), { qos: 1 });
 if (isPbCmdReady()) {
  mqttClient.publish(TOPICS.CMD_PB, JSON.stringify({ b_id: activeBid, get_pb: 'state' }), { qos: 1 });
 }
}

const mqttOptions = { 
 clientId: `shelter-core-server-${Date.now()}`, 
 reconnectPeriod: 3000, 
 keepalive: 60
}; 

const mqttClient = mqtt.connect(MQTT_BROKER, mqttOptions); 

mqttClient.on('connect', () => {
 console.log(`[Mosquitto 로컬 브로커망] 마스터 스케줄 관제 코어 결속 가동 완료!`);
 const allSubTopics = [
 ...Object.keys(TELE_KEY_MAP),
 ...Object.keys(STAT_KEY_MAP),
 'dev/cmnd/ad', 'dev/cmnd/fan', 'dev/cmnd/pb', 'dev/cmnd/ap', 'dev/cmnd/ac', 'dev/cmnd/dev',
 TOPICS.STAT_DEV
 ];
 mqttClient.subscribe(allSubTopics);
}); 

// [v2.0 신규] 펌웨어의 {"in":[p4,p5,p6,p7],"count":[c4,c5,c6,c7],"conn":1} 형식을
// 웹 UI가 다루기 쉬운 평평한(flat) 구조로 변환
function normalizeInputPayload(payload) {
 if (payload && Array.isArray(payload.in) && Array.isArray(payload.count)) {
 return {
 p4: payload.in[0] ? 1 : 0, p5: payload.in[1] ? 1 : 0,
 p6: payload.in[2] ? 1 : 0, p7: payload.in[3] ? 1 : 0,
 cnt4: payload.count[0] || 0, cnt5: payload.count[1] || 0,
 cnt6: payload.count[2] || 0, cnt7: payload.count[3] || 0,
 conn: payload.conn !== undefined ? payload.conn : 1
 };
 }
 return payload;
}

function getMinuteKey(date) {
 const d = date || new Date();
 const kst = new Date(d.toLocaleString('en-US', { timeZone: 'Asia/Seoul' }));
 return `${kst.getFullYear()}-${kst.getMonth()}-${kst.getDate()}-${kst.getHours()}-${kst.getMinutes()}`;
}

function getKstHour(date) {
 const d = date || new Date();
 return new Date(d.toLocaleString('en-US', { timeZone: 'Asia/Seoul' })).getHours();
}

function emitPowerMinutePoint(minuteKey, total, samples, offline) {
 const point = {
  t: Date.now(),
  minuteKey,
  total: Math.round((total || 0) * 10) / 10,
  samples: samples || 0,
  offline: !!offline
 };
 powerMinuteHistory.push(point);
 if (powerMinuteHistory.length > POWER_MINUTE_MAX) powerMinuteHistory.shift();
 io.emit('power_minute', point);
 return point;
}

function emitZeroPowerSample(offline) {
 const sample = {
  t: Date.now(),
  ch: Array(8).fill(0),
  total: 0,
  offline: !!offline
 };
 powerHistory.push(sample);
 if (powerHistory.length > POWER_HISTORY_MAX) powerHistory.shift();
 io.emit('power_sample', sample);
 return sample;
}

function finalizePowerMinuteBucket(forceKey) {
 const avg = powerMinuteBucket.count ? (powerMinuteBucket.sum / powerMinuteBucket.count) : 0;
 return {
  t: Date.now(),
  minuteKey: forceKey || powerMinuteBucket.minuteKey,
  total: Math.round(avg * 10) / 10,
  samples: powerMinuteBucket.count
 };
}

function isPbGraphOnline() {
 return lastPbDataValid && lastPbTeleAt > 0 && (Date.now() - lastPbTeleAt < PB_TELE_STALE_MS);
}

function isPbCmdReady() {
 return lastPbDataValid && deviceState.pb.conn === 1;
}

function ingestPbOfflineSample() {
 emitZeroPowerSample(true);
 ingestPowerSample(0);
}

function ensureInstantAnchorForMinute(minuteKey, total, offline) {
 const hasAnchor = powerHistory.some(s => getMinuteKey(new Date(s.t)) === minuteKey);
 if (!hasAnchor) emitZeroPowerSample(offline);
 else if (total === 0 && !offline) {
  const sample = { t: Date.now(), ch: Array(8).fill(0), total: 0, offline: false };
  powerHistory.push(sample);
  if (powerHistory.length > POWER_HISTORY_MAX) powerHistory.shift();
  io.emit('power_sample', sample);
 }
}

function ingestPowerSample(totalPowerSum) {
 const minuteKey = getMinuteKey();
 if (powerMinuteBucket.minuteKey === null) {
  powerMinuteBucket.minuteKey = minuteKey;
 } else if (powerMinuteBucket.minuteKey !== minuteKey) {
  powerMinuteBucket = { minuteKey, sum: totalPowerSum, count: 1 };
  return;
 }
 powerMinuteBucket.sum += totalPowerSum;
 powerMinuteBucket.count += 1;
}

// ⚡ 센서 데이터 원본 인입 즉시 UI 전체 매핑 바이패스 락 해제
mqttClient.on('message', (topic, message) => {
 const rawMsg = message.toString();
 const logItem = { time: new Date().toLocaleTimeString('ko-KR', { hour12: false, timeZone: 'Asia/Seoul' }), topic, payload: rawMsg };
 mqttLogSession.push(logItem);
 if (mqttLogSession.length > 50) mqttLogSession.shift();
 io.emit('mqtt_live_log', logItem); 
 let payload;
 try { payload = JSON.parse(rawMsg); } catch { payload = { raw: rawMsg }; }
 resetDeviceTimeout();

 const statKey = STAT_KEY_MAP[topic];
 if (statKey && statKey !== 'dev') {
 const normalized = statKey === 'input' ? normalizeInputPayload(payload) : payload;
 if (payload.result === 1) {
 Object.assign(deviceState[statKey], normalized);
 deviceState[statKey].conn = normalizeConn(payload, deviceState[statKey].conn);
 } else {
 deviceState[statKey].conn = normalizeConn(payload, 0);
 }
 const teleTopic = TELE_TOPIC_BY_KEY[statKey];
 if (teleTopic) io.emit('device_update', { topic: teleTopic, data: { ...deviceState[statKey] } });
 io.emit('cmd_result', { success: payload.result === 1, device: statKey, topic, payload });
 return;
 }
 
 const key = TELE_KEY_MAP[topic];
 if (key) {
 if (key === 'pb') {
 lastPbTeleAt = Date.now();
 if (Array.isArray(payload.pb) && payload.pb.length > 0) {
 const teleBid = payload.b_id !== undefined ? payload.b_id : payload.id;
 if (teleBid !== undefined) deviceState.pb.current_b_id = teleBid;
 let totalPowerSum = 0;
 deviceState.pb.channels = payload.pb.map(item => {
 totalPowerSum += (item.w || 0.0);
 return {
 b_id: deviceState.pb.current_b_id,
 ch: item.ch,
 sw: item.sw !== undefined ? item.sw : 0,
 w: item.w !== undefined ? item.w : 0.0,
 c: item.c !== undefined ? item.c : 0.0,
 v: item.v !== undefined ? item.v : 220
 };
 });
 deviceState.pb.conn = normalizeConn(payload, 0);
 lastPbDataValid = true;
 const hour = getKstHour();
 hourlyPowerStats[hour] += (totalPowerSum / 60);

 // [v2.0 신규] 실시간 개별 채널 전력 그래프용 샘플 적립
 // PB tele는 10초 주기로 오는 "순간값(instant)"이라 시간별 누적(Wh)과는
 // 별개로, 최근 값들을 그대로 롤링 버퍼에 쌓아서 라인 그래프로 보여줍니다.
 const sample = {
 t: Date.now(),
 ch: deviceState.pb.channels.map(c => c.w || 0),
 total: totalPowerSum,
 offline: false
 };
 powerHistory.push(sample);
 if (powerHistory.length > POWER_HISTORY_MAX) powerHistory.shift();
 ingestPowerSample(totalPowerSum);
 io.emit('power_sample', sample);
 } else {
 // result:0 또는 pb 배열 없음 — 전원보드 미연결/무응답이어도 0W 그래프 유지
 lastPbDataValid = false;
 deviceState.pb.conn = normalizeConn(payload, 0);
 ingestPbOfflineSample();
 }
 payload = {
 channels: deviceState.pb.channels,
 conn: lastPbDataValid && deviceState.pb.conn ? 1 : 0,
 current_b_id: deviceState.pb.current_b_id,
 pb_valid: lastPbDataValid
 };
 } else if (key === 'input') {
 // [v2.0 신규] {"in":[...],"count":[...]} → {p4..p7, cnt4..cnt7}
 payload = normalizeInputPayload(payload);
 Object.assign(deviceState.input, payload);
 } else {
 Object.assign(deviceState[key], payload);
 deviceState[key].conn = normalizeConn(payload, deviceState[key].conn ?? 0);
 }
 payload.conn = deviceState[key].conn;
 
 // 💡 어떤 센서 수치(미세먼지/온습도)가 들어와도 공백 없이 화면 UI로 다이렉트 즉각 전파
 io.emit('device_update', { topic, data: payload });
 }
}); 

// 📅 일괄 자동 제어 스케줄러 및 개별 하위 장치 동시 연동 결속 레이어
// [2026-08-14 신규] 채널 배열(0/1) → MQTT set_ad/set_pb 문자열 변환
function channelArrayToStr(arr, len) {
 const a = verifyChannelArrayGlobal(arr, len);
 return a.join('');
}
function verifyChannelArrayGlobal(arr, len) {
 if (!Array.isArray(arr) || arr.length !== len) return Array(len).fill(1);
 return arr.map(v => (v ? 1 : 0));
}
let lastProcessedMinute = -1; 
setInterval(() => {
 const now = new Date();
 const kstString = now.toLocaleString('en-US', { timeZone: 'Asia/Seoul' });
 const kstDate = new Date(kstString);
 const currentHour = kstDate.getHours();
 const currentMin = kstDate.getMinutes();
 const currentSec = now.getSeconds(); 
 
 if (currentSec % 10 === 0) { 
 console.log(`[LIVE_HEARTBEAT] 서버 감시 중 -> KST: ${currentHour}시 ${currentMin}분 | 출근설정: ${globalScheduleConfig.morningHour}:${globalScheduleConfig.morningMin} | 퇴근설정: ${globalScheduleConfig.nightHour}:${globalScheduleConfig.nightMin}`);
 }
 
 if (currentMin !== lastProcessedMinute) {
 const activeBid = deviceState.pb.current_b_id;
 
 // [🌅 전체 출근 타이머 작동]
 if (currentHour === globalScheduleConfig.morningHour && currentMin === globalScheduleConfig.morningMin) {
 console.log(`\r\n[🚀 SYSTEM] 전체 출근 타이머 작동 -> 하위 개별 장치 전체 동기화!!`);
 const flg = globalScheduleConfig.morningFlags || { ac: true, ap: true, ad: true, pb: true };
 const adStr = channelArrayToStr(globalScheduleConfig.morningAdChannels, 15);
 const pbStr = channelArrayToStr(globalScheduleConfig.morningPbChannels, 8);
 
 if (flg.ac !== false) mqttClient.publish('dev/cmnd/ac', JSON.stringify({ set_ac: { pwr: 1, mode: 'COOL', temp: 24, spd: 2 } }), { qos: 1 });
 if (flg.ap !== false) mqttClient.publish('dev/cmnd/ap', JSON.stringify({ set_ap: { pwr: 1, mode: 'AUTO', spd: 2, uv: 0, filter_reset: 0, bypass: 0, timer: 0 } }), { qos: 1 });
 if (flg.ad !== false) mqttClient.publish('dev/cmnd/ad', JSON.stringify({ set_ad: adStr }), { qos: 1 });
 if (flg.pb !== false && isPbCmdReady()) mqttClient.publish('dev/cmnd/pb', JSON.stringify({ b_id: activeBid, set_pb: pbStr }), { qos: 1 });
 
 if (flg.ac !== false) { Object.assign(deviceState.ac, { pwr: 1, mode: 'COOL', temp: 24, spd: 2 }); io.emit('device_update', { topic: TOPICS.TELE_AC, data: deviceState.ac }); }
 if (flg.ap !== false) { Object.assign(deviceState.ap, { pwr: 1, mode: 'AUTO', spd: 2 }); io.emit('device_update', { topic: TOPICS.TELE_AP, data: deviceState.ap }); }
 if (flg.ad !== false) { deviceState.ad.relays = verifyChannelArrayGlobal(globalScheduleConfig.morningAdChannels, 15); io.emit('device_update', { topic: TOPICS.TELE_AD, data: deviceState.ad }); }
 if (flg.pb !== false) { const pbArr = verifyChannelArrayGlobal(globalScheduleConfig.morningPbChannels, 8); deviceState.pb.channels.forEach((ch, i) => ch.sw = pbArr[i]); io.emit('device_update', { topic: TOPICS.TELE_PB, data: { channels: deviceState.pb.channels, conn: deviceState.pb.conn, current_b_id: activeBid, pb_valid: lastPbDataValid } }); }

 io.emit('mqtt_live_log', {
 time: now.toLocaleTimeString('ko-KR', { hour12: false, timeZone: 'Asia/Seoul' }),
 topic: 'SYSTEM/AUTO_CRON',
 payload: `🌅 출근 일괄 가동 제어 완료 및 개별 UI 컴포넌트 강제 동기화 락 해제`
 });
 lastProcessedMinute = currentMin;
 }
 // [🌙 전체 퇴근 타이머 작동]
 else if (currentHour === globalScheduleConfig.nightHour && currentMin === globalScheduleConfig.nightMin) {
 console.log(`\r\n[🚀 SYSTEM] 전체 퇴근 타이머 작동 -> 하위 개별 장치 전체 차단!!`);
 const flg = globalScheduleConfig.nightFlags || { ac: true, ap: true, ad: true, pb: true };
 const adStr = channelArrayToStr(globalScheduleConfig.nightAdChannels, 15);
 const pbStr = channelArrayToStr(globalScheduleConfig.nightPbChannels, 8);
 
 if (flg.ac !== false) mqttClient.publish('dev/cmnd/ac', JSON.stringify({ pwr: 0 }), { qos: 1 });
 if (flg.ap !== false) mqttClient.publish('dev/cmnd/ap', JSON.stringify({ pwr: 0 }), { qos: 1 });
 if (flg.ad !== false) mqttClient.publish('dev/cmnd/ad', JSON.stringify({ set_ad: adStr }), { qos: 1 });
 if (flg.pb !== false && isPbCmdReady()) mqttClient.publish('dev/cmnd/pb', JSON.stringify({ b_id: activeBid, set_pb: pbStr }), { qos: 1 });
 
 if (flg.ac !== false) { deviceState.ac.pwr = 0; io.emit('device_update', { topic: TOPICS.TELE_AC, data: deviceState.ac }); }
 if (flg.ap !== false) { deviceState.ap.pwr = 0; io.emit('device_update', { topic: TOPICS.TELE_AP, data: deviceState.ap }); }
 if (flg.ad !== false) { deviceState.ad.relays = verifyChannelArrayGlobal(globalScheduleConfig.nightAdChannels, 15); io.emit('device_update', { topic: TOPICS.TELE_AD, data: deviceState.ad }); }
 if (flg.pb !== false) { const pbArr = verifyChannelArrayGlobal(globalScheduleConfig.nightPbChannels, 8); deviceState.pb.channels.forEach((ch, i) => ch.sw = pbArr[i]); io.emit('device_update', { topic: TOPICS.TELE_PB, data: { channels: deviceState.pb.channels, conn: deviceState.pb.conn, current_b_id: activeBid, pb_valid: lastPbDataValid } }); }

 io.emit('mqtt_live_log', {
 time: now.toLocaleTimeString('ko-KR', { hour12: false, timeZone: 'Asia/Seoul' }),
 topic: 'SYSTEM/AUTO_CRON',
 payload: `🌙 퇴근 일괄 차단 제어 완료 및 개별 UI 컴포넌트 보안 전환 수립`
 });
 lastProcessedMinute = currentMin;
 }
 }
}, 1000); 

app.use(express.static(path.join(__dirname, 'public'))); 

app.get('/api/server-info', (_req, res) => {
 res.json({
  serverIp: SERVER_LAN_IP,
  mqttPort: 1883,
  webPort: PORT,
  mqttBroker: MQTT_BROKER
 });
});

io.on('connection', (socket) => {
 if (powerHistory.length === 0 && !isPbGraphOnline()) {
  ingestPbOfflineSample();
  const minuteKey = getMinuteKey();
  if (lastOfflineMinuteKey !== minuteKey) {
   lastOfflineMinuteKey = minuteKey;
   emitPowerMinutePoint(minuteKey, 0, 0, true);
  }
 }
 socket.emit('initial_state', {
  ...deviceState,
  _hourlyPowerStats: hourlyPowerStats,
  _powerHistory: powerHistory,
  _powerMinuteHistory: powerMinuteHistory,
  _serverInfo: { serverIp: SERVER_LAN_IP, mqttPort: 1883 },
  _mqttLogSession: mqttLogSession
 });
 
 // [2026-08-14 신규] AD 15채널 / PB 8채널 개별 선택값도 함께 검증/저장.
 // 이전에는 클라이언트가 이 값을 서버로 보내지도 않아서, 스케줄 실행 시
 // 항상 "전체 ON/OFF"만 나가는 버그가 있었습니다.
 function verifyChannelArray(arr, len) {
 if (!Array.isArray(arr) || arr.length !== len) return Array(len).fill(1);
 return arr.map(v => (v ? 1 : 0));
 }

 socket.on('update_schedule_config', ({ type, time, flags, adChannels, pbChannels }) => {
 if (!time || !time.includes(':')) return;
 try {
 const parts = time.split(':');
 const h = parseInt(parts[0], 10); 
 const m = parseInt(parts[1], 10);
 if (isNaN(h) || isNaN(m)) return;
 const verifiedFlags = {
 ac: (flags && (flags.ac === false || flags.ac === 'false')) ? false : true,
 ap: (flags && (flags.ap === false || flags.ap === 'false')) ? false : true,
 ad: (flags && (flags.ad === false || flags.ad === 'false')) ? false : true,
 pb: (flags && (flags.pb === false || flags.pb === 'false')) ? false : true
 };
 const verifiedAdChannels = verifyChannelArray(adChannels, 15);
 const verifiedPbChannels = verifyChannelArray(pbChannels, 8);
 if (type === 'morning') {
 globalScheduleConfig.morningHour = h;
 globalScheduleConfig.morningMin = m;
 globalScheduleConfig.morningFlags = verifiedFlags;
 globalScheduleConfig.morningAdChannels = verifiedAdChannels;
 globalScheduleConfig.morningPbChannels = verifiedPbChannels;
 } else if (type === 'night') {
 globalScheduleConfig.nightHour = h;
 globalScheduleConfig.nightMin = m;
 globalScheduleConfig.nightFlags = verifiedFlags;
 globalScheduleConfig.nightAdChannels = verifiedAdChannels;
 globalScheduleConfig.nightPbChannels = verifiedPbChannels;
 }
 lastProcessedMinute = -1;
 saveScheduleConfig();
 console.log(`[CONFIG_SYNC] 스케줄 설정 동기화 성공 (파일로 저장됨) — AD:[${verifiedAdChannels.join('')}] PB:[${verifiedPbChannels.join('')}]`);
 } catch (e) {
 console.error('[CONFIG_EXCEPTION]', e);
 }
 });
 
 // ⚙ 수동 개별 버튼 클릭 제어 파트 (상태 선반영 및 데이터 변환 매핑 무결화)
 socket.on('control_cmd', ({ device, cmd }) => {
 const activeBid = deviceState.pb.current_b_id;
 
 // ★ v2.0 수정: 기존에는 device==='dev' 이면 cmd 내용과 무관하게 무조건
 //   DEV_RESET을 보내버리는 버그가 있었음 (예: set_net/set_broker 명령도
 //   전부 리셋으로 대체되어 버림). "reset" 필드가 실제로 있을 때만 리셋.
 if (cmd && (cmd.reset !== undefined || cmd.reset_pb !== undefined)) {
 const resetPacket = { reset: 'DEV_RESET' };
 mqttClient.publish(TOPICS.CMD_DEV, JSON.stringify(resetPacket), { qos: 1 });
 setAllOffline();
 return;
 }

 // [v2.0 신규] 장치(dev) 대상 기타 명령 — 네트워크/브로커 설정 변경 등
 // 명령 전송 후 보드가 재부팅되므로 응답(SET_NET/SET_BROKER stat)은
 // mqtt_live_log 및 cmd_result 로 확인 가능
 if (device === 'dev') {
 mqttClient.publish(TOPICS.CMD_DEV, JSON.stringify(cmd), { qos: 1 });
 io.emit('cmd_result', { success: true, device: 'dev', topic: 'sent' });
 return;
 }

 // [v2.0 신규] 외부 입력 4채널 — 카운트 조회/초기화
 if (device === 'input') {
 mqttClient.publish(TOPICS.CMD_INPUT, JSON.stringify(cmd), { qos: 1 });
 if (cmd.reset_count !== undefined) {
 Object.assign(deviceState.input, { cnt4: 0, cnt5: 0, cnt6: 0, cnt7: 0 });
 io.emit('device_update', { topic: TOPICS.TELE_INPUT, data: { ...deviceState.input } });
 }
 io.emit('cmd_result', { success: true, device: 'input', topic: 'sent' });
 return;
 }
 
 // 전력제어분배기(pb) 개별/일괄 스위칭 완벽 가드
 if (device === 'pb') {
 if (!isPbCmdReady()) {
 io.emit('cmd_result', { success: false, device: 'pb', topic: 'sent', message: 'PB tele 미수신 — b_id 미학습' });
 return;
 }
 const pbFinalPacket = { b_id: activeBid, ...cmd };
 mqttClient.publish(TOPICS.CMD_PB, JSON.stringify(pbFinalPacket), { qos: 1 });

 if (cmd.ch !== undefined && cmd.sw !== undefined) {
 const targetCh = deviceState.pb.channels.find(c => c.ch === cmd.ch);
 if (targetCh) targetCh.sw = parseInt(cmd.sw) === 1 ? 1 : 0;
 io.emit('device_update', { topic: TOPICS.TELE_PB, data: { channels: deviceState.pb.channels, conn: deviceState.pb.conn, current_b_id: activeBid, pb_valid: lastPbDataValid } });
 }

 if (cmd.set_pb && (cmd.set_pb.length === 8 || typeof cmd.set_pb === 'string')) {
 const bitString = String(cmd.set_pb);
 for(let i = 0; i < 8; i++) {
 const targetCh = deviceState.pb.channels.find(c => c.ch === (i + 1));
 if (targetCh) targetCh.sw = parseInt(bitString[i]) === 1 ? 1 : 0;
 }
 io.emit('device_update', { topic: TOPICS.TELE_PB, data: { channels: deviceState.pb.channels, conn: deviceState.pb.conn, current_b_id: activeBid, pb_valid: lastPbDataValid } });
 }
 return;
 }
 
 const topic = TOPICS[`CMD_${device.toUpperCase()}`];
 if (topic) {
 mqttClient.publish(topic, JSON.stringify(cmd), { qos: 1 });
 
 // 에어컨(ac) 개별 수동 버튼 반응 강화
 if (device === 'ac') {
 if (cmd.pwr !== undefined) deviceState.ac.pwr = (cmd.pwr === 1 || cmd.pwr === true || cmd.pwr === '1') ? 1 : 0;
 if (cmd.mode) deviceState.ac.mode = cmd.mode;
 if (cmd.temp !== undefined) deviceState.ac.temp = parseFloat(cmd.temp);
 if (cmd.spd !== undefined) deviceState.ac.spd = parseInt(cmd.spd);
 if (cmd.set_ac) Object.assign(deviceState.ac, cmd.set_ac);
 io.emit('device_update', { topic: TOPICS.TELE_AC, data: deviceState.ac });
 } 
 // 공기청정기(ap) 개별 수동 버튼 반응 강화
 else if (device === 'ap') {
 if (cmd.pwr !== undefined) deviceState.ap.pwr = (cmd.pwr === 1 || cmd.pwr === true || cmd.pwr === '1') ? 1 : 0;
 if (cmd.mode) deviceState.ap.mode = cmd.mode;
 if (cmd.spd !== undefined) deviceState.ap.spd = parseInt(cmd.spd);
 if (cmd.uv !== undefined) deviceState.ap.uv = parseInt(cmd.uv);
 if (cmd.bypass !== undefined) deviceState.ap.bypass = parseInt(cmd.bypass);
 if (cmd.set_ap) Object.assign(deviceState.ap, cmd.set_ap);
 io.emit('device_update', { topic: TOPICS.TELE_AP, data: deviceState.ap });
 } 
 // 광고판 릴레이(ad) 15채널 개별 수동 반응 강화
 else if (device === 'ad' && cmd.set_ad && cmd.set_ad.length === 15) {
 for(let i=0; i<15; i++) deviceState.ad.relays[i] = parseInt(cmd.set_ad[i]);
 io.emit('device_update', { topic: TOPICS.TELE_AD, data: deviceState.ad });
 }
 io.emit('cmd_result', { success: true, device, topic: 'sent' });
 }
 });
 
 socket.on('refresh_all', () => {
 console.log('[CMS] refresh_all: ALL_DATA (dust/th/ad/fan/ac/pb/ap/input tele) + device poll');
 mqttClient.publish(TOPICS.CMD_DEV, JSON.stringify({ data: 'ALL_DATA' }), { qos: 1 });
 // ALL_DATA tele(센서·입력) 처리 후 RS485 장치 stat 폴링
 setTimeout(publishRefreshPolls, 400);
 });
});

// 매 분 경계 확정 + 전원보드 미연결 시 1분마다 0W 하트비트 (그래프 생존 확인)
setInterval(() => {
 const now = new Date();
 const minuteKey = getMinuteKey(now);

 if (powerMinuteBucket.minuteKey !== null && powerMinuteBucket.minuteKey !== minuteKey) {
  const point = finalizePowerMinuteBucket(powerMinuteBucket.minuteKey);
  emitPowerMinutePoint(point.minuteKey, point.total, point.samples, false);
  ensureInstantAnchorForMinute(point.minuteKey, point.total, false);
  powerMinuteBucket = { minuteKey, sum: 0, count: 0 };
 } else if (powerMinuteBucket.minuteKey === null) {
  powerMinuteBucket.minuteKey = minuteKey;
 }

 if (!isPbGraphOnline() && minuteKey !== lastOfflineMinuteKey) {
  lastOfflineMinuteKey = minuteKey;
  emitPowerMinutePoint(minuteKey, 0, 0, true);
  emitZeroPowerSample(true);
 }
}, 5000);

// 서버 기동 직후 PB 미연결이면 0W 그래프 시드
setTimeout(() => {
 if (!isPbGraphOnline()) {
  const minuteKey = getMinuteKey();
  if (lastOfflineMinuteKey !== minuteKey) {
   lastOfflineMinuteKey = minuteKey;
   emitPowerMinutePoint(minuteKey, 0, 0, true);
  }
  if (powerHistory.length === 0) ingestPbOfflineSample();
 }
}, 1500);

server.listen(PORT, '0.0.0.0', () => {
 console.log(`오픈 관제 동적 스케줄링 통합 마스터 로컬 서버 가동 포트: ${PORT}`);
 console.log(`[SERVER] LAN IP: ${SERVER_LAN_IP} (브로커 설정용)`);
});
