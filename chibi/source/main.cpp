#include "ch.h"
#include "hal.h"
#include "rt_test_root.h"
#include "oslib_test_root.h"
#include "voltage_monitor.h"
#include "printf.h"
#include "stdutil.h"
#include "ttyConsole.h"

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

/*
 * Application entry point.
 */
int main(void) {
  halInit();
  chSysInit();
  initHeap();

//  palSetLine(LINE_POWER_KEY);
  sdStart(&SD1, &sd1conf);


  // sduObjectInit(&PORTAB_SDU1);
  // sduStart(&PORTAB_SDU1, &serusbcfg);

  /*
   * Activates the USB driver and then the USB bus pull-up on D+.
   * Note, a delay is inserted in order to not have to disconnect the cable
   * after a reset.
   */
  // usbDisconnectBus(serusbcfg.usbp);
  // chThdSleepMilliseconds(1500);
  // usbStart(serusbcfg.usbp, &usbcfg);
  // usbConnectBus(serusbcfg.usbp);


  consoleInit();


  /*
   * Creates the blinker thread.
   */
  chThdCreateStatic(waThread1, sizeof(waThread1), NORMALPRIO, Thread1, NULL);

  start_adc_thread();
  
  
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

