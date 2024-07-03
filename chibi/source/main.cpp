#include "ch.h"
#include "hal.h"
#include "rt_test_root.h"
#include "oslib_test_root.h"
#include "voltage_monitor.h"
#include "printf.h"
#include "stdutil.h"
#include "ttyConsole.h"
#include "temp_sensor.h"
#include "ui.h"
#include "power_switch.h"
#include "reporting.h"

/*
 * Green LED blinker thread, times are in milliseconds.
 */
static THD_WORKING_AREA(waThread1, 512);
static THD_FUNCTION(Thread1, arg) {

  (void)arg;
  chRegSetThreadName("blinker");
  const char buf[] = "plip\r\n";
  while (true) {
    palClearLine(LINE_LED_HB);
    chThdSleepMilliseconds(200);
    palSetLine(LINE_LED_HB);
    chThdSleepMilliseconds(200);
    chprintf ((BaseSequentialStream*)&SD1, "%s", buf);
    //chnWrite(&SD1, (uint8_t*) buf, sizeof buf - 1);
  }
}


SerialConfig sd1conf = {
  .speed = 115200,
  .cr1 = 0,
  .cr2 = USART_CR2_STOP1_BITS | USART_CR2_LINEN,
  .cr3 = 0
};

I2CConfig i2cconf = {
  .timingr =
    STM32_TIMINGR_PRESC(0U)   |
    STM32_TIMINGR_SCLDEL(14U) |
    STM32_TIMINGR_SDADEL(0U)  |
    STM32_TIMINGR_SCLH(87U)   |
    STM32_TIMINGR_SCLL(253U),
  .cr1 = 0,
  .cr2 = 0
};

/*
 * Application entry point.
 */
int main(void) {
  halInit();
  chSysInit();
  initHeap();

//  palSetLine(LINE_POWER_KEY);
  sdStart(&SD1, &sd1conf);
  i2cStart(&I2CD2, &i2cconf);

  consoleInit();


  /*
   * Creates the blinker thread.
   */
  //chThdCreateStatic(waThread1, sizeof(waThread1), NORMALPRIO, Thread1, NULL);

  start_adc_thread();
  start_temp_monitor();
  start_ui();
  start_power_switch();
  start_reporting();
  
  
  consoleLaunch();  // launch shell. Never returns.

  /*
   * Normal main() thread activity, in this demo it does nothing except
   * sleeping in a loop and check the button state.
   */
  while (true) {
    chThdSleep(TIME_INFINITE);
    //chThdSleepMilliseconds(500);
  }
}

