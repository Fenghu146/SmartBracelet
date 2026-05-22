#include "tf_card.h"
#include "pin_config.h"
#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>

bool tf_init(void) {
  SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_D0);
  if (!SD_MMC.begin("/sdcard", true)) {
    USBSerial.println("TF: mount failed");
    return false;
  }
  uint64_t total = SD_MMC.totalBytes() / 1024;
  uint64_t used = SD_MMC.usedBytes() / 1024;
  USBSerial.printf("TF: mounted, %lluKB total, %lluKB used\n", total, used);
  return true;
}

bool tf_available(void) {
  return SD_MMC.cardType() != CARD_NONE;
}

uint64_t tf_total_kb(void) {
  return SD_MMC.totalBytes() / 1024;
}

uint64_t tf_used_kb(void) {
  return SD_MMC.usedBytes() / 1024;
}

int tf_list_dir(const char *path, char (*names)[32], int max) {
  File root = SD_MMC.open(path);
  if (!root) return 0;
  int count = 0;
  File f = root.openNextFile();
  while (f && count < max) {
    const char *n = f.name();
    if (n[0] != '.') {
      strncpy(names[count], n, 31);
      names[count][31] = 0;
      count++;
    }
    f.close();
    f = root.openNextFile();
  }
  root.close();
  return count;
}
