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

#include "FreeRTOS.h"

#include "platform/iot_network.h"
#include "platform/iot_threads.h"

#include "iot_network_manager_private.h"


/**
 * @brief All C SDK demo functions have this signature.
 */
typedef int (* demoFunction_t)( bool awsIotMqttMode,
                                const char * pIdentifier,
                                void * pNetworkServerInfo,
                                void * pNetworkCredentialInfo,
                                const IotNetworkInterface_t * pNetworkInterface );


typedef void (* networkConnectedCallback_t)( bool awsIotMqttMode,
                                             const char * pIdentifier,
                                             void * pNetworkServerInfo,
                                             void * pNetworkCredentialInfo,
                                             const IotNetworkInterface_t * pNetworkInterface );

typedef void (* networkDisconnectedCallback_t)( const IotNetworkInterface_t * pNetworkInteface );

typedef struct appMqttContext
{
    /* Network types for the demo */
    uint32_t networkTypes;
    /* Function pointers to be set by the implementations for the demo */
    demoFunction_t demoFunction;
    networkConnectedCallback_t networkConnectedCallback;
    networkDisconnectedCallback_t networkDisconnectedCallback;
} appMqttContext_t;

typedef struct appNetworkSetting
{
    const IotNetworkInterface_t *pNetworkInterface;
    void *pConnectionParams;
    void *pCredentials;
} appNetworkSetting_t;

int network_initialize(appMqttContext_t *pContext);
appNetworkSetting_t getNetworkSetting();

