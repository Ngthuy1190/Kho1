# v1.6.4 MERGED NOTES

Đây là bản **merge trực tiếp** theo yêu cầu (không phải patch rời):
- `ESP32_HMI_v1.6.4_MERGED.ino`

Các điểm đã merge:
1. Non-blocking pulse write thay cho `delay(150)` trong loop.
2. Scheduler Modbus theo tầng thời gian để giảm nghẽn bus.
3. API `/snapshot` trả dữ liệu gộp để web polling 1 request/giây.

## Lưu ý
- File này là bản merged tối ưu phần lõi truyền thông + polling.
- Nếu bạn muốn giữ nguyên toàn bộ HTML/CSS/JS lớn của v1.6.3, chỉ cần thay phần JS polling bằng mẫu trong `WEB_SNAPSHOT_JS_PATCH.js`.
