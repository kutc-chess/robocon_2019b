#include <mbed.h>
#include <pid.hpp>
#include <rotary_inc.hpp>
#include <scrp_slave.hpp>

using namespace arrc;

ScrpSlave slave(PA_9, PA_10, PA_12, SERIAL_TX, SERIAL_RX, 0x0803e000);

constexpr int NUM_PORT = 5;
// 0: Motor, 1: Encoder, 2: Other
constexpr int PORT_FUNCTION[NUM_PORT] = {0, 2, 0, 2, 2};

constexpr int NUM_MOTOR_PORT = 4;
constexpr int MAX_PWM = 250;
constexpr double MAX_PWM_MBED = 0.95;
constexpr float PERIOD = 1 / 1000.0;
constexpr PinName MOTOR_PIN[NUM_MOTOR_PORT][3] = {{PB_0, PB_1, PB_3},
                                                  {PA_1, PA_3, PB_4},
                                                  {PA_8, PA_7, PB_5},
                                                  {PB_6, PA_11, PB_7}};

constexpr int NUM_ENCODER_PORT = 4;
constexpr int RANGE = 512;
constexpr PinName ENCODER_PIN[NUM_ENCODER_PORT][2] = {
    {PA_0, PA_4}, {PA_1, PA_3}, {PA_8, PA_7}, {PB_6, PA_11}};
constexpr int diameter = 101.6; //直径

int goal_speed_3;

float map(float value, float from_low, float from_high, float to_low,
          float to_high) {
  if (value > from_high) {
    value = from_high;
  } else if (value < from_low) {
    value = from_low;
  }
  return (value - from_low) * (to_high - to_low) / (from_high - from_low) +
         to_low;
}

bool spinMotor(int id, int value) {
  DigitalOut motor_led = DigitalOut(MOTOR_PIN[id][2]);
  if (value == 0) {
    DigitalOut motor_off[2] = {DigitalOut(MOTOR_PIN[id][0]),
                               DigitalOut(MOTOR_PIN[id][1])};
    motor_off[0].write(0);
    motor_off[1].write(0);
    motor_led.write(0);
  } else if (0 < value) {
    PwmOut motor_on(MOTOR_PIN[id][0]);
    motor_on.period(PERIOD);
    DigitalOut motor_off(MOTOR_PIN[id][1]);

    motor_on.write(map(value, -MAX_PWM, MAX_PWM, -MAX_PWM_MBED, MAX_PWM_MBED));
    motor_off.write(0);
    motor_led.write(1);
  } else {
    PwmOut motor_on(MOTOR_PIN[id][1]);
    motor_on.period(PERIOD);
    DigitalOut motor_off(MOTOR_PIN[id][0]);

    motor_off.write(0);
    motor_on.write(-map(value, -MAX_PWM, MAX_PWM, -MAX_PWM_MBED, MAX_PWM_MBED));
    motor_led.write(1);
  }
  return true;
}

bool spinMotor(int cmd, int rx_data, int &tx_data) {
  return spinMotor(cmd - 2, rx_data);
}

bool safe(int cmd, int rx_data, int &tx_data) {
  for (int i = 0; i < 4; ++i) {
    spinMotor(i, 0);
  }
  return true;
}

int goal_tray_point = 0;
bool loadTray(int cmd, int rx_data, int &tx_data) {
  goal_tray_point = rx_data;
  return true;
}

bool d3_speed(int cmd, int rx_data, int &tx_data) {
  goal_speed_3 = rx_data / 100;
  return true;
}

int main() {
  if (PORT_FUNCTION[0] == 0) {
    slave.addCMD(2, spinMotor);
  }
  if (PORT_FUNCTION[1] == 0) {
    rotary[0] = new RotaryInc(ENCODER_PIN[0][0], ENCODER_PIN[0][1], RANGE, 1);
  }
  for (int i = 2; i < NUM_PORT; ++i) {
    switch (PORT_FUNCTION[i]) {
    case 0:
      slave.addCMD(i + 1, spinMotor);
      break;
    case 1:
      rotary[i - 1] =
          new RotaryInc(ENCODER_PIN[i - 1][0], ENCODER_PIN[i - 1][1], RANGE, 1);
      break;
    }
  }
  slave.addCMD(255, safe);
  slave.addCMD(20, loadTray);

  constexpr int TARY_MOTOR_ID = 0, MAX_TARY_MOTOR_SPEED = 100; // 下向き
  DigitalIn slit(PA_0);
  int current_slit = 0, prev_slit = 0;
  constexpr int LIGHT = 1, DARK = 0;
  DigitalIn limit_lower(PB_6);
  int phase = 0, current_tray_point = 0;
  while (true) {
    prev_slit = current_slit;
    current_slit = slit.read();

    switch (phase) {
    case 0:
      if (current_slit == LIGHT && prev_slit == DARK) {
        ++current_tray_point;
      }
      if (limit_lower.read() == 1) {
        current_tray_point = 7;
        goal_tray_point = 0;
        phase = 1;
      }
      break;
    case 1:
      if (current_slit == DARK && prev_slit == LIGHT) {
        --current_tray_point;
      }
      if (current_tray_point == 0) {
        phase = 0;
      }
      break;
    }
    if (current_tray_point < goal_tray_point) {
      spinMotor(TARY_MOTOR_ID, MAX_TARY_MOTOR_SPEED);
    } else if (current_tray_point > goal_tray_point) {
      spinMotor(TARY_MOTOR_ID, -MAX_TARY_MOTOR_SPEED);
    } else {
      spinMotor(TARY_MOTOR_ID, 0);
    }
  }
}