#ifndef UI_VOICE_H
#define UI_VOICE_H

#include "lvgl.h"

/**
 * @brief Create the voice recognition page
 * @param parent  Parent object (main_screen)
 */
void ui_voice_create(lv_obj_t *parent);

/**
 * @brief Get the voice page container (for hide/show)
 */
lv_obj_t *ui_voice_get_page(void);

/**
 * @brief Toggle ASR recording on/off (called from floating mic button)
 */
void ui_voice_toggle_recording(void);

#endif
