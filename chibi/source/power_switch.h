#pragma once

void start_power_switch();
void start_current_sensing();

float get_power_switch_throttle(int switch_no);
void set_power_switch_throttle(int switch_no, float throttle);
float get_current(int index);
