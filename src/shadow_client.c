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

/* Standard includes. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "aws_demo.h"

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* SHADOW API header. */
#include "shadow.h"

/* JSON library includes. */
#include "core_json.h"

/* shadow demo helpers header. */
#include "mqtt_demo_helpers.h"

#include "shadow_client.h"
#include "device.h"
#include "app_network.h"
#include "iot_demo_logging.h"

#define LOCK_STATE_OPEN (1)
#define LOCK_STATE_CLOSE (0)

#define LOCK_MQTT_PUBACK_WAIT_MS (5000)

#define SHADOW_DESIRED_JSON     \
    "{"                         \
    "\"state\":{"               \
    "\"desired\":{"             \
    "\"lockState\":%01d"        \
    "},"                        \
    "\"reported\":{"            \
    "\"lockState\":%01d"        \
    "}"                         \
    "},"                        \
    "\"clientToken\":\"%06lu\"" \
    "}"

/**
 * @brief The expected size of #SHADOW_DESIRED_JSON.
 *
 * Because all the format specifiers in #SHADOW_DESIRED_JSON include a length,
 * its full actual size is known by pre-calculation, here's the formula why
 * the length need to minus 3:
 * 1. The length of "%01d" is 4.
 * 2. The length of %06lu is 5.
 * 3. The actual length we will use in case 1. is 1 ( for the state of powerOn ).
 * 4. The actual length we will use in case 2. is 6 ( for the clientToken length ).
 * 5. Thus the additional size 3 = 4 + 5 - 1 - 6 + 1 (termination character).
 *
 * In your own application, you could calculate the size of the json doc in this way.
 */
#define SHADOW_DESIRED_JSON_LENGTH (sizeof(SHADOW_DESIRED_JSON) - 6)

#define SHADOW_REPORTED_JSON    \
    "{"                         \
    "\"state\":{"               \
    "\"reported\":{"            \
    "\"lockState\":%01d"        \
    "}"                         \
    "},"                        \
    "\"clientToken\":\"%06lu\"" \
    "}"

/**
 * @brief The expected size of #SHADOW_REPORTED_JSON.
 *
 * Because all the format specifiers in #SHADOW_REPORTED_JSON include a length,
 * its full size is known at compile-time by pre-calculation. Users could refer to
 * the way how to calculate the actual length in #SHADOW_DESIRED_JSON_LENGTH.
 */
#define SHADOW_REPORTED_JSON_LENGTH (sizeof(SHADOW_REPORTED_JSON) - 3)

#ifndef THING_NAME

/**
 * @brief Predefined thing name.
 *
 * This is the example predefine thing name and could be compiled in ROM code.
 */
#define THING_NAME democonfigCLIENT_IDENTIFIER
#endif

/**
 * @brief The length of #THING_NAME.
 */
#define THING_NAME_LENGTH ((uint16_t)(sizeof(THING_NAME) - 1))

/**
 * @brief Timeout for MQTT_ProcessLoop in milliseconds.
 */
#define MQTT_PROCESS_LOOP_TIMEOUT_MS (700U)

/*-----------------------------------------------------------*/

/**
 * @brief The MQTT context used for MQTT operation.
 */
static MQTTContext_t xMqttContext;

/**
 * @brief The network context used for Openssl operation.
 */
static NetworkContext_t xNetworkContext;

/**
 * @brief Static buffer used to hold MQTT messages being sent and received.
 */
static uint8_t ucSharedBuffer[democonfigNETWORK_BUFFER_SIZE];

/**
 * @brief Static buffer used to hold MQTT messages being sent and received.
 */
static MQTTFixedBuffer_t xBuffer =
        {
                .pBuffer = ucSharedBuffer,
                .size = democonfigNETWORK_BUFFER_SIZE};

/**
 * @brief The simulated device current power on state.
 */
static uint32_t ulCurrentLockState = 0U;

/**
 * @brief The flag to indicate the device current power on state changed.
 */
static bool stateChanged = false;

/**
 * @brief When we send an update to the device shadow, and if we care about
 * the response from cloud (accepted/rejected), remember the clientToken and
 * use it to match with the response.
 */
static uint32_t ulClientToken = 0U;

/**
 * @brief The return status of prvUpdateDeltaHandler callback function.
 */
static BaseType_t xUpdateDeltaReturn = pdPASS;

static TaskHandle_t actuatorHandle;

static SemaphoreHandle_t xPubAckWaitLock = NULL;

/*-----------------------------------------------------------*/

/**
 * @brief This example uses the MQTT library of the AWS IoT Device SDK for
 * Embedded C. This is the prototype of the callback function defined by
 * that library. It will be invoked whenever the MQTT library receives an
 * incoming message.
 *
 * @param[in] pxMqttContext MQTT context pointer.
 * @param[in] pxPacketInfo Packet Info pointer for the incoming packet.
 * @param[in] pxDeserializedInfo Deserialized information from the incoming packet.
 */
static void prvEventCallback(MQTTContext_t *pxMqttContext,
                             MQTTPacketInfo_t *pxPacketInfo,
                             MQTTDeserializedInfo_t *pxDeserializedInfo);

/**
 * @brief Process payload from /update/delta topic.
 *
 * This handler examines the version number and the powerOn state. If powerOn
 * state has changed, it sets a flag for the main function to take further actions.
 *
 * @param[in] pPublishInfo Deserialized publish info pointer for the incoming
 * packet.
 */
static void prvUpdateDeltaHandler(MQTTPublishInfo_t *pxPublishInfo);

/*-----------------------------------------------------------*/

static void prvUpdateDeltaHandler(MQTTPublishInfo_t *pxPublishInfo) {
    static uint32_t ulCurrentVersion = 0; /* Remember the latestVersion # we've ever received */
    uint32_t ulVersion = 0U;
    uint32_t ulNewState = 0U;
    char *pcOutValue = NULL;
    uint32_t ulOutValueLength = 0U;
    JSONStatus_t result = JSONSuccess;

    assert(pxPublishInfo != NULL);
    assert(pxPublishInfo->pPayload != NULL);

    LogInfo(("/update/delta json payload:%s.", (const char *) pxPublishInfo->pPayload));

    /* Make sure the payload is a valid json document. */
    result = JSON_Validate(pxPublishInfo->pPayload,
                           pxPublishInfo->payloadLength);

    if (result == JSONSuccess) {
        /* Then we start to get the version value by JSON keyword "version". */
        result = JSON_Search((char *) pxPublishInfo->pPayload,
                             pxPublishInfo->payloadLength,
                             "version",
                             sizeof("version") - 1,
                             &pcOutValue,
                             (size_t * ) & ulOutValueLength);
    } else {
        LogError(("The json document is invalid!!"));
    }

    if (result == JSONSuccess) {
        LogInfo(("version: %.*s",
                ulOutValueLength,
                pcOutValue));

        /* Convert the extracted value to an unsigned integer value. */
        ulVersion = (uint32_t) strtoul(pcOutValue, NULL, 10);
    } else {
        LogError(("No version in json document!!"));
    }

    LogInfo(("version:%d, ulCurrentVersion:%d \r\n", ulVersion, ulCurrentVersion));

    /* When the version is much newer than the on we retained, that means the powerOn
     * state is valid for us. */
    if (ulVersion > ulCurrentVersion) {
        /* Set to received version as the current version. */
        ulCurrentVersion = ulVersion;

        /* Get powerOn state from json documents. */
        result = JSON_Search((char *) pxPublishInfo->pPayload,
                             pxPublishInfo->payloadLength,
                             "state.lockState",
                             sizeof("state.lockState") - 1,
                             &pcOutValue,
                             (size_t * ) & ulOutValueLength);
    } else {
        /* In this demo, we discard the incoming message
         * if the version number is not newer than the latest
         * that we've received before. Your application may use a
         * different approach.
         */
        LogWarn(("The received version is smaller than current one!!"));
    }

    if (result == JSONSuccess) {
        /* Convert the lock state value to an unsigned integer value. */
        ulNewState = (uint32_t) strtoul(pcOutValue, NULL, 10);

        LogInfo(("The new state newState:%d, ulCurrentLockState:%d \r\n",
                ulNewState, ulCurrentLockState));

        if (ulNewState != ulCurrentLockState) {
            ulCurrentLockState = ulNewState;

            if (ulNewState == LOCK_STATE_OPEN) {
                xTaskNotifyGive(actuatorHandle);
            }

            /* State change will be handled in main(), where we will publish a "reported"
             * state to the device shadow. We do not do it here because we are inside of
             * a callback from the MQTT library, so that we don't re-enter
             * the MQTT library. */
            stateChanged = true;
        }
    } else {
        LogError(("No lockState in json document!!"));
        xUpdateDeltaReturn = pdFAIL;
    }
}

/*-----------------------------------------------------------*/

/* This is the callback function invoked by the MQTT stack when it receives
 * incoming messages. This function demonstrates how to use the Shadow_MatchTopic
 * function to determine whether the incoming message is a device shadow message
 * or not. If it is, it handles the message depending on the message type.
 */
static void prvEventCallback(MQTTContext_t *pxMqttContext,
                             MQTTPacketInfo_t *pxPacketInfo,
                             MQTTDeserializedInfo_t *pxDeserializedInfo) {
    ShadowMessageType_t messageType = ShadowMessageTypeMaxNum;
    const char *pcThingName = NULL;
    uint16_t usThingNameLength = 0U;
    uint16_t usPacketIdentifier;

    (void) pxMqttContext;

    assert(pxDeserializedInfo != NULL);
    assert(pxMqttContext != NULL);
    assert(pxPacketInfo != NULL);

    usPacketIdentifier = pxDeserializedInfo->packetIdentifier;

    LogInfo(("Received a packet."));
    /* Handle incoming publish. The lower 4 bits of the publish packet
     * type is used for the dup, QoS, and retain flags. Hence masking
     * out the lower bits to check if the packet is publish. */
    if ((pxPacketInfo->type & 0xF0U) == MQTT_PACKET_TYPE_PUBLISH) {
        assert(pxDeserializedInfo->pPublishInfo != NULL);
        LogInfo(("pPublishInfo->pTopicName:%s.", pxDeserializedInfo->pPublishInfo->pTopicName));

        /* Let the Device Shadow library tell us whether this is a device shadow message. */
        if (SHADOW_SUCCESS == Shadow_MatchTopic(pxDeserializedInfo->pPublishInfo->pTopicName,
                                                pxDeserializedInfo->pPublishInfo->topicNameLength,
                                                &messageType,
                                                &pcThingName,
                                                &usThingNameLength)) {
            /* Upon successful return, the messageType has been filled in. */
            if (messageType == ShadowMessageTypeUpdateDelta) {
                /* Handler function to process payload. */
                prvUpdateDeltaHandler(pxDeserializedInfo->pPublishInfo);
            } else {
                LogInfo(("Other message type:%d !!", messageType));
            }
        } else {
            LogError(
                    ("Shadow_MatchTopic parse failed:%s !!", (const char *) pxDeserializedInfo->pPublishInfo->pTopicName));
        }
    } else {
        vHandleOtherIncomingPacket(pxPacketInfo, usPacketIdentifier);
        if (pxPacketInfo->type == MQTT_PACKET_TYPE_PUBACK && xPubAckWaitLock != NULL) {
            xSemaphoreGive(xPubAckWaitLock);
        }
    }
}

void publishCurrentStateTask(void *pArgument) {
    /* A buffer containing the update document. It has static duration to prevent
     * it from being placed on the call stack. */
    static char pcUpdateDocument[SHADOW_DESIRED_JSON_LENGTH + 1] = {0};
    BaseType_t xDemoStatus = pdPASS;

    xPubAckWaitLock = xSemaphoreCreateBinary();

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        LogInfo(("ulTaskNotifyTake(pdTRUE, portMAX_DELAY);"));

        LogInfo(("Report to the state change: %d", ulCurrentLockState));
        (void) memset(pcUpdateDocument,
                      0x00,
                      sizeof(pcUpdateDocument));

        ulClientToken = (xTaskGetTickCount() % 1000000);
        ulCurrentLockState = LOCK_STATE_OPEN;

        snprintf(pcUpdateDocument,
                 SHADOW_REPORTED_JSON_LENGTH + 1,
                 SHADOW_REPORTED_JSON,
                 (int) ulCurrentLockState,
                 (long unsigned) ulClientToken);

        xDemoStatus = PublishToTopic(&xMqttContext,
                                     SHADOW_TOPIC_STRING_UPDATE(THING_NAME),
                                     SHADOW_TOPIC_LENGTH_UPDATE(THING_NAME_LENGTH),
                                     pcUpdateDocument,
                                     (SHADOW_REPORTED_JSON_LENGTH));
        if (xDemoStatus == pdFAIL) {
            /* Log error to indicate connection failure. */
            LogError(("Failed to publish to MQTT broker."));
        }

        if (xSemaphoreTake(xPubAckWaitLock, pdMS_TO_TICKS(LOCK_MQTT_PUBACK_WAIT_MS)) != pdTRUE) {
            LogError(("Failed to receive puback"));
        }

        // TODO the following code should be executed after a sensor detects lock closing.
        // but this demo, there is no sensor we call it soon.
        vTaskDelay(pdMS_TO_TICKS(5000));

        // remove desired value and change the reported state to CLOSE.
        ulClientToken = (xTaskGetTickCount() % 1000000);
        ulCurrentLockState = LOCK_STATE_CLOSE;

        (void) memset(pcUpdateDocument,
                      0x00,
                      sizeof(pcUpdateDocument));

        snprintf(pcUpdateDocument,
                 SHADOW_DESIRED_JSON_LENGTH + 1,
                 SHADOW_DESIRED_JSON,
                 (int) ulCurrentLockState,
                 (int) ulCurrentLockState,
                 (long unsigned) ulClientToken);

        xDemoStatus = PublishToTopic(&xMqttContext,
                                     SHADOW_TOPIC_STRING_UPDATE(THING_NAME),
                                     SHADOW_TOPIC_LENGTH_UPDATE(THING_NAME_LENGTH),
                                     pcUpdateDocument,
                                     (SHADOW_DESIRED_JSON_LENGTH));
        if (xDemoStatus == pdFAIL) {
            /* Log error to indicate connection failure. */
            LogError(("Failed to publish to MQTT broker."));
        }

        if (xSemaphoreTake(xPubAckWaitLock, pdMS_TO_TICKS(LOCK_MQTT_PUBACK_WAIT_MS)) != pdTRUE) {
            LogError(("Failed to receive puback"));
        }
    }
}

/*-----------------------------------------------------------*/

int RunDeviceShadowClient(bool awsIotMqttMode,
                          const char *pIdentifier,
                          void *pNetworkServerInfo,
                          void *pNetworkCredentialInfo,
                          const void *pNetworkInterface,
                          TaskHandle_t *pHandle) {
    BaseType_t xDemoStatus = pdPASS;

    actuatorHandle = *pHandle;

    /* Remove compiler warnings about unused parameters. */
    (void) awsIotMqttMode;
    (void) pIdentifier;
    (void) pNetworkServerInfo;
    (void) pNetworkCredentialInfo;
    (void) pNetworkInterface;

    xDemoStatus = EstablishMqttSession(&xMqttContext,
                                       &xNetworkContext,
                                       &xBuffer,
                                       prvEventCallback);

    if (xDemoStatus == pdFAIL) {
        /* Log error to indicate connection failure. */
        LogError(("Failed to connect to MQTT broker."));
    } else {
        if (xDemoStatus == pdPASS) {
            xDemoStatus = SubscribeToTopic(&xMqttContext,
                                           SHADOW_TOPIC_STRING_UPDATE_DELTA(THING_NAME),
                                           SHADOW_TOPIC_LENGTH_UPDATE_DELTA(THING_NAME_LENGTH));
        }

        if (xDemoStatus == pdPASS) {

            while (true) {
                MQTTStatus_t eMqttStatus = MQTTSuccess;

                xDemoStatus = MQTT_ProcessLoop(&xMqttContext, 500U);

                if (eMqttStatus != MQTTSuccess) {
                    LogWarn(("MQTT_ProcessLoop returned with status = %s.",
                            MQTT_Status_strerror(eMqttStatus)));
                }
            }
        }
    }

    return ((xDemoStatus == pdPASS) ? EXIT_SUCCESS : EXIT_FAILURE);
}

void subscribeUpdateTask(void *pArgument) {

    TaskHandle_t *pHandle = (TaskHandle_t *) pArgument;

    static appMqttContext_t appMqttContext =
            {
                    .networkTypes = AWSIOT_NETWORK_TYPE_WIFI,
                    .networkConnectedCallback = NULL,
                    .networkDisconnectedCallback = NULL
            };

    int status;

    status = network_initialize(&appMqttContext);

    if (status != EXIT_SUCCESS) {
        IotLogInfo("_initialize failed");
        return;
    }

    appNetworkSetting_t setting = getNetworkSetting();
    // receive command from server.
    RunDeviceShadowClient(true,
                          clientcredentialIOT_THING_NAME,
                          setting.pConnectionParams,
                          setting.pCredentials,
                          setting.pNetworkInterface,
                          pHandle);
}

