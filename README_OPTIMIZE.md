# Tối ưu hiệu năng ESP32 Modbus RTU + WebServer

File `ESP32_HMI_v1.6.3_perf_patch.ino` chứa các đoạn patch chính để áp vào code hiện tại:

1. **Bỏ `delay(150)` khi ghi bit xung** bằng state machine không-blocking.
2. **Scheduler Modbus theo chu kỳ** (200/400/1000 ms) thay vì tick 150 ms tuần tự.
3. **Endpoint `/snapshot`** gộp dữ liệu để giảm request từ frontend.

## Cách áp nhanh
- Copy các hàm trong file patch vào project chính.
- Gọi `registerSnapshotEndpoint()` trong `setup()` sau khi khai báo các route khác.
- Trong `loop()`, gọi:
  - `processCommandWrite();`
  - `processModbusScheduler();`
  - `mb.task();`
  - `server.handleClient();`
- Chuyển JS từ nhiều `fetch` mỗi giây sang một `fetch('/snapshot')` mỗi giây.

## Kỳ vọng cải thiện
- Giảm block loop do `delay`, giúp Modbus ổn định khi nhiều thao tác web.
- Giảm tải CPU + heap fragmentation do giảm concat String và số request.
- UI mượt hơn khi cập nhật trạng thái nhanh.
