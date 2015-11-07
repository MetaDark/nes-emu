#include "dmc.h"

/**
 * References:
 * http://wiki.nesdev.com/w/index.php/APU_DMC
 */

struct DMC {
  bool irq_enable          : 1;
  bool loop                : 1;
  byte frequency           : 4;

  byte loader_counter      : 7;

  byte sample_address      : 8;
  byte sample_length       : 8;
};

byte dmc_sample(DMC * dmc) {
  (void)dmc;
  return 0;
}

void dmc_tick(DMC * dmc) {
  (void)dmc;
}

void dmc_write(DMC * dmc, byte addr, byte val) {
  (void)dmc;
  (void)addr;
  (void)val;
}

byte dmc_read(DMC * dmc, byte addr) {
  (void)dmc;
  (void)addr;
  return 0;
}