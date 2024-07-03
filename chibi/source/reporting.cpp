#include "reporting.h"
#include "voltage_monitor.h"
#include "power_switch.h"
#include "temp_sensor.h"
#include "printf.h"
#include "hal.h"
#include "usb_serial.h"
#include "stdutil.h"
#include "ff.h"
#include "string.h"



static const char LOG_DIR[] = "BATTERY_TESTER";
static const char prefix[] = "bt";
FIL log_fd;
bool log_opened = false;

FRESULT setup_file(FIL* log_file);


/*===========================================================================*/
/* Card insertion monitor.                                                   */
/*===========================================================================*/

#define POLLING_INTERVAL                10
#define POLLING_DELAY                   10


// Card monitor timer.
static virtual_timer_t tmr;

// Debounce counter.
static unsigned cnt;

// Card event sources.
static event_source_t inserted_event, removed_event;

/**
 * @brief   Insertion monitor timer callback function.
 * @param[in] p         pointer to the @p BaseBlockDevice object
 */
static void tmrfunc(virtual_timer_t *vtp, void *p) {
  BaseBlockDevice *bbdp = (BaseBlockDevice*)p;

  (void)vtp;

  chSysLockFromISR();
  if (cnt > 0) {
    if (blkIsInserted(bbdp)) {
      if (--cnt == 0) {
        chEvtBroadcastI(&inserted_event);
      }
    }
    else
      cnt = POLLING_INTERVAL;
  }
  else {
    if (!blkIsInserted(bbdp)) {
      cnt = POLLING_INTERVAL;
      chEvtBroadcastI(&removed_event);
    }
  }
  chVTSetI(&tmr, TIME_MS2I(POLLING_DELAY), tmrfunc, bbdp);
  chSysUnlockFromISR();
}

/**
 * @brief   Polling monitor start.
 * @param[in] p         pointer to an object implementing @p BaseBlockDevice
 */
static void tmr_init(void *p) {

  chEvtObjectInit(&inserted_event);
  chEvtObjectInit(&removed_event);
  chSysLock();
  cnt = POLLING_INTERVAL;
  chVTSetI(&tmr, TIME_MS2I(POLLING_DELAY), tmrfunc, p);
  chSysUnlock();
}



/*===========================================================================*/
/* FatFs related.                                                            */
/*===========================================================================*/

/**
 * @brief FS object.
 */
static FATFS SDC_FS;

/* FS mounted and ready.*/
static bool fs_ready = FALSE;

/* Maximum speed SPI configuration (18MHz, CPHA=0, CPOL=0, MSb first).*/
static SPIConfig hs_spicfg = {
  .circular         = false,
  .slave            = false,
  .data_cb          = NULL,
  .error_cb         = NULL,
  .ssline           = LINE_SD_CS,
  .cr1              = 0U,
  .cr2              = 0U
};

/* Low speed SPI configuration (281.250kHz, CPHA=0, CPOL=0, MSb first).*/
static SPIConfig ls_spicfg = {
  .circular         = false,
  .slave            = false,
  .data_cb          = NULL,
  .error_cb         = NULL,
  .ssline           = LINE_SD_CS,
  .cr1              = SPI_CR1_BR_2 | SPI_CR1_BR_1,
  .cr2              = 0U
};


/* MMC/SD over SPI driver configuration.*/
static mmc_spi_config_t mmccfg = {&SPID1, &ls_spicfg, &hs_spicfg};

/* Generic large buffer.*/
static uint8_t fbuff[1024];

static FRESULT scan_files(BaseSequentialStream *chp, char *path) {
  static FILINFO fno;
  FRESULT res;
  DIR dir;
  size_t i;
  char *fn;

  res = f_opendir(&dir, path);
  if (res == FR_OK) {
    i = strlen(path);
    while (((res = f_readdir(&dir, &fno)) == FR_OK) && fno.fname[0]) {
      if (FF_FS_RPATH && fno.fname[0] == '.')
        continue;
      fn = fno.fname;
      if (fno.fattrib & AM_DIR) {
        *(path + i) = '/';
        strcpy(path + i + 1, fn);
        res = scan_files(chp, path);
        *(path + i) = '\0';
        if (res != FR_OK)
          break;
      }
      else {
        chprintf(chp, "%s/%s\r\n", path, fn);
      }
    }
  }
  return res;
}


void cmd_tree(BaseSequentialStream *chp, int argc, const char * const argv[]) {
  FRESULT err;
  uint32_t clusters;
  FATFS *fsp;

  (void)argv;
  if (argc > 0) {
    chprintf(chp, "Usage: tree\r\n");
    return;
  }
  if (!fs_ready) {
    chprintf(chp, "File System not mounted\r\n");
    return;
  }
  err = f_getfree("/", &clusters, &fsp);
  if (err != FR_OK) {
    chprintf(chp, "FS: f_getfree() failed\r\n");
    return;
  }
  chprintf(chp,
           "FS: %lu free clusters, %lu sectors per cluster, %lu bytes free\r\n",
           clusters, (uint32_t)SDC_FS.csize,
           clusters * (uint32_t)SDC_FS.csize * (uint32_t)MMCSD_BLOCK_SIZE);
  fbuff[0] = 0;
  scan_files(chp, (char *)fbuff);
}




static uint8_t mmcbuf[MMC_BUFFER_SIZE];
MMCDriver MMCD1;


// Card insertion event.
static void InsertHandler(eventid_t id) {
  FRESULT err;

  (void)id;
  /*
   * On insertion SDC initialization and FS mount.
   */
  if (mmcConnect(&MMCD1))
    return;

  err = f_mount(&SDC_FS, "/", 1);
  if (err != FR_OK) {
    mmcDisconnect(&MMCD1);
    return;
  }
  fs_ready = TRUE;
  
  if(setup_file(&log_fd) == FR_OK) {
    log_opened = true;
    palSetLine(LINE_LED_HB);
      const char log_header[] = "U,du,u1,u2,u3,u4,u5,u6,ctot,c1,c2,cacs\r\n";
      UINT nbw;
      f_write(&log_fd, log_header, sizeof(log_header), &nbw);
  }
}

UINT log_write(const char* buffer, size_t len) {
  if(log_opened) {
    UINT nbw;
    FRESULT res = f_write(&log_fd, buffer, len, &nbw);
    res = f_sync(&log_fd);
    return nbw;
  } else {
    return 0;
  }
}

// Card removal event.
static void RemoveHandler(eventid_t id) {

  (void)id;
  mmcDisconnect(&MMCD1);
  fs_ready = FALSE;
  palClearLine(LINE_LED_HB);
}

bool is_sd_card_ready() {
  return fs_ready;
}


FRESULT setup_file(FIL* log_file) {
  DIR log_dir;
  

  FRESULT res;
  res = f_opendir(&log_dir, LOG_DIR);
  if(res != FR_OK) {
    res = f_mkdir(LOG_DIR);
  }

  char filepath[100];
  int n = 0;
  while(true) {
    chsnprintf(filepath, 100, "%s/%s_%d.csv", LOG_DIR, prefix, n);
    res = f_stat(filepath, NULL);
    if(res == FR_NO_FILE) {
      res = f_open(log_file, filepath, FA_CREATE_ALWAYS | FA_WRITE);
      break;
    }
    n++;
  }
  
  return res;
}



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

    //float temp = get_temperature();
    
    float c1 = get_current(CS_BTS1);
    float c2 = get_current(CS_BTS2);
    float ct = get_current(CS_ACS713);
    float c_total = c1 + c2;
    //float power = u_total * c_total;

    int l = chsnprintf(buffer, len, "%02f,%02f,%02f,%02f,%02f,%02f,%02f,%02f,%02f,%02f,%02f,%02f\r\n",
        u_total, delta_cell,
        cell_voltages[0], cell_voltages[1], cell_voltages[2],
        cell_voltages[3], cell_voltages[4], cell_voltages[5],
        c_total, c1, c2, ct
    );
    return l;
}

// if fatfs use stack for working buffers, stack size should be reserved accordingly
#define WA_LOG_BASE_SIZE 2000
#if FF_USE_LFN == 2
static THD_WORKING_AREA(waThreadReporting, WA_LOG_BASE_SIZE+((FF_MAX_LFN+1)*2));
#else
static THD_WORKING_AREA(waThreadReporting, WA_LOG_BASE_SIZE);
#endif


static THD_FUNCTION(ThreadReporting, arg) {
  (void)arg;
  chRegSetThreadName("Reporting");
  char buf[100];

  static const evhandler_t evhndl[] = {
    InsertHandler,
    RemoveHandler,
    //ShellHandler
  };
  event_listener_t el0, el1;//, el2;

  /*
   * Initializes the MMC driver to work with SPI1.
   */
  mmcObjectInit(&MMCD1, mmcbuf);
  mmcStart(&MMCD1, &mmccfg);

  /*
   * Activates the card insertion monitor.
   */
  tmr_init(&MMCD1);



  /*
   * Normal main() thread activity, handling SD card events and shell
   * start/exit.
   */
  chEvtRegister(&inserted_event, &el0, 0);
  chEvtRegister(&removed_event, &el1, 1);
  //chEvtRegister(&shell_terminated, &el2, 2);







  while(true) {
    chEvtDispatch(evhndl, chEvtWaitOneTimeout(ALL_EVENTS, TIME_MS2I(500)));

    int len = print_data(buf, sizeof(buf));
    log_write(buf, len);
    //chprintf ((BaseSequentialStream*)&SDU1, "%s", buf);
    //chThdSleepMilliseconds(500);
  }

}



void start_reporting() {
    chThdCreateStatic(waThreadReporting, sizeof(waThreadReporting), NORMALPRIO, ThreadReporting, NULL);
}
