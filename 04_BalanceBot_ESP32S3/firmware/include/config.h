#pragma once
// ===== Xe cân bằng ESP32-S3: cấu hình phần cứng =====
// CHÂN DƯỚI ĐÂY LÀ TẠM, sẽ sửa theo sơ đồ mạch thật.
// Tránh trên ESP32-S3: GPIO19/20 (USB), 26-37 (flash/PSRAM), 43/44 (UART0), 0/3/45/46 (strapping).

// IMU MPU6050 (I2C). Kit hiện tại: GPIO8 bị kéo GND nên dùng 10/11.
#define PIN_SDA        10
#define PIN_SCL        11
#define IMU_ADDR       0x68
// Cách gắn IMU: trục nào hướng về phía trước xe, trục nào hướng lên.
// theta_acc = atan2(IMU_FWD_SIGN * a_fwd, a_up); gyro pitch = IMU_GYRO_SIGN * g_axis
#define IMU_FWD_AXIS   0   // 0=ax 1=ay 2=az
#define IMU_UP_AXIS    2
#define IMU_GYRO_AXIS  1   // 0=gx 1=gy 2=gz
#define IMU_FWD_SIGN   1
#define IMU_GYRO_SIGN  1
#define COMP_ALPHA     0.98f   // lọc bù: tin gyro 98 %, gia tốc 2 %

// Driver TB6612FNG
#define PIN_AIN1   4
#define PIN_AIN2   5
#define PIN_PWMA   6
#define PIN_BIN1   7
#define PIN_BIN2   15
#define PIN_PWMB   16
#define PIN_STBY   17
#define PWM_FREQ   20000   // 20 kHz: ngoài ngưỡng nghe
#define PWM_BITS   10
#define MOTOR_L_SIGN  1    // đảo nếu bánh quay ngược
#define MOTOR_R_SIGN  1

// Encoder Hall 2 kênh (GA25-370: 11 xung/vòng động cơ, hộp số 1:34)
#define PIN_ENC_LA  1
#define PIN_ENC_LB  2
#define PIN_ENC_RA  41
#define PIN_ENC_RB  42
#define ENC_L_SIGN  1
#define ENC_R_SIGN  -1     // 2 động cơ gắn đối xứng nên thường ngược dấu

// Đo pin qua cầu chia 100k/33k (11.1 V -> ~2.75 V)
#define PIN_VBAT      9
#define VBAT_RATIO    ((100.0f + 33.0f) / 33.0f)
#define VBAT_MIN      10.0f   // dưới mức này tắt động cơ để bảo vệ pin 3S

// Điều khiển
#define CTRL_HZ       200
#define VEL_STEP      2.0f    // rad/s bánh mỗi lần nhấn w/s
#define VEL_MAX       8.0f
#define TURN_STEP     0.05f
#define REARM_DEG     5.0f    // chỉ cho chạy lại khi xe gần thẳng đứng

// FreeRTOS
#define PRIO_CTRL     5
#define PRIO_TELE     1
#define PRIO_CMD      2
#define SERIAL_BAUD   921600
