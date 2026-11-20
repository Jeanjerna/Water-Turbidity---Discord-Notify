# ระบบตรวจวัดความขุ่นของน้ำด้วย ESP32 แจ้งเตือนผ่าน Discord

เฟิร์มแวร์สำหรับ ESP32 DevKit V1 (30 ขา) อ่านค่าจากโมดูลวัดความขุ่นแบบ Analog
แสดงผลบนจอ LCD ST7735 (160×128) และส่งข้อความแจ้งเตือนเข้า Discord ผ่าน webhook
เมื่อน้ำขุ่นเกินเกณฑ์ พัฒนาด้วย PlatformIO (Arduino framework)

> ค่าที่แสดงคือ **NTU โดยประมาณ (NTU est.)** คำนวณจากสมการของกราฟอ้างอิงของเซ็นเซอร์
> และปรับค่าอ้างอิงน้ำสะอาดแบบ empirical ไม่ใช่ค่าที่สอบเทียบกับมาตรฐาน
> จึงเหมาะกับต้นแบบเพื่อการศึกษา ไม่ใช้ตัดสินความปลอดภัยของน้ำดื่ม

## การทำงาน

- อ่านค่า ADC ที่ GPIO34 วินาทีละครั้ง (เฉลี่ย 64 ครั้ง) กรองด้วย median 3 ค่า และ EMA (alpha 0.2)
- แสดง NTU, แรงดัน, ค่า ADC, สถานะน้ำ, สถานะ Wi-Fi และผลการส่ง Discord บนจอ
- **แจ้งเตือน** เมื่อ NTU > 100 ต่อเนื่อง 10 ครั้ง และ **แจ้งกลับสู่ปกติ** เมื่อ NTU < 80 ต่อเนื่อง 10 ครั้ง
  ช่วง 80–100 คงสถานะเดิม เพื่อไม่ให้แจ้งซ้ำไปมา
- ส่งรายงานครั้งแรกหลังเริ่มทำงาน และส่งเมื่อสถานะเปลี่ยน
- งานเครือข่ายแยกจาก loop หลัก เซ็นเซอร์และจอจึงทำงานต่อแม้ Wi-Fi หลุด
- กด BOOT ค้าง 3 วินาทีเพื่อตั้งค่า Wi-Fi และพิมพ์ `s` ใน Serial Monitor เพื่อส่งรายงานทันที

## อุปกรณ์

ESP32 DevKit V1 (30 ขา), โมดูลวัดความขุ่นแบบ Analog พร้อมหัววัด, จอ TFT 1.8" ST7735 แบบ SPI,
ตัวต้านทาน 10 kΩ 2 ตัว, สาย jumper และสาย USB

## การต่อวงจร

| อุปกรณ์ / ขา | เชื่อมต่อ |
|---|---|
| Sensor V (VCC) | 3V3 |
| Sensor G (GND) | GND |
| Sensor สัญญาณ | R1 10 kΩ → GPIO34 |
| จุด GPIO34 | R2 10 kΩ → GND |
| LCD SCL / CLK | GPIO18 |
| LCD SDA / MOSI | GPIO23 (SPI ไม่ใช่ I2C) |
| LCD RES | GPIO17 |
| LCD DC | GPIO16 |
| LCD CS | GPIO4 |
| LCD VCC, BLK | 3V3 |
| LCD GND | GND |

```text
Sensor signal -- R1 10k --+-- GPIO34
                          |
                        R2 10k
                          |
                         GND
```

- R1/R2 แบ่งแรงดันครึ่งหนึ่ง ทำให้สัญญาณ 3.3 V เหลือประมาณ 1.65 V ที่ GPIO34 (ESP32 ไม่ทน 5 V)
- ADC ตั้งเป็น 12 บิต, ADC_6db (ช่วงใช้งาน ~0.15–1.75 V) ค่านอกช่วงถือว่าอินพุตผิดปกติและจอแสดง CHECK INPUT
- ต่อกราวด์ร่วมกันทั้งหมด ถอดสาย USB ก่อนเปลี่ยนสาย จุ่มเฉพาะส่วนหัววัดในน้ำ เก็บบอร์ดและขั้วต่อให้แห้ง

## การคำนวณ NTU

```text
sensorVoltage     = analogReadMilliVolts(GPIO34) / 1000 * 2
normalizedVoltage = filteredSensorVoltage * (4.200246 / 1.762)
NTU est.          = -1120.4 * v^2 + 5742.3 * v - 4352.9      (v = normalizedVoltage)
```

- พหุนามมาจากกราฟอ้างอิงของเซ็นเซอร์ ส่วน `1.762 V` คือแรงดันน้ำสะอาดที่วัดได้กับชุดทดลองนี้ (ค่าคงที่ ไม่ปรับเองตอนเริ่มเครื่อง)
  ตัวคูณ `4.200246 / 1.762` เป็นการปรับเทียบจุดเดียวแบบ empirical **ไม่ใช่ค่ามาตรฐานจากผู้ผลิต**
- ที่แรงดันน้ำสะอาดผลเป็น 0, ค่าเกิน 1000 แสดงเป็น `1000+`, ค่าติดลบปัดเป็น 0
- ค่าบนหน้าจอมี deadband 5 NTU และต้องเปลี่ยนทิศเดียวกัน 3 ครั้งติด เพื่อไม่ให้ตัวเลขกระพริบ (ไม่กระทบเกณฑ์แจ้งเตือน)
- ค่าคงที่ทั้งหมดอยู่ใน [include/Config.h](include/Config.h)

แหล่งอ้างอิง: [กราฟสมการอ้างอิง](https://i.lnwfile.com/_/i/_webp_max/4096/4096/sx/w4/0g.webp),
[DFRobot Turbidity FAQ](https://www.dfrobot.com/forum/topic/315216),
[Espressif ADC API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/adc.html)

## ตั้งค่าและใช้งาน

1. ติดตั้ง VS Code + [PlatformIO](https://platformio.org/)
2. สร้าง Discord webhook: Server Settings → Integrations → Webhooks → New Webhook เลือก channel แล้วคัดลอก URL
3. คัดลอก `include/secrets.example.h` เป็น `include/secrets.h` แล้วใส่ URL
   ไฟล์นี้อยู่ใน `.gitignore` **ห้ามนำขึ้น GitHub** เพราะใครมี URL ก็โพสต์เข้า channel ได้
4. ต่อบอร์ดผ่าน USB แล้วสั่ง `pio run --target upload` (หรือกดปุ่ม Upload ของ PlatformIO)
5. เปิด Serial Monitor ที่ 115200: `pio device monitor --baud 115200`
6. ตั้งค่า Wi-Fi: กด **BOOT** ค้าง 3 วินาทีหลังบอร์ดเริ่มทำงาน เชื่อมต่อ Wi-Fi ชื่อ `WaterMonitor-Setup`
   (รหัสผ่านอยู่ใน `include/Config.h`) แล้วเปิด <http://192.168.4.1> เลือกเครือข่าย 2.4 GHz
   ปุ่ม EN คือรีเซ็ต อย่ากด BOOT ค้างตอนเปิดเครื่อง เพราะจะเข้าโหมดอัปโหลด

### สถานะ Discord / การส่งซ้ำ

- ส่งสำเร็จ = HTTP 200 / ถ้า 401, 403, 404 (webhook ผิดหรือถูกลบ) จะหยุดส่งอัตโนมัติจนกว่าจะรีสตาร์ตหรือพิมพ์ `s`
- 429 รอตามเวลาที่ Discord กำหนด / ข้อผิดพลาดชั่วคราวลองใหม่หลัง 30, 60, 300 วินาที
- หลังเน็ตหลุดจะส่งเฉพาะสถานะปัจจุบัน ไม่ส่งเหตุการณ์เก่าย้อนหลัง และไม่รับประกันว่าไม่ซ้ำ (ถ้า timeout ข้อความอาจถึงปลายทางแล้ว)
- TLS ตรวจใบรับรองกับ `certs/discord_roots.pem` (root CA สาธารณะ) และรอเวลา NTP ก่อนเชื่อมต่อ

## PlatformIO environments

| Environment | หน้าที่ |
|---|---|
| `esp32doit-devkit-v1` | โปรแกรมหลัก (ค่าเริ่มต้น) |
| `lcd-test` | ทดสอบจอแสดงผล |
| `wifi-test` | ทดสอบการตั้งค่า Wi-Fi |
| `discord-test` | ทดสอบการส่งข้อความ Discord (พิมพ์ `s` เพื่อส่ง) |
| `adc-test` | เปรียบเทียบช่วงแรงดันของ ADC 4 ระดับ |

## การทดสอบบนคอมพิวเตอร์

```bash
python scripts/run_host_tests.py
```

ต้องมี `g++` หรือ `clang++` สคริปต์คอมไพล์และรัน `tests/monitor_tests.cpp` ซึ่งทดสอบโค้ดชุดเดียวกับเฟิร์มแวร์
(การคำนวณ NTU, ตัวกรอง, เงื่อนไขแจ้งเตือน, การส่งซ้ำ, ข้อความบนจอ) โดยไม่ใช้บอร์ดและไม่ส่งข้อความจริง
การทดสอบนี้ไม่ครอบคลุมพฤติกรรมของฮาร์ดแวร์จริง เช่น ADC, จอ และ Wi-Fi

## โครงสร้างไฟล์

```text
src/main.cpp                     วงจรหลัก: อ่านเซ็นเซอร์ ปุ่ม และวาดหน้าจอ
src/NetworkService.{h,cpp}       Wi-Fi, NTP, HTTPS, Discord webhook (แยก task)
include/Config.h                 ขา GPIO ค่าเกณฑ์ เวลา และค่าคงที่ของสูตร
include/MonitorCore.h            คำนวณ NTU, ตัวกรอง, เครื่องสถานะแจ้งเตือน
include/MonitorPresentation.h    จัดข้อความที่แสดงบนจอ
include/MonitorTypes.h           ชนิดข้อมูลสถานะเครือข่าย
include/secrets.example.h        ตัวอย่างไฟล์เก็บ webhook
src/test/                        โปรแกรมทดสอบฮาร์ดแวร์ (lcd, wifi, discord, adc)
tests/monitor_tests.cpp          unit test ที่รันบนคอมพิวเตอร์
certs/discord_roots.pem          root certificate สำหรับ TLS ของ Discord
```
