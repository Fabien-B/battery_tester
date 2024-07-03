#include "reporting.h"
#include "voltage_monitor.h"
#include "power_switch.h"
#include "temp_sensor.h"
#include "printf.h"
#include "hal.h"
#include "usb_serial.h"
#include "stdutil.h"


int print_data(char* buffer, size_t len) {
    float u_total = get_total_voltage();
    std::array<float, 6> cell_voltages;
    get_cell_voltages(cell_voltages);

    float cell_min = cell_voltages[0];
    float cell_max = cell_voltages[0];
    for(int i=1; i<6; i++) {
      if(cell_voltages[i]>0.1) {
        cell_min = MIN(cell_voltages[i], cell_min);
        cell_max = MAX(cell_voltages[i], cell_max);
      }
    }

    float delta_cell = cell_max - cell_min;

    float temp = get_temperature();
    
    float c1 = get_current(CS_BTS1);
    float c2 = get_current(CS_BTS2);
    float ct = get_current(CS_ACS713);
    float c_total = c1 + c2;
    float power = u_total * c_total;

    int l = chsnprintf(buffer, len, "%02f %02f %02f,%02f,%02f,%02f,%02f,%02f %02f %02f %02f,%02f,%02f\r\n",
        u_total, delta_cell,
        cell_voltages[0], cell_voltages[1], cell_voltages[2],
        cell_voltages[3], cell_voltages[4], cell_voltages[5],
        c_total, c1, c2, ct
    );
    return l;
}

static THD_WORKING_AREA(waThreadReporting, 1024);
static THD_FUNCTION(ThreadReporting, arg) {
  (void)arg;
  chRegSetThreadName("Reporting");
  char buf[100];

  while(true) {
    int len = print_data(buf, sizeof(buf));
    chprintf ((BaseSequentialStream*)&SDU1, "%s", buf);
    chThdSleepMilliseconds(500);
  }

}



void start_reporting() {
    chThdCreateStatic(waThreadReporting, sizeof(waThreadReporting), NORMALPRIO, ThreadReporting, NULL);
}
