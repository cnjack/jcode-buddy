#ifndef AUDIO_BSP_H
#define AUDIO_BSP_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Initialize audio codec (ES8311 DAC + ES7210 ADC via codec_board)
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

/**
 * @brief Open recording device (ES7210 ADC, 16kHz mono 16-bit)
 */
esp_err_t audio_record_start(void);

/**
 * @brief Read PCM samples from microphone
 * @param buf   Buffer to fill with PCM data
 * @param len   Number of bytes to read
 * @return Number of bytes actually read, or -1 on error
 */
int audio_record_read(uint8_t *buf, uint32_t len);

/**
 * @brief Close recording device
 */
void audio_record_stop(void);

/**
 * @brief Check if recording is active
 */
bool audio_is_recording(void);

#endif
