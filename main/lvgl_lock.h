#ifndef LVGL_LOCK_H
#define LVGL_LOCK_H

#include <stdbool.h>

bool lvgl_lock(int timeout_ms);
void lvgl_unlock(void);

#endif
