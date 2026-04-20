#ifndef WIFI_PROV_H
#define WIFI_PROV_H

#include <stdbool.h>

/**
 * @brief Start WiFi provisioning via AP + captive portal.
 *        Starts AP, DNS redirect, and HTTP server.
 *        Blocks until user submits credentials and STA connects.
 */
void wifi_prov_start(void);

/**
 * @brief Check if provisioning is done and STA connected.
 */
bool wifi_prov_is_done(void);

#endif
