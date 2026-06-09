#include "lv_port_disp.h"
#include "pin_config.h"
#include <lvgl.h>

#define LV_DISP_BUF_SIZE (LCD_WIDTH * LCD_HEIGHT / 10)

static lv_disp_drv_t disp_drv;
static lv_disp_draw_buf_t disp_buf;
static lv_color_t buf[LV_DISP_BUF_SIZE];

static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    gfx->draw16bitRGBBitmap(
        area->x1, area->y1,
        (uint16_t *)&color_p->full,
        area->x2 - area->x1 + 1,
        area->y2 - area->y1 + 1
    );
    lv_disp_flush_ready(drv);
}

void lv_port_disp_init(void)
{
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, LV_DISP_BUF_SIZE);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &disp_buf;
    lv_disp_drv_register(&disp_drv);
}
