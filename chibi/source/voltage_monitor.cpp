#include "voltage_monitor.h"

#include "ch.h"
#include "hal.h"
#include "stdutil.h"
#include "printf.h"


void adcerrorcallback(ADCDriver *adcp, adcerror_t err);

const ADCConfig adccfg1 = {
  .difsel       = 0U
};

#define ADC_GRP1_NUM_CHANNELS   6
#define ADC_GRP1_BUF_DEPTH      1

#define VC1_CHANNEL   ADC_CHANNEL_IN7
#define VC2_CHANNEL   ADC_CHANNEL_IN8
#define VC3_CHANNEL   ADC_CHANNEL_IN9
#define VC4_CHANNEL   ADC_CHANNEL_IN1
#define VC5_CHANNEL   ADC_CHANNEL_IN2
#define VC6_CHANNEL   ADC_CHANNEL_IN3


// voltage divider factors
float VFACTORS[6] = {1.32, 2.77, 4.28, 5.65, 6.59, 7.76};

// cell voltages
float cell_voltages[6];
float max_voltage;

/*
 * ADC conversion group 1.
 * Mode:        One shot, 2 channels, SW triggered.
 * Channels:    IN1, IN2.
 */
const ADCConversionGroup adcgrpcfg1 = {
  .circular     = false,
  .num_channels = ADC_GRP1_NUM_CHANNELS,
  .end_cb       = NULL,
  .error_cb     = adcerrorcallback,
  .cfgr         = 0U,
  .cfgr2        = 0U,
  .tr1          = ADC_TR_DISABLED,
  .tr2          = ADC_TR_DISABLED,
  .tr3          = ADC_TR_DISABLED,
  .awd2cr       = 0U,
  .awd3cr       = 0U,
  .smpr         = {
    ADC_SMPR1_SMP_AN1(ADC_SMPR_SMP_247P5) |
    ADC_SMPR1_SMP_AN2(ADC_SMPR_SMP_247P5) |
    ADC_SMPR1_SMP_AN3(ADC_SMPR_SMP_247P5) |
    ADC_SMPR1_SMP_AN7(ADC_SMPR_SMP_247P5) |
    ADC_SMPR1_SMP_AN8(ADC_SMPR_SMP_247P5) |
    ADC_SMPR1_SMP_AN9(ADC_SMPR_SMP_247P5),
    0U
  },
  .sqr          = {
    ADC_SQR1_SQ1_N(VC1_CHANNEL) |
    ADC_SQR1_SQ2_N(VC2_CHANNEL) |
    ADC_SQR1_SQ3_N(VC3_CHANNEL) |
    ADC_SQR1_SQ4_N(VC4_CHANNEL),
    ADC_SQR2_SQ5_N(VC5_CHANNEL) |
    ADC_SQR2_SQ6_N(VC6_CHANNEL),
    0U,
    0U
  }
};


/*
 * ADC errors callback, should never happen.
 */
void adcerrorcallback(ADCDriver *adcp, adcerror_t err) {

  (void)adcp;
  (void)err;

  chSysHalt("it happened");
}




#if CACHE_LINE_SIZE > 0
CC_ALIGN_DATA(CACHE_LINE_SIZE)
#endif
adcsample_t samples[CACHE_SIZE_ALIGN(adcsample_t, ADC_GRP1_NUM_CHANNELS * ADC_GRP1_BUF_DEPTH)];



/*
 * ADC readings
 */
static THD_WORKING_AREA(waThreadADC, 512);
static THD_FUNCTION(ThreadADC, arg) {
  (void)arg;
  chRegSetThreadName("ADC reading");

  adcStart(&ADCD1, &adccfg1);

  while (true) {
    /* Performing a one-shot conversion*/

    msg_t status = adcConvert(&ADCD1, &adcgrpcfg1, samples, ADC_GRP1_BUF_DEPTH);
    cacheBufferInvalidate(samples, sizeof (samples) / sizeof (adcsample_t));
    if(status == MSG_OK) {

      float voltages[6];
      for(int i=0; i<6; i++) {
        voltages[i] = samples[i] * VFACTORS[i] * 3.3 / powf(2, 12);
      }

      // protect global variable
      chSysLock();
      max_voltage = 0;
      for(int i=5; i>0; i--) {
        if(voltages[i] > 0.1) {
          cell_voltages[i] = voltages[i] - voltages[i-1];
          max_voltage = MAX(max_voltage, voltages[i]);
        } else {
          cell_voltages[i] = voltages[i];
        }
      }
      cell_voltages[0] = voltages[0];
      chSysUnlock();
    }


    chThdSleepMilliseconds(200);
  }
}

void get_cell_voltages(std::array<float, 6>& voltages) {
  chSysLock();
  for(int i=0; i<6; i++) {
    voltages[i] = cell_voltages[i];
  }
  chSysUnlock();
}

float get_total_voltage() {
  return max_voltage;
}

void start_adc_thread() {
  chThdCreateStatic(waThreadADC, sizeof(waThreadADC), NORMALPRIO, ThreadADC, NULL);
}
