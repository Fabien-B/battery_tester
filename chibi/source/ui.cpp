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

typedef void * (*screen_func)(msg_t);

void* screen_measure(msg_t buttons);
void* screen_test_choice(msg_t buttons);
void* screen_test(msg_t buttons);

static const GPTConfig gpt4cfg = {
  frequency:    10000U,
  callback:     gpt3cb,
  cr2:          0U,
  dier:         0U
};

SSD1306Pixel SD_ICON[] = {
  {127, 13},
  {127, 0},
  {120, 0},
  {117, 3},
  {117, 13},
  {127, 13},
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


void draw_header() {
  float u = get_total_voltage();
  int s = get_cells_number();
  float avr_cell = u / s;

  ssd1306_MoveCursor(0, 7);
  ssd1306_WriteFmt(Font5x7FixedMono, WHITE, "%dS  %02fV  %02fV", s, u, avr_cell);
  ssd1306_DrawLine(0, 9, 110, 9, WHITE);

  if(is_sd_card_ready()) {
    ssd1306_DrawPolyline(SD_ICON, sizeof(SD_ICON)/sizeof(SSD1306Pixel), WHITE);
  }

  if(is_log_opened()) {
      ssd1306_DrawCircle(122, 7, 3, WHITE, true);
  }


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
  
  screen_func current_screen = (screen_func)screen_measure;

  while(true) {

    ssd1306_Fill(BLACK);
    draw_header();

    msg_t buttons = 0;
    chMBFetchTimeout(&mb_ui_event, &buttons, chTimeMS2I(200));

    screen_func next_screen = (screen_func)current_screen(buttons);
    i2cAcquireBus(&SSD1306_I2CD);
    ssd1306_UpdateScreen();
    i2cReleaseBus(&SSD1306_I2CD);

    if(next_screen != nullptr) {
      current_screen = next_screen;
    }

    //chThdSleepMilliseconds(200);
  }


}


void start_ui() {
    chThdCreateStatic(waThreadUi, sizeof(waThreadUi), NORMALPRIO, ThreadUi, NULL);
}

void* screen_measure(msg_t buttons) {
  static uint8_t c = 0;

  float pwr_switch0_thr = get_power_switch_throttle(0);
  float pwr_switch1_thr = get_power_switch_throttle(1);

  // TODO priority order
  if(buttons & BP_NEXT) {
    pwr_switch0_thr = std::min(pwr_switch0_thr + 10, 100.f);
  }
  else if(buttons & BP_PREV) {
    pwr_switch0_thr = std::max(pwr_switch0_thr - 10, 0.f);
  }
  else if(buttons & BP_OK) {
    // turn off power before changing screen
    set_power_switch_throttle(0, 0.);
    set_power_switch_throttle(1, 0.);
    return (void*)screen_test_choice;
  }
  else if(buttons & BP_RET) {
    pwr_switch0_thr = 0;
  }

  std::array<float, 6> cell_voltages;
  get_cell_voltages(cell_voltages);
  float temp = get_temperature();
  
  float current0 = get_current(CS_BTS1);
  float current1 = get_current(CS_BTS2);
  float current2 = get_current(CS_ACS713);

  set_power_switch_throttle(0, pwr_switch0_thr);
  set_power_switch_throttle(1, pwr_switch1_thr);

  ssd1306_MoveCursor(0, 20);
  ssd1306_WriteFmt(Font5x7FixedMono, WHITE, "%02f    %02f    %02f", cell_voltages[0], cell_voltages[1], cell_voltages[2]);
  ssd1306_MoveCursor(0, 30);
  ssd1306_WriteFmt(Font5x7FixedMono, WHITE, "%02f    %02f    %02f", cell_voltages[3], cell_voltages[4], cell_voltages[5]);
  ssd1306_MoveCursor(0, 40);
  ssd1306_WriteFmt(Font5x7FixedMono, WHITE, "TEMP: %01f", temp);
  ssd1306_MoveCursor(20, 50);
  ssd1306_WriteFmt(Font5x7FixedMono, WHITE, "%01f %%", pwr_switch0_thr);
  ssd1306_MoveCursor(80, 50);
  ssd1306_WriteFmt(Font5x7FixedMono, WHITE, "%01f %%", pwr_switch1_thr);
  ssd1306_MoveCursor(0, 60);
  ssd1306_WriteFmt(Font5x7FixedMono, WHITE, "%01f  %01f  %01f", current0, current1, current2);

  SSD1306Color color = static_cast<SSD1306Color>(c%2);
  ssd1306_DrawPixel(127, 63, color);

  c += 1;

  return nullptr;
}

void* screen_test_choice(msg_t buttons) {
  static uint8_t index = 0;

  ssd1306_MoveCursor(20, 20);
  ssd1306_WriteString("Capacity test", Font5x7FixedMono, WHITE);
  ssd1306_MoveCursor(20, 30);
  if(is_log_opened()) {
    ssd1306_WriteString("stop log", Font5x7FixedMono, WHITE);
  } else {
    ssd1306_WriteString("new log", Font5x7FixedMono, WHITE);
  }
  
  ssd1306_MoveCursor(0, 20+10*index);
  ssd1306_WriteString("*", Font5x7FixedMono, WHITE);

    // TODO priority order
  if(buttons & BP_NEXT) {
    index = (index + 1) % 2;
  }
  else if(buttons & BP_PREV) {
    index = (uint8_t)(index - 1) % 2;
  }
  else if(buttons & BP_OK) {
    if(index == 0) {
      return (void*)screen_test;
    }
    else if(index == 1) {
      if(is_log_opened()) {
        close_log();
      } else {
        new_log();
      }
      
      return (void*)screen_measure;
    }
    else {
      return nullptr;
    }
  }
  else if(buttons & BP_RET) {
    return (void*)screen_measure;
  }

  return nullptr;
}

void* screen_test(msg_t buttons) {
  static uint8_t index = 0;
  static bool editing = false;
  static int hist;  // history (when editing canceled)

  static int capacity = 1000; //mAh
  static int discharge_rate = 2;  // C*10 (tenth of C)

  ssd1306_MoveCursor(20, 20);
  ssd1306_WriteString("Capacity Test", Font5x7FixedMono, WHITE);


  ssd1306_MoveCursor(20, 30);
  ssd1306_WriteFmt(Font5x7FixedMono, WHITE, "%d mAh", capacity);

  ssd1306_MoveCursor(20, 40);
  ssd1306_WriteFmt(Font5x7FixedMono, WHITE, "%01f C", discharge_rate/10.0);

  ssd1306_MoveCursor(0, 30+10*index);
  if(editing) {
    ssd1306_WriteString("**", Font5x7FixedMono, WHITE);
  } else {
    ssd1306_WriteString("*", Font5x7FixedMono, WHITE);
  }
  

  if(buttons & BP_NEXT) {
    if(editing) {
      if(index == 0) {
        capacity += 100;
      }
      else if(index == 1) {
        discharge_rate += 1;
      }
    } else {
      index = (index + 1) % 2;
    }
  }
  else if(buttons & BP_PREV) {
        if(editing) {
      if(index == 0) {
        capacity -= 100;
      }
      else if(index == 1) {
        discharge_rate -= 1;
      }
    } else {
      index = (uint8_t)(index - 1) % 2;
    }
  }
  else if(buttons & BP_OK) {
    if(editing) {

    } else {
      // save current settings
      if(index == 0) {
        hist = capacity;
      } else if(index == 1) {
        hist = discharge_rate ;
      }
    }
    editing = !editing;
  }
  else if(buttons & BP_RET) {
    if(editing) {
      // restore last value
      if(index == 0) {
        capacity = hist;
      } else if(index == 1) {
        discharge_rate = hist;
      }
      editing = false;
    } else {
      return (void*)screen_test_choice;
    }
  }

  return nullptr;
}
