#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#include <memory>
#include "Arduino_IIC.h"

extern std::unique_ptr<Arduino_IIC> touch;

void lv_port_indev_init(void);

#endif
