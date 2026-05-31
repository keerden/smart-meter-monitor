#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "string.h"



#include "driver/uart.h"
#include "driver/gpio.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include <sys/socket.h>

#include "wifi.h"
#include "parser.h"

#include <cJSON.h>

#define UART_PORT_NUM UART_NUM_1
#define UART_RX_BUFF_SIZE (129)
#define UART_RX_PIN 18



EventGroupHandle_t parser_event_group;
static const uint64_t connection_timeout_ms = 10000;
char *tag = "main";


void start_network(EventGroupHandle_t* network_event_group){
    EventBits_t network_event_bits;

    // Initialize TCP/IP network interface  and event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init(*network_event_group);

    // Wait for network to connect
    ESP_LOGI(tag, "Waiting for network to connect...");
    network_event_bits = xEventGroupWaitBits(
        *network_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(connection_timeout_ms)
    );

    if (network_event_bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(tag, "Connected to WiFi network");
    } else {
        ESP_LOGE(tag, "Failed to connect to network");
        abort();
    }

    // Wait for IP address
    ESP_LOGI(tag, "Waiting for IP address...");
    network_event_bits = xEventGroupWaitBits(
        *network_event_group,
        (WIFI_IPV4_OBTAINED_BIT),
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(connection_timeout_ms)
    );
    if (network_event_bits & WIFI_IPV4_OBTAINED_BIT) {
        ESP_LOGI(tag, "Connected to IPv4 network");
    } else {
        ESP_LOGE(tag, "Failed to obtain IP address");
        abort();
    }
}

static void uart_task(void *arg)
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, 3 * UART_RX_BUFF_SIZE, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_PIN_NO_CHANGE, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    const int used_buffer_size = UART_RX_BUFF_SIZE;

    // Configure a temporary buffer for the incoming data
    char *data = (char *) malloc(used_buffer_size);
    ESP_LOGI(tag, "UART initialized");
    while (1) {
        // Read data from the UART
        int len = uart_read_bytes(1, data, (used_buffer_size - 1), 100 / portTICK_PERIOD_MS);

        if (len) {
            //data[len] = '\0';
            //ESP_LOGI(tag, "Recv str: %s", (char *) data);
            parser_process_data(data, len);
        }
    }
}


cJSON *get_p1_json(p1_telegram_t *telegram) {
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "timestamp", telegram->timestamp);

    cJSON_AddNumberToObject(result, "energy_consumed_t1", telegram->energy_consumed_t1);
    cJSON_AddNumberToObject(result, "energy_consumed_t2", telegram->energy_consumed_t2);
    cJSON_AddNumberToObject(result, "energy_produced_t1", telegram->energy_produced_t1);
    cJSON_AddNumberToObject(result, "energy_produced_t2", telegram->energy_produced_t2);

    cJSON_AddNumberToObject(result, "tariff", telegram->tariff);

    cJSON_AddNumberToObject(result, "power_consumed", telegram->power_consumed);
    cJSON_AddNumberToObject(result, "power_produced", telegram->power_produced);

    cJSON_AddNumberToObject(result, "num_power_failures", telegram->num_power_failures);
    cJSON_AddNumberToObject(result, "num_long_power_failures", telegram->num_long_power_failures);
    cJSON_AddNumberToObject(result, "num_voltage_sags_l1", telegram->num_voltage_sags_l1);
    cJSON_AddNumberToObject(result, "num_voltage_sags_l2", telegram->num_voltage_sags_l2);
    cJSON_AddNumberToObject(result, "num_voltage_sags_l3", telegram->num_voltage_sags_l3);
    cJSON_AddNumberToObject(result, "num_voltage_swells_l1", telegram->num_voltage_swells_l1);
    cJSON_AddNumberToObject(result, "num_voltage_swells_l2", telegram->num_voltage_swells_l2);
    cJSON_AddNumberToObject(result, "num_voltage_swells_l3", telegram->num_voltage_swells_l3);

    cJSON_AddNumberToObject(result, "voltage_l1", telegram->voltage_l1);
    cJSON_AddNumberToObject(result, "voltage_l2", telegram->voltage_l2);
    cJSON_AddNumberToObject(result, "voltage_l3", telegram->voltage_l3);

    cJSON_AddNumberToObject(result, "current_l1", telegram->current_l1);
    cJSON_AddNumberToObject(result, "current_l2", telegram->current_l2);
    cJSON_AddNumberToObject(result, "current_l3", telegram->current_l3);

    cJSON_AddNumberToObject(result, "active_power_consumed_l1", telegram->active_power_consumed_l1);
    cJSON_AddNumberToObject(result, "active_power_consumed_l2", telegram->active_power_consumed_l2);
    cJSON_AddNumberToObject(result, "active_power_consumed_l3", telegram->active_power_consumed_l3);

    cJSON_AddNumberToObject(result, "active_power_produced_l1", telegram->active_power_produced_l1);
    cJSON_AddNumberToObject(result, "active_power_produced_l2", telegram->active_power_produced_l2);
    cJSON_AddNumberToObject(result, "active_power_produced_l3", telegram->active_power_produced_l3);
    return result;
}

static void meter_task(void *arg)
{
    EventBits_t parser_event_bits;

    esp_http_client_config_t http_config = {
        .url = CONFIG_HTTP_P1_DATA_URL,
        .timeout_ms = 500,
    };

    esp_http_client_handle_t http_client =  esp_http_client_init(&http_config);

    while(1){
        ESP_LOGI(tag, "meter task: waiting for P1 telegram");
        parser_event_bits = xEventGroupWaitBits(
            parser_event_group,
            PARSER_TELEGRAM_READY_BIT | PARSER_ERROR_BIT | PARSER_CRC_ERROR_BIT,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY
        );

        if(parser_event_bits & PARSER_TELEGRAM_READY_BIT) {
            ESP_LOGI(tag, "meter task: P1 telegram ready");

            p1_telegram_t telegram = parser_get_result();
            cJSON *p1_json = get_p1_json(&telegram);

            esp_http_client_set_method(http_client, HTTP_METHOD_POST);
            esp_http_client_set_header(
                http_client,
                "Content-Type",
                "application/json"
            );

            char *json_str = cJSON_Print(p1_json);
            esp_http_client_set_post_field(
                http_client,
                json_str,
                strlen(json_str)
            );

            esp_err_t http_err = esp_http_client_perform(http_client);

            if (http_err == ESP_OK) {
                int status = esp_http_client_get_status_code(http_client);

                if (status >= 200 && status < 300) {
                    ESP_LOGI(tag, "HTTP OK (%d)", status);
                } else {
                    ESP_LOGW(tag, "HTTP POST failed (%d)", status);
                }
            } else {
                ESP_LOGE(tag, "HTTP request failed: %s", esp_err_to_name(http_err));
            }

            cJSON_Delete(p1_json);
            free(json_str);
        } else if(parser_event_bits & PARSER_ERROR_BIT) {
            ESP_LOGI(tag, "meter task: parse error");
        } else if(parser_event_bits & PARSER_CRC_ERROR_BIT) {
            ESP_LOGI(tag, "meter task: crc checksum error");
        } else {
            ESP_LOGI(tag, "meter task: timeout");
        }

    }

    esp_http_client_cleanup(http_client);
}

void app_main(void)
{
    tag = pcTaskGetName(NULL);
    EventGroupHandle_t network_event_group;

    // Initialize event group
    network_event_group = xEventGroupCreate();

    ESP_LOGI(tag, "Starting up!\n");

    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 1.OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // 2.NVS partition contains data in new format and cannot be recognized by this version of code.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(tag, "NVM initialized.\n");

    start_network(&network_event_group);

    //TODO:
    // start_updater();
    // start_web_interface();


    //init parser
    parser_event_group = xEventGroupCreate();
    parser_init(parser_event_group);

    xTaskCreate(uart_task, "uart task", 3072, NULL, 1, NULL);
    xTaskCreate(meter_task, "meter task", 3072, NULL, 2, NULL);

    ESP_LOGI(tag, "Started.\n");

    gpio_reset_pin(15);
    gpio_set_direction(15 , GPIO_MODE_OUTPUT);



    while(1){
        gpio_set_level(15, 1);
        vTaskDelay(50);
        gpio_set_level(15, 0);
        vTaskDelay(50);
    }
}



