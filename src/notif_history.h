#ifndef NOTIF_HISTORY_H
#define NOTIF_HISTORY_H

#include <stdint.h>

#define NOTIF_MAX_HISTORY 10
#define NOTIF_APP_MAX 16
#define NOTIF_TITLE_MAX 48
#define NOTIF_BODY_MAX 80

typedef struct {
    char app[NOTIF_APP_MAX];
    char title[NOTIF_TITLE_MAX];
    char body[NOTIF_BODY_MAX];
    uint8_t hour, minute;
} notif_entry_t;

// Initialize notification history
void notif_history_init(void);

// Add a notification to history (ring buffer)
void notif_history_add(const char *app, const char *title, const char *body,
                       int hour, int minute);

// Get number of stored notifications
int notif_history_count(void);

// Get entry by index (0 = most recent)
const notif_entry_t* notif_history_get(int index);

// Clear all history
void notif_history_clear(void);

#endif // NOTIF_HISTORY_H
