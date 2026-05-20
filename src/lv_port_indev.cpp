#include "lv_port_indev.h"
#include <lvgl.h>

static lv_indev_drv_t indev_drv;

static void touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    static bool was_pressed = false;
    (void)drv;

    if (touch && touch->available()) {
        data->point.x = touch->data.x;
        data->point.y = touch->data.y;
        data->state = (touch->data.event == 0) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
        was_pressed = (data->state == LV_INDEV_STATE_PR);
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void lv_port_indev_init(void)
{
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);
}
