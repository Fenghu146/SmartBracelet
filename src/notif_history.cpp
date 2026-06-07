// Notification history: ring buffer of recent notifications
#include "notif_history.h"
#include <string.h>

static notif_entry_t history[NOTIF_MAX_HISTORY];
static int head = 0;
static int count = 0;

void notif_history_init(void) {
    memset(history, 0, sizeof(history));
    head = 0;
    count = 0;
}

void notif_history_add(const char *app, const char *title, const char *body,
                       int hour, int minute) {
    notif_entry_t *e = &history[head];
    strncpy(e->app, app, NOTIF_APP_MAX - 1);
    e->app[NOTIF_APP_MAX - 1] = '\0';
    strncpy(e->title, title, NOTIF_TITLE_MAX - 1);
    e->title[NOTIF_TITLE_MAX - 1] = '\0';
    strncpy(e->body, body, NOTIF_BODY_MAX - 1);
    e->body[NOTIF_BODY_MAX - 1] = '\0';
    e->hour = (uint8_t)hour;
    e->minute = (uint8_t)minute;

    head = (head + 1) % NOTIF_MAX_HISTORY;
    if (count < NOTIF_MAX_HISTORY) count++;
}

int notif_history_count(void) {
    return count;
}

const notif_entry_t* notif_history_get(int index) {
    if (index < 0 || index >= count) return NULL;
    // index 0 = most recent
    int real_idx = (head - 1 - index + NOTIF_MAX_HISTORY) % NOTIF_MAX_HISTORY;
    return &history[real_idx];
}

void notif_history_clear(void) {
    head = 0;
    count = 0;
    memset(history, 0, sizeof(history));
}
