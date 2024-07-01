#include "power_switch.h"
#include "hal.h"
#include "ch.h"
#include "math.h"

#define BTS1_CHANNEL 1
#define BTS2_CHANNEL 3

// factor due to the voltage divider 5V->3.3V (10k, 18k)
#define ACS713_VFACTOR 1.55

constexpr uint32_t PWM_FREQ = 10000;
constexpr pwmcnt_t PWM_PERIOD = 200;

float throttle0 = 0;
float throttle1 = 0;


#define ADC_GRP2_NUM_CHANNELS   3
#define ADC_GRP2_BUF_DEPTH      2
#define BTS1_SENSE_CHANNEL  ADC_CHANNEL_IN3
#define BTS2_SENSE_CHANNEL  ADC_CHANNEL_IN11
#define VACS_CHANNEL        ADC_CHANNEL_IN13


adcsample_t samples2[CACHE_SIZE_ALIGN(adcsample_t, ADC_GRP2_NUM_CHANNELS * ADC_GRP2_BUF_DEPTH)];

float v_ac_bias = 0;

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
  .cr2 = TIM_CR2_MMS_2,                   // tim_oc2refc
  .bdtr = 0,
  .dier = 0
};

float current_bts1, current_bts2, current_acs713;

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
  .cfgr         = ADC_CFGR_EXTEN_1 | ADC_CFGR_EXTSEL_2 | ADC_CFGR_CONT,
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

  float v_bts1 = 0;
  float v_bts2 = 0;
  float v_ac = 0;
  for(int i=0; i<ADC_GRP2_BUF_DEPTH; i++) {
    v_bts1 += samples2[0 + i*ADC_GRP2_NUM_CHANNELS] / static_cast<float>(ADC_GRP2_BUF_DEPTH);
    v_bts2 += samples2[1 + i*ADC_GRP2_NUM_CHANNELS] / static_cast<float>(ADC_GRP2_BUF_DEPTH);
    v_ac   += samples2[2 + i*ADC_GRP2_NUM_CHANNELS] / static_cast<float>(ADC_GRP2_BUF_DEPTH);
  }
  // current mesured on BTS1
  v_bts1 *= 3.3 / (powf(2, 12)-1);// * throttle0 /100.0;
  // current mesured on BTS2
  v_bts2 *= 3.3 / (powf(2, 12)-1);// * throttle1 /100.0;
  // total current
  // hypothesis: the resistors are exactly the same on BTS1 and BTS2
  v_ac   *= 3.3 / (powf(2, 12)-1) * ACS713_VFACTOR;// * (throttle0 + throttle1) / 100.0;

  if(v_ac_bias == 0) {
    v_ac_bias = v_ac;
  }
  v_ac_bias = v_ac_bias*0.95 + v_ac*0.05;
  v_ac_bias = 0;

  // TODO convert voltage to current.

  float iis_bts1 = v_bts1 / 1200.0;
  float iis_bts2 = v_bts2 / 1200.0;
  current_bts1 = iis_bts1 * 20000.0;
  current_bts2 = iis_bts2 * 20000.0;
  current_acs713 = (v_ac - v_ac_bias)   * 1000.0;
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
    }else {
      // measure current if fully off
      pwmEnableChannel(&PWMD3, 0, 1.0*PWM_PERIOD/100.0);
    }
    chThdSleepMilliseconds(10);
  }
}




float get_current(enum CurrentSensor sensor) {
  switch (sensor)
  {
  case CS_BTS1:
    return current_bts1;
    break;
  case CS_BTS2:
    return current_bts2;
    break;
  case CS_ACS713:
    return current_acs713;
    break;
  default:
    return -1;
  }
}


void start_power_switch() {
    chThdCreateStatic(waThreadPowerSwitch, sizeof(waThreadPowerSwitch), NORMALPRIO, ThreadPowerSwitch, NULL);
}
