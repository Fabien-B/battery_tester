#include "temp_sensor.h"
#include "ch.h"
#include "hal.h"
#include "math.h"

#include "stdutil.h"
#include "printf.h"

#define TMP101_ADDR 0x48

#define TMP101_TEMP_REG         0x00
#define TMP101_CONF_REG         0x01
#define TMP101_ALERT_TH_REG     0x02
#define TMP101_ALERT_TL_REG     0x03

float temp = 0;

float get_temperature() {
  return temp;
}

static THD_WORKING_AREA(waThreadTemp, 512);
static THD_FUNCTION(ThreadTemp, arg) {
  (void)arg;
  chRegSetThreadName("Temperature sensor");


  while(true) {
    uint8_t temp_reg = TMP101_TEMP_REG;
    uint8_t buf[2];
    i2cAcquireBus(&I2CD2);
    i2cMasterTransmitTimeout(&I2CD2, TMP101_ADDR, &temp_reg, 1, buf, 2, chTimeMS2I(100));
    i2cReleaseBus(&I2CD2);
    int16_t raw_temp = (buf[0] << 8 | buf[1]);
    temp = raw_temp / powf(2, 15) * 128;

    //chprintf ((BaseSequentialStream*)&SDU1, "%f\r\n", temp);

    chThdSleepMilliseconds(1000);
  }

}



void start_temp_monitor() {
    chThdCreateStatic(waThreadTemp, sizeof(waThreadTemp), NORMALPRIO, ThreadTemp, NULL);
}
