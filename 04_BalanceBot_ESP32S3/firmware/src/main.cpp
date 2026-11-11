// Xe cân bằng ESP32-S3 + FreeRTOS + MLP (Lê Ngọc Hoàn - 24119038)
// Task ctrl (core 1, 200 Hz): IMU -> lọc bù -> encoder -> MLP hoặc LQR -> PWM
// Task tele (core 0): in trạng thái / log dữ liệu để tinh chỉnh model
// Task cmd  (core 0): nhận lệnh qua Serial
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "driver/pulse_cnt.h"
#include "esp_timer.h"
#include "config.h"
#include "policy.h"
#include "balance_policy.h"
#include "lqr_gains.h"
#include "balance_policy_test.h"

enum Mode : uint8_t { MODE_OFF, MODE_LQR, MODE_NN };
static const char *MODE_NAME[] = {"OFF", "LQR", "NN"};

struct LogRow { float x[BP_IN]; float u; };
struct Status {
  float th_deg, phd, u, vbat;
  uint32_t loop_us_max, nn_us;
  Mode mode;
};

static volatile Mode g_mode = MODE_OFF;
static volatile bool g_log = false;
static volatile float g_vel_ref = 0, g_turn = 0;
static volatile bool g_recal = false;
static Status g_status;
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t g_logQ;
static SemaphoreHandle_t g_serial;
static pcnt_unit_handle_t g_encL, g_encR;
static float g_gyro_bias = 0;

#define SPRINTF(...) do { xSemaphoreTake(g_serial, portMAX_DELAY); Serial.printf(__VA_ARGS__); xSemaphoreGive(g_serial); } while (0)

// ---------------- IMU MPU6050 ----------------
static void imu_write(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(IMU_ADDR); Wire.write(reg); Wire.write(v); Wire.endTransmission();
}
static bool imu_read(float acc[3], float gyr[3]) {
  Wire.beginTransmission(IMU_ADDR); Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(IMU_ADDR, 14) != 14) return false;
  int16_t r[7];
  for (int i = 0; i < 7; i++) r[i] = (int16_t)((Wire.read() << 8) | Wire.read());
  for (int i = 0; i < 3; i++) { acc[i] = r[i] / 8192.0f; gyr[i] = r[4 + i] / 65.5f * DEG_TO_RAD; }
  return true;
}
static bool imu_init() {
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  Wire.beginTransmission(IMU_ADDR); Wire.write(0x75); Wire.endTransmission(false);
  Wire.requestFrom(IMU_ADDR, 1);
  int who = Wire.available() ? Wire.read() : -1;
  Serial.printf("[IMU] WHO_AM_I = 0x%02X\n", who);
  if (who < 0 || who == 0xFF) return false;
  imu_write(0x6B, 0x00);   // thức dậy
  imu_write(0x1A, 0x03);   // lọc thông thấp ~44 Hz
  imu_write(0x1B, 0x08);   // gyro ±500 °/s
  imu_write(0x1C, 0x08);   // accel ±4 g
  return true;
}
static void imu_calibrate() {   // xe phải đứng yên
  float a[3], g[3], s = 0; int n = 0;
  for (int i = 0; i < 400; i++) { if (imu_read(a, g)) { s += g[IMU_GYRO_AXIS]; n++; } delay(2); }
  g_gyro_bias = n ? s / n : 0;
}

// ---------------- Encoder (PCNT phần cứng) ----------------
static pcnt_unit_handle_t enc_init(int pa, int pb) {
  pinMode(pa, INPUT_PULLUP); pinMode(pb, INPUT_PULLUP);
  pcnt_unit_config_t uc = {};
  uc.low_limit = -30000; uc.high_limit = 30000; uc.flags.accum_count = 1;
  pcnt_unit_handle_t u; ESP_ERROR_CHECK(pcnt_new_unit(&uc, &u));
  pcnt_glitch_filter_config_t fc = {}; fc.max_glitch_ns = 1000;
  ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(u, &fc));
  pcnt_chan_config_t ca = {}; ca.edge_gpio_num = pa; ca.level_gpio_num = pb;
  pcnt_chan_config_t cb = {}; cb.edge_gpio_num = pb; cb.level_gpio_num = pa;
  pcnt_channel_handle_t a, b;
  ESP_ERROR_CHECK(pcnt_new_channel(u, &ca, &a)); ESP_ERROR_CHECK(pcnt_new_channel(u, &cb, &b));
  pcnt_channel_set_edge_action(a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
  pcnt_channel_set_level_action(a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  pcnt_channel_set_edge_action(b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
  pcnt_channel_set_level_action(b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  pcnt_unit_add_watch_point(u, uc.low_limit); pcnt_unit_add_watch_point(u, uc.high_limit);   // cộng dồn khi tràn
  ESP_ERROR_CHECK(pcnt_unit_enable(u)); pcnt_unit_clear_count(u); ESP_ERROR_CHECK(pcnt_unit_start(u));
  return u;
}
static int enc_get(pcnt_unit_handle_t u) { int c = 0; pcnt_unit_get_count(u, &c); return c; }

// ---------------- Động cơ TB6612 ----------------
static void motor_one(int in1, int in2, int pwm, float d) {
  const int maxd = (1 << PWM_BITS) - 1;
  digitalWrite(in1, d > 0); digitalWrite(in2, d < 0);
  ledcWrite(pwm, (uint32_t)(fabsf(d) * maxd));
}
static float duty_from_u(float u) {   // bù vùng chết, giống hệt notebook
  float a = fabsf(u);
  if (a < 1e-3f) return 0;
  float d = BP_DZ_COMP + (1 - BP_DZ_COMP) * a;
  return copysignf(d > 1 ? 1 : d, u);
}
static void motors(float uL, float uR) {
  motor_one(PIN_AIN1, PIN_AIN2, PIN_PWMA, MOTOR_L_SIGN * duty_from_u(uL));
  motor_one(PIN_BIN1, PIN_BIN2, PIN_PWMB, MOTOR_R_SIGN * duty_from_u(uR));
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---------------- Task điều khiển 200 Hz ----------------
static void task_ctrl(void *) {
  float obs[BP_H][4] = {}, uh[BP_NU] = {};
  int32_t cnt_hist[5] = {};
  float th = 0, phi_ref = 0, phid_ref = 0, u_prev = 0;
  uint32_t k = 0, loop_max = 0, nn_us = 0;
  Mode last_mode = MODE_OFF;
  const float dt = 1.0f / CTRL_HZ, rad_per_cnt = 2 * PI / BP_CPR;
  float a[3], g[3];
  if (imu_read(a, g)) th = atan2f(IMU_FWD_SIGN * a[IMU_FWD_AXIS], a[IMU_UP_AXIS]);

  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(1000 / CTRL_HZ));
    int64_t t0 = esp_timer_get_time();

    // 1. Góc nghiêng: lọc bù gyro + gia tốc
    float thd = 0;
    if (imu_read(a, g)) {
      thd = IMU_GYRO_SIGN * (g[IMU_GYRO_AXIS] - g_gyro_bias);
      float th_acc = atan2f(IMU_FWD_SIGN * a[IMU_FWD_AXIS], a[IMU_UP_AXIS]);
      th = COMP_ALPHA * (th + thd * dt) + (1 - COMP_ALPHA) * th_acc;
    }
    // 2. Encoder -> góc và tốc độ bánh tuyệt đối (giống notebook)
    int32_t cnt = (ENC_L_SIGN * enc_get(g_encL) + ENC_R_SIGN * enc_get(g_encR)) / 2;
    memmove(&cnt_hist[1], &cnt_hist[0], 4 * sizeof(int32_t)); cnt_hist[0] = cnt;
    int win = k < 4 ? (k ? k : 1) : 4;
    float ph = cnt * rad_per_cnt + th;
    float phd = (cnt_hist[0] - cnt_hist[win]) * rad_per_cnt / (win * dt) + thd;

    Mode mode = g_mode;
    if (mode != last_mode) {            // vừa bật: đặt mốc vị trí tại chỗ hiện tại
      phi_ref = ph; phid_ref = 0; u_prev = 0;
      memset(uh, 0, sizeof(uh));
    }
    last_mode = mode;
    if (mode != MODE_OFF) phid_ref = g_vel_ref;
    phi_ref += phid_ref * dt;

    // 3. Vector quan sát chuẩn hoá + lịch sử
    float e_ph = clampf(ph - phi_ref, -BP_SCALE[2], BP_SCALE[2]);
    float o[4] = {th, thd, e_ph, phd - phid_ref};
    memmove(&obs[1], &obs[0], (BP_H - 1) * sizeof(obs[0]));
    for (int i = 0; i < 4; i++) obs[0][i] = clampf(o[i] / BP_SCALE[i], -3, 3);
    float x[BP_IN];
    memcpy(x, obs, sizeof(obs));
    memcpy(&x[4 * BP_H], uh, sizeof(uh));

    // 4. Luật điều khiển
    float u_lqr = clampf(-(LQR_K[0] * th + LQR_K[1] * thd + LQR_K[2] * e_ph + LQR_K[3] * (phd - phid_ref) + LQR_K[4] * u_prev), -1, 1);
    float u = 0;
    if (mode == MODE_LQR) u = u_lqr;
    else if (mode == MODE_NN) {
      int64_t s = esp_timer_get_time();
      u = policy_run(x);
      nn_us = (uint32_t)(esp_timer_get_time() - s);
    }
    // 5. An toàn: ngã hoặc pin yếu thì tắt
    if (mode != MODE_OFF && fabsf(th) > BP_FALL_RAD) {
      g_mode = MODE_OFF; mode = MODE_OFF; u = 0;
    }
    float turn = mode == MODE_OFF ? 0 : g_turn;
    motors(mode == MODE_OFF ? 0 : clampf(u + turn, -1, 1), mode == MODE_OFF ? 0 : clampf(u - turn, -1, 1));

    u_prev = clampf(u, -1, 1);
    memmove(&uh[1], &uh[0], (BP_NU - 1) * sizeof(float)); uh[0] = u_prev;

    // 6. Log dữ liệu thật (khi chạy LQR) để tinh chỉnh model trên Colab
    if (g_log && mode == MODE_LQR && (k & 1) == 0) {
      LogRow r; memcpy(r.x, x, sizeof(x)); r.u = u_lqr;
      xQueueSend(g_logQ, &r, 0);
    }
    uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    if (us > loop_max) loop_max = us;
    portENTER_CRITICAL(&g_mux);
    g_status.th_deg = th * RAD_TO_DEG; g_status.phd = phd; g_status.u = u;
    g_status.loop_us_max = loop_max; g_status.nn_us = nn_us; g_status.mode = mode;
    portEXIT_CRITICAL(&g_mux);
    if ((k % CTRL_HZ) == 0) loop_max = 0;
    k++;
  }
}

// ---------------- Task in trạng thái / log ----------------
static void task_tele(void *) {
  LogRow r; uint32_t n = 0;
  for (;;) {
    if (xQueueReceive(g_logQ, &r, pdMS_TO_TICKS(200)) == pdTRUE) {
      xSemaphoreTake(g_serial, portMAX_DELAY);
      Serial.print("L");
      for (int i = 0; i < BP_IN; i++) { Serial.print(','); Serial.print(r.x[i], 4); }
      Serial.print(','); Serial.println(r.u, 4);
      xSemaphoreGive(g_serial);
      continue;
    }
    if (g_log) continue;
    float vb = analogReadMilliVolts(PIN_VBAT) / 1000.0f * VBAT_RATIO;
    Status s;
    portENTER_CRITICAL(&g_mux); s = g_status; portEXIT_CRITICAL(&g_mux);
    if (s.mode != MODE_OFF && vb > 3.0f && vb < VBAT_MIN) {   // vb < 3 V: chưa nối cầu chia, bỏ qua
      g_mode = MODE_OFF;
      SPRINTF("[SAFE] Pin yeu %.2f V -> tat dong co\n", vb);
    }
    if ((n++ % 5) == 0)
      SPRINTF("[%s] theta=%6.2f deg  phi_dot=%6.2f rad/s  u=%+.3f  vbat=%.2f V  loop_max=%lu us  nn=%lu us\n",
              MODE_NAME[s.mode], s.th_deg, s.phd, s.u, vb, (unsigned long)s.loop_us_max, (unsigned long)s.nn_us);
  }
}

// ---------------- Task nhận lệnh ----------------
static void help() {
  SPRINTF("Lenh: n=chay MLP  l=chay LQR  o=tat  w/s=tien/lui  a/d=re  x=dung yen  g=bat/tat log  c=hieu chinh gyro (khi tat)\n");
}
static void task_cmd(void *) {
  for (;;) {
    while (Serial.available()) {
      char c = Serial.read();
      Status s; portENTER_CRITICAL(&g_mux); s = g_status; portEXIT_CRITICAL(&g_mux);
      switch (c) {
        case 'n': case 'l':
          if (fabsf(s.th_deg) > REARM_DEG) { SPRINTF("Dung xe thang (|goc| < %.0f deg) roi bat lai\n", REARM_DEG); break; }
          g_vel_ref = 0; g_turn = 0; g_mode = c == 'n' ? MODE_NN : MODE_LQR;
          SPRINTF("-> %s\n", MODE_NAME[g_mode]); break;
        case 'o': g_mode = MODE_OFF; SPRINTF("-> OFF\n"); break;
        case 'w': g_vel_ref = fminf(g_vel_ref + VEL_STEP, VEL_MAX); break;
        case 's': g_vel_ref = fmaxf(g_vel_ref - VEL_STEP, -VEL_MAX); break;
        case 'a': g_turn = fminf(g_turn + TURN_STEP, 0.3f); break;
        case 'd': g_turn = fmaxf(g_turn - TURN_STEP, -0.3f); break;
        case 'x': g_vel_ref = 0; g_turn = 0; break;
        case 'g': g_log = !g_log; SPRINTF("log %s\n", g_log ? "BAT (chay LQR de ghi)" : "TAT"); break;
        case 'c':
          if (g_mode == MODE_OFF) { imu_calibrate(); SPRINTF("gyro bias = %.5f rad/s\n", g_gyro_bias); }
          break;
        case 'h': case '?': help(); break;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);
  Serial.println("\n== Xe can bang ESP32-S3 + FreeRTOS + MLP ==");
  pinMode(PIN_STBY, OUTPUT); digitalWrite(PIN_STBY, HIGH);
  for (int p : {PIN_AIN1, PIN_AIN2, PIN_BIN1, PIN_BIN2}) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  ledcAttach(PIN_PWMA, PWM_FREQ, PWM_BITS); ledcAttach(PIN_PWMB, PWM_FREQ, PWM_BITS);
  motors(0, 0);
  analogSetPinAttenuation(PIN_VBAT, ADC_11db);

  if (!imu_init()) Serial.println("[IMU] KHONG thay MPU6050 - kiem tra day SDA/SCL/VCC");
  Serial.println("[IMU] Hieu chinh gyro, giu xe dung yen 1 s...");
  imu_calibrate();
  Serial.printf("[IMU] gyro bias = %.5f rad/s\n", g_gyro_bias);
  g_encL = enc_init(PIN_ENC_LA, PIN_ENC_LB);
  g_encR = enc_init(PIN_ENC_RA, PIN_ENC_RB);

  // tự kiểm tra MLP với vector từ notebook
  float y = policy_run(BP_TEST_X);
  Serial.printf("[MLP] %d tham so, kiem tra: %.6f (Python %.6f)\n",
                BP_IN * BP_H1 + BP_H1 + BP_H1 * BP_H2 + BP_H2 + BP_H2 + 1, y, BP_TEST_Y[0]);

  g_serial = xSemaphoreCreateMutex();
  g_logQ = xQueueCreate(64, sizeof(LogRow));
  configASSERT(g_serial && g_logQ);
  xTaskCreatePinnedToCore(task_ctrl, "ctrl", 6144, nullptr, PRIO_CTRL, nullptr, 1);
  xTaskCreatePinnedToCore(task_tele, "tele", 4096, nullptr, PRIO_TELE, nullptr, 0);
  xTaskCreatePinnedToCore(task_cmd, "cmd", 3072, nullptr, PRIO_CMD, nullptr, 0);
  help();
}

void loop() { vTaskDelete(nullptr); }
