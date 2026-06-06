#ifndef WRIST_DETECT_H
#define WRIST_DETECT_H

void wrist_detect_init(void);
void wrist_detect_update(float ax, float ay, float az);
bool wrist_detect_is_raised(void);

// Callback type for when wrist is raised
typedef void (*wrist_raise_callback_t)(void);
void wrist_detect_set_callback(wrist_raise_callback_t cb);

#endif // WRIST_DETECT_H
