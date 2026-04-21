#ifndef ASR_CLIENT_H
#define ASR_CLIENT_H

#include <stdbool.h>

/**
 * @brief Callback for real-time transcription text updates
 * @param text   Current confirmed text
 * @param stash  Current unconfirmed (preview) text
 */
typedef void (*asr_text_cb_t)(const char *text, const char *stash);

/**
 * @brief Callback for final transcription result
 * @param transcript  Final recognized text
 */
typedef void (*asr_done_cb_t)(const char *transcript);

/**
 * @brief Callback for ASR error
 * @param message  Error description
 */
typedef void (*asr_error_cb_t)(const char *message);

/**
 * @brief Callback when ASR session is ready (WS connected + session established)
 */
typedef void (*asr_ready_cb_t)(void);

typedef struct {
    asr_text_cb_t  on_text;
    asr_done_cb_t  on_done;
    asr_error_cb_t on_error;
    asr_ready_cb_t on_ready;
} asr_callbacks_t;

/**
 * @brief Start ASR session: connect WebSocket, begin streaming mic audio
 * @param cbs  Callback functions for receiving results
 * @return true on success
 */
bool asr_start(const asr_callbacks_t *cbs);

/**
 * @brief Stop ASR session: stop recording, close WebSocket
 */
void asr_stop(void);

/**
 * @brief Check if ASR session is active
 */
bool asr_is_active(void);

#endif
