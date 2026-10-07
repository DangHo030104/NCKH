# Cấu hình WiFi cho Rx_LoraE32

Thông tin WiFi được lưu trong NVS của ESP32 và không còn đặt cố định trong `AppConfig.h`.

## Cấu hình lần đầu

1. Nạp firmware và khởi động ESP32.
2. Kết nối điện thoại hoặc máy tính với WiFi `NCKH-Rx-LoRa-Setup`.
3. Nhập mật khẩu `nckh1234`.
4. Mở `http://192.168.4.1`.
5. Quét mạng, chọn WiFi 2.4 GHz, nhập mật khẩu rồi nhấn **Lưu và kết nối**.

ESP32 lưu cấu hình vào NVS và thử kết nối ngay. Khi kết nối thành công, AP cấu hình sẽ tự tắt.

## Thay đổi WiFi

- Khi ESP32 đang online, mở địa chỉ IP của ESP32 trong cùng mạng để vào trang cấu hình.
- Nếu không truy cập được mạng cũ, giữ nút **BOOT (GPIO0)** trong 5 giây. ESP32 xóa cấu hình cũ và mở lại AP `NCKH-Rx-LoRa-Setup`.
- Nếu thông tin đã lưu không kết nối được trong 15 giây, AP cấu hình cũng tự mở.

## Log cần kiểm tra

```text
[WIFI] Configuration portal started
[WIFI] AP: NCKH-Rx-LoRa-Setup
[WIFI] Portal: http://192.168.4.1
```

Sau khi lưu đúng WiFi:

```text
[WIFI] Connecting to: <SSID>
[WIFI] Connected
[WIFI] IP: <địa chỉ IP>
[WIFI] Configuration AP stopped
```
