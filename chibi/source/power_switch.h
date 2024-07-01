#pragma once

enum CurrentSensor {
    CS_BTS1,
    CS_BTS2,
    CS_ACS713
};

void start_power_switch();

float get_power_switch_throttle(int switch_no);
void set_power_switch_throttle(int switch_no, float throttle);
float get_current(enum CurrentSensor);
