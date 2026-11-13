/*
 * Project cá nhân RTOS - FreeRTOS trên ESP32
 * Task 1..6 theo đề + các kỹ thuật mở rộng (Task 7, 8, software timer, ISR, event group, mutex, queue).
 *
 * Bản đồ kỹ thuật -> code (dùng làm minh chứng trong report):
 *   [K1] xTaskCreatePinnedToCore, priority, stack ......... setup()
 *   [K2] vTaskDelayUntil (chu kỳ cố định) ................. task1_led1, task2_sensor, task6_oled
 *   [K3] vTaskDelay (nhường CPU, trạng thái Blocked) ...... task3_btn1, task4_btn2, task5_led3
 *   [K4] Binary semaphore (đồng bộ task-task) ............. task4_btn2 -> task5_led3
 *   [K5] Mutex (bảo vệ biến toàn cục g_sensor) ............ task2_sensor, task6_oled
 *   [K6] Queue + gatekeeper task (chỉ 1 task dùng Serial) . logf(), task7_logger
 *   [K7] Event group (chờ điều kiện khởi động) ............ task2_sensor, task6_oled, setup()
 *   [K8] ISR + task notification (deferred interrupt) ..... isr_boot(), task8_monitor
 *   [K9] Critical section (spinlock ESP32 dual-core) ...... led3 deadline, g_btn2Count
 *   [K10] Software timer (auto-reload, timer daemon task) . heartbeat_cb()
 *   [K11] Giám sát stack/heap: uxTaskGetStackHighWaterMark  task8_monitor
 *   [K12] Xoá task: vTaskDelete(NULL) cho loopTask Arduino  loop()
 */
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <freertos/timers.h>
#include "config.h"
#include <esp_log.h>

// ---------------- Dữ liệu dùng chung ----------------
struct SensorData {
  uint16_t raw;      // ADC 12 bit
  float    lux;      // ước lượng
  uint8_t  percent;  // % ánh sáng
  uint32_t seq;      // số lần đọc
};

static SensorData g_sensor = {0, 0.0f, 0, 0};    // [Task 2] ghi, [Task 6] đọc -> mutex
static volatile bool g_led2On = false;           // chỉ Task 3 ghi, bool 1 byte -> đọc/ghi nguyên tử
static volatile bool g_led3On = false;
static volatile TickType_t g_led3OffTick = 0;
static volatile uint32_t g_btn2Count = 0;
static volatile uint32_t g_logDropped = 0;

static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;   // [K9]

static SemaphoreHandle_t  g_btn2Sem;     // [K4] binary semaphore
static SemaphoreHandle_t  g_sensorMutex; // [K5]
static QueueHandle_t      g_logQueue;    // [K6]
static EventGroupHandle_t g_sysEvents;   // [K7]
static TimerHandle_t      g_heartbeat;   // [K10]

static TaskHandle_t h_led1, h_sensor, h_btn1, h_btn2, h_led3, h_oled, h_logger, h_monitor;

static Adafruit_SSD1306 oled(128, 64, &Wire, -1);

// ---------------- Logger qua queue [K6] ----------------
struct LogMsg {
  TickType_t tick;
  char text[56];
};

// Gọi được từ mọi task (không gọi từ ISR). Không block: queue đầy thì bỏ và đếm.
static void logf(const char *fmt, ...) {
  LogMsg m;
  m.tick = xTaskGetTickCount();
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(m.text, sizeof(m.text), fmt, ap);
  va_end(ap);
  if (xQueueSend(g_logQueue, &m, 0) != pdPASS) {
    portENTER_CRITICAL(&g_mux);
    g_logDropped = g_logDropped + 1;
    portEXIT_CRITICAL(&g_mux);
  }
}

// ---------------- Task 1: LED1 nháy T = 200 ms ----------------
static void task1_led1(void *arg) {
  TickType_t last = xTaskGetTickCount();
  const TickType_t half = pdMS_TO_TICKS(T_LED1_MS / 2);
  bool on = false;
  for (;;) {
    on = !on;
    digitalWrite(PIN_LED1, on);
    vTaskDelayUntil(&last, half);   // [K2] không trôi chu kỳ dù thân task tốn thời gian
  }
}

// ---------------- Task 2: đọc cảm biến ánh sáng, 500 ms/lần ----------------
static float mv_to_lux(uint32_t mv) {
  // 3V3 -[LDR_R_FIXED]- ADC -[quang trở]- GND: tối -> R quang trở lớn -> điện áp cao
  float v = mv / 1000.0f;
  if (v >= 3.25f) return 0.0f;
  if (v < 0.005f) v = 0.005f;
  float r = LDR_R_FIXED * v / (3.3f - v);                    // điện trở quang trở
  return 10.0f * powf(LDR_RL10 / r, 1.0f / LDR_GAMMA);       // R = RL10 * (lux/10)^-gamma
}

static uint8_t lux_to_percent(float lux) {
  // thang log: 1 lux = 0 %, LUX_FULL = 100 % (mắt người cảm nhận độ sáng theo log)
  if (lux <= 1.0f) return 0;
  float p = log10f(lux) / log10f(LUX_FULL) * 100.0f;
  return (uint8_t)(p > 100.0f ? 100.0f : p);
}

static void task2_sensor(void *arg) {
  TickType_t last = xTaskGetTickCount();
  uint32_t nlog = 0;
  for (;;) {
    uint16_t raw = analogRead(PIN_LDR);
    uint32_t mv = analogReadMilliVolts(PIN_LDR);   // đã hiệu chỉnh theo eFuse của chip
    float lux = mv_to_lux(mv);
    uint8_t pct = lux_to_percent(lux);
    if ((++nlog & 1) == 0)                                              // log mỗi 1 s
      logf("LDR raw=%u %lumV light=%u%% ~%.0f lx", raw, (unsigned long)mv, pct, lux);

    if (xSemaphoreTake(g_sensorMutex, pdMS_TO_TICKS(50)) == pdTRUE) {   // [K5]
      g_sensor.raw = raw;
      g_sensor.lux = lux;
      g_sensor.percent = pct;
      g_sensor.seq++;
      xSemaphoreGive(g_sensorMutex);
    }
    xEventGroupSetBits(g_sysEvents, EVT_SENSOR_READY);                  // [K7]
    vTaskDelayUntil(&last, pdMS_TO_TICKS(T_SENSOR_MS));
  }
}

// Phát hiện cạnh nhấn có chống dội. Trả true đúng 1 lần mỗi lần nhấn.
struct Debounce {
  uint8_t pin;
  bool stable;       // HIGH = nhả (pull-up)
  bool lastRead;
  TickType_t since;
};

static bool pressed_edge(Debounce &d) {
  bool r = digitalRead(d.pin);
  TickType_t now = xTaskGetTickCount();
  if (r != d.lastRead) { d.lastRead = r; d.since = now; }
  if ((now - d.since) >= pdMS_TO_TICKS(T_DEBOUNCE_MS) && r != d.stable) {
    d.stable = r;
    return r == LOW;
  }
  return false;
}

// ---------------- Task 3: BTN1 đảo trạng thái LED2 ----------------
static void task3_btn1(void *arg) {
  Debounce d = {PIN_BTN1, HIGH, HIGH, 0};
  for (;;) {
    if (pressed_edge(d)) {
      g_led2On = !g_led2On;
      digitalWrite(PIN_LED2, g_led2On);
      logf("BTN1 -> LED2 %s", g_led2On ? "ON" : "OFF");
    }
    vTaskDelay(pdMS_TO_TICKS(T_BTN_POLL_MS));   // [K3] nếu thiếu dòng này task sẽ chiếm CPU, task prio thấp bị đói
  }
}

// ---------------- Task 4: BTN2 cấp binary semaphore ----------------
static void task4_btn2(void *arg) {
  Debounce d = {PIN_BTN2, HIGH, HIGH, 0};
  for (;;) {
    if (pressed_edge(d)) {
      BaseType_t ok = xSemaphoreGive(g_btn2Sem);   // [K4] đã có token thì trả pdFALSE
      portENTER_CRITICAL(&g_mux);
      g_btn2Count = g_btn2Count + 1;
      portEXIT_CRITICAL(&g_mux);
      logf("BTN2 give sem: %s", ok == pdTRUE ? "OK" : "da co san");
    }
    vTaskDelay(pdMS_TO_TICKS(T_BTN_POLL_MS));
  }
}

// ---------------- Task 5: chờ semaphore -> LED3 sáng 3 s ----------------
static void task5_led3(void *arg) {
  for (;;) {
    xSemaphoreTake(g_btn2Sem, portMAX_DELAY);   // Blocked, không tốn CPU cho tới khi có token
    TickType_t off = xTaskGetTickCount() + pdMS_TO_TICKS(T_LED3_ON_MS);
    portENTER_CRITICAL(&g_mux);                  // [K9] 2 biến phải cập nhật cùng lúc cho Task 6
    g_led3OffTick = off;
    g_led3On = true;
    portEXIT_CRITICAL(&g_mux);
    digitalWrite(PIN_LED3, HIGH);
    logf("LED3 ON 3s");

    vTaskDelay(pdMS_TO_TICKS(T_LED3_ON_MS));

    digitalWrite(PIN_LED3, LOW);
    portENTER_CRITICAL(&g_mux);
    g_led3On = false;
    portEXIT_CRITICAL(&g_mux);
    logf("LED3 OFF, cho tiep");
  }
}

// OLED: cách làm như firmware Smart Aquarium / Smart Energy v22 (Wire.begin + setClock,
// quét bus, tự chọn 0x3C/0x3D, begin()). Thêm: tự dò cặp chân SDA/SCL để chịu được cắm nhầm dây.
// Chưa thấy màn thì Task 6 thử lại mỗi 5 s (cắm lại dây không cần reset).
static uint8_t g_oledAddr = OLED_ADDR;
static int8_t  g_sda = PIN_SDA, g_scl = PIN_SCL;

// Cặp chân thử theo thứ tự: chân chuẩn trong config.h, đảo SDA/SCL, rồi các chân trống của kit.
// Không dùng chân đã có LED/nút/cảm biến.
static const int8_t I2C_TRY[][2] = {
  {PIN_SDA, PIN_SCL}, {PIN_SCL, PIN_SDA},
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  {8, 9}, {17, 18}, {41, 42},
#else
  {21, 22}, {22, 21},
#endif
};

// Dò OLED trên một cặp chân. Trả về: 1 = thấy OLED, 0 = không thấy, -1 = SDA/SCL bị kéo xuống GND.
static int probe_pair(int8_t sda, int8_t scl, bool verbose) {
  Wire.end();
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, INPUT_PULLUP);
  delay(2);
  int ls = digitalRead(sda), lc = digitalRead(scl);
  if (!ls || !lc) {                                     // có pull-up mà vẫn 0 = dây đang nối xuống GND
    if (verbose) Serial.printf("[I2C] SDA=%d SCL=%d: SDA=%d SCL=%d (0 = bi keo xuong GND) -> bo qua\n", sda, scl, ls, lc);
    return -1;
  }
  Wire.begin(sda, scl);
  Wire.setClock(100000);                                // 100 kHz: ổn định hơn với dây testboard
  int nAck = 0;
  bool found = false;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      nAck++;
      if (a == 0x3C || a == 0x3D) { g_oledAddr = a; found = true; }
    }
  }
  if (nAck > 8) found = false;                          // gần như mọi địa chỉ đều ACK = bus lỗi
  if (verbose) Serial.printf("[I2C] SDA=%d SCL=%d: %d thiet bi%s\n", sda, scl, nAck, found ? "  <- co OLED" : "");
  return found ? 1 : 0;
}

static bool oled_init(bool verbose) {
  bool ok = false;
  for (auto &pr : I2C_TRY) {
    if (probe_pair(pr[0], pr[1], verbose) == 1) { g_sda = pr[0]; g_scl = pr[1]; ok = true; break; }
  }
  if (!ok) {
    if (verbose) Serial.println("[OLED] Khong thay man hinh tren cac cap chan da thu - kiem tra VCC(3V3)/GND/SDA/SCL");
    Wire.end();
    Wire.begin(PIN_SDA, PIN_SCL);
    return false;
  }
  if (!oled.begin(SSD1306_SWITCHCAPVCC, g_oledAddr, true, false)) {   // false: không gọi lại Wire.begin() mặc định, giữ cặp chân vừa dò
    if (verbose) Serial.printf("[OLED] Khong khoi tao duoc tai 0x%02X\n", g_oledAddr);
    return false;
  }
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextWrap(false);
  oled.clearDisplay();
  oled.display();
  if (verbose || g_sda != PIN_SDA) {
    Serial.printf("[OLED] San sang tai 0x%02X (SDA=GPIO%d, SCL=GPIO%d)\n", g_oledAddr, g_sda, g_scl);
    if (g_sda != PIN_SDA) Serial.printf("[OLED] Luu y: day dang cam khac config.h (SDA=%d, SCL=%d)\n", PIN_SDA, PIN_SCL);
  }
  return true;
}

// ---------------- Task 6: hiển thị OLED 500 ms/lần ----------------
static void task6_oled(void *arg) {
  // [K7] đợi Task 2 có dữ liệu đầu tiên rồi mới vẽ, tránh hiển thị số rác
  xEventGroupWaitBits(g_sysEvents, EVT_SENSOR_READY, pdFALSE, pdTRUE, portMAX_DELAY);
  bool haveOled = xEventGroupGetBits(g_sysEvents) & EVT_OLED_READY;
  uint8_t retry = 0, oledFail = 0, oledReport = 0;
  uint32_t oledErr = 0;
  TickType_t last = xTaskGetTickCount();

  for (;;) {
    SensorData s;
    if (xSemaphoreTake(g_sensorMutex, pdMS_TO_TICKS(50)) == pdTRUE) {   // [K5] chép nhanh rồi trả mutex
      s = g_sensor;
      xSemaphoreGive(g_sensorMutex);
    } else {
      s = {0, 0, 0, 0};
    }

    bool led3;
    TickType_t offTick;
    portENTER_CRITICAL(&g_mux);
    led3 = g_led3On;
    offTick = g_led3OffTick;
    portEXIT_CRITICAL(&g_mux);

    int32_t remainMs = 0;
    if (led3) {
      int32_t d = (int32_t)(offTick - xTaskGetTickCount());
      remainMs = d > 0 ? d * portTICK_PERIOD_MS : 0;
    }

    if (haveOled) {
      oled.clearDisplay();
      oled.setTextSize(1);
      oled.setTextColor(SSD1306_WHITE);
      oled.setCursor(0, 0);
      oled.println("FreeRTOS ESP32 - MrZ");
      oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);
      oled.setCursor(0, 16);
      oled.printf("LED2 : %s\n", g_led2On ? "ON" : "OFF");
      oled.setCursor(0, 28);
      oled.printf("Light: %u%% (%.0f lx)\n", s.percent, s.lux);
      oled.setCursor(0, 40);
      if (led3) oled.printf("LED3 : ON  %.1fs\n", remainMs / 1000.0f);
      else      oled.println("LED3 : OFF");
      oled.setCursor(0, 54);
      oled.printf("raw=%u n=%lu", s.raw, (unsigned long)s.seq);
      oled.display();
      Wire.beginTransmission(g_oledAddr);               // OLED còn trả lời không?
      if (Wire.endTransmission() != 0) {
        oledErr++;
        if (++oledFail >= 6) {                          // 3 s liên tiếp lỗi -> coi như mất màn, dò lại
          oledFail = 0;
          haveOled = false;
          xEventGroupClearBits(g_sysEvents, EVT_OLED_READY);
          logf("[OLED] Mat ket noi (SDA=%d SCL=%d), thu lai", g_sda, g_scl);
        }
      } else {
        oledFail = 0;
      }
      if (++oledReport >= 10) {                         // 5 s báo số lần lỗi một lần
        oledReport = 0;
        if (oledErr) logf("[OLED] %lu lan loi I2C trong 5 s", (unsigned long)oledErr);
        oledErr = 0;
      }
    } else {
      if (++retry >= 10) {                              // chưa có OLED: 5 s thử lại một lần (cắm lại dây không cần reset)
        retry = 0;
        if (oled_init(false)) {
          haveOled = true;
          xEventGroupSetBits(g_sysEvents, EVT_OLED_READY);
          logf("[OLED] Da tim thay man hinh 0x%02X SDA=%d SCL=%d", g_oledAddr, g_sda, g_scl);
        }
      }
      static uint8_t n = 0;
      if (++n >= 4) {                                   // không có OLED: in ra Serial mỗi 2 s
        n = 0;
        logf("[OLED] L2=%s light=%u%% L3=%s %.1fs", g_led2On ? "ON" : "OFF", s.percent,
             led3 ? "ON" : "OFF", remainMs / 1000.0f);
      }
    }
    vTaskDelayUntil(&last, pdMS_TO_TICKS(T_OLED_MS));
  }
}

// ---------------- Task 7: logger (gatekeeper của Serial) ----------------
static void task7_logger(void *arg) {
  LogMsg m;
  for (;;) {
    if (xQueueReceive(g_logQueue, &m, portMAX_DELAY) == pdPASS) {
      Serial.printf("[%7lu ms][c%d] %s\n",
                    (unsigned long)(m.tick * portTICK_PERIOD_MS), xPortGetCoreID(), m.text);
    }
  }
}

// ---------------- Task 8: monitor, đánh thức bởi ISR nút BOOT ----------------
static void IRAM_ATTR isr_boot() {
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(h_monitor, &woken);   // [K8] ISR chỉ báo hiệu, xử lý nặng để task làm
  if (woken) portYIELD_FROM_ISR();
}

static void print_task(const char *name, TaskHandle_t h) {
  logf("%-8s prio=%u stackFree=%u B", name, (unsigned)uxTaskPriorityGet(h),
       (unsigned)uxTaskGetStackHighWaterMark(h));
}

static void task8_monitor(void *arg) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(50));               // chống dội đơn giản cho nút BOOT
    ulTaskNotifyTake(pdTRUE, 0);
    logf("=== MONITOR: %u tasks, heap %u B ===",
         (unsigned)uxTaskGetNumberOfTasks(), (unsigned)ESP.getFreeHeap());
    print_task("led1", h_led1);
    print_task("sensor", h_sensor);
    print_task("btn1", h_btn1);
    print_task("btn2", h_btn2);
    print_task("led3", h_led3);
    print_task("oled", h_oled);
    print_task("logger", h_logger);
    print_task("monitor", h_monitor);
  }
}

// ---------------- Software timer [K10] ----------------
static void heartbeat_cb(TimerHandle_t t) {
  // chạy trong timer daemon task: không được block lâu
  uint32_t cnt, drop;
  portENTER_CRITICAL(&g_mux);
  cnt = g_btn2Count;
  drop = g_logDropped;
  portEXIT_CRITICAL(&g_mux);
  logf("heartbeat up=%lus btn2=%lu drop=%lu", (unsigned long)(millis() / 1000),
       (unsigned long)cnt, (unsigned long)drop);
}

void setup() {
#if ARDUINO_USB_CDC_ON_BOOT
  // USB CDC mặc định đệm gửi 256 B: monitor in 9 dòng liền nhau sẽ làm rơi byte -> tăng đệm
  Serial.setTxBufferSize(2048);
#endif
  Serial.begin(115200);
  pinMode(PIN_LED1, OUTPUT);
  pinMode(PIN_LED2, OUTPUT);
  pinMode(PIN_LED3, OUTPUT);
  pinMode(PIN_BTN1, INPUT_PULLUP);
  pinMode(PIN_BTN2, INPUT_PULLUP);
  pinMode(PIN_BTN_BOOT, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_LDR, ADC_11db);   // đo được tới ~3.3 V

  g_btn2Sem     = xSemaphoreCreateBinary();      // tạo ra ở trạng thái rỗng
  g_sensorMutex = xSemaphoreCreateMutex();       // có priority inheritance
  g_logQueue    = xQueueCreate(16, sizeof(LogMsg));
  g_sysEvents   = xEventGroupCreate();
  configASSERT(g_btn2Sem && g_sensorMutex && g_logQueue && g_sysEvents);

  esp_log_level_set("i2c.master", ESP_LOG_NONE);     // lỗi I2C do Task 6 tự đếm và báo gọn
  if (oled_init(true)) xEventGroupSetBits(g_sysEvents, EVT_OLED_READY);
  else Serial.println("[OLED] Chua co man hinh: Task 6 in ra Serial va thu lai moi 5 s");

  // [K1] core 1: task I/O cần phản hồi nhanh; core 0: cảm biến, hiển thị, log
  xTaskCreatePinnedToCore(task7_logger,  "logger",  STACK_LOG,   NULL, PRIO_LOGGER,  &h_logger,  0);
  xTaskCreatePinnedToCore(task1_led1,    "led1",    STACK_SMALL, NULL, PRIO_LED1,    &h_led1,    1);
  xTaskCreatePinnedToCore(task2_sensor,  "sensor",  STACK_SMALL + 1024, NULL, PRIO_SENSOR, &h_sensor, 0);
  xTaskCreatePinnedToCore(task3_btn1,    "btn1",    STACK_SMALL + 1024, NULL, PRIO_BTN1,   &h_btn1,   1);
  xTaskCreatePinnedToCore(task4_btn2,    "btn2",    STACK_SMALL + 1024, NULL, PRIO_BTN2,   &h_btn2,   1);
  xTaskCreatePinnedToCore(task5_led3,    "led3",    STACK_SMALL + 1024, NULL, PRIO_LED3,   &h_led3,   1);
  xTaskCreatePinnedToCore(task6_oled,    "oled",    STACK_OLED,  NULL, PRIO_OLED,    &h_oled,    0);
  xTaskCreatePinnedToCore(task8_monitor, "monitor", STACK_OLED,  NULL, PRIO_MONITOR, &h_monitor, 0);

  attachInterrupt(digitalPinToInterrupt(PIN_BTN_BOOT), isr_boot, FALLING);

  g_heartbeat = xTimerCreate("hb", pdMS_TO_TICKS(T_HEARTBEAT_MS), pdTRUE, NULL, heartbeat_cb);
  xTimerStart(g_heartbeat, 0);

  logf("Khoi dong xong, %u tasks", (unsigned)uxTaskGetNumberOfTasks());
}

void loop() {
  // [K12] loopTask của Arduino không còn việc gì -> tự xoá để trả lại RAM
  vTaskDelete(NULL);
}
