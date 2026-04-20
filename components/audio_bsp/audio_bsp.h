#ifndef AUDIO_BSP_H
#define AUDIO_BSP_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize audio codec (ES8311 DAC via codec_board)
 */
void audio_bsp_init(void);

/**
 * @brief Play a raw PCM buffer (16-bit, 24 kHz, stereo)
 * @note  Blocks until playback finishes. Safe to call from any task.
 */
void audio_play_pcm(const uint8_t *data, uint32_t len);

/**
 * @brief Set playback volume (0-100)
 */
void audio_set_volume(uint8_t vol);

/**
 * @brief Check if audio subsystem is initialized
 */
bool audio_is_ready(void);

#endif
