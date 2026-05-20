#include "lv_port_indev.h"
#include <lvgl.h>

static lv_indev_drv_t indev_drv;

static void touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;

    int32_t tx = 0, ty = 0;

    if (touch && touch->IIC_Interrupt_Flag == true) {
        touch->IIC_Interrupt_Flag = false;
        tx = touch->IIC_Read_Device_Value(
            Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
        ty = touch->IIC_Read_Device_Value(
            Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
    }

    if (tx > 20 && ty > 20) {
        data->point.x = tx;
        data->point.y = ty;
        data->state = LV_INDEV_STATE_PR;
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
