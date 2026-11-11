#pragma once
#include <Arduino.h>

// ---- Chân kết nối ----
#if defined(CONFIG_IDF_TARGET_ESP32S3)
// ESP32-S3 DevKitC-1 (N8R2/N16R8): tránh 19/20 (USB), 26-37 (flash/PSRAM), 43/44 (UART0), 0/3/45/46 (strapping)
#define PIN_LED1       4
#define PIN_LED2       5
#define PIN_LED3       6
#define PIN_BTN1      15   // nút nhấn nối GND, dùng INPUT_PULLUP
#define PIN_BTN2      16
#define PIN_BTN_BOOT   0   // nút BOOT có sẵn trên kit -> ngắt (ISR) cho task Monitor
#define PIN_LDR        1   // AO của module quang trở, ADC1_CH0
#define PIN_SDA       10   // mạch thật: GPIO8 bị kéo xuống GND nên chuyển OLED sang 10/11
#define PIN_SCL       11   // main.cpp vẫn tự dò cặp 8/9, 17/18, 41/42 nếu cắm khác
#else
// ESP32 DevKit v1 / DevKitC (ESP32-WROOM)
#define PIN_LED1      25
#define PIN_LED2      26
#define PIN_LED3      27
#define PIN_BTN1      32
#define PIN_BTN2      33
#define PIN_BTN_BOOT   0
#define PIN_LDR       34   // ADC1_CH6 (chỉ input)
#define PIN_SDA       21
#define PIN_SCL       22
#endif
#define OLED_ADDR   0x3C

// ---- Chu kỳ các task (ms) ----
#define T_LED1_MS        200   // Task 1: chu kỳ nháy, mỗi nửa chu kỳ đảo 1 lần
#define T_SENSOR_MS      500   // Task 2
#define T_BTN_POLL_MS     10   // Task 3, Task 4: quét nút
#define T_DEBOUNCE_MS     30
#define T_LED3_ON_MS    3000   // Task 5
#define T_OLED_MS        500   // Task 6
#define T_HEARTBEAT_MS  5000   // software timer

// ---- Priority: số lớn = ưu tiên cao (FreeRTOS) ----
#define PRIO_LED1     1
#define PRIO_SENSOR   2
#define PRIO_BTN1     3
#define PRIO_BTN2     3
#define PRIO_LED3     4   // phản hồi semaphore nhanh nhất
#define PRIO_OLED     1
#define PRIO_LOGGER   1
#define PRIO_MONITOR  2

// ---- Stack (byte, trên ESP32 đơn vị của xTaskCreate là byte) ----
#define STACK_SMALL  2048
#define STACK_OLED   4096
#define STACK_LOG    3072

// ---- Event group bits ----
#define EVT_OLED_READY    (1 << 0)
#define EVT_SENSOR_READY  (1 << 1)
