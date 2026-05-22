#include "tf_card.h"
#include "pin_config.h"
#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>

bool tf_init(void) {
  // Attempt 1: SDMMC 1-bit with explicit pins
  USBSerial.printf("TF: trying SDMMC pins CLK=%d CMD=%d D0=%d ...\n",
    SDMMC_CLK, SDMMC_CMD, SDMMC_D0);
  SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_D0);
  if (SD_MMC.begin("/sdcard", true)) {
    uint64_t total = SD_MMC.totalBytes() / 1024;
    uint64_t used = SD_MMC.usedBytes() / 1024;
    USBSerial.printf("TF: SDMMC OK, %lluKB total, %lluKB used\n", total, used);
    return true;
  }
  USBSerial.println("TF: SDMMC failed (card type? format? try FAT32)");

  // Attempt 2: SDMMC without explicit setPins (use SDK defaults)
  USBSerial.println("TF: trying SDMMC without setPins...");
  if (SD_MMC.begin("/sdcard", true)) {
    USBSerial.printf("TF: SDMMC (no setPins) OK, %lluKB total\n",
      SD_MMC.totalBytes() / 1024);
    return true;
  }
  USBSerial.println("TF: all attempts failed");
  USBSerial.println("  -> Check: card inserted? FAT32? <32GB?");
  return false;
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
