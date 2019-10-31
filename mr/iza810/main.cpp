//立ルンです, Ma/nLoop from 294
#include "raspi_utility/include/dualshock3.hpp"
#include "raspi_utility/include/gy521.hpp"
#include "raspi_utility/include/motor_serial.hpp"
#include "raspi_utility/include/pid.hpp"
#include "raspi_utility/include/pigpiod.hpp"
#include "raspi_utility/include/time.hpp"
#include <cmath>
#include <iostream>
#include <pigpio.h>
#include <string>
#include <vector>

using namespace std;
using namespace arrc_raspi;

double map(double value, double from_low, double from_high, double to_low,
           double to_high) {
  if (value > from_high) {
    value = from_high;
  } else if (value < from_low) {
    value = from_low;
  }
  return (value - from_low) * (to_high - to_low) / (from_high - from_low) +
         to_low;
}

bool checkError(double goal, double now, double error) {
  if (goal - error < now && now < goal - error) {
    return true;
  } else {
    return false;
  }
}

double calcTriangleTheta(double a, double b, double c) {
  return acos((a * a + b * b - c * c) / (2 * a * b));
}

// GPIO
const char *arrc_raspi::PIGPIOD_HOST = "localhost";
const char *arrc_raspi::PIGPIOD_PORT = "8888";

int main() {
  Pigpiod &pigpio = Pigpiod::gpio();

  // Check Pins
  constexpr int RUN_LED = 13;
  pigpio.set(RUN_LED, OUT, 1);
  constexpr int EMERGENCY_PIN = 14;
  pigpio.set(EMERGENCY_PIN, IN, PULL_UP);

  // Serial
  MotorSerial ms;

  // DualShock3
  DualShock3 controller;
  pigpio.write(RUN_LED, 0);

  // Tape LED

  // Calibration
  Timer calibration_timer;
  bool level = true;
  UPDATELOOP(controller, !(controller.button(START) && controller.button(UP))) {
    calibration_timer.update();
    if (calibration_timer.wait(0.5)) {
      pigpio.write(RUN_LED, level);
      level = !level;
      calibration_timer.reset();
    }
  }
  pigpio.write(RUN_LED, 0);
  constexpr double START_YAW = 0;
  Gy521 gyro(0x68, 2, 1000, 1.01);
  pigpio.write(RUN_LED, 1);

  // Wheel ver three omuni
  constexpr int NUM_WHEEL = 3, WHEEL_MDD_ID[NUM_WHEEL] = {1, 1, 4},
                WHEEL_CMD[NUM_WHEEL] = {2, 5, 3};
  constexpr double WHEEL_OFFSET_THETA[NUM_WHEEL] = {M_PI / 3, M_PI * 2 / 3, 0};
  constexpr int MAX_ROBOT_SPEED = 250, MAX_ROBOT_MOMENT = 100,
                MAX_WHEEL_SPEED = 250;
  PidPosition robot_pose(0.0, 0.0, 0.0, MAX_ROBOT_MOMENT);

  // Leg
  constexpr int LEG_MDD_ID = 2, LEG_CMD = 12;

  // Shoot
  constexpr int SHOOT_MDD_ID = 4, SHOOT_STROKE_CMD = 30, SHOOT_ROLL_CMD = 31;
  constexpr int SHOOT_CHARGE_STROKE = 350, SHOOT_ROLL_SPEED = 100;

  // Load
  constexpr int LOAD_MDD_ID = 4, LOAD_CMD = 13;
  constexpr int MAX_LOAD_TARY = 8, NUM_STEP_TRAY = 1;
  constexpr int NUM_LOAD_ARM = 6;
  constexpr int LOAD_ARM_POSITION[NUM_LOAD_ARM][2] = {{0, 0}, {0, 0}, {0, 0},
                                                      {0, 0}, {0, 0}, {0, 0}};

  // Hand
  constexpr int HAND_MDD_ID = 6, HAND_CMD = 12;
  constexpr int HAND_CLOSE_ANGLE = 12, HAND_OPEN_ANGLE = 170;
  constexpr double WAIT_HAND_TIME = 1.5;

  // Laundry
  constexpr int LAUNDRY_MDD_ID = 3, LAUNDRY_CMD = 20;
  constexpr int LAUNDRY_ARM_X = 360, LAUNDRY_ARM_Y = 860;
  int laundry_mode = 0;

  // Arm
  constexpr int ARM_MDD_ID = 6, SHOULDER_CMD = 29, ELBO_CMD = 20;
  constexpr int FRONT_ARM_LENGTH = 400 + 100, SECOND_ARM_LENGTH = 500;
  constexpr int ARM_START_X = 0, ARM_START_Y = 0;
  constexpr double OFFSET_SHOULDER_ANGLE = 0, OFFSET_ELBO_ANGLE = 10;
  constexpr double ARM_STICK_SPEED = 0.1;

  Timer timer;
  int finish_mode = 1;
  cout << "Main Start" << endl;
  // MainLoop
  UPDATELOOP(controller,
             !(controller.button(START) && controller.button(CROSS))) {
    // Reset
    int should_reset = false;
    if (controller.button(START) && controller.button(UP)) {
      should_reset = true;
    }

    // Sensor Update
    gyro.update();
    if (should_reset) {
      gyro.yaw = 0;
    }
    /* cout << gyro.yaw << endl; */
    timer.update();
    timer.reset();

    // Emergency
    static bool emergency_stop = false;
    if (controller.press(SELECT)) {
      emergency_stop = !emergency_stop;
      finish_mode = emergency_stop ? 0 : 1;
    }
    if (emergency_stop) {
      continue;
    }

    // Wheel Goal Input Manual
    double stick_x = controller.stick(LEFT_X) / 128.0;
    double stick_y = -controller.stick(LEFT_Y) / 128.0;
    double robot_theta = atan2(stick_y, stick_x) - gyro.yaw / 180 * M_PI;
    double robot_speed = MAX_ROBOT_SPEED * hypot(stick_x, stick_y) *
                         (0.3 * fabs(cos(2 * robot_theta) + 0.7));
    cout << robot_speed << ", ";
    if (!controller.button(L1)) {
      robot_speed *= 0.5;
    }
    static double goal_yaw = START_YAW;
    if (controller.press(LEFT)) {
      goal_yaw = 90;
    } else if (controller.press(RIGHT)) {
      goal_yaw = -90;
    }

    // Arm Goal Input Manual
    static double arm_goal_x = ARM_START_X;
    static double arm_goal_y = ARM_START_Y;
    double arm_diff_x = ARM_STICK_SPEED * controller.stick(RIGHT_X) / 128;
    double arm_diff_y = ARM_STICK_SPEED * -controller.stick(RIGHT_Y) / 128;
    arm_goal_x += arm_diff_x * sin(gyro.yaw / 180 * M_PI) -
                  arm_diff_y * cos(gyro.yaw / 180 * M_PI);
    arm_goal_y += arm_diff_x * cos(gyro.yaw / 180 * M_PI) +
                  arm_diff_y * sin(gyro.yaw / 180 * M_PI);

    // Laundry
    if (should_reset) {
      if (laundry_mode == 1) {
        laundry_mode = 2;
      }
    }
    if (controller.button(CROSS) && laundry_mode == 2) {
      laundry_mode = 0;
    } else if (controller.press(CROSS) && laundry_mode == 0) {
      laundry_mode = 2;
    }
    // Arm avoid laundry
    if (laundry_mode == 0) {
      arm_goal_x = LAUNDRY_ARM_X;
      arm_goal_y = LAUNDRY_ARM_Y;
    }
    ms.send(LAUNDRY_MDD_ID, LAUNDRY_CMD, laundry_mode);

    // Leg Input

    // Shoot & Load & Hand
    static int phase = 0;
    static int tray_position = 0;
    static int load_arm_id = 0;
    static bool changed_phase = false;
    static Timer hand_time;
    hand_time.update();
    switch (phase) {
    // Tary Up or Down
    case -1:
      if (changed_phase) {
        tray_position += NUM_STEP_TRAY;
        if (tray_position > MAX_LOAD_TARY) {
          tray_position = 0;
        }
        changed_phase = false;
      }
      if (controller.press(SQUARE)) {
        phase = 0;
        changed_phase = true;
      }
      break;
    case 1:
      if (changed_phase) {
        ms.send(HAND_MDD_ID, HAND_CMD, HAND_CLOSE_ANGLE);
        hand_time.reset();
        changed_phase = false;
      }
      if (hand_time.wait(WAIT_HAND_TIME)) {
        phase = 2;
        changed_phase = true;
      }
      break;
    case 4:
      if (changed_phase) {
        ms.send(LOAD_MDD_ID, LOAD_CMD, 1);
        changed_phase = false;
      }
      if (controller.press(SQUARE)) {
        phase = 5;
        changed_phase = true;
      }
      break;
    case 6:
      if (changed_phase) {
        ms.send(SHOOT_MDD_ID, SHOOT_ROLL_CMD, SHOOT_ROLL_SPEED);
        ms.send(SHOOT_MDD_ID, SHOOT_STROKE_CMD, SHOOT_CHARGE_STROKE);
        changed_phase = false;
      }
      if (controller.press(SQUARE)) {
        phase = 7;
        changed_phase = true;
      }
      break;
    case 8:
      if (changed_phase) {
        ms.send(LOAD_MDD_ID, LOAD_CMD, -1);
        laundry_mode = 1;
        changed_phase = false;
      }
      if (controller.press(SQUARE)) {
        phase = 9;
        changed_phase = true;
      }
      break;
    case 10:
      if (changed_phase) {
        ms.send(HAND_MDD_ID, HAND_CMD, HAND_OPEN_ANGLE);
        hand_time.reset();
        changed_phase = false;
      }
      if (hand_time.wait(WAIT_HAND_TIME)) {
        phase = 2;
        changed_phase = true;
      }
      break;
    case 11:
      if (changed_phase) {
        changed_phase = false;
      }
      if (controller.press(SQUARE)) {
        phase = -1;
        changed_phase = true;
      }
      break;
    // Arm Next Goal
    default:
      if (changed_phase) {
        load_arm_id = (load_arm_id + 1) % NUM_LOAD_ARM;
        arm_goal_x = LOAD_ARM_POSITION[load_arm_id][0];
        arm_goal_y = LOAD_ARM_POSITION[load_arm_id][1];
        changed_phase = false;
      }
      if (controller.press(SQUARE)) {
        ++phase;
        changed_phase = true;
      }
      break;
    }

    // Wheel Output
    double diff_yaw = goal_yaw- gyro.yaw;
    diff_yaw = diff_yaw - (int)diff_yaw / 180 * 360;
    double moment = robot_pose.control(diff_yaw);

    double wheel_goal_speed[NUM_WHEEL];
    double dummy_max = MAX_WHEEL_SPEED;
    for (int i = 0; i < NUM_WHEEL; ++i) {
      wheel_goal_speed[i] =
          robot_speed * cos(robot_theta + WHEEL_OFFSET_THETA[i]) + moment;
      if (wheel_goal_speed[i] > dummy_max) {
        dummy_max = wheel_goal_speed[i];
      }
    }
    for (int i = 0; i < NUM_WHEEL; ++i) {
      wheel_goal_speed[i] *= dummy_max / MAX_WHEEL_SPEED;
      ms.send(WHEEL_MDD_ID[i], WHEEL_CMD[i], wheel_goal_speed[i]);
      cout << wheel_goal_speed[i] << ", ";
    }
    cout << endl;

    // Arm Output
    double arm_radius = hypot(arm_goal_x, arm_goal_y);
    double angle_shoulder =
        calcTriangleTheta(SECOND_ARM_LENGTH, arm_radius, FRONT_ARM_LENGTH) +
        atan2(arm_goal_y, arm_goal_x);
    double angle_elbo =
        calcTriangleTheta(FRONT_ARM_LENGTH, SECOND_ARM_LENGTH, arm_radius);
    ms.send(ARM_MDD_ID, SHOULDER_CMD,
            angle_shoulder / M_PI * 180 + OFFSET_SHOULDER_ANGLE);
    ms.send(ARM_MDD_ID, ELBO_CMD, angle_elbo / M_PI * 180 + OFFSET_ELBO_ANGLE);
  }
  cout << "Main Finish" << endl;
  ms.send(255, 255, 0);
  pigpio.write(RUN_LED, 0);
  return finish_mode;
}
