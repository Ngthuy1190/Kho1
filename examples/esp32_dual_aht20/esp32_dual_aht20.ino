#include <Wire.h>
#include <Adafruit_AHTX0.h>

// AHT20 có địa chỉ I2C cố định 0x38.
// Vì vậy để đọc 2 cảm biến AHT20 cùng lúc, ta dùng 2 bus I2C khác nhau của ESP32:
// - Bus 0 (Wire)  -> AHT20 #1
// - Bus 1 (Wire1) -> AHT20 #2

// Chỉnh lại chân theo phần cứng của bạn
constexpr int SDA_1 = 21;
constexpr int SCL_1 = 22;
constexpr int SDA_2 = 25;
constexpr int SCL_2 = 26;

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t I2C_FREQ = 100000;  // 100kHz
constexpr uint32_t READ_INTERVAL_MS = 2000;

Adafruit_AHTX0 aht1;
Adafruit_AHTX0 aht2;

bool initAHT20(Adafruit_AHTX0 &sensor, TwoWire &wireBus, const char *name) {
  if (!sensor.begin(&wireBus)) {
    Serial.print("[LOI] Khong khoi tao duoc ");
    Serial.println(name);
    return false;
  }

  Serial.print("[OK ] Da khoi tao ");
  Serial.println(name);
  return true;
}

void readAndPrint(Adafruit_AHTX0 &sensor, const char *name) {
  sensors_event_t humidity;
  sensors_event_t temp;
  sensor.getEvent(&humidity, &temp);

  Serial.print(name);
  Serial.print(" | Nhiet do: ");
  Serial.print(temp.temperature, 2);
  Serial.print(" *C | Do am: ");
  Serial.print(humidity.relative_humidity, 2);
  Serial.println(" %");
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(500);

  Serial.println("\n=== ESP32 doc 2 AHT20 tren 2 bus I2C ===");

  Wire.begin(SDA_1, SCL_1, I2C_FREQ);
  Wire1.begin(SDA_2, SCL_2, I2C_FREQ);

  bool ok1 = initAHT20(aht1, Wire, "AHT20 #1 (Wire)");
  bool ok2 = initAHT20(aht2, Wire1, "AHT20 #2 (Wire1)");

  if (!ok1 || !ok2) {
    Serial.println("Kiem tra lai day SDA/SCL, nguon, va thu vien Adafruit_AHTX0.");
  }
}

void loop() {
  readAndPrint(aht1, "AHT20 #1");
  readAndPrint(aht2, "AHT20 #2");
  Serial.println("-------------------------------");

  delay(READ_INTERVAL_MS);
}
