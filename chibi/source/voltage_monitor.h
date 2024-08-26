#pragma once
#include <array>

#define CONSOLE_DEV_USB TRUE

void start_adc_thread(void);

void get_cell_voltages(std::array<float, 6>& voltages);

float get_total_voltage();
int get_cells_number();
