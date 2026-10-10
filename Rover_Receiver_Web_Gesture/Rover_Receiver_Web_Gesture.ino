/*
  Rover_Receiver_Web_Gesture.ino
  Board: ESP32 DevKit V1 / ESP32-WROOM-32
  Role: L298N motor control + Wi-Fi dashboard + ESP-NOW receiver

  Wiring:
    L298N ENA -> GPIO32 (remove ENA jumper)
    L298N IN1 -> GPIO25
    L298N IN2 -> GPIO26
    L298N ENB -> GPIO33 (remove ENB jumper)
    L298N IN3 -> GPIO27
    L298N IN4 -> GPIO14
    ESP32 GND -> L298N GND (common ground required)

  Wi-Fi:
    SSID: ESP32_Rover
    Password: rover123
    Dashboard: http://192.168.4.1

  Compatible with Arduino-ESP32 core 3.x (ledcAttach/ledcWrite).
*/

#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include <esp_wifi.h>

const char* AP_SSID = "ESP32_Rover";
const char* AP_PASSWORD = "rover123";
constexpr uint8_t WIFI_CHANNEL = 1;

constexpr int ENA = 32, IN1 = 25, IN2 = 26;
constexpr int ENB = 33, IN3 = 27, IN4 = 14;
constexpr uint32_t PWM_FREQ = 15000;
constexpr uint8_t PWM_BITS = 8;
constexpr uint8_t GESTURE_MAX_PERCENT = 80;

enum PacketType : uint8_t { PKT_GESTURE = 1, PKT_SETTINGS = 2 };
enum Command : uint8_t { CMD_STOP=0, CMD_FORWARD=1, CMD_BACKWARD=2, CMD_LEFT=3, CMD_RIGHT=4 };

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t command;
  uint8_t speedPercent;
  uint8_t sensitivityDeg;
  float pitch;
  float roll;
  uint32_t counter;
} RoverPacket;

WebServer server(80);
portMUX_TYPE packetMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool packetReady = false;
RoverPacket latestPacket = {};
uint32_t lastGesturePacketMs = 0;

enum Mode : uint8_t { MODE_GESTURE, MODE_WEB };
Mode controlMode = MODE_GESTURE;
uint8_t webSpeedPercent = 60;
uint8_t sensitivityDeg = 12;
String currentCommand = "stop";
uint32_t lastWebCommandMs = 0;
uint32_t bootMs = 0;
bool emergencyStop = false;
volatile uint32_t receivedPackets = 0;
volatile uint32_t invalidPackets = 0;
String lastStopReason = "Startup";

// Advanced connection diagnostics state
constexpr uint8_t RX_TIME_RING = 16;
volatile uint32_t rxTimeRing[RX_TIME_RING];
volatile uint8_t rxTimeIdx = 0;
volatile float rxRateHz = 0;
volatile uint32_t lastPacketCounter = 0;
volatile bool haveLastCounter = false;
volatile uint32_t gapPackets = 0;
volatile int8_t lastRxRssi = 0;

// Ramped PWM state. Safety stops (emergency stop / gesture timeout) remain immediate.
uint8_t currentDuty = 0;
uint8_t targetDuty = 0;
uint8_t pendingCommand = CMD_STOP;
uint8_t activeDirection = CMD_STOP;
uint32_t lastRampMs = 0;
constexpr uint8_t RAMP_STEP = 8;       // PWM counts per ramp tick
constexpr uint16_t RAMP_INTERVAL_MS = 10;
// Starting kick: 95% PWM for 300 ms, only when starting from a full stop.
constexpr uint8_t START_BOOST_PERCENT = 95;
constexpr uint16_t START_BOOST_MS = 300;
bool stoppedSinceLastMove = true;
bool startBoostActive = false;
uint32_t startBoostUntilMs = 0;
uint8_t postBoostDuty = 0;

String commandName(uint8_t c) {
  switch (c) {
    case CMD_FORWARD: return "forward";
    case CMD_BACKWARD: return "backward";
    case CMD_LEFT: return "left";
    case CMD_RIGHT: return "right";
    default: return "stop";
  }
}

void motorsOff() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
  ledcWrite(ENA, 0); ledcWrite(ENB, 0);
  currentDuty = 0; targetDuty = 0;
  pendingCommand = CMD_STOP; activeDirection = CMD_STOP;
  startBoostActive = false;
  stoppedSinceLastMove = true;
}

void applyDirection(uint8_t cmd) {
  switch (cmd) {
    case CMD_FORWARD:
      digitalWrite(IN1,HIGH); digitalWrite(IN2,LOW);
      digitalWrite(IN3,HIGH); digitalWrite(IN4,LOW); break;
    case CMD_BACKWARD:
      digitalWrite(IN1,LOW); digitalWrite(IN2,HIGH);
      digitalWrite(IN3,LOW); digitalWrite(IN4,HIGH); break;
    case CMD_LEFT:
      digitalWrite(IN1,LOW); digitalWrite(IN2,HIGH);
      digitalWrite(IN3,HIGH); digitalWrite(IN4,LOW); break;
    case CMD_RIGHT:
      digitalWrite(IN1,HIGH); digitalWrite(IN2,LOW);
      digitalWrite(IN3,LOW); digitalWrite(IN4,HIGH); break;
    default:
      digitalWrite(IN1,LOW); digitalWrite(IN2,LOW);
      digitalWrite(IN3,LOW); digitalWrite(IN4,LOW); break;
  }
}

void drive(uint8_t cmd, uint8_t percent) {
  if (emergencyStop) { motorsOff(); return; }
  if (cmd == CMD_STOP || percent == 0) {
    pendingCommand = CMD_STOP;
    targetDuty = 0;              // normal stop decelerates smoothly
    startBoostActive = false;
    currentCommand = "stop";
    stoppedSinceLastMove = true;
    return;
  }

  uint8_t duty = (uint16_t)constrain(percent, 0, 100) * 255 / 100;

  // Direction changes ramp down to zero before changing H-bridge direction.
  if (cmd != activeDirection && currentDuty > 0) {
    pendingCommand = cmd;
    targetDuty = 0;
    startBoostActive = false;
    return;
  }

  if (currentDuty == 0 && cmd != activeDirection) {
    applyDirection(cmd);
    activeDirection = cmd;
  }

  pendingCommand = cmd;
  postBoostDuty = duty;

  // Apply one short kick only on the first movement command after a full stop.
  // Do not restart the 300 ms timer for every repeated gesture/web packet.
  if (stoppedSinceLastMove && currentDuty == 0) {
    currentDuty = (uint16_t)START_BOOST_PERCENT * 255 / 100;
    ledcWrite(ENA, currentDuty);
    ledcWrite(ENB, currentDuty);
    startBoostActive = true;
    startBoostUntilMs = millis() + START_BOOST_MS;
    targetDuty = currentDuty;
    stoppedSinceLastMove = false;
    return;
  }

  stoppedSinceLastMove = false;
  if (!startBoostActive) targetDuty = duty;
}

void updateMotorRamp() {
  if (startBoostActive) {
    if ((int32_t)(millis() - startBoostUntilMs) < 0) return;
    startBoostActive = false;
    targetDuty = postBoostDuty; // return to the selected speed after 300 ms
  }

  if (millis() - lastRampMs < RAMP_INTERVAL_MS) return;
  lastRampMs = millis();

  if (currentDuty > targetDuty) {
    currentDuty = currentDuty - targetDuty > RAMP_STEP ? currentDuty - RAMP_STEP : targetDuty;
  } else if (currentDuty < targetDuty) {
    currentDuty = targetDuty - currentDuty > RAMP_STEP ? currentDuty + RAMP_STEP : targetDuty;
  }

  ledcWrite(ENA, currentDuty);
  ledcWrite(ENB, currentDuty);

  if (currentDuty == 0 && pendingCommand != CMD_STOP && pendingCommand != activeDirection) {
    applyDirection(pendingCommand);
    activeDirection = pendingCommand;
    uint8_t percent = controlMode == MODE_GESTURE
      ? min((int)latestPacket.speedPercent, (int)GESTURE_MAX_PERCENT)
      : webSpeedPercent;
    uint8_t normalDuty = (uint16_t)constrain(percent, 0, 100) * 255 / 100;
    // A direction change that reaches zero is a fresh start; apply the same kick.
    currentDuty = (uint16_t)START_BOOST_PERCENT * 255 / 100;
    ledcWrite(ENA, currentDuty);
    ledcWrite(ENB, currentDuty);
    postBoostDuty = normalDuty;
    targetDuty = currentDuty;
    startBoostActive = true;
    startBoostUntilMs = millis() + START_BOOST_MS;
    stoppedSinceLastMove = false;
  }
}

void stopRover() {
  currentCommand = "stop";
  lastStopReason = "Command stop";
  targetDuty = 0;
  pendingCommand = CMD_STOP;
  startBoostActive = false;
  stoppedSinceLastMove = true;
}

void sendSettings() {
  RoverPacket p = {};
  p.type = PKT_SETTINGS;
  p.command = CMD_STOP;
  p.speedPercent = GESTURE_MAX_PERCENT;
  p.sensitivityDeg = sensitivityDeg;
  p.counter = millis();

  uint8_t broadcastAddress[6] = {255,255,255,255,255,255};
  esp_now_send(broadcastAddress, (uint8_t*)&p, sizeof(p));
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(RoverPacket)) { invalidPackets++; return; }
  RoverPacket p;
  memcpy(&p, data, sizeof(p));
  if (p.type != PKT_GESTURE || p.command > CMD_RIGHT || p.speedPercent > 100) {
    invalidPackets++;
    return;
  }
  portENTER_CRITICAL(&packetMux);
  latestPacket = p;
  lastGesturePacketMs = millis();
  packetReady = true;
  receivedPackets++;

  // Sequence gap tracking (detects lost packets between 50 ms sends).
  if (haveLastCounter && p.counter > lastPacketCounter) {
    uint32_t delta = p.counter - lastPacketCounter;
    if (delta > 1) gapPackets += delta - 1;
  }
  haveLastCounter = true;
  lastPacketCounter = p.counter;

  // Rolling packet-rate estimate from a ring of arrival timestamps.
  rxTimeRing[rxTimeIdx] = millis();
  rxTimeIdx = (uint8_t)((rxTimeIdx + 1) % RX_TIME_RING);
  uint32_t newest = rxTimeRing[(rxTimeIdx + RX_TIME_RING - 1) % RX_TIME_RING];
  uint32_t oldest = rxTimeRing[rxTimeIdx];
  uint32_t span = newest - oldest;
  if (span >= 25 && span <= 2000) rxRateHz = (RX_TIME_RING - 1) * 1000.0f / (float)span;

  // Signal strength of the incoming ESP-NOW packet.
  if (info->rx_ctrl) lastRxRssi = info->rx_ctrl->rssi;
  portEXIT_CRITICAL(&packetMux);
}

const char DASHBOARD[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Rover</title><style>
*{box-sizing:border-box}
:root{color-scheme:dark;--bg:#080d18;--panel:#111a2b;--panel2:#0c1423;--line:#26344b;--text:#edf3ff;--muted:#8fa1bc;--accent:#63e6be;--blue:#7ca7ff;--danger:#ff657a}
body{font-family:Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;margin:0;background:radial-gradient(ellipse at top right,#152743 0%,var(--bg) 48%);color:var(--text);min-height:100vh}
main{max-width:1040px;margin:auto;padding:24px 18px 42px}
.topbar{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:10px 2px 22px;border-bottom:1px solid var(--line);margin-bottom:22px}
.brand{display:flex;align-items:center;gap:13px}.brandmark{width:44px;height:44px;border-radius:13px;display:grid;place-items:center;background:linear-gradient(145deg,#1e9d87,#2464a8);font-size:23px;font-weight:800;color:white;box-shadow:0 8px 24px #0004}
h1{font-size:clamp(23px,4vw,32px);letter-spacing:-.7px;margin:0;font-weight:760}h2{font-size:15px;letter-spacing:.2px;margin:0 0 16px;font-weight:700}
.subtitle{color:var(--muted);font-size:13px;margin-top:5px}.livebadge{display:flex;align-items:center;gap:8px;border:1px solid var(--line);border-radius:999px;padding:8px 12px;color:var(--muted);font-size:12px;white-space:nowrap}.dot{width:8px;height:8px;border-radius:50%;background:var(--accent);box-shadow:0 0 12px #63e6be70}
.grid{display:grid;grid-template-columns:repeat(12,minmax(0,1fr));gap:14px}.panel{background:linear-gradient(155deg,#141f32,#0f1727);border:1px solid var(--line);border-radius:17px;padding:19px;box-shadow:0 12px 32px #00000018;min-width:0}.span12{grid-column:span 12}.span7{grid-column:span 7}.span5{grid-column:span 5}
.sectionhead{display:flex;align-items:center;gap:10px;margin-bottom:15px}.sectionicon{width:30px;height:30px;border-radius:9px;background:#20334a;color:var(--accent);display:grid;place-items:center;font-size:15px}.sectionhead h2{margin:0}
.muted{color:var(--muted);font-size:12px;line-height:1.55}.row{display:flex;gap:10px;flex-wrap:wrap}
.stat{flex:1;min-width:125px;background:var(--panel2);border:1px solid #202d42;padding:13px;border-radius:12px}
.value{font-size:clamp(17px,2.5vw,22px);font-weight:750;letter-spacing:-.3px;margin-top:7px;overflow-wrap:anywhere}
button{font:inherit;font-size:13px;font-weight:700;border:1px solid #34445e;border-radius:11px;padding:12px 14px;background:#1b2940;color:var(--text);cursor:pointer;min-height:44px;transition:background .15s,border-color .15s,transform .15s}
button:hover{background:#263953;border-color:#526987}button:active{transform:scale(.98)}button.active{background:#124b47;border-color:#218d7b;color:#9ff8dc}button.stop{background:#5a2635;border-color:#8d394d;color:#ffdce2}
.modes button{flex:1;min-width:140px}.controls{display:grid;grid-template-columns:repeat(3,minmax(65px,1fr));gap:8px;max-width:390px;margin:8px auto 16px}
.controls button{min-height:54px}.blank{visibility:hidden}input[type=range]{width:100%;accent-color:var(--accent);cursor:pointer}
label{display:block;margin:14px 0 8px;color:#c8d4e7;font-size:13px}label b{color:var(--accent)}
.rule{height:1px;background:var(--line);margin:16px 0}.good{color:var(--accent)}.bad{color:var(--danger)}
.diagnostics{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px}
.diagnostics .stat{min-width:0;overflow-wrap:anywhere}.diagnostics .muted{font-size:11px;line-height:1.35}.diagnostics .value{font-size:clamp(13px,1.8vw,18px);line-height:1.25;word-break:normal;overflow-wrap:anywhere}.tiltbox{margin-top:14px;max-width:460px}.tiltbox svg{display:block;width:100%;height:auto;margin-top:8px;border-radius:12px}.footer{color:#70829d;font-size:11px;line-height:1.6;margin:20px 2px 0}
@media(max-width:720px){main{padding:16px 12px 30px}.span7,.span5{grid-column:span 12}.diagnostics{grid-template-columns:repeat(2,minmax(0,1fr))}.panel{padding:15px}.livebadge{font-size:0;padding:10px}.livebadge .dot{margin:0}}
@media(prefers-reduced-motion:reduce){button{transition:none}}
</style></head><body><main>
<header class="topbar"><div class="brand"><div class="brandmark">R</div><div><h1>Rover Control</h1><div class="subtitle">ESP32 robotics platform · Live operations console</div></div></div><div class="livebadge"><span class="dot"></span> DASHBOARD ACTIVE</div></header><div class="grid">
<div class="panel span12"><div class="sectionhead"><div class="sectionicon">01</div><h2>Control mode</h2></div><div class="row modes">
<button id="gestureBtn" onclick="setMode('gesture')">Gesture Mode (max 80%)</button>
<button id="webBtn" onclick="setMode('web')">Web Mode (max 100%)</button></div>
<p class="muted">Changing modes stops the rover. Gesture speed is capped at 80% in receiver firmware.</p></div>
<div class="panel span5"><div class="sectionhead"><div class="sectionicon">02</div><h2>Manual drive</h2></div><div class="controls">
<button class="blank"> </button><button onpointerdown="startMove('forward')" onpointerup="stopMove()" onpointerleave="stopMove()">Forward</button><button class="blank"> </button>
<button onpointerdown="startMove('left')" onpointerup="stopMove()" onpointerleave="stopMove()">Left</button><button class="stop" onclick="stopMove()">STOP</button><button onpointerdown="startMove('right')" onpointerup="stopMove()" onpointerleave="stopMove()">Right</button>
<button class="blank"> </button><button onpointerdown="startMove('backward')" onpointerup="stopMove()" onpointerleave="stopMove()">Backward</button><button class="blank"> </button></div>
<label>Web speed: <b id="speedLabel">60%</b></label><input id="speed" type="range" min="0" max="100" value="60" oninput="speedLabel.textContent=this.value+'%'" onchange="setSpeed(this.value)">
<p class="muted">Hold a movement button to move; release to stop.</p></div>
<div class="panel span7"><div class="sectionhead"><div class="sectionicon">03</div><h2>Gesture sensitivity & IMU telemetry</h2></div>
<label>Tilt threshold: <b id="sensLabel">12°</b></label><input id="sens" type="range" min="6" max="25" value="12" oninput="sensLabel.textContent=this.value+'°'" onchange="setSensitivity(this.value)">
<p class="muted">Lower threshold is more sensitive. Higher threshold requires a larger tilt.</p>
<div class="row"><div class="stat"><div class="muted">Pitch</div><div class="value" id="pitch">—</div></div><div class="stat"><div class="muted">Roll</div><div class="value" id="roll">—</div></div><div class="stat"><div class="muted">Gesture</div><div class="value" id="gesture">—</div></div></div>
<div class="tiltbox"><div class="muted">LIVE CONTROLLER TILT</div><svg id="tiltSvg" viewBox="0 0 300 150" role="img" aria-label="Live controller tilt visualization"><rect x="1" y="1" width="298" height="148" rx="12" fill="#0b1423" stroke="#26344b"/><path d="M150 14V136 M18 75H282" stroke="#34445e" stroke-dasharray="4 5"/><circle cx="150" cy="75" r="48" fill="none" stroke="#34445e" stroke-dasharray="3 5"/><circle id="tiltDot" cx="150" cy="75" r="12" fill="#63e6be" stroke="#d5fff3" stroke-width="2"/><text x="12" y="18" fill="#8fa1bc" font-size="10">LEFT</text><text x="251" y="18" fill="#8fa1bc" font-size="10">RIGHT</text><text x="12" y="140" fill="#8fa1bc" font-size="10">BACK</text><text x="251" y="140" fill="#8fa1bc" font-size="10">FORWARD</text></svg><div class="muted">Dot position follows roll (left/right) and pitch (forward/backward).</div></div></div>
<div class="panel span12"><div class="sectionhead"><div class="sectionicon">04</div><h2>System diagnostics</h2></div><div class="diagnostics">
<div class="stat"><div class="muted">Mode</div><div class="value" id="mode">—</div></div><div class="stat"><div class="muted">Movement</div><div class="value" id="cmd">—</div></div><div class="stat"><div class="muted">PWM</div><div class="value" id="pwm">—</div></div><div class="stat"><div class="muted">Gesture link</div><div class="value" id="link">—</div></div><div class="stat"><div class="muted">Receiver Wi-Fi RSSI</div><div class="value" id="rssi">—</div></div><div class="stat"><div class="muted">Uptime</div><div class="value" id="uptime">—</div></div><div class="stat"><div class="muted">Packet age</div><div class="value" id="age">—</div></div><div class="stat"><div class="muted">Emergency stop</div><div class="value" id="estop">—</div></div>
<div class="stat"><div class="muted">Packets received</div><div class="value" id="packets">0</div></div>
<div class="stat"><div class="muted">Invalid packets</div><div class="value" id="invalid">0</div></div>
<div class="stat"><div class="muted">Last stop reason</div><div class="value" id="stopReason">Startup</div></div>
<div class="stat"><div class="muted">Motor ramp</div><div class="value" id="ramp">Ready</div></div></div>
<p class="muted">RSSI may show N/A because the receiver is operating as an access point and is not connected to an external Wi-Fi network. Packet counters reset when the receiver reboots.</p>
<button class="stop" onclick="estop(true)">EMERGENCY STOP</button> <button onclick="estop(false)">Re-enable motors</button></div>
<div class="panel span12"><div class="sectionhead"><div class="sectionicon">05</div><h2>Advanced connection diagnostics</h2></div><div class="diagnostics">
<div class="stat"><div class="muted">ESP-NOW channel</div><div class="value">1</div></div>
<div class="stat"><div class="muted">Last packet seq</div><div class="value" id="lastSeq">—</div></div>
<div class="stat"><div class="muted">Missing packets</div><div class="value" id="gaps">0</div></div>
<div class="stat"><div class="muted">Live packet rate</div><div class="value" id="rate">—</div></div>
<div class="stat"><div class="muted">RX signal (RSSI)</div><div class="value" id="rxrssi">—</div></div>
<div class="stat"><div class="muted">Gesture link</div><div class="value" id="link2">—</div></div></div>
<p class="muted">Measured from incoming ESP-NOW packets. Missing packets counts sequence gaps; the live rate should be ~20 Hz while the transmitter is active. RX RSSI reflects the gesture-link radio signal, not the dashboard Wi-Fi.</p></div><div class="footer">ROVER CONTROL SYSTEM · Gesture mode is firmware-limited to 80% PWM. Web mode supports up to 100%. Test with drive wheels lifted before floor operation.</div></div>
</main><script>
let mode='gesture', moveTimer=null, moving=false;
async function req(path){try{let r=await fetch(path,{cache:'no-store'});return await r.json()}catch(e){return null}}
async function setMode(m){await req('/mode?value='+m);mode=m;stopLocal();refresh()}
async function setSpeed(v){await req('/speed?value='+v)}
async function setSensitivity(v){await req('/sensitivity?value='+v)}
async function estop(v){await req('/estop?value='+(v?1:0));if(v)stopLocal();refresh()}
function startMove(c){if(mode!=='web')return;moving=true;sendCmd(c);clearInterval(moveTimer);moveTimer=setInterval(()=>{if(moving)sendCmd(c)},200)}
async function sendCmd(c){await req('/cmd?value='+c)}
function stopMove(){if(!moving)return;moving=false;clearInterval(moveTimer);sendCmd('stop')}
function stopLocal(){moving=false;clearInterval(moveTimer)}
function val(id,t,cls=''){let e=document.getElementById(id);e.textContent=t;e.className='value '+cls}
async function refresh(){let s=await req('/status');if(!s)return;mode=s.mode;
document.getElementById('gestureBtn').classList.toggle('active',mode==='gesture');document.getElementById('webBtn').classList.toggle('active',mode==='web');
document.getElementById('speed').disabled=mode!=='web';document.getElementById('speed').value=s.webSpeed;document.getElementById('speedLabel').textContent=s.webSpeed+'%';
document.getElementById('sens').value=s.sensitivity;document.getElementById('sensLabel').textContent=s.sensitivity+'°';
val('pitch',s.pitch.toFixed(1)+'°');val('roll',s.roll.toFixed(1)+'°');val('gesture',s.gesture.toUpperCase());val('mode',mode.toUpperCase());val('cmd',s.command.toUpperCase());
val('pwm',s.pwm+' / 255 ('+s.percent+'%)');val('link',s.link?'CONNECTED':'DISCONNECTED',s.link?'good':'bad');
val('rssi',s.rssi===999?'N/A':s.rssi+' dBm');val('uptime',s.uptime);val('age',s.packetAge<0?'No packets':s.packetAge+' ms');
val('estop',s.estop?'ACTIVE':'Inactive',s.estop?'bad':'good');
val('packets',String(s.packets));val('invalid',String(s.invalidPackets));val('stopReason',s.stopReason);val('ramp',s.ramp+'%');
val('lastSeq',s.haveSeq?String(s.lastSeq):'—');val('gaps',String(s.gaps),s.gaps===0?'good':'bad');
val('rate',s.rate>0?s.rate.toFixed(1)+' Hz':'—');val('rxrssi',s.rxRssi? s.rxRssi+' dBm':'—');val('link2',s.link?'CONNECTED':'DISCONNECTED',s.link?'good':'bad');
let dx=Math.max(-48,Math.min(48,s.roll*1.6)),dy=Math.max(-38,Math.min(38,s.pitch*1.2));
document.getElementById('tiltDot').setAttribute('cx',150+dx);document.getElementById('tiltDot').setAttribute('cy',75+dy)}
setInterval(refresh,500);refresh();
</script></body></html>
)HTML";

void handleRoot() { server.send_P(200, "text/html", DASHBOARD); }

void handleMode() {
  lastStopReason = "Control mode changed";
  motorsOff();
  stopRover();
  controlMode = server.arg("value") == "web" ? MODE_WEB : MODE_GESTURE;
  lastWebCommandMs = millis();
  sendSettings();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleSpeed() {
  webSpeedPercent = constrain(server.arg("value").toInt(), 0, 100);
  if (controlMode == MODE_WEB && currentCommand != "stop") {
    uint8_t c = currentCommand == "forward" ? CMD_FORWARD : currentCommand == "backward" ? CMD_BACKWARD : currentCommand == "left" ? CMD_LEFT : CMD_RIGHT;
    drive(c, webSpeedPercent);
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleSensitivity() {
  sensitivityDeg = constrain(server.arg("value").toInt(), 6, 25);
  sendSettings();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleCmd() {
  if (controlMode != MODE_WEB || emergencyStop) {
    stopRover();
    server.send(409, "application/json", "{\"ok\":false}");
    return;
  }
  String v = server.arg("value");
  uint8_t c = CMD_STOP;
  if (v == "forward") c = CMD_FORWARD;
  else if (v == "backward") c = CMD_BACKWARD;
  else if (v == "left") c = CMD_LEFT;
  else if (v == "right") c = CMD_RIGHT;
  else v = "stop";
  currentCommand = v;
  lastWebCommandMs = millis();
  if (c == CMD_STOP) lastStopReason = "Web stop command";
  drive(c, webSpeedPercent);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleEStop() {
  emergencyStop = server.arg("value") == "1";
  if (emergencyStop) { lastStopReason = "Emergency stop"; motorsOff(); stopRover(); }
  server.send(200, "application/json", "{\"ok\":true}");
}

String uptimeText(uint32_t ms) {
  uint32_t sec=ms/1000;
  char b[20];
  snprintf(b,sizeof(b),"%02lu:%02lu:%02lu",(unsigned long)(sec/3600),(unsigned long)((sec/60)%60),(unsigned long)(sec%60));
  return String(b);
}

void handleStatus() {
  RoverPacket p;
  uint32_t packetMs;
  float rateHz;
  uint32_t lpc, gaps;
  int8_t rxRssi;
  bool haveSeq;
  portENTER_CRITICAL(&packetMux);
  p=latestPacket; packetMs=lastGesturePacketMs;
  rateHz=rxRateHz; lpc=lastPacketCounter; gaps=gapPackets; rxRssi=lastRxRssi; haveSeq=haveLastCounter;
  portEXIT_CRITICAL(&packetMux);
  bool link = packetMs != 0 && millis()-packetMs < 1500;
  int age = packetMs == 0 ? -1 : (int)(millis()-packetMs);
  uint8_t percent = currentCommand == "stop" ? 0 : (controlMode == MODE_GESTURE ? min((int)p.speedPercent,(int)GESTURE_MAX_PERCENT) : webSpeedPercent);
  uint8_t pwm = currentDuty;
  int rssi = WiFi.status()==WL_CONNECTED ? WiFi.RSSI() : 999;
  String j="{";
  j += "\"mode\":\""+String(controlMode==MODE_GESTURE?"gesture":"web")+"\",";
  j += "\"command\":\""+currentCommand+"\",";
  j += "\"webSpeed\":"+String(webSpeedPercent)+",";
  j += "\"sensitivity\":"+String(sensitivityDeg)+",";
  j += "\"pitch\":"+String(p.pitch,1)+",\"roll\":"+String(p.roll,1)+",";
  j += "\"gesture\":\""+commandName(p.command)+"\",";
  j += "\"link\":"+String(link?"true":"false")+",\"packetAge\":"+String(age)+",";
  j += "\"pwm\":"+String(pwm)+",\"percent\":"+String((uint16_t)pwm*100/255)+",\"rssi\":"+String(rssi)+",";
  j += "\"uptime\":\""+uptimeText(millis()-bootMs)+"\",";
  j += "\"estop\":"+String(emergencyStop?"true":"false")+",";
  j += "\"packets\":"+String((uint32_t)receivedPackets)+",";
  j += "\"invalidPackets\":"+String((uint32_t)invalidPackets)+",";
  j += "\"stopReason\":\""+lastStopReason+"\",";
  j += "\"ramp\":"+String((uint16_t)currentDuty*100/255)+",";
  j += "\"lastSeq\":"+String(lpc)+",";
  j += "\"haveSeq\":"+String(haveSeq?"true":"false")+",";
  j += "\"gaps\":"+String((uint32_t)gaps)+",";
  j += "\"rate\":"+String(rateHz,1)+",";
  j += "\"rxRssi\":"+String(rxRssi)+"}";
  server.send(200,"application/json",j);
}

void setup() {
  Serial.begin(115200);
  bootMs=millis();
  pinMode(IN1,OUTPUT); pinMode(IN2,OUTPUT); pinMode(IN3,OUTPUT); pinMode(IN4,OUTPUT);
  ledcAttach(ENA,PWM_FREQ,PWM_BITS);
  ledcAttach(ENB,PWM_FREQ,PWM_BITS);
  motorsOff();

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.softAP(AP_SSID,AP_PASSWORD,WIFI_CHANNEL);
  Serial.print("Dashboard: http://"); Serial.println(WiFi.softAPIP());

  if (esp_now_init()==ESP_OK) {
    esp_now_register_recv_cb(onDataRecv);
    esp_now_peer_info_t peer={};
    memset(peer.peer_addr,0xFF,6); peer.channel=WIFI_CHANNEL; peer.encrypt=false;
    if (!esp_now_is_peer_exist(peer.peer_addr)) esp_now_add_peer(&peer);
  } else Serial.println("ERROR: ESP-NOW init failed");

  server.on("/",HTTP_GET,handleRoot);
  server.on("/mode",HTTP_GET,handleMode);
  server.on("/speed",HTTP_GET,handleSpeed);
  server.on("/sensitivity",HTTP_GET,handleSensitivity);
  server.on("/cmd",HTTP_GET,handleCmd);
  server.on("/estop",HTTP_GET,handleEStop);
  server.on("/status",HTTP_GET,handleStatus);
  server.begin();
  sendSettings();
}

void loop() {
  server.handleClient();

  RoverPacket p; bool got=false;
  portENTER_CRITICAL(&packetMux);
  if(packetReady){p=latestPacket;packetReady=false;got=true;}
  uint32_t packetMs=lastGesturePacketMs;
  portEXIT_CRITICAL(&packetMux);

  if(controlMode==MODE_GESTURE && !emergencyStop) {
    if(packetMs==0 || millis()-packetMs>500) { lastStopReason = "Gesture timeout"; motorsOff(); stopRover(); }
    else if(got && p.type==PKT_GESTURE) {
      uint8_t percent=min((int)p.speedPercent,(int)GESTURE_MAX_PERCENT);
      if(p.command<=CMD_RIGHT) {
        currentCommand=commandName(p.command);
        drive(p.command,percent);
      }
    }
  }
  if(controlMode==MODE_WEB && currentCommand!="stop" && millis()-lastWebCommandMs>700) { lastStopReason = "Web command timeout"; motorsOff(); stopRover(); }
  updateMotorRamp();
}
