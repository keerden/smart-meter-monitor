
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "wifi.h"

#if CONFIG_WIFI_WPA3_SAE_PWE_HUNT_AND_PECK
#define CONFIG_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define CONFIG_WIFI_H2E_IDENTIFIER ""
#elif CONFIG_WIFI_WPA3_SAE_PWE_H2E
#define CONFIG_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define CONFIG_WIFI_H2E_IDENTIFIER CONFIG_WIFI_WPA3_PASSWORD_ID
#elif CONFIG_WIFI_WPA3_SAE_PWE_BOTH
#define CONFIG_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define CONFIG_WIFI_H2E_IDENTIFIER CONFIG_WIFI_WPA3_PASSWORD_ID
#endif
#if CONFIG_WIFI_AUTH_OPEN
#define CONFIG_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_WIFI_AUTH_WPA2_PSK
#define CONFIG_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_WIFI_AUTH_WPA_WPA2_PSK
#define CONFIG_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_WIFI_AUTH_WPA3_PSK
#define CONFIG_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_WIFI_AUTH_WPA2_WPA3_PSK
#define CONFIG_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_WIFI_AUTH_WAPI_PSK
#define CONFIG_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif


#define RETURN_ON_ERR(retval, logtxt) {\
    if(retval != ESP_OK) {\
        ESP_LOGE(TAG, "logtxt");\
        return retval;\
    }\
}


static esp_netif_t *s_wifi_netif = NULL;
static EventGroupHandle_t s_wifi_event_group = NULL;

// Tag for debug messages
static const char *TAG = "wifi";


static void wifi_event_handler (
    void * arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
) {

    switch(event_id){
        case WIFI_EVENT_STA_START:
            if(s_wifi_netif != NULL){
                // (s3.3) Connect to WiFi
                ESP_LOGI(TAG, "Connecting to %s...", CONFIG_WIFI_SSID);
                if (esp_wifi_connect()!= ESP_OK) {
                    ESP_LOGE(TAG, "Failed to connect to WiFi");
                }
            }
            break;

        case WIFI_EVENT_STA_CONNECTED:
            // Make sure we have a valid interface handle
            if (s_wifi_netif == NULL) {
                ESP_LOGE(TAG, "WiFi not started: interface handle is NULL");
                return;
            }

            // Print AP information
            wifi_event_sta_connected_t *event_sta_connected = (wifi_event_sta_connected_t *) event_data;
            ESP_LOGI(TAG, "Connected to AP");
            ESP_LOGI(TAG, "  SSID: %s", (char *)event_sta_connected->ssid);
            ESP_LOGI(TAG, "  Channel: %d", event_sta_connected->channel);
            ESP_LOGI(TAG, "  Auth mode: %d", event_sta_connected->authmode);
            ESP_LOGI(TAG, "  AID: %d", event_sta_connected->aid);
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            break;


        case WIFI_EVENT_STA_DISCONNECTED:
            // Make sure we have a valid interface handle
            if (s_wifi_netif == NULL) {
                ESP_LOGE(TAG, "WiFi not started: interface handle is NULL");
                return;
            }
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            ESP_LOGI(TAG, "WiFi disconnected");
#if CONFIG_WIFI_AUTO_RECONNECT
            ESP_LOGI(TAG, "Attempting to reconnect...");
            wifi_reconnect();
#endif
            break;

        case WIFI_EVENT_STA_STOP:
        default:
            ESP_LOGI(TAG, "Unhandled Wifi event: %li", event_id);
            break;
    }
}


static void ip_event_handler (
    void * arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
) {
    switch(event_id){
        case IP_EVENT_STA_GOT_IP:
            if (s_wifi_netif == NULL) {
                ESP_LOGE(TAG, "On obtain IPv4 addr: Interface handle is NULL");
                return;
            }
            xEventGroupSetBits(s_wifi_event_group, WIFI_IPV4_OBTAINED_BIT);

            ip_event_got_ip_t *event_ip = (ip_event_got_ip_t *)event_data;
            esp_netif_ip_info_t *ip_info = &event_ip->ip_info;
            ESP_LOGI(TAG, "WiFi IPv4 address obtained");
            ESP_LOGI(TAG, "  IP address: " IPSTR, IP2STR(&ip_info->ip));
            ESP_LOGI(TAG, "  Netmask: " IPSTR, IP2STR(&ip_info->netmask));
            ESP_LOGI(TAG, "  Gateway: " IPSTR, IP2STR(&ip_info->gw));

            break;
        case IP_EVENT_GOT_IP6:
            if (s_wifi_netif == NULL) {
                ESP_LOGE(TAG, "On obtain IPv6 addr: Interface handle is NULL");
                return;
            }
            xEventGroupSetBits(s_wifi_event_group, WIFI_IPV6_OBTAINED_BIT);

            ip_event_got_ip6_t *event_ipv6 = (ip_event_got_ip6_t *)event_data;
            esp_netif_ip6_info_t *ip6_info = &event_ipv6->ip6_info;
            ESP_LOGI(TAG, "Ethernet IPv6 address obtained");
            ESP_LOGI(TAG, "  IP address: " IPV6STR, IPV62STR(ip6_info->ip));

            break;

        // Lost IP address
        case IP_EVENT_STA_LOST_IP:
            xEventGroupClearBits(s_wifi_event_group, WIFI_IPV4_OBTAINED_BIT);
            ESP_LOGI(TAG, "WiFi lost IP address");
            break;

        default:
            ESP_LOGI(TAG, "Unhandled IP event: %li", event_id);
        break;
    }
}





esp_err_t wifi_init(EventGroupHandle_t event_group) {
    esp_err_t ret;

    // Save the event group handle
    if (event_group != NULL) {
        s_wifi_event_group = event_group;
    }

    if (s_wifi_event_group == NULL) { //create a event group
        ESP_LOGE(TAG, "Event group handle is NULL");
        return ESP_FAIL;
    }


    s_wifi_netif = esp_netif_create_default_wifi_sta();

    if(s_wifi_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create wifi network interface");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    RETURN_ON_ERR(ret, "Failed to initialize wifi");

    //create handlers

    ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START, &wifi_event_handler, NULL);
    RETURN_ON_ERR(ret, "Failed to set handler for WIFI_EVENT_STA_START");
    ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &wifi_event_handler, NULL);
    RETURN_ON_ERR(ret, "Failed to set handler for WIFI_EVENT_STA_CONNECTED");
    ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_event_handler, NULL);
    RETURN_ON_ERR(ret, "Failed to set handler for WIFI_EVENT_STA_DISCONNECTED");


    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL);
    RETURN_ON_ERR(ret, "Failed to set handler for IP_EVENT_STA_GOT_IP");
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &ip_event_handler, NULL);
    RETURN_ON_ERR(ret, "Failed to set handler for IP_EVENT_GOT_IP6");

    //set up wifi connection config

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    RETURN_ON_ERR(ret, "Failed to set wifi mode");

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .threshold.authmode = CONFIG_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = CONFIG_WIFI_SAE_MODE,
            .sae_h2e_identifier = CONFIG_WIFI_H2E_IDENTIFIER,
        },
    };

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    RETURN_ON_ERR(ret, "Failed to configure wifi");

    ret = esp_wifi_start();
    RETURN_ON_ERR(ret, "Failed to start wifi interface");

    return ESP_OK;
}


esp_err_t wifi_stop() {
    esp_err_t ret;

    ESP_LOGI(TAG, "Stopping WiFi...");

    ret = esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_START, &wifi_event_handler);
    RETURN_ON_ERR(ret, "Failed to unset handler for WIFI_EVENT_STA_START");
    ret = esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &wifi_event_handler);
    RETURN_ON_ERR(ret, "Failed to unset handler for WIFI_EVENT_STA_CONNECTED");
    ret = esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_event_handler);
    RETURN_ON_ERR(ret, "Failed to unset handler for WIFI_EVENT_STA_DISCONNECTED");


    ret = esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler);
    RETURN_ON_ERR(ret, "Failed to unset handler for IP_EVENT_STA_GOT_IP");
    ret = esp_event_handler_unregister(IP_EVENT, IP_EVENT_GOT_IP6, &ip_event_handler);
    RETURN_ON_ERR(ret, "Failed to unset handler for IP_EVENT_GOT_IP6");

    if (s_wifi_event_group != NULL) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupClearBits(s_wifi_event_group, WIFI_IPV4_OBTAINED_BIT);
        xEventGroupClearBits(s_wifi_event_group, WIFI_IPV6_OBTAINED_BIT);
    }

    ret = esp_wifi_disconnect();
    RETURN_ON_ERR(ret, "Failed to disconnect wifi");
    ret = esp_wifi_stop();
    RETURN_ON_ERR(ret, "Failed to stop wifi");
    ret = esp_wifi_deinit();
    RETURN_ON_ERR(ret, "Failed to deinitialize wifi");

    // Destroy network interface
    if (s_wifi_netif != NULL) {
        esp_netif_destroy(s_wifi_netif);
        s_wifi_netif = NULL;
    }

    // Print message
    ESP_LOGI(TAG, "WiFi stopped");

    return ESP_OK;

}


// Attempt reconnection to WiFi
esp_err_t wifi_reconnect(void)
{
    esp_err_t ret;

    // Stop WiFi
    ret = wifi_stop();
    RETURN_ON_ERR(ret, "Failed to stop wifi during reconnect");
    // Start WiFi
    ret = wifi_init(NULL);
    RETURN_ON_ERR(ret, "Failed to start wifi during reconnect");

    return ESP_OK;
}