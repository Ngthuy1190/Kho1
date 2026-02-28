/**********************************************************
 * LOC NUOC – ESP32 HMI
 * VERSION : v1.6.4 – MERGED OPTIMIZED
 * NOTE    : Merged directly from v1.6.3 + performance improvements
 *           - Non-blocking pulse write
 *           - Tiered Modbus scheduler
 *           - /snapshot endpoint for UI polling
 **********************************************************/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <EEPROM.h>
#include <ModbusRTU.h>
#include <time.h>

#define GMT_OFFSET_SEC   (7 * 3600)
#define DAYLIGHT_OFFSET  0
#define SLAVE_ID         1
#define RXD2             16
#define TXD2             17

ModbusRTU mb;
WebServer server(80);

// ===== MAP / CACHE =====
bool M_cache[600];
uint16_t D_cache[600];

// ===== TIME =====
time_t espEpoch = 0;
uint32_t lastMillisTick = 0;
uint32_t lastNTPSync = 0;
bool timeValid = false;

// ===== FLOW =====
#define FLOW1_PIN 5
#define FLOW2_PIN 4
volatile unsigned long pulse1 = 0, pulse2 = 0;
float flow1_Lmin = 0, flow2_Lmin = 0;
float deltaV1_L = 0, deltaV2_L = 0;
unsigned long lastCalc1s = 0, lastSend5s = 0;

// ===== INTERNAL =====
bool plcWatchdogLost = false;
unsigned long lastModbusOK = 0;

// ===== COMMAND BUFFER =====
volatile bool cmdPending = false;
volatile uint16_t cmdM = 0;
volatile bool cmdVal = false;
volatile bool cmdPulse = false;
volatile bool dWritePending = false;
volatile uint16_t dWriteAddr = 0;
volatile uint16_t dWriteVal = 0;

// ===== NON-BLOCKING PULSE STATE =====
bool pulseActive = false;
uint16_t pulseM = 0;
uint32_t pulseStartedAt = 0;

bool getBit(uint16_t w, uint8_t b){ return (w >> b) & 1; }

bool modbusCB(Modbus::ResultCode event, uint16_t, void*) {
  if (event == Modbus::EX_SUCCESS) lastModbusOK = millis();
  return true;
}

void readMBlock(uint16_t mStart, uint16_t len){
  mb.readCoil(SLAVE_ID, 2048 + mStart, &M_cache[mStart], len, modbusCB);
}
void readDBlock(uint16_t dStart, uint16_t len){
  mb.readHreg(SLAVE_ID, 4096 + dStart, &D_cache[dStart], len, modbusCB);
}
void readD(uint16_t d){
  mb.readHreg(SLAVE_ID, 4096 + d, &D_cache[d], 1, modbusCB);
}
void writeM(uint16_t m, bool v){
  mb.writeCoil(SLAVE_ID, 2048 + m, v, modbusCB);
}
void writeD(uint16_t d, uint16_t v){
  mb.writeHreg(SLAVE_ID, 4096 + d, v, modbusCB);
}

ICACHE_RAM_ATTR void flow1ISR(){ pulse1++; }
ICACHE_RAM_ATTR void flow2ISR(){ pulse2++; }

String buildAlarmJsonFromWord(uint16_t al){
  String j = "[";
  bool first = true;
  auto add = [&](const char* s){
    if(!first) j += ",";
    j += '"';
    j += s;
    j += '"';
    first = false;
  };

  if(getBit(al,0)) add("Quá tải bơm giếng");
  if(getBit(al,1)) add("Quá tải bơm lọc");
  if(getBit(al,2)) add("Quá tải bơm sử dụng");
  if(getBit(al,3)) add("Bể lắng cạn");
  if(getBit(al,4)) add("Bể chứa cạn");
  if(getBit(al,5)) add("Áp suất cao");
  if(getBit(al,6)) add("Mất kết nối PLC");
  if(getBit(al,7)) add("Bơm sử dụng chạy quá lâu");
  if(getBit(al,8)) add("⏸️ Đang trong giờ nghỉ");

  j += "]";
  return j;
}

void processCommandWrite(){
  if (pulseActive) {
    if (millis() - pulseStartedAt >= 150 && !mb.slave()) {
      writeM(pulseM, false);
      pulseActive = false;
    }
    return;
  }

  if (!cmdPending || mb.slave()) return;

  if (cmdPulse) {
    writeM(cmdM, true);
    pulseM = cmdM;
    pulseStartedAt = millis();
    pulseActive = true;
  } else {
    writeM(cmdM, cmdVal);
  }
  cmdPending = false;
}

void processModbusScheduler(){
  static uint32_t tFast = 0, tMedium = 0, tSlow = 0;
  const uint32_t now = millis();
  if (mb.slave()) return;

  if (now - tFast >= 200) {
    tFast = now;
    readMBlock(129, 16);
    return;
  }
  if (now - tMedium >= 400) {
    tMedium = now;
    readMBlock(520, 3);
    readDBlock(415, 10);
    return;
  }
  if (now - tSlow >= 1000) {
    tSlow = now;
    readDBlock(462, 7);
    readDBlock(20, 4);
    readD(100);
  }
}

bool syncTimeFromNTP(){
  struct tm t;
  if(!getLocalTime(&t)) return false;
  espEpoch = mktime(&t);
  lastMillisTick = millis();
  lastNTPSync = millis();
  timeValid = true;
  return true;
}

// ===== MERGED WEB PAGE (rút gọn để tập trung phần merge logic) =====
const char MAIN_PAGE[] PROGMEM = R"====(
<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>LOC NUOC HMI v1.6.4</title></head>
<body>
<h3>LOC NUOC HMI v1.6.4 MERGED</h3>
<pre id='out'>loading...</pre>
<script>
async function loadSnapshot(){
  try{
    const j = await fetch('/snapshot').then(r=>r.json());
    document.getElementById('out').textContent = JSON.stringify(j,null,2);
  }catch(e){
    document.getElementById('out').textContent = 'snapshot error';
  }
}
setInterval(loadSnapshot, 1000);
loadSnapshot();
</script>
</body></html>
)====";

void registerRoutes(){
  server.on("/", [](){ server.send_P(200, "text/html", MAIN_PAGE); });

  server.on("/cmd", [](){
    int b = server.arg("b").toInt();
    cmdPulse = false;
    switch(b){
      case 0: cmdM = 520; cmdVal = false; break;
      case 1: cmdM = 520; cmdVal = true;  break;
      case 2: cmdM = 521; cmdVal = false; break;
      case 3: cmdM = 521; cmdVal = true;  break;
      case 4: cmdM = 84;  cmdPulse = true; break;
      case 5: cmdM = 85;  cmdPulse = true; break;
      case 6: cmdM = 86;  cmdPulse = true; break;
      case 7: cmdM = 522; cmdVal = !M_cache[522]; break;
      default: server.send(400, "text/plain", "INVALID"); return;
    }
    cmdPending = true;
    server.send(200, "text/plain", "OK");
  });

  server.on("/write_one", [](){
    dWriteAddr = server.arg("d").toInt();
    dWriteVal = server.arg("v").toInt();
    dWritePending = true;
    server.send(200, "text/plain", "OK");
  });

  server.on("/snapshot", [](){
    uint16_t al = D_cache[3];
    if (plcWatchdogLost) al |= (1 << 6);

    int sec=0,min=0,hour=0,day=0,mon=0,year=0;
    if(timeValid){
      struct tm *t = localtime(&espEpoch);
      sec=t->tm_sec; min=t->tm_min; hour=t->tm_hour;
      day=t->tm_mday; mon=t->tm_mon+1; year=t->tm_year+1900;
    }

    String alarm = buildAlarmJsonFromWord(al);
    char buf[1024];
    int n = snprintf(
      buf, sizeof(buf),
      "{\"status\":[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d],"
      "\"flow\":{\"f1\":%.2f,\"f2\":%.2f},"
      "\"d100\":%u,\"m522\":%d,"
      "\"vol\":{\"v0\":%.3f,\"v1\":%.3f,\"v2\":%.3f,\"v3\":%.3f,\"v4\":%.3f,\"v5\":%.3f,\"v6\":%.3f},"
      "\"autoval\":{\"bw_m\":%u,\"bw_s\":%u,\"fw_m\":%u,\"fw_s\":%u},"
      "\"clock\":{\"sec\":%d,\"min\":%d,\"hour\":%d,\"day\":%d,\"mon\":%d,\"year\":%d},"
      "\"alarm\":%s}",
      M_cache[130],M_cache[131],M_cache[132],M_cache[133],M_cache[134],
      M_cache[135],M_cache[136],M_cache[137],M_cache[138],M_cache[139],
      M_cache[140],M_cache[141],M_cache[142],M_cache[143],M_cache[144],
      flow1_Lmin, flow2_Lmin, D_cache[100], M_cache[522] ? 1 : 0,
      D_cache[462]/1000.0, D_cache[463]/1000.0, D_cache[464]/1000.0,
      D_cache[465]/1000.0, D_cache[466]/1000.0, D_cache[467]/1000.0, D_cache[468]/1000.0,
      D_cache[21], D_cache[20], D_cache[23], D_cache[22],
      sec,min,hour,day,mon,year,
      alarm.c_str()
    );

    if(n < 0 || n >= (int)sizeof(buf)){
      server.send(500, "text/plain", "snapshot overflow");
      return;
    }
    server.send(200, "application/json", buf);
  });
}

void setup(){
  memset(M_cache, 0, sizeof(M_cache));
  memset(D_cache, 0, sizeof(D_cache));

  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  mb.begin(&Serial2);
  mb.master();
  lastModbusOK = millis();

  pinMode(FLOW1_PIN, INPUT_PULLUP);
  pinMode(FLOW2_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW1_PIN), flow1ISR, RISING);
  attachInterrupt(digitalPinToInterrupt(FLOW2_PIN), flow2ISR, RISING);

  WiFi.mode(WIFI_AP);
  WiFi.softAP("LOCNUOC_SETUP");

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET, "pool.ntp.org", "time.nist.gov");
  syncTimeFromNTP();

  registerRoutes();
  server.begin();
}

void loop(){
  processCommandWrite();

  if(dWritePending && !mb.slave()){
    writeD(dWriteAddr, dWriteVal);
    dWritePending = false;
  }

  processModbusScheduler();

  mb.task();
  server.handleClient();
  yield();

  if(millis() - lastCalc1s >= 1000){
    lastCalc1s += 1000;
    noInterrupts();
    unsigned long p1 = pulse1; pulse1 = 0;
    unsigned long p2 = pulse2; pulse2 = 0;
    interrupts();

    flow1_Lmin = p1 / 6.6;
    flow2_Lmin = p2 / 6.6;
    deltaV1_L += flow1_Lmin / 60.0;
    deltaV2_L += flow2_Lmin / 60.0;
  }

  static uint16_t lastV1 = 0, lastV2 = 0;
  if(millis() - lastSend5s >= 5000 && !mb.slave()){
    lastSend5s = millis();
    uint16_t v1 = (uint16_t)(deltaV1_L * 100);
    uint16_t v2 = (uint16_t)(deltaV2_L * 100);

    if(v1 != lastV1){ writeD(30, v1); lastV1 = v1; }
    if(v2 != lastV2){ writeD(32, v2); lastV2 = v2; }
  }

  plcWatchdogLost = (millis() - lastModbusOK > 5000);

  if(timeValid){
    uint32_t nowMs = millis();
    if(nowMs - lastMillisTick >= 1000){
      uint32_t sec = (nowMs - lastMillisTick) / 1000;
      espEpoch += sec;
      lastMillisTick += sec * 1000;
    }
  }

  if(WiFi.status() == WL_CONNECTED && millis() - lastNTPSync > 5UL * 60UL * 1000UL){
    syncTimeFromNTP();
  }
}
