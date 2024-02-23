#include "power_switch.h"
#include "hal.h"
#include "ch.h"

#define BTS1_CHANNEL 1
#define BTS2_CHANNEL 3

constexpr uint32_t PWM_FREQ = 10000;
constexpr pwmcnt_t PWM_PERIOD = 200;

float throttle0 = 0;
float throttle1 = 0;


#define ADC_GRP2_NUM_CHANNELS   3
#define ADC_GRP2_BUF_DEPTH      1
#define BTS1_SENSE_CHANNEL  ADC_CHANNEL_IN3
#define BTS2_SENSE_CHANNEL  ADC_CHANNEL_IN11
#define VACS_CHANNEL        ADC_CHANNEL_IN13


adcsample_t samples2[CACHE_SIZE_ALIGN(adcsample_t, ADC_GRP2_NUM_CHANNELS * ADC_GRP2_BUF_DEPTH)];

static PWMConfig pwm3cfg = {
  .frequency = PWM_FREQ,
  .period = PWM_PERIOD,
  .callback = NULL,
  .channels = {
    {                                                               //ch1
      .mode = PWM_OUTPUT_ACTIVE_HIGH,
      .callback = NULL,
    },
    {                                                               //ch2
      .mode = PWM_OUTPUT_ACTIVE_HIGH,
      .callback = NULL
    },
    {                                                               //ch3
      .mode = PWM_OUTPUT_DISABLED,
      .callback = NULL
    },
    {                                                               //ch4
      .mode = PWM_OUTPUT_ACTIVE_HIGH,
      .callback = NULL
    },
  },
  //.cr2 = TIM_CR2_MMS_1,                 // Update event
  .cr2 = TIM_CR2_MMS_2,                   // tim_oc2refc
  //.cr2 = TIM_CR2_MMS_2 |TIM_CR2_MMS_1 | TIM_CR2_MMS_0, // tim_oc4refc
  .bdtr = 0,
  .dier = 0
};

float current0, current1, current2;

void adcendcb(ADCDriver*);

/*
 * ADC conversion group 2
 * Mode:        One shot, 2 channels, SW triggered.
 * Channels:    IN1, IN2.
 */
const ADCConversionGroup adcgrpcfg2 = {
  .circular     = true,
  .num_channels = ADC_GRP2_NUM_CHANNELS,
  .end_cb       = adcendcb,
  .error_cb     = NULL,
  .cfgr         = ADC_CFGR_EXTEN_1 | ADC_CFGR_EXTSEL_2,
  .cfgr2        = 0U,
  .tr1          = ADC_TR_DISABLED,
  .tr2          = ADC_TR_DISABLED,
  .tr3          = ADC_TR_DISABLED,
  .awd2cr       = 0U,
  .awd3cr       = 0U,
  .smpr         = {
    ADC_SMPR1_SMP_AN1(ADC_SMPR_SMP_247P5) |
    ADC_SMPR1_SMP_AN2(ADC_SMPR_SMP_247P5) |
    ADC_SMPR1_SMP_AN3(ADC_SMPR_SMP_247P5),
    0U
  },
  .sqr          = {
    ADC_SQR1_SQ1_N(BTS1_SENSE_CHANNEL) |
    ADC_SQR1_SQ2_N(BTS2_SENSE_CHANNEL) |
    ADC_SQR1_SQ3_N(VACS_CHANNEL),
    0U,
    0U,
    0U
  }
};

const ADCConfig adccfg2 = {
  .difsel       = 0U
};

void adcendcb(ADCDriver*) {
  palToggleLine(LINE_LED_RUN);
  current0 = 0;
  current1 = 0;
  current2 = 0;
  for(int i=0; i<ADC_GRP2_BUF_DEPTH; i++) {
    current0 += samples2[0 + i*ADC_GRP2_NUM_CHANNELS] / static_cast<float>(ADC_GRP2_BUF_DEPTH);
    current1 += samples2[1 + i*ADC_GRP2_NUM_CHANNELS] / static_cast<float>(ADC_GRP2_BUF_DEPTH);
    current2 += samples2[2 + i*ADC_GRP2_NUM_CHANNELS] / static_cast<float>(ADC_GRP2_BUF_DEPTH);
  }
  current0 *= throttle0 /100.0;
  current1 *= throttle1 /100.0;
  current2 *= (throttle0 + throttle1) / 100.0;
}


float get_power_switch_throttle(int switch_no) {
  switch (switch_no)
  {
  case 0:
    return throttle0;
    break;
  case 1:
    return throttle1;
    break;
  
  default:
    return -1;
    break;
  }
}

void set_power_switch_throttle(int switch_no, float throttle) {
  switch (switch_no)
  {
  case 0:
    throttle0 = throttle;
    break;
  case 1:
    throttle1 = throttle;
    break;
  default:
    break;
  }
}

static void init_power_switch() {
  pwmStart(&PWMD3, &pwm3cfg);
  
  // enable current sense
  palSetLine(LINE_BTS1_DEN);
  palSetLine(LINE_BTS2_DEN);
}

static THD_WORKING_AREA(waThreadPowerSwitch, 512);
static THD_FUNCTION(ThreadPowerSwitch, arg) {
  (void)arg;
  chRegSetThreadName("Power Switch");
  adcStart(&ADCD2, &adccfg2);
  adcStartConversion(&ADCD2, &adcgrpcfg2, samples2, ADC_GRP2_BUF_DEPTH);
  init_power_switch();

  while(true) {
    pwmEnableChannel(&PWMD3, BTS1_CHANNEL, throttle0*PWM_PERIOD/100.0);
    if(throttle0 != 0) {
      pwmEnableChannel(&PWMD3, 0, throttle0*0.5*PWM_PERIOD/100.0);
    }
    chThdSleepMilliseconds(10);
  }
}




float get_current(int index) {
  switch (index)
  {
  case 0:
    return current0;
    break;
  case 1:
    return current1;
    break;
  case 2:
    return current2;
    break;
  default:
    return -1;
    break;
  }
}


void start_power_switch() {
    chThdCreateStatic(waThreadPowerSwitch, sizeof(waThreadPowerSwitch), NORMALPRIO, ThreadPowerSwitch, NULL);
}
