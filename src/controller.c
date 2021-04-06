/*
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 * SPDX-License-Identifier: MIT-0
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#define democonfigNETWORK_BUFFER_SIZE (1024U)

/* The config header is always included first. */
#include "iot_config.h"

/* Standard includes. */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Set up logging for this demo. */
#include "iot_demo_logging.h"

/* Platform layer includes. */
#include "platform/iot_clock.h"
#include "platform/iot_threads.h"

#include "semphr.h"
#include "esp_log.h"
#include "esp_event.h"

#include "device.h"
#include "controller.h"
#include "shadow_client.h"


static const char *TAG = "project";

void _mainButtonEventHandler(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data) {
    if (base == BUTTON_MAIN_EVENT_BASE) {
        if (id == BUTTON_CLICK) {
            ESP_LOGI(TAG, "Main Button Pressed");
        }
        if (id == BUTTON_HOLD) {
            ESP_LOGI(TAG, "Main Button Held");
        }
    }
}

void _resetButtonEventHandler(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data) {
    if (base == BUTTON_RESET_EVENT_BASE) {
        if (id == BUTTON_HOLD) {
            ESP_LOGI(TAG, "Reset Button Held");
            ESP_LOGI(TAG, "Reseting Wifi Networks");
            //vLabConnectionResetWifiNetworks();
        }
        if (id == BUTTON_CLICK) {
            ESP_LOGI(TAG, "Reset Button Clicked");
            ESP_LOGI(TAG, "Restarting in 2secs");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        }
    }
}

esp_err_t eControllerRun(void) {
    esp_err_t res = ESP_FAIL;

    res = eDeviceInit();

    if (res == ESP_OK) {

        res = eDeviceRegisterButtonCallback(BUTTON_MAIN_EVENT_BASE, _mainButtonEventHandler);
        if (res != ESP_OK) {
            IotLogError("eControllerRun: Register main button ... failed");
        }

        res = eDeviceRegisterButtonCallback(BUTTON_RESET_EVENT_BASE, _resetButtonEventHandler);
        if (res != ESP_OK) {
            IotLogError("eControllerRun: Register reset button ... failed");
        }
    } else {
        IotLogError("eControllerRun: eControllerRun ... failed");
    }

    static TaskHandle_t xCoreMqttTask = NULL, xActuatorTask = NULL, xPublishTask = NULL;

    BaseType_t xReturned;
    xReturned = xTaskCreate(publishCurrentStateTask, "publish", configMINIMAL_STACK_SIZE * 8, (void *) NULL,
                            tskIDLE_PRIORITY + 4, &xPublishTask);

    if (xReturned != pdPASS) {
        IotLogError("error while creating publishCurrentStateTask");
    }

    xReturned = xTaskCreate(runActuatorTask, "actuator", configMINIMAL_STACK_SIZE * 8, &xPublishTask,
                            tskIDLE_PRIORITY + 4, &xActuatorTask);
    if (xReturned != pdPASS) {
        IotLogError("error while creating runActuatorTask");
    }

    xReturned = xTaskCreate(subscribeUpdateTask, "subscribe", configMINIMAL_STACK_SIZE * 8, &xActuatorTask,
                            tskIDLE_PRIORITY + 5, &xCoreMqttTask);
    if (xReturned != pdPASS) {
        IotLogError("error while creating subscribeUpdateTask");
    }

    return res;
}
