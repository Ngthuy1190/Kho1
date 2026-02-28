/*
 * Performance patch for v1.6.3 (drop-in sections)
 * Mục tiêu:
 * 1) Bỏ delay() trong loop để không chặn Modbus/WebServer.
 * 2) Giảm số request HTTP từ UI bằng endpoint /snapshot.
 * 3) Giảm phân mảnh heap do String concat liên tục.
 */

#include <Arduino.h>

// ===== 1) Non-blocking pulse command (thay cho delay(150)) =====
volatile bool cmdPending = false;
volatile uint16_t cmdM = 0;
volatile bool cmdVal = false;
volatile bool cmdPulse = false;

bool pulseActive = false;
uint32_t pulseStartedAt = 0;
uint16_t pulseM = 0;

inline void processCommandWrite() {
  // giữ nguyên điều kiện bus rảnh như code gốc: !mb.slave()
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


// ===== 2) Scheduler ưu tiên polling nhanh, nhóm theo chu kỳ =====
inline void processModbusScheduler() {
  static uint32_t tFast = 0, tMedium = 0, tSlow = 0;
  const uint32_t now = millis();

  if (mb.slave()) return;

  // 200ms: trạng thái bơm/van + mode
  if (now - tFast >= 200) {
    tFast = now;
    readMBlock(129, 16);   // M129..M144
    return;
  }

  // 400ms: setting operator + mode bits
  if (now - tMedium >= 400) {
    tMedium = now;
    readMBlock(520, 3);    // M520..M522
    readDBlock(415, 10);   // D415..D424
    return;
  }

  // 1000ms: data ít thay đổi
  if (now - tSlow >= 1000) {
    tSlow = now;
    readDBlock(462, 7);    // volume
    readDBlock(20, 4);     // autoval timer
    readD(100);            // heartbeat
  }
}


// ===== 3) JSON snapshot: gộp nhiều endpoint thành 1 request =====
String buildAlarmJsonFromWord(uint16_t alarmWord) {
  String a = "[";
  bool first = true;

  auto add = [&](const char* s) {
    if (!first) a += ",";
    a += '"';
    a += s;
    a += '"';
    first = false;
  };

  if ((alarmWord >> 0) & 1) add("Quá tải bơm giếng");
  if ((alarmWord >> 1) & 1) add("Quá tải bơm lọc");
  if ((alarmWord >> 2) & 1) add("Quá tải bơm sử dụng");
  if ((alarmWord >> 3) & 1) add("Bể lắng cạn");
  if ((alarmWord >> 4) & 1) add("Bể chứa cạn");
  if ((alarmWord >> 5) & 1) add("Áp suất cao");
  if ((alarmWord >> 6) & 1) add("Mất kết nối PLC");
  if ((alarmWord >> 7) & 1) add("Bơm sử dụng chạy quá lâu");
  if ((alarmWord >> 8) & 1) add("⏸️ Đang trong giờ nghỉ");

  a += "]";
  return a;
}

void registerSnapshotEndpoint() {
  server.on("/snapshot", []() {
    uint16_t al = D_cache[3];
    if (plcWatchdogLost) al |= (1 << 6);

    char buf[1024];
    int n = snprintf(
      buf, sizeof(buf),
      "{"
      "\"status\":[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d],"
      "\"flow\":{\"f1\":%.2f,\"f2\":%.2f},"
      "\"d100\":%u,"
      "\"m522\":%d,"
      "\"vol\":{\"v0\":%.3f,\"v1\":%.3f,\"v2\":%.3f,\"v3\":%.3f,\"v4\":%.3f,\"v5\":%.3f,\"v6\":%.3f},"
      "\"autoval\":{\"bw_m\":%u,\"bw_s\":%u,\"fw_m\":%u,\"fw_s\":%u},"
      "\"clock\":{\"sec\":%d,\"min\":%d,\"hour\":%d,\"day\":%d,\"mon\":%d,\"year\":%d},"
      "\"alarm\":%s"
      "}",
      M_cache[130], M_cache[131], M_cache[132], M_cache[133], M_cache[134],
      M_cache[135], M_cache[136], M_cache[137], M_cache[138], M_cache[139],
      M_cache[140], M_cache[141], M_cache[142], M_cache[143], M_cache[144],
      flow1_Lmin, flow2_Lmin,
      D_cache[100],
      M_cache[522] ? 1 : 0,
      D_cache[462] / 1000.0, D_cache[463] / 1000.0, D_cache[464] / 1000.0,
      D_cache[465] / 1000.0, D_cache[466] / 1000.0, D_cache[467] / 1000.0,
      D_cache[468] / 1000.0,
      D_cache[21], D_cache[20], D_cache[23], D_cache[22],
      localtime(&espEpoch)->tm_sec,
      localtime(&espEpoch)->tm_min,
      localtime(&espEpoch)->tm_hour,
      localtime(&espEpoch)->tm_mday,
      localtime(&espEpoch)->tm_mon + 1,
      localtime(&espEpoch)->tm_year + 1900,
      buildAlarmJsonFromWord(al).c_str()
    );

    if (n < 0 || n >= (int)sizeof(buf)) {
      server.send(500, "text/plain", "snapshot overflow");
      return;
    }

    server.send(200, "application/json", buf);
  });
}

/*
 * 4) JS tối ưu (thay setInterval hiện tại)
 *
 * setInterval(async ()=>{
 *   const j = await fetch('/snapshot').then(r=>r.json());
 *   // update UI bằng j.status / j.flow / j.vol / j.clock / j.alarm ...
 * }, 1000);
 *
 * => Giảm từ 6-8 request/s còn 1 request/s.
 */

/*
 * 5) Tích hợp nhanh trong loop() hiện tại:
 *
 * void loop(){
 *   processCommandWrite();
 *   processModbusScheduler();
 *   mb.task();
 *   server.handleClient();
 *   // phần flow/wifi/watchdog giữ nguyên
 * }
 */
