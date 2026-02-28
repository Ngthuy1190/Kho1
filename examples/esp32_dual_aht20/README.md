# ESP32 đọc dữ liệu từ 2 cảm biến AHT20

## Vì sao phải dùng 2 bus I2C?
AHT20 có địa chỉ I2C cố định `0x38`, nên không thể đặt 2 cảm biến trên cùng 1 bus I2C (nếu không dùng I2C multiplexer như TCA9548A).

Ví dụ này dùng 2 bộ I2C của ESP32:
- `Wire` (I2C0) cho cảm biến 1
- `Wire1` (I2C1) cho cảm biến 2

## Wiring mẫu
- AHT20 #1
  - SDA -> GPIO21
  - SCL -> GPIO22
- AHT20 #2
  - SDA -> GPIO25
  - SCL -> GPIO26
- Cả hai cảm biến: cấp nguồn đúng mức (thường `3.3V`) và GND chung với ESP32.

Bạn có thể đổi chân trong file `.ino`.

## Thư viện cần cài
- `Adafruit AHTX0`
- `Adafruit Unified Sensor`

## Kết quả Serial mẫu
`AHT20 #1 | Nhiet do: 29.10 *C | Do am: 62.40 %`
