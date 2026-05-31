#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"

/**
 * @brief Event group bits for WiFi events
 */
#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_IPV4_OBTAINED_BIT  BIT1
#define WIFI_IPV6_OBTAINED_BIT  BIT2

/**
 * @brief Initialize wifi station
 *
 * Starts the wifi interface in station mode and obtains an ip adress
 *
 * This function will be asynchronous. Therefor you should pass an event group to
 * wait for a connection and obtained ip address.
 *
 * Important! esp_netif_init() and esp_event_loop_create_default() need to be called.
 *
 * @param[in] event_group Event group handle for WiFi and IP events. Pass NULL
 *                        to use the existing event group.
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t wifi_init(EventGroupHandle_t event_group);

/**
 * @brief Stops WiFi
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t wifi_stop(void);

/**
 * @brief Attempt to reconnect WiFi
 *
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t wifi_reconnect(void);

#endif // WIFI_H