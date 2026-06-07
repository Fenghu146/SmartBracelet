#ifndef WATCH_FACES_H
#define WATCH_FACES_H

#include <lvgl.h>
#include "ui_pages.h"

// Watch face IDs
#define FACE_DIGITAL  0
#define FACE_ANALOG   1
#define FACE_SPORT    2
#define FACE_COUNT    3

// Create sport watch face page
void sport_face_create(lv_obj_t *parent);

// Update sport face with telemetry
void sport_face_update(const ui_telemetry_t *t);

// Cycle to next watch face (call from gesture handler)
int  watch_face_next(int current_face);

// Get display name for a face
const char* watch_face_name(int face);

#endif // WATCH_FACES_H
