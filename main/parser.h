#ifndef PARSER_H
#define PARSER_H
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/**
 * @brief Event group bits for parser events
 */
#define PARSER_TELEGRAM_READY_BIT   BIT0
#define PARSER_ERROR_BIT            BIT1
#define PARSER_CRC_ERROR_BIT        BIT2

#define P1_TIMESTAMP_SIZE 13

typedef struct {
    char timestamp[P1_TIMESTAMP_SIZE + 1];
    double energy_consumed_t1;
    double energy_consumed_t2;
    double energy_produced_t1;
    double energy_produced_t2;
    uint16_t tariff;
    double power_consumed;
    double power_produced;

    uint32_t num_power_failures;
    uint32_t num_long_power_failures;
    uint32_t num_voltage_sags_l1;
    uint32_t num_voltage_sags_l2;
    uint32_t num_voltage_sags_l3;
    uint32_t num_voltage_swells_l1;
    uint32_t num_voltage_swells_l2;
    uint32_t num_voltage_swells_l3;

    double voltage_l1;
    double voltage_l2;
    double voltage_l3;

    uint32_t current_l1;
    uint32_t current_l2;
    uint32_t current_l3;

    double active_power_consumed_l1;
    double active_power_consumed_l2;
    double active_power_consumed_l3;

    double active_power_produced_l1;
    double active_power_produced_l2;
    double active_power_produced_l3;
} p1_telegram_t;



/**
 * This function should be called to initialize the parser context
 * and before parser_process_data is ever called
 */
void parser_init(EventGroupHandle_t event_group);

/**
 * This function will consume all data in the given buffer and
 * parse the P1 telegram inside of it. Because the size of the P1 telegram is not known upfront, there
 * could be a partial or even more than one P1 telegram in the data.
 *
 * In order to use this function, the parser should have been initalized first together with a event group.
 * When a P1 telegram is parsed, the PARSER_TELEGRAM_READY_BIT will be set and the parsed result could be retreived.
 * if a parse error or CRC error occur, the corrseponding PARSER_ERROR_BIT or PARSER_CRC_ERROR_BIT will be set.
 *
 * This function is intended to run asynchronously in a separate task than the task that consumes the P1 telegram.
 * Depending on the size of the buffer and the size of the telegram, a single call of this function could result in multiple P1 telegrams that are parsed.
 * The task that consumes these telegrams should therefor run at a higer priority.
 *
 * When the next chunk of data is received, this function should be called again.
 * If the prevous call contained a partial P1 telegramn, the parser will then contiue with parsing the rest this telegram
 */
void parser_process_data(char* buffer, uint32_t buffer_len);

/**
 * this function will return the latest parsed p1 telegram.
 * This function should be called only after the PARSER_TELEGRAM_READY event has been transmitted
 */
p1_telegram_t parser_get_result();

#endif // PARSER_H