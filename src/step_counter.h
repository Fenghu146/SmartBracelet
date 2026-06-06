#ifndef STEP_COUNTER_H
#define STEP_COUNTER_H

#include <stdint.h>

void step_counter_init(void);
void step_counter_update(float ax, float ay, float az);
int  step_counter_get(void);
void step_counter_reset(void);
void step_counter_set(int count);

#endif // STEP_COUNTER_H
