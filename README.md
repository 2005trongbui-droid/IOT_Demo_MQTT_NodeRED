# Nhóm 14 — Băng tải phân loại theo chiều cao

Mã nguồn mô phỏng và triển khai luồng trong slide 3P: cảm biến → ESP32 → MQTT Broker → Node-RED → Dashboard 2.0 / InfluxDB; lệnh START, STOP, RESET đi theo chiều ngược lại.

## Đã đồng bộ trong firmware

- `IR_IN` GPIO27 bắt đầu chu kỳ đo; `IR_OUT` GPIO26 xác nhận sản phẩm đi qua. IR_OUT bị che rồi thông thoáng mới cho phép nhận sản phẩm kế tiếp.
- HC-SR04 dùng TRIG GPIO5, ECHO GPIO18; lấy 5 mẫu hợp lệ, dùng median và tính `h = H - d` với H hiệu chuẩn mặc định 20 cm. Khoảng cách `d > 20 cm` được hiểu là chưa có sản phẩm trong vùng đo: không tạo chiều cao âm và không cộng vào thống kê.
- Ngưỡng PASS mặc định 8–12 cm; kết quả khác ngưỡng là REJECT. Kết luận được thực hiện ngay trên ESP32.
- Servo SG90 GPIO13 gạt sản phẩm REJECT. L298N điều khiển GA12-N20: IN1 GPIO32, IN2 GPIO33, ENA GPIO25 (đóng/ngắt). Hệ thống khởi động ở trạng thái STOP; lệnh START mới chạy băng tải.
- ESP32 PUBLISH `/telemetry`, `/event`, `/status`, `/keepalive`; SUBSCRIBE `/command`. Telemetry phát khi có sản phẩm, lỗi đo hoặc lệnh reset; `/keepalive` phát riêng mỗi 5 giây; status giữ bộ đếm mới nhất. Node-RED lọc bản tin không phải kết quả PASS/REJECT khỏi biểu đồ và Data History. Mỗi topic có tiền tố `huit/nhom14/detai31/conveyor/`.
- Heartbeat ứng dụng `/keepalive` gửi mỗi 5 giây. Node-RED đánh dấu OFFLINE sau hơn 15 giây không nhận heartbeat. MQTT protocol Keep Alive được đặt riêng là 15 giây.
- Mỗi kết quả phân loại có `sample_id` để nhận diện cùng một mẫu giữa Dashboard và Data History.

## Chạy trên Docker Desktop

1. Cài Docker Desktop và mở thư mục project này trong VS Code.
2. Sao chép `.env.example` thành `.env` (`Copy-Item .env.example .env` trong PowerShell hoặc `copy .env.example .env` trong CMD). Token mẫu chỉ dùng cho demo nội bộ; có thể đổi cùng một giá trị ở `.env` trước khi khởi động.
3. Mở **PowerShell** tại thư mục project và chạy `Set-ExecutionPolicy -Scope Process Bypass`, sau đó chạy `./setup-mqtt-users.ps1`. Script hỏi riêng mật khẩu cho `esp32` và `nodered`, rồi tạo file băm `mosquitto/passwordfile`. Không đưa file này lên Git.
4. Chạy `docker compose up -d --build --force-recreate mosquitto node-red` trong Terminal tại thư mục có `docker-compose.yml`. Việc tạo lại Mosquitto giúp áp dụng `passwordfile` và ACL vừa thiết lập.
5. Mở Node-RED tại `http://localhost:1880`. Import `node_red_flow.json`, cấu hình tài khoản broker theo mục bên dưới rồi Deploy. Container cài Node-RED Dashboard 2.0 từ `node-red/package.json`.
6. Mở Dashboard tại `http://localhost:1880/dashboard/`.
7. Mở InfluxDB tại `http://localhost:8086`; đăng nhập bằng thông tin trong `.env`. Tổ chức là `nhom14`, bucket là `conveyor`.

Nếu dùng CMD thay vì PowerShell, chạy script bằng `powershell -ExecutionPolicy Bypass -File .\setup-mqtt-users.ps1`.

## Hai cách kết nối MQTT

### Mô phỏng Wokwi

Wokwi không kết nối trực tiếp đến Mosquitto chỉ nằm trong Docker Desktop. Firmware mặc định dùng `test.mosquitto.org`; trong Node-RED Import `node_red_flow_wokwi.json` để cùng dùng broker công khai. Nhấn nút xanh trên sơ đồ để giả lập IR_IN; nhấn nút đỏ rồi thả ra để giả lập IR_OUT. LED xanh biểu diễn tín hiệu bật/tắt ENA của L298N; đây không phải mô phỏng tốc độ motor. Wokwi không mô phỏng L298N và động cơ GA12-N20 trong sơ đồ này.

Wokwi online dùng broker công khai, không có Authentication/ACL. Luồng Wokwi chỉ phục vụ mô phỏng; không gửi thông tin riêng hoặc dùng broker này để điều khiển thiết bị thật.

### ESP32 thật và Mosquitto Docker

1. Trong Node-RED, mở cấu hình MQTT Broker dùng chung: nhấp đúp một node `SUBSCRIBE`, bấm biểu tượng cây bút cạnh broker → tab **Security**. Nhập username `nodered` và mật khẩu đã đặt trong script. Chọn **Update**, **Done**, rồi **Deploy**. Các node MQTT dùng chung cấu hình này. Không dùng `node_red_flow_wokwi.json` cho broker Docker; flow đó dành cho broker công khai Wokwi.
2. Tạo file cấu hình riêng cho ESP32. Trong PowerShell chạy `Copy-Item .\include\mqtt_secrets.example.h .\include\mqtt_secrets.h`; trong CMD chạy `copy .\include\mqtt_secrets.example.h .\include\mqtt_secrets.h`. Mở `include/mqtt_secrets.h`, đổi Wi-Fi, `MQTT_HOST` thành IPv4 LAN của máy chạy Docker Desktop (ví dụ `192.168.1.10`), `MQTT_USERNAME` thành `esp32`, và `MQTT_PASSWORD` thành mật khẩu đã đặt cho tài khoản `esp32`. Không dùng `localhost` làm broker host trên ESP32.
3. Build và nạp firmware. ESP32 khởi động ở STOP; nhấn START trên Dashboard để chạy băng tải. ESP32 và máy chạy Docker cần cùng mạng LAN; cho phép TCP 1883 qua firewall nếu Windows yêu cầu.

Sau khi chạy `setup-mqtt-users.ps1` và khởi động lại broker, Mosquitto Docker bật xác thực và ACL thật: kết nối anonymous bị từ chối; mỗi tài khoản chỉ được publish/subscribe đúng các topic trong `mosquitto/aclfile`.

#### Quyền tài khoản và topic

Tiền tố đầy đủ: `huit/nhom14/detai31/conveyor`.

| Tài khoản | Quyền PUBLISH | Quyền SUBSCRIBE |
|---|---|---|
| `esp32` | `/telemetry`, `/event`, `/status`, `/keepalive` | `/command` |
| `nodered` | `/command` | `/telemetry`, `/event`, `/status`, `/keepalive` |

Topic ngoài danh sách bị từ chối theo mặc định. Wokwi online vẫn dùng broker công khai và flow riêng không xác thực; Authentication/ACL được kiểm chứng trên Mosquitto Docker khi dùng ESP32 thật.

**Câu ghi trên slide:** “Mosquitto Docker: bật xác thực tài khoản, tắt anonymous; ACL phân quyền topic riêng cho ESP32 và Node-RED.” Thêm chú thích nhỏ: “Wokwi online dùng broker công khai riêng để mô phỏng.”

## Cập nhật flow sau khi thay project

Node-RED lưu flow trong Docker volume, nên thay file ZIP không tự cập nhật flow đã deploy. Trong Node-RED, sao lưu flow cũ nếu cần, xóa tab demo cũ, rồi **Import** đúng một file:

- `node_red_flow_wokwi.json` khi ESP32 chạy trên Wokwi và dùng broker công khai.
- `node_red_flow.json` khi ESP32 thật dùng Mosquitto Docker có Authentication/ACL.

Sau đó nhập lại username/password của tài khoản `nodered` trong cấu hình broker, bấm **Deploy**, rồi tải lại Dashboard để xóa các điểm biểu đồ cũ còn trong trình duyệt.

## Build và nạp firmware bằng PlatformIO

- Cài extension PlatformIO IDE trong VS Code, mở thư mục có `platformio.ini`.
- Chạy `PlatformIO: Build` để biên dịch; chọn `PlatformIO: Upload` để nạp vào ESP32 thật.
- Mở Serial Monitor ở 115200 baud. Dự án đặt UART2 trên RX16/TX17; dùng đúng bộ chuyển USB-UART/board đã đấu.
- Với Wokwi VS Code, chọn `Wokwi: Start Simulator`; sơ đồ có hai nút giả lập cảm biến IR và LED trạng thái motor.

## Cấu hình chân

| Thành phần | ESP32 |
| --- | --- |
| IR_IN | GPIO27, active LOW |
| IR_OUT | GPIO26, active LOW |
| HC-SR04 TRIG / ECHO | GPIO5 / GPIO18 |
| Servo SG90 | GPIO13 |
| L298N IN1 / IN2 / ENA | GPIO32 / GPIO33 / GPIO25 |

Phần cứng thật cần nối chung GND giữa ESP32, L298N và nguồn điều khiển. Không cấp motor từ chân ESP32. Dùng nguồn motor phù hợp, kiểm tra dòng tải và dùng cầu chia áp ECHO HC-SR04 về mức an toàn cho ESP32. Nguồn servo nên đủ dòng.

## Khoảng cách trong Wokwi

Thanh Distance của HC-SR04 trong Wokwi có phạm vi cố định 2–400 cm, không cấu hình được mức tối đa 20 cm bằng `diagram.json`. Project đặt `distance: "20"` làm giá trị ban đầu. Firmware chỉ phân loại trong vùng từ mặt cảm biến tới mặt băng tải H = 20 cm; nếu thanh được kéo lớn hơn 20 cm, bản tin mang trạng thái `NO_PRODUCT`, không tạo chiều cao âm và không cộng thống kê. Vì vậy khi trình diễn, kéo thanh về khoảng 2–20 cm.

## Biểu đồ và đối chiếu Dashboard với InfluxDB

Dashboard chỉ vẽ sản phẩm có kết quả `PASS` hoặc `REJECT` và số đo hợp lệ. Bản tin `BOOT`, `NO_PRODUCT`, `MEASURE_ERROR` không được đưa vào gauge, biểu đồ chiều cao hoặc Data History. Data History ghi mỗi kết quả phân loại một lần; `/keepalive` chỉ phục vụ watchdog, còn status được lưu ở measurement lịch sử trạng thái riêng.

Trong Data Explorer của InfluxDB, dùng truy vấn Flux sau để xem mẫu phân loại mới nhất (gồm `sample_id` và bộ đếm tại đúng thời điểm đó):

```flux
from(bucket: "conveyor")
  |> range(start: -1h)
  |> filter(fn: (r) => r._measurement == "conveyor")
  |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
  |> filter(fn: (r) => exists r.sample_id and (r.result == "PASS" or r.result == "REJECT"))
  |> sort(columns: ["_time"], desc: true)
  |> limit(n: 1)
```

Để chụp số liệu báo cáo, chạy RESET trước, phân loại ít nhất một sản phẩm, đợi Node-RED báo InfluxDB `204`, rồi so `Mã mẫu` trên Dashboard với `sample_id` trong InfluxDB. Chụp hai màn hình gần nhau; chỉ so bộ đếm thuộc cùng `sample_id`. Các điểm BOOT/0 cũ đã ghi trước khi cập nhật vẫn còn trong bucket, nhưng truy vấn trên loại chúng khỏi kết quả.

## Kiểm tra demo

1. Thanh Distance mở đầu ở 20 cm. Đặt H = 20 cm; chỉnh HC-SR04 để `d = 10 cm` thì `h = 10 cm`, kết quả PASS.
2. Chỉnh `d = 5 cm` thì `h = 15 cm`, kết quả REJECT và servo gạt.
3. Nhấn START/STOP/RESET trên Dashboard; kiểm tra ESP32 nhận topic `/command` và trạng thái motor/counters thay đổi.
4. Nhấn rồi thả IR_OUT sau mỗi sản phẩm; Serial Monitor in `IR_OUT clear ... re-arm`.
5. Dừng heartbeat hoặc ngắt thiết bị trên 15 giây; Node-RED hiển thị OFFLINE.
6. Phân loại sản phẩm; kiểm tra Dashboard hiện mã mẫu, Node-RED báo ghi thành công `204`, rồi dùng truy vấn Flux ở trên để xác nhận cùng mã mẫu trong measurement `conveyor`.

## Ghi chú phạm vi

Mosquitto Docker bật MQTT Authentication/ACL sau khi tạo `passwordfile`; InfluxDB dùng tài khoản/token trong `.env`. Node-RED/Dashboard của demo nội bộ chưa bật đăng nhập riêng. Wokwi online tiếp tục dùng broker công khai không xác thực; đây là giới hạn riêng của đường mô phỏng. Trong Wokwi, hai nút có nhãn `IR_IN (mo phong)` và `IR_OUT (mo phong)` giả lập cảm biến quang. LED chỉ báo tín hiệu ENA; Wokwi không mô phỏng mạch L298N và động cơ giảm tốc GA12-N20 12 V. Khi báo cáo, nói rõ cụm motor là phần cứng/cơ khí của mô hình thật; ảnh mô phỏng chỉ cho thấy tín hiệu điều khiển motor.

Mật khẩu demo cần được thay trước khi triển khai thật; không mở cổng 1883 ra Internet. Giá trị H và ngưỡng chiều cao cần hiệu chuẩn theo mô hình thực tế.
