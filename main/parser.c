#include "parser.h"
#include "esp_log.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Tag for debug messages
static const char *TAG = "parser";

typedef enum {
    WAIT_FOR_START,
    ID_LINE_START,
    ID_LINE,
    EMPTY_LINE,
    DATA_LINE_START,
    DATA_LINE_OBIS,
    DATA_LINE_VALUE,
    DATA_SKIP_TO_END,
    CRC
} parser_state_t;


#define SYMBOL_BUFF_LEN 15

typedef enum {
    DATA_TYPE_INT,
    DATA_TYPE_DECIMAL,
    DATA_TYPE_OCTET_STRING,
    DATA_TYPE_TIMESTAMP,
} value_data_type_t;


// Forward declarations
typedef struct value_type_def_t value_type_def_t;
typedef struct parser_context_t parser_context_t;

struct value_type_def_t{
    char* obis;
    uint16_t len;
    uint8_t num_integral;
    uint8_t num_decimals;
    size_t p1_telegram_offset;
    void (*handler)(parser_context_t*, value_type_def_t const*);

};

struct parser_context_t{
    p1_telegram_t temp;
    p1_telegram_t result;
    uint32_t buffer_len;
    char* buffer;
    uint32_t buffer_pos;
    parser_state_t state;
    uint32_t line_pos;
    char symbol_buff[SYMBOL_BUFF_LEN + 1];
    uint32_t symbol_pos;
    value_type_def_t const *line_type;
    EventGroupHandle_t event_group;
    uint16_t crc;
};


static void handle_uint32(parser_context_t*, value_type_def_t const*);
static void handle_double(parser_context_t*, value_type_def_t const*);
static void handle_octet_string(parser_context_t*, value_type_def_t const*);
static void handle_timestamp(parser_context_t*, value_type_def_t const*);

static bool wait_for_start(parser_context_t *context);
static bool id_line_start(parser_context_t *context);
static bool id_line(parser_context_t *context);
static bool empty_line(parser_context_t *context);
static bool empty_line(parser_context_t *context);
static bool data_line_start(parser_context_t *context);
static bool data_line_obis(parser_context_t *context);
static bool data_line_value(parser_context_t *context);
static bool data_skip_to_end(parser_context_t *context);
static bool verify_crc(parser_context_t *context);

static bool parse_chunk(parser_context_t *context);
static bool skip_until(char check, parser_context_t *context);
static bool skip_until_with_crc(char check, parser_context_t *context);
static bool skip_with_crc(char amount, parser_context_t *context);
static inline void change_state(parser_context_t *context, parser_state_t state);
static inline void reset_crc(parser_context_t *context);
static void update_crc(parser_context_t *context);
static bool check_crc(parser_context_t *context);
static inline void parser_error(parser_context_t *context);
static inline bool is_end_of_chunk(parser_context_t *context);
static value_type_def_t const *parse_obis(parser_context_t *context);

static parser_context_t s_context;


static const value_type_def_t s_value_type_def[] = {
    { "0-0:1.0.0", P1_TIMESTAMP_SIZE, P1_TIMESTAMP_SIZE, 0, offsetof(p1_telegram_t, timestamp), handle_timestamp },
    { "1-0:1.8.1", 10, 6, 3,  offsetof(p1_telegram_t, energy_consumed_t1), handle_double },
    { "1-0:1.8.2", 10, 6, 3,  offsetof(p1_telegram_t, energy_consumed_t2), handle_double },
    { "1-0:2.8.1", 10, 6, 3,  offsetof(p1_telegram_t, energy_produced_t1), handle_double },
    { "1-0:2.8.2", 10, 6, 3,  offsetof(p1_telegram_t, energy_produced_t2), handle_double },
    { "0-0:96.14.0", 4, 4, 0,  offsetof(p1_telegram_t, tariff), handle_octet_string },
    { "1-0:1.7.0", 6, 2, 3,  offsetof(p1_telegram_t, power_consumed), handle_double },
    { "1-0:2.7.0", 6, 2, 3,  offsetof(p1_telegram_t, power_produced), handle_double },
    { "0-0:96.7.21", 5, 5, 0,  offsetof(p1_telegram_t, num_power_failures), handle_uint32 },
    { "0-0:96.7.9", 5, 5, 0,  offsetof(p1_telegram_t, num_long_power_failures), handle_uint32 },
    { "1-0:32.32.0", 5, 5, 0,  offsetof(p1_telegram_t, num_voltage_sags_l1), handle_uint32 },
    { "1-0:52.32.0", 5, 5, 0,  offsetof(p1_telegram_t, num_voltage_sags_l2), handle_uint32 },
    { "1-0:72.32.0", 5, 5, 0,  offsetof(p1_telegram_t, num_voltage_sags_l3), handle_uint32 },
    { "1-0:32.36.0", 5, 5, 0,  offsetof(p1_telegram_t, num_voltage_swells_l1), handle_uint32 },
    { "1-0:52.36.0", 5, 5, 0,  offsetof(p1_telegram_t, num_voltage_swells_l2), handle_uint32 },
    { "1-0:72.36.0", 5, 5, 0,  offsetof(p1_telegram_t, num_voltage_swells_l3), handle_uint32 },
    { "1-0:32.7.0", 5, 3, 1,  offsetof(p1_telegram_t, voltage_l1), handle_double },
    { "1-0:35.7.0", 5, 3, 1,  offsetof(p1_telegram_t, voltage_l2), handle_double },
    { "1-0:75.7.0", 5, 3, 1,  offsetof(p1_telegram_t, voltage_l3), handle_double },
    { "1-0:31.7.0", 3, 3, 0,  offsetof(p1_telegram_t, current_l1), handle_uint32 },
    { "1-0:51.7.0", 3, 3, 0,  offsetof(p1_telegram_t, current_l2), handle_uint32 },
    { "1-0:71.7.0", 3, 3, 0,  offsetof(p1_telegram_t, current_l3), handle_uint32 },
    { "1-0:21.7.0", 6, 2, 3,  offsetof(p1_telegram_t, active_power_consumed_l1), handle_double },
    { "1-0:41.7.0", 6, 2, 3,  offsetof(p1_telegram_t, active_power_consumed_l2), handle_double },
    { "1-0:61.7.0", 6, 2, 3,  offsetof(p1_telegram_t, active_power_consumed_l3), handle_double },
    { "1-0:22.7.0", 6, 2, 3,  offsetof(p1_telegram_t, active_power_produced_l1), handle_double },
    { "1-0:42.7.0", 6, 2, 3,  offsetof(p1_telegram_t, active_power_produced_l2), handle_double },
    { "1-0:62.7.0", 6, 2, 3,  offsetof(p1_telegram_t, active_power_produced_l3), handle_double }
};

#define S_VALUE_TYPE_DEF_LENGHT (sizeof(s_value_type_def) / sizeof(s_value_type_def[0]))

/**
 * This function should be called to initialize the parser context
 * and before parser_process_data is ever called
 */
void parser_init(EventGroupHandle_t event_group) {
    s_context.buffer_len = 0;
    s_context.buffer = NULL;
    s_context.buffer_pos = 0;
    s_context.state = WAIT_FOR_START;
    s_context.line_pos = 0;
    s_context.symbol_pos = 0;
    memset(s_context.symbol_buff, 0, SYMBOL_BUFF_LEN + 1);
    s_context.line_type = NULL;
    s_context.event_group = event_group;
    s_context.crc = 0;

    xEventGroupClearBits(s_context.event_group, PARSER_TELEGRAM_READY_BIT | PARSER_ERROR_BIT | PARSER_CRC_ERROR_BIT);
}

/**
 * This functiom will consume all data in the given buffer and
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
void parser_process_data(char* buffer, uint32_t buffer_len) {
    parser_context_t *context = &s_context;
    context->buffer_len = buffer_len;
    context->buffer_pos = 0;
    context->buffer = buffer;
    bool repeat;
    do {
        repeat = parse_chunk(context);
    } while (repeat);
}

/**
 * this function will return the latest parsed p1 telegram.
 * This function should be called only after the PARSER_TELEGRAM_READY event has been transmitted
 */
p1_telegram_t parser_get_result() {
    return s_context.result;
};

/**
 * This function will select the right parsing function depending on the parser state.
 *
 * Arguments:
 *  - parser_context_t *context: The parser context containing the buffer state and other variables that should be preserved across calls.
 *
 * returns:
 *  - true whenever the parser state is changed, and should be called again to continue with the next state.
 *  - false when all data in the buffer has been consumend and the parser should wait for the next chunk.
 */
static bool parse_chunk(parser_context_t *context){
    switch(context->state){
        case WAIT_FOR_START:
            return wait_for_start(context);
        case ID_LINE_START:
            return id_line_start(context);
        case ID_LINE:
            return id_line(context);
        case EMPTY_LINE:
            return empty_line(context);
        case DATA_LINE_START:
            return data_line_start(context);
        case DATA_LINE_OBIS:
            return data_line_obis(context);
        case DATA_LINE_VALUE:
            return data_line_value(context);
        case DATA_SKIP_TO_END:
            return data_skip_to_end(context);
        case CRC:
            return verify_crc(context);
        default:
            parser_error(context);
    }

    return true;
}

static bool wait_for_start(parser_context_t *context) {
    if(!skip_until('/', context)){
        return false;
    }
    reset_crc(context);
    context->buffer_pos++;  //consume the '/' symbol
    change_state(context, ID_LINE_START);
    return true;
}


static bool id_line_start(parser_context_t *context) {
    //skip the first 3 characters
    if(!skip_with_crc(3, context)){
        return false;
    }
    //check if there is data left in this chunk
    if(is_end_of_chunk(context)){
        return false;
    }

    //4th character should be a 5 indicating DSMR version 5
    if(context->buffer[context->buffer_pos] != '5'){
        parser_error(context);
        return true;
    }

    update_crc(context);
    context->buffer_pos++;  //consume the '5'
    change_state(context, ID_LINE);
    return true;
}


static bool id_line(parser_context_t *context) {
    //we skip everything until the line ending.
    //when symbol_pos is still 0, this means we did not find the \r yet
    if(context->symbol_pos == 0){
        if(!skip_until_with_crc('\r', context)){
            return false;
        }
        context->symbol_pos = 1;
        context->buffer_pos++;
    }
    //check if there is data left in this chunk
    if(is_end_of_chunk(context)){
        return false;
    }

    //if we reach here, the context->symbol_pos will be 1, and the next character should be a newline
    if(context->buffer[context->buffer_pos] != '\n'){
        parser_error(context);
        return true;
    }

    update_crc(context);
    context->buffer_pos++;
    change_state(context, EMPTY_LINE);
    return true;
}

static bool empty_line(parser_context_t *context) {
    //when symbol_pos is still 0, this means we did not find the \r yet
    if(context->symbol_pos == 0){
        if(is_end_of_chunk(context)){
            return false;
        }
        if(context->buffer[context->buffer_pos] != '\r'){
            parser_error(context);
            return true;
        }
        update_crc(context);
        context->symbol_pos = 1;
        context->buffer_pos++;
    }
    if(is_end_of_chunk(context)){
        return false;
    }

    //if we reach here, the context->symbol_pos will be be 1, and the next character should be a newline
    if(context->buffer[context->buffer_pos] != '\n'){
        parser_error(context);
        return true;
    }

    update_crc(context);
    context->buffer_pos++;
    change_state(context, DATA_LINE_START);
    return true;
}

static bool data_line_start(parser_context_t *context) {
    if(is_end_of_chunk(context)){
        return false;
    }
    //check if this is the end of the telegram
    if(context->buffer[context->buffer_pos] == '!') {
        update_crc(context);
        context->buffer_pos++;
        change_state(context, CRC);
        return true;
    }
    //this is not the end of the telegram, continue with reading obis code
    change_state(context, DATA_LINE_OBIS);
    return true;
}

static bool data_line_obis(parser_context_t *context) {
    //fill symbol buffer with obis code until it is full, end of obis is found or line ends
    while (
        context->symbol_pos < SYMBOL_BUFF_LEN
        && context->buffer_pos < context->buffer_len
    ){
        //if we reach a \r or \n, something must have been wrong for sure
        if(
            context->buffer[context->buffer_pos] == '\r'
            || context->buffer[context->buffer_pos] == '\n'
        ){
            parser_error(context);
            return true;
        }
        update_crc(context);

        //if we reach a '(' char, the obis code ends and data starts.
        if(context->buffer[context->buffer_pos]  == '('){
            //parse obis code and find right data type
            value_type_def_t const* type = parse_obis(context);

            //consume the '(' symbol
            context->buffer_pos++;
            //if obis code is unknown or not wanted, skip the line
            if(type == NULL){
                change_state(context, DATA_SKIP_TO_END);
                return true;
            }

            context->line_type = type;
            change_state(context, DATA_LINE_VALUE);
            return true;
        }

        //if we reach here, the end of obis code is still not found. Store the character in symbol buffer
        context->symbol_buff[context->symbol_pos] = context->buffer[context->buffer_pos];

        context->symbol_pos++;
        context->buffer_pos++;
    }
    //if we reach here, the obis code is not fully found, either because the chunk ended or the symbol buffer is full
    if(is_end_of_chunk(context)){
        return false;
    }

    //the symbol buffer is full, so something must have been wrong
    parser_error(context);
    return true;
}
static bool data_line_value(parser_context_t *context) {
    //fill symbol buffer with data until the required length is reached or the chunk ended
    while (
        context->symbol_pos < context->line_type->len
        && context->buffer_pos < context->buffer_len
    ){
        //if we reach end of line indicating the end of value before we consumed the required length, parse error
        if(
            context->buffer[context->buffer_pos] == '\r'
            || context->buffer[context->buffer_pos] == '\n'
        ){
            parser_error(context);
            return true;
        }

        //save character in symbol buffer
        context->symbol_buff[context->symbol_pos] = context->buffer[context->buffer_pos];
        update_crc(context);
        context->symbol_pos++;
        context->buffer_pos++;
    }

    //if we reach here, either the full symbol buffered, or the end of chunk is reached
    if(is_end_of_chunk(context)){
        return false;
    }

    //here will parse the saved data. we will add a null character so the buffer can be read as a string
    context->symbol_buff[context->symbol_pos] = '\0';
    //call the handler
    context->line_type->handler(context, context->line_type);
    //we have parsed the value, skip rest of the line
    change_state(context, DATA_SKIP_TO_END);
    return true;
}
static bool data_skip_to_end(parser_context_t *context) {
    //check if we did not find a \r already
    if(context->symbol_pos == 0){
        if(!skip_until_with_crc('\r', context)){
            return false;
        }
        context->symbol_pos = 1;
        context->buffer_pos++;
    }

    if(is_end_of_chunk(context)){
        return false;
    }

    //here symbol_pos should be always 1, so we should expect a linefeed
    if(context->buffer[context->buffer_pos] != '\n'){
        parser_error(context);
        return true;
    }

    update_crc(context);
    context->buffer_pos++;
    change_state(context, DATA_LINE_START);
    return true;
}
static bool verify_crc(parser_context_t *context) {
    //save 4 crc characters into symbol buffer
    while(
        context->symbol_pos < 4 &&
        context->buffer_pos < context->buffer_len
    ){
        context->symbol_buff[context->symbol_pos] = context->buffer[context->buffer_pos];
        context->symbol_pos++;
        context->buffer_pos++;
    }

    //when we reach here, either we reach end of chunc or saved 4 crc hex characters
    if(context->symbol_pos < 4){
        return false;
    }

    context->symbol_buff[context->symbol_pos] = '\0';
    if(!check_crc(context)){
        parser_error(context);
        return true;
    }
    //here the datagram is ready. copy it to right field
    context->result = context->temp;

    xEventGroupSetBits(s_context.event_group, PARSER_TELEGRAM_READY_BIT);

    change_state(context, WAIT_FOR_START);
    return true;
}

static bool skip_until(char check, parser_context_t *context) {
    while(context->buffer_pos < context->buffer_len){
        if(context->buffer[context->buffer_pos] == check){
            return true;
        }
        context->buffer_pos++;
    }
    return false;
}

static bool skip_until_with_crc(char check, parser_context_t *context) {
    while(context->buffer_pos < context->buffer_len){
        update_crc(context);
        if(context->buffer[context->buffer_pos] == check){
            return true;
        }
        context->buffer_pos++;
    }
    return false;
}

static bool skip_with_crc(char amount, parser_context_t *context) {

    for(;
        context->symbol_pos < amount && context->buffer_pos < context->buffer_len;
        context->buffer_pos++, context->symbol_pos++
    ){
        update_crc(context);
    }
    return context->symbol_pos >= amount;
}

static inline void change_state(parser_context_t *context, parser_state_t state){
    context->line_pos = 0;
    context->symbol_pos = 0;
    context->state = state;
}


static inline void reset_crc(parser_context_t *context){
    context->crc = 0;
    update_crc(context);
}

static void update_crc(parser_context_t *context){
    uint8_t input = (uint8_t) context->buffer[context->buffer_pos];

    context->crc ^= input;

    for(int i = 0; i < 8; i++)
    {
        if(context->crc & 1)
            context->crc = (context->crc >> 1) ^ 0xA001;
        else
            context->crc >>= 1;
    }
}

static bool check_crc(parser_context_t *context){
    uint16_t received_crc = (uint16_t) strtoul(context->symbol_buff, NULL, 16);

    ESP_LOGD(TAG, "CRC received %s, parsed %d, computed %x", context->symbol_buff, received_crc, context->crc);
    return context->crc == received_crc;
}

static inline void parser_error(parser_context_t *context){
    ESP_LOGE("parser", "Parser error. state: %d", context->state);
    change_state(context, WAIT_FOR_START);
    xEventGroupSetBits(s_context.event_group, PARSER_ERROR_BIT);
}

static inline void parser_crc_error(parser_context_t *context){
    ESP_LOGE("parser", "Parser crc error.");
    change_state(context, WAIT_FOR_START);
    xEventGroupSetBits(s_context.event_group, PARSER_CRC_ERROR_BIT);
}

static inline bool is_end_of_chunk(parser_context_t *context){
    return context->buffer_pos >= context->buffer_len;
}

static void handle_uint32(parser_context_t *context, value_type_def_t const *value_def) {
    uint8_t* base = (uint8_t*) &context->temp;
    uint32_t* destination = (uint32_t*) &base[value_def->p1_telegram_offset];

    *destination = strtoul(context->symbol_buff, NULL, 10);
}
static void handle_double(parser_context_t *context, value_type_def_t const *value_def) {
    uint8_t* base = (uint8_t*) &context->temp;
    double* destination = (double*) &base[value_def->p1_telegram_offset];

    *destination = strtod(context->symbol_buff, NULL);
}
static void handle_octet_string(parser_context_t *context, value_type_def_t const *value_def) {
    uint8_t* base = (uint8_t*) &context->temp;
    uint16_t* destination = (uint16_t*) &base[value_def->p1_telegram_offset];

    *destination = strtoul(context->symbol_buff, NULL, 16);
}
static void handle_timestamp(parser_context_t *context, value_type_def_t const *value_def) {
    uint8_t* base = (uint8_t*) &context->temp;
    char* destination = (char*) &base[value_def->p1_telegram_offset];

    strcpy(destination, context->symbol_buff);
}

static value_type_def_t const *parse_obis(parser_context_t *context){

    if(context->symbol_pos < SYMBOL_BUFF_LEN){
        context->symbol_buff[context->symbol_pos] = '\0';
    }

    for(int i = 0; i < S_VALUE_TYPE_DEF_LENGHT; ++i){
        if(!strcmp(s_value_type_def[i].obis, context->symbol_buff)){
            return &s_value_type_def[i];
        }
    }
    return NULL;
}