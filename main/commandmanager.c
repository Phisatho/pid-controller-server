//
//  commandmanager.c
//  pid-controller-server
//
//  Created by Andrey Chufyrev on 18.11.2018.
//  Copyright © 2018 Andrey Chufyrev. All rights reserved.
//

#include "commandmanager.h"


// void _print_bin_hex(unsigned char byte) {
//     printf("0b");
//     for (int i=7; i>=0; i--) {
//         if ((byte & (1<<i)) == (1<<i))
//             printf("1");
//         else
//             printf("0");
//     }
//     printf(", ");
//     printf("0x%X\n", byte);
// }


// simulate a real system when streaming data will be stored in a separate array and probably will be collected in
// another thread. Therefore in this case there should be some locking mechanism (mutex)
static float stream_values[2];


#define STREAM_BUF_SIZE (sizeof(char)+2*sizeof(float))
#define STREAM_THREAD_SLEEP_TIME_MS 20

static bool stream_run = false;

static int points_cnt = 0;

void _stream_task(void *data) {

    unsigned char stream_buf[STREAM_BUF_SIZE];
    stream_buf[0] = STREAM_PREFIX;

    double x = 0.0;
    double const dx = 0.1;

    ESP_LOGI(TAG, "Stream task started");

    while (1) {
        if (stream_run) {

            if (x > (2.0 * M_PI))
                x = 0.0;
            stream_values[0] = (float)sin(x);  // Process Variable
            stream_values[1] = (float)cos(x);  // Controller Output
            x = x + dx;

            memcpy(&stream_buf[1], stream_values, 2*sizeof(float));
            
            // datagram sockets support multiple readers/writers even simultaneously so we do not need any mutex in
            // this simple case
            sendto(sock, (const void *)stream_buf, STREAM_BUF_SIZE, 0, (const struct sockaddr *)&sourceAddr, socklen);
            // if (len < 0)
            //     error("ERROR on sendto");

            points_cnt++;
        }

        vTaskDelay(STREAM_THREAD_SLEEP_TIME_MS/portTICK_PERIOD_MS);
    }
}


void stream_start(void) {
    if (!stream_run)
        stream_run = true;
}

void stream_stop(void) {
    if (stream_run) {
        stream_run = false;

        ESP_LOGI(TAG, "points: %d", points_cnt);
        points_cnt = 0;
    }
}


/*
 *  Sample values, not constants so client can both read/write them
 */
static float setpoint = 1238.0f;
static float kP = 19.4f;
static float kI = 8.7f;
static float kD = 1.6f;
static float err_I = 2055.0f;
static float err_P_limits[2] = {-3500.0f, 3500.0f};
static float err_I_limits[2] = {-6500.0f, 6500.0f};


static const char *tag_read = "read";
static const char *tag_write = "write";

int process_request(unsigned char *request_response_buf) {
// int process_request(unsigned char *request_buf, unsigned char *response_buf) {

    int result = 0;

    /*
     *  Currently we use the same one buffer for both parsing the request and constructing the response. As
     *  corresponding bit fields are match each other we can map the real request byte to the response structure. Also,
     *  the first byte of the response buffer is the same as the first one of the request except the result field.
     *
     *  Such approach looks more messy but, guess, should be faster to execute in hardware
     */
    response_t request;
    // request_t request;
    memcpy(&request, &request_response_buf[0], sizeof(char));

    // response_t response;
    // memcpy(&response, request_buf, sizeof(char));

    // float values[2];
    // memset(values, 0, 2*sizeof(float));

    // printf("OPCODE: 0x%X\n", opcode);
    // printf("VAR CMD: 0x%X\n", var_cmd);

    if (request.opcode == OPCODE_read) {
        // 'read' request from the client - we do not need cells allocated for values (doesn't care whether they were
        // supplied or not). Instead, we will use them to return values
        memset(&request_response_buf[1], 0, 2*sizeof(float));
        
        switch (request.var_cmd) {
            case CMD_stream_stop:
                ESP_LOGI(tag_read, "CMD_stream_stop");
                stream_stop();
                result = RESULT_ok;
                break;
            case CMD_stream_start:
                ESP_LOGI(tag_read, "CMD_stream_start");
                stream_start();
                result = RESULT_ok;
                break;

            case VAR_setpoint:
                ESP_LOGI(tag_read, "VAR_setpoint");
                memcpy(&request_response_buf[1], &setpoint, sizeof(float));
                result = RESULT_ok;
                break;
            case VAR_kP:
                ESP_LOGI(tag_read, "VAR_kP");
                memcpy(&request_response_buf[1], &kP, sizeof(float));
                result = RESULT_ok;
                break;
            case VAR_kI:
                ESP_LOGI(tag_read, "VAR_kI");
                memcpy(&request_response_buf[1], &kI, sizeof(float));
                result = RESULT_ok;
                break;
            case VAR_kD:
                ESP_LOGI(tag_read, "VAR_kD");
                memcpy(&request_response_buf[1], &kD, sizeof(float));
                result = RESULT_ok;
                break;
            case VAR_err_I:
                ESP_LOGI(tag_read, "VAR_err_I");
                memcpy(&request_response_buf[1], &err_I, sizeof(float));
                result = RESULT_ok;
                break;
            case VAR_err_P_limits:
                ESP_LOGI(tag_read, "VAR_err_P_limits");
                memcpy(&request_response_buf[1], err_P_limits, 2*sizeof(float));
                result = RESULT_ok;
                break;
            case VAR_err_I_limits:
                ESP_LOGI(tag_read, "VAR_err_I_limits");
                memcpy(&request_response_buf[1], err_I_limits, 2*sizeof(float));
                result = RESULT_ok;
                break;

            case CMD_save_to_eeprom:
                ESP_LOGI(tag_read, "CMD_save_to_eeprom");
                result = RESULT_ok;
                break;

            default:
                ESP_LOGI(tag_read, "Unknown request");
                result = RESULT_error;
                break;
        }
    }
    
    else {        
        switch (request.var_cmd) {
            case VAR_setpoint:
                ESP_LOGI(tag_write, "VAR_setpoint");
                memcpy(&setpoint, &request_response_buf[1], sizeof(float));
                result = RESULT_ok;
                break;
            case VAR_kP:
                ESP_LOGI(tag_write, "VAR_kP");
                memcpy(&kP, &request_response_buf[1], sizeof(float));
                result = RESULT_ok;
                break;
            case VAR_kI:
                ESP_LOGI(tag_write, "VAR_kI");
                memcpy(&kI, &request_response_buf[1], sizeof(float));
                result = RESULT_ok;
                break;
            case VAR_kD:
                ESP_LOGI(tag_write, "VAR_kD");
                memcpy(&kD, &request_response_buf[1], sizeof(float));
                result = RESULT_ok;
                break;
            case VAR_err_I:
                ESP_LOGI(tag_write, "VAR_err_I");
                if (*(float *)&request_response_buf[1] == 0.0f) {
                    err_I = 0.0f;
                    result = RESULT_ok;
                }
                else {
                    result = RESULT_error;
                }
                break;
            case VAR_err_P_limits:
                ESP_LOGI(tag_write, "VAR_err_P_limits");
                memcpy(err_P_limits, &request_response_buf[1], 2*sizeof(float));
                result = RESULT_ok;
                break;
            case VAR_err_I_limits:
                ESP_LOGI(tag_write, "VAR_err_I_limits");
                memcpy(err_I_limits, &request_response_buf[1], 2*sizeof(float));
                result = RESULT_ok;
                break;

            default:
                ESP_LOGI(tag_write, "Unknown request");
                result = RESULT_error;
                break;
        }
        
        memset(&request_response_buf[1], 0, 2*sizeof(float));
    }

    request.result = result;
    memcpy(request_response_buf, &request, sizeof(char));
    //    response.result = result;
    //    memcpy(response_buf, &response, sizeof(char));

    return result;
}
