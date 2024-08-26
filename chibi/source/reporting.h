#pragma once
#include "inttypes.h"
#include <stddef.h>
#include "ch.h"
#include "hal.h"
#include "ff.h"

void start_reporting();
void cmd_tree(BaseSequentialStream *chp, int argc, const char * const argv[]);
bool is_sd_card_ready();
bool is_log_opened();
void new_log();
void close_log();
