const express = require('express'); 
const http = require('http'); 
const { Server } = require('socket.io'); 
const mqtt = require('mqtt'); 
const path = require('path'); 

const PORT = process.env.PORT || 3000; 
const MQTT_BROKER = process.env.MQTT_BROKER || 'mqtt://localhost:1883'; 
const DEVICE_TIMEOUT_MS = 5 * 60 * 1000; 

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
let mqttLogSession = []; 
let globalScheduleConfig = { 
 morningHour: 7, morningMin: 0, 
 morningFlags: { ac: true, ap: true, ad: true, pb: true }, 
 nightHour: 20, nightMin: 0, 
 nightFlags: { ac: true, ap: true, ad: true, pb: true }
}; 

// 뼈대 마스터 상태 메모리
const deviceState = { 
 state: { status: 'OFFLINE', uid: '', ip: '', mode: '', conn: 0 }, 
 dust: { pm1_0: 0, pm2_5: 0, pm10: 0, conn: 0 }, 
 th_in: { temp: 0, humi: 0, conn: 0 }, 
 th_out: { temp: 0, humi: 0, conn: 0 }, 
 ad: { relays: Array(15).fill(0), conn: 0 }, 
 fan: { mode: 'AUTO', duty: 0, conn: 0 }, 
 pb: { current_b_id: 1, channels: Array(8).fill(0).map((_,i)=>({b_id:1, ch:i+1, sw:0, w:0, c:0.0, v:220})), conn: 0 }, 
 ap: { pwr: 0, mode: 'AUTO', spd: 0, uv: 0, co2: 0, pm25: 0, pm10: 0, pm1_0: 0, temp: 0, temp_out: 0, humi: 0, tvoc: 0, filter: 100, conn: 0 }, 
 ac: { pwr: 0, mode: 'COOL', temp: 24, curr: 0, spd: 1, err: 0, conn: 0 },
 // [v2.0 신규] 외부 입력 4채널 (PD4~PD7)
 input: { p4: 0, p5: 0, p6: 0, p7: 0, cnt4: 0, cnt5: 0, cnt6: 0, cnt7: 0, conn: 0 }
}; 

let deviceTimeoutTimer = null; 
function resetDeviceTimeout() {
 if (deviceTimeoutTimer) clearTimeout(deviceTimeoutTimer);
 Object.keys(deviceState).forEach(k => { deviceState[k].conn = 1; });
 deviceState.state.status = 'ONLINE';
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
 [TOPICS.STAT_INPUT]: 'input'
};

const TELE_TOPIC_BY_KEY = {
 state: TOPICS.TELE_STATE, dust: TOPICS.TELE_DUST, th_in: TOPICS.TELE_TH_IN,
 th_out: TOPICS.TELE_TH_OUT, ad: TOPICS.TELE_AD, fan: TOPICS.TELE_FAN,
 pb: TOPICS.TELE_PB, ap: TOPICS.TELE_AP, ac: TOPICS.TELE_AC, input: TOPICS.TELE_INPUT
}; 

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
 deviceState[statKey].conn = payload.conn !== undefined ? payload.conn : 1;
 } else {
 deviceState[statKey].conn = payload.conn !== undefined ? payload.conn : 0;
 }
 const teleTopic = TELE_TOPIC_BY_KEY[statKey];
 if (teleTopic) io.emit('device_update', { topic: teleTopic, data: { ...deviceState[statKey] } });
 io.emit('cmd_result', { success: payload.result === 1, device: statKey, topic, payload });
 return;
 }
 
 const key = TELE_KEY_MAP[topic];
 if (key) {
 if (key === 'pb') {
 if (payload && payload.result === 1 && Array.isArray(payload.pb)) {
 if (payload.b_id !== undefined) deviceState.pb.current_b_id = payload.b_id;
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
 deviceState.pb.conn = 1;
 const hour = new Date().getHours();
 hourlyPowerStats[hour] += (totalPowerSum / 60);

 // [v2.0 신규] 실시간 개별 채널 전력 그래프용 샘플 적립
 // PB tele는 10초 주기로 오는 "순간값(instant)"이라 시간별 누적(Wh)과는
 // 별개로, 최근 값들을 그대로 롤링 버퍼에 쌓아서 라인 그래프로 보여줍니다.
 const sample = {
 t: Date.now(),
 ch: deviceState.pb.channels.map(c => c.w || 0),
 total: totalPowerSum
 };
 powerHistory.push(sample);
 if (powerHistory.length > POWER_HISTORY_MAX) powerHistory.shift();
 io.emit('power_sample', sample);
 } else if (payload && payload.result === 0) {
 deviceState.pb.conn = 0;
 }
 payload = { channels: deviceState.pb.channels, conn: deviceState.pb.conn, current_b_id: deviceState.pb.current_b_id };
 } else if (key === 'input') {
 // [v2.0 신규] {"in":[...],"count":[...]} → {p4..p7, cnt4..cnt7}
 payload = normalizeInputPayload(payload);
 Object.assign(deviceState.input, payload);
 } else {
 Object.assign(deviceState[key], payload);
 }
 payload.conn = deviceState[key].conn;
 
 // 💡 어떤 센서 수치(미세먼지/온습도)가 들어와도 공백 없이 화면 UI로 다이렉트 즉각 전파
 io.emit('device_update', { topic, data: payload });
 }
}); 

// 📅 일괄 자동 제어 스케줄러 및 개별 하위 장치 동시 연동 결속 레이어
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
 const activeBid = deviceState.pb.current_b_id !== undefined ? deviceState.pb.current_b_id : 1;
 
 // [🌅 전체 출근 타이머 작동]
 if (currentHour === globalScheduleConfig.morningHour && currentMin === globalScheduleConfig.morningMin) {
 console.log(`\r\n[🚀 SYSTEM] 전체 출근 타이머 작동 -> 하위 개별 장치 전체 동기화!!`);
 const flg = globalScheduleConfig.morningFlags || { ac: true, ap: true, ad: true, pb: true };
 
 if (flg.ac !== false) mqttClient.publish('dev/cmnd/ac', JSON.stringify({ pwr: 1, mode: 'COOL', temp: 24 }), { qos: 1 });
 if (flg.ap !== false) mqttClient.publish('dev/cmnd/ap', JSON.stringify({ pwr: 1, mode: 'AUTO' }), { qos: 1 });
 if (flg.ad !== false) mqttClient.publish('dev/cmnd/ad', JSON.stringify({ set_ad: '111111111111111' }), { qos: 1 });
 if (flg.pb !== false) mqttClient.publish('dev/cmnd/pb', JSON.stringify({ b_id: activeBid, set_pb: '11111111' }), { qos: 1 });
 
 if (flg.ac !== false) { Object.assign(deviceState.ac, { pwr: 1, mode: 'COOL', temp: 24 }); io.emit('device_update', { topic: TOPICS.TELE_AC, data: deviceState.ac }); }
 if (flg.ap !== false) { Object.assign(deviceState.ap, { pwr: 1, mode: 'AUTO' }); io.emit('device_update', { topic: TOPICS.TELE_AP, data: deviceState.ap }); }
 if (flg.ad !== false) { deviceState.ad.relays = Array(15).fill(1); io.emit('device_update', { topic: TOPICS.TELE_AD, data: deviceState.ad }); }
 if (flg.pb !== false) { deviceState.pb.channels.forEach(ch => ch.sw = 1); io.emit('device_update', { topic: TOPICS.TELE_PB, data: { channels: deviceState.pb.channels, conn: 1, current_b_id: activeBid } }); }

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
 
 if (flg.ac !== false) mqttClient.publish('dev/cmnd/ac', JSON.stringify({ pwr: 0 }), { qos: 1 });
 if (flg.ap !== false) mqttClient.publish('dev/cmnd/ap', JSON.stringify({ pwr: 0 }), { qos: 1 });
 if (flg.ad !== false) mqttClient.publish('dev/cmnd/ad', JSON.stringify({ set_ad: '000000000000000' }), { qos: 1 });
 if (flg.pb !== false) mqttClient.publish('dev/cmnd/pb', JSON.stringify({ b_id: activeBid, set_pb: '00000000' }), { qos: 1 });
 
 if (flg.ac !== false) { deviceState.ac.pwr = 0; io.emit('device_update', { topic: TOPICS.TELE_AC, data: deviceState.ac }); }
 if (flg.ap !== false) { deviceState.ap.pwr = 0; io.emit('device_update', { topic: TOPICS.TELE_AP, data: deviceState.ap }); }
 if (flg.ad !== false) { deviceState.ad.relays = Array(15).fill(0); io.emit('device_update', { topic: TOPICS.TELE_AD, data: deviceState.ad }); }
 if (flg.pb !== false) { deviceState.pb.channels.forEach(ch => ch.sw = 0); io.emit('device_update', { topic: TOPICS.TELE_PB, data: { channels: deviceState.pb.channels, conn: 1, current_b_id: activeBid } }); }

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

io.on('connection', (socket) => {
 socket.emit('initial_state', { ...deviceState, _hourlyPowerStats: hourlyPowerStats, _powerHistory: powerHistory, _mqttLogSession: mqttLogSession });
 
 socket.on('update_schedule_config', ({ type, time, flags }) => {
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
 if (type === 'morning') {
 globalScheduleConfig.morningHour = h;
 globalScheduleConfig.morningMin = m;
 globalScheduleConfig.morningFlags = verifiedFlags;
 } else if (type === 'night') {
 globalScheduleConfig.nightHour = h;
 globalScheduleConfig.nightMin = m;
 globalScheduleConfig.nightFlags = verifiedFlags;
 }
 lastProcessedMinute = -1;
 console.log(`[CONFIG_SYNC] 스케줄 설정 동기화 성공`);
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
 const pbFinalPacket = { b_id: activeBid, ...cmd };
 mqttClient.publish(TOPICS.CMD_PB, JSON.stringify(pbFinalPacket), { qos: 1 });
 
 if (cmd.set_pb && (cmd.set_pb.length === 8 || typeof cmd.set_pb === 'string')) {
 const bitString = String(cmd.set_pb);
 for(let i = 0; i < 8; i++) {
 const targetCh = deviceState.pb.channels.find(c => c.ch === (i + 1));
 if (targetCh) targetCh.sw = parseInt(bitString[i]) === 1 ? 1 : 0;
 }
 deviceState.pb.conn = 1;
 io.emit('device_update', { topic: TOPICS.TELE_PB, data: { channels: deviceState.pb.channels, conn: 1, current_b_id: activeBid } });
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
 const activeBid = deviceState.pb.current_b_id;
 mqttClient.publish(TOPICS.CMD_DEV, JSON.stringify({ data: 'ALL_DATA' }), { qos: 1 });
 mqttClient.publish(TOPICS.CMD_AC, JSON.stringify({ get_ac: 'state' }), { qos: 1 });
 mqttClient.publish(TOPICS.CMD_AP, JSON.stringify({ get_ap: 'state' }), { qos: 1 });
 mqttClient.publish(TOPICS.CMD_FAN, JSON.stringify({ get_fan: 'state' }), { qos: 1 });
 mqttClient.publish(TOPICS.CMD_PB, JSON.stringify({ b_id: activeBid, get_pb: 'state' }), { qos: 1 });
 mqttClient.publish(TOPICS.CMD_AD, JSON.stringify({ get_ad: 'state' }), { qos: 1 });
 });
}); 

server.listen(PORT, '0.0.0.0', () => {
 console.log(`오픈 관제 동적 스케줄링 통합 마스터 로컬 서버 가동 포트: ${PORT}`);
});
