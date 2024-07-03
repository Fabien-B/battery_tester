#include "ui.h"
#include "ch.h"
#include "hal.h"
#include "ssd1306.h"
#include "ssd1306_conf.h"
#include "voltage_monitor.h"
#include "temp_sensor.h"
#include "power_switch.h"
#include "reporting.h"


constexpr uint32_t BP_OK = 1;
constexpr uint32_t BP_PREV = 2;
constexpr uint32_t BP_NEXT = 4;
constexpr uint32_t BP_RET = 8;

#define UI_EVENT_NB 10
msg_t ui_events_buf[UI_EVENT_NB];
MAILBOX_DECL(mb_ui_event, ui_events_buf, UI_EVENT_NB);


void gpt3cb(GPTDriver *gptp);

static const GPTConfig gpt4cfg = {
  frequency:    10000U,
  callback:     gpt3cb,
  cr2:          0U,
  dier:         0U
};

SSD1306Pixel SD_ICON[] = {
  {110, 63},
  {110, 50},
  {103, 50},
  {100, 53},
  {100, 63},
  {110, 63},
};

void button_cb(void*) {
  chSysLockFromISR();
  gptStopTimerI(&GPTD4);
  gptStartOneShotI(&GPTD4, 50);
  chSysUnlockFromISR();
}

void gpt3cb(GPTDriver*) {
  uint32_t buttons = 0;
  if(palReadLine(LINE_BP_NEXT) == PAL_LOW) {
    buttons |= BP_NEXT;
  }
  if(palReadLine(LINE_BP_PREV) == PAL_LOW) {
    buttons |= BP_PREV;
  }
  if(palReadLine(LINE_BP_OK) == PAL_LOW) {
    buttons |= BP_OK;
  }
  if(palReadLine(LINE_BP_RET) == PAL_LOW) {
    buttons |= BP_RET;
  }

  chSysLockFromISR();
  chMBPostI(&mb_ui_event, buttons);
  chSysUnlockFromISR();
}


static THD_WORKING_AREA(waThreadUi, 1024);
static THD_FUNCTION(ThreadUi, arg) {
  (void)arg;
  chRegSetThreadName("User interface");

  i2cAcquireBus(&SSD1306_I2CD);
  ssd1306_Init();
  ssd1306_SetDisplayOn();
  ssd1306_Fill(BLACK);
  ssd1306_UpdateScreen();
  i2cReleaseBus(&SSD1306_I2CD);

  gptStart(&GPTD4, &gpt4cfg);

  palEnableLineEvent(LINE_BP_NEXT, PAL_EVENT_MODE_FALLING_EDGE);
  palEnableLineEvent(LINE_BP_PREV, PAL_EVENT_MODE_FALLING_EDGE);
  palEnableLineEvent(LINE_BP_OK,   PAL_EVENT_MODE_FALLING_EDGE);
  palEnableLineEvent(LINE_BP_RET,  PAL_EVENT_MODE_FALLING_EDGE);
  palSetLineCallback(LINE_BP_NEXT, button_cb, NULL);
  palSetLineCallback(LINE_BP_PREV, button_cb, NULL);
  palSetLineCallback(LINE_BP_OK,   button_cb, NULL);
  palSetLineCallback(LINE_BP_RET,  button_cb, NULL);
  
  uint8_t c = 0;

  while(true) {
    float pwr_switch0_thr = get_power_switch_throttle(0);
    float pwr_switch1_thr = get_power_switch_throttle(1);

    msg_t buttons;
    msg_t ret = chMBFetchTimeout(&mb_ui_event, &buttons, chTimeMS2I(200));
    if(ret == MSG_OK) {
      // TODO priority order
      if(buttons & BP_NEXT) {
        pwr_switch0_thr = std::min(pwr_switch0_thr + 10, 100.f);
      }
      else if(buttons & BP_PREV) {
        pwr_switch0_thr = std::max(pwr_switch0_thr - 10, 0.f);
      }
      else if(buttons & BP_OK) {
        pwr_switch0_thr = 100;
      }
      else if(buttons & BP_RET) {
        pwr_switch0_thr = 0;
      }
    }

    std::array<float, 6> cell_voltages;
    get_cell_voltages(cell_voltages);
    float temp = get_temperature();
    
    
    float current0 = get_current(CS_BTS1);
    float current1 = get_current(CS_BTS2);
    float current2 = get_current(CS_ACS713);

    set_power_switch_throttle(0, pwr_switch0_thr);
    set_power_switch_throttle(1, pwr_switch1_thr);

    ssd1306_Fill(BLACK);
    ssd1306_MoveCursor(0, 7);
    ssd1306_WriteFmt(Font5x7FixedMono, WHITE, "%02f    %02f    %02f", cell_voltages[0], cell_voltages[1], cell_voltages[2]);
    ssd1306_MoveCursor(0, 16);
    ssd1306_WriteFmt(Font5x7FixedMono, WHITE, "%02f    %02f    %02f", cell_voltages[3], cell_voltages[4], cell_voltages[5]);
    ssd1306_MoveCursor(0, 30);
    ssd1306_WriteFmt(Font5x7FixedMono, WHITE, "TEMP: %01f", temp);
    ssd1306_MoveCursor(20, 40);
    ssd1306_WriteFmt(Font5x7FixedMono, WHITE, "%01f %%", pwr_switch0_thr);
    ssd1306_MoveCursor(80, 40);
    ssd1306_WriteFmt(Font5x7FixedMono, WHITE, "%01f %%", pwr_switch1_thr);

    ssd1306_MoveCursor(0, 50);
    ssd1306_WriteFmt(Font5x7FixedMono, WHITE, "%01f  %01f  %01f", current0, current1, current2);

    ssd1306_MoveCursor(10, 60);

    if(is_sd_card_ready()) {
      ssd1306_DrawPolyline(SD_ICON, sizeof(SD_ICON)/sizeof(SSD1306Pixel), WHITE);
    } else {

    }

    //ssd1306_WriteChar('a', Lato_Heavy_12, WHITE);
    //ssd1306_DrawCircle(20 + c%88, 20, 5, WHITE);
    SSD1306Color color = static_cast<SSD1306Color>(c%2);
    ssd1306_DrawPixel(120, 60, color);

    i2cAcquireBus(&SSD1306_I2CD);
    
    ssd1306_UpdateScreen();
    i2cReleaseBus(&SSD1306_I2CD);

    c += 1;
    chThdSleepMilliseconds(200);
  }


}


void start_ui() {
    chThdCreateStatic(waThreadUi, sizeof(waThreadUi), NORMALPRIO, ThreadUi, NULL);
}