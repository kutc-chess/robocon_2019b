#include <mbed.h>
#include <rotary_inc.hpp>
#include <scrp_slave.hpp>

ScrpSlave slave(PA_9, PA_10, PA_12, SERIAL_TX, SERIAL_RX, 0x0803e000);

constexpr int NUM_PORT = 5;
// 0: Motor, 1: Encoder, 2: Other
constexpr int PORT_FUNCTION[NUM_PORT] = {0, 2, 0, 2, 2};

constexpr int NUM_MOTOR_PORT = 4;
constexpr int MAX_PWM = 250;
constexpr double MAX_PWM_MBED = 0.95;
constexpr float PERIOD = 1 / 4000.0;
constexpr PinName MOTOR_PIN[NUM_MOTOR_PORT][3] = {{PB_0, PB_1, PB_3},
                                                  {PA_1, PA_3, PB_4},
                                                  {PA_8, PA_7, PB_5},
                                                  {PB_6, PA_11, PB_7}};

constexpr int NUM_ENCODER_PORT = 4;
constexpr int RANGE = 200;
constexpr PinName ENCODER_PIN[NUM_ENCODER_PORT][2] = {
    {PA_0, PA_4}, {PA_1, PA_3}, {PA_8, PA_7}, {PB_6, PA_11}};
RotaryInc *rotary[NUM_ENCODER_PORT];

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
    if (PORT_FUNCTION[i] == 4) {
      spinMotor(i, 0);
    }
  }
  return true;
}

int hanger_goal_speed = 0, hanger_current_speed = 0;
bool serveHanger(int cmd, int rx_data, int &tx_data) {
  hanger_goal_speed = rx_data;
  tx_data = hanger_current_speed;
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

  DigitalIn hanger_limit[2] = {DigitalIn(PA_8), DigitalIn(PB_6)}; // PullUp
  for (int i = 0; i < 2; ++i) {
    hanger_limit[i].mode(PullUp);
  }
  slave.addCMD(20, serveHanger);
  while (true) {
    hanger_current_speed = hanger_goal_speed;
    if ((hanger_current_speed > 0 && hanger_limit[0].read() == 0) ||
        (hanger_current_speed < 0 && hanger_limit[1].read() == 0)) {
      hanger_current_speed = 0;
    }
    spinMotor(1, -hanger_current_speed);
  }
}