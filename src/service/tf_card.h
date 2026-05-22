#pragma once
#include <stdint.h>

bool tf_init(void);
bool tf_available(void);
uint64_t tf_total_kb(void);
uint64_t tf_used_kb(void);
int tf_list_dir(const char *path, char (*names)[32], int max);
