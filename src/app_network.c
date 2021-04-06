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

/* The config header is always included first. */
#include "iot_config.h"

#include "FreeRTOS.h"

#include <string.h>
#include "aws_clientcredential.h"
#include "aws_clientcredential_keys.h"
#include "iot_demo_logging.h"
#include "iot_init.h"

#include "app_network.h"

/*-----------------------------------------------------------*/

static IotNetworkManagerSubscription_t subscription = IOT_NETWORK_MANAGER_SUBSCRIPTION_INITIALIZER;

/* Semaphore used to wait for a network to be available. */
static IotSemaphore_t demoNetworkSemaphore;

/* Variable used to indicate the connected network. */
static uint32_t demoConnectedNetwork = AWSIOT_NETWORK_TYPE_NONE;


/*-----------------------------------------------------------*/

static uint32_t _getConnectedNetworkForDemo(appMqttContext_t *pAppMqttContext)
{
    uint32_t ret = (AwsIotNetworkManager_GetConnectedNetworks() & pAppMqttContext->networkTypes);

    if ((ret & AWSIOT_NETWORK_TYPE_WIFI) == AWSIOT_NETWORK_TYPE_WIFI)
    {
        ret = AWSIOT_NETWORK_TYPE_WIFI;
    }
    else
    {
        ret = AWSIOT_NETWORK_TYPE_NONE;
    }

    return ret;
}

/*-----------------------------------------------------------*/

static uint32_t _waitForDemoNetworkConnection(appMqttContext_t *pAppMqttContext)
{
    IotSemaphore_Wait(&demoNetworkSemaphore);

    return _getConnectedNetworkForDemo(pAppMqttContext);
}

/*-----------------------------------------------------------*/

static void _onNetworkStateChangeCallback(uint32_t network,
                                          AwsIotNetworkState_t state,
                                          void *pContext)
{
    const IotNetworkInterface_t *pNetworkInterface = NULL;
    void *pConnectionParams = NULL, *pCredentials = NULL;
    uint32_t disconnectedNetworks = AWSIOT_NETWORK_TYPE_NONE;

    appMqttContext_t *pAppMqttContext = (appMqttContext_t *)pContext;

    if ((state == eNetworkStateEnabled) && (demoConnectedNetwork == AWSIOT_NETWORK_TYPE_NONE))
    {
        demoConnectedNetwork = network;
        IotSemaphore_Post(&demoNetworkSemaphore);

        /* Disable the disconnected networks to save power and reclaim any unused memory. */
        disconnectedNetworks = configENABLED_NETWORKS & (~demoConnectedNetwork);

        if (disconnectedNetworks != AWSIOT_NETWORK_TYPE_NONE)
        {
            AwsIotNetworkManager_DisableNetwork(disconnectedNetworks);
        }

        if (pAppMqttContext->networkConnectedCallback != NULL)
        {
            pNetworkInterface = AwsIotNetworkManager_GetNetworkInterface(network);
            pConnectionParams = AwsIotNetworkManager_GetConnectionParams(network);
            pCredentials = AwsIotNetworkManager_GetCredentials(network),

            pAppMqttContext->networkConnectedCallback(true,
                                                      clientcredentialIOT_THING_NAME,
                                                      pConnectionParams,
                                                      pCredentials,
                                                      pNetworkInterface);
        }
    }
    else if (((state == eNetworkStateDisabled) || (state == eNetworkStateUnknown)) &&
             (demoConnectedNetwork == network))
    {
        if (pAppMqttContext->networkDisconnectedCallback != NULL)
        {
            pNetworkInterface = AwsIotNetworkManager_GetNetworkInterface(network);
            pAppMqttContext->networkDisconnectedCallback(pNetworkInterface);
        }

        /* Re-enable all the networks for the demo for reconnection. */
        disconnectedNetworks = configENABLED_NETWORKS & (~demoConnectedNetwork);

        if (disconnectedNetworks != AWSIOT_NETWORK_TYPE_NONE)
        {
            AwsIotNetworkManager_EnableNetwork(disconnectedNetworks);
        }

        demoConnectedNetwork = _getConnectedNetworkForDemo(pAppMqttContext);

        if (demoConnectedNetwork != AWSIOT_NETWORK_TYPE_NONE)
        {
            if (pAppMqttContext->networkConnectedCallback != NULL)
            {
                pNetworkInterface = AwsIotNetworkManager_GetNetworkInterface(demoConnectedNetwork);
                pConnectionParams = AwsIotNetworkManager_GetConnectionParams(demoConnectedNetwork);
                pCredentials = AwsIotNetworkManager_GetCredentials(demoConnectedNetwork);

                pAppMqttContext->networkConnectedCallback(true,
                                                          clientcredentialIOT_THING_NAME,
                                                          pConnectionParams,
                                                          pCredentials,
                                                          pNetworkInterface);
            }
        }
    }
}

/**
 * @brief Initialize the common libraries, Mqtt library and network manager.
 *
 * @return `EXIT_SUCCESS` if all libraries were successfully initialized;
 * `EXIT_FAILURE` otherwise.
 */
int network_initialize(appMqttContext_t *pContext)
{
    int status = EXIT_SUCCESS;
    bool commonLibrariesInitialized = false;
    bool semaphoreCreated = false;

    /* Initialize the C-SDK common libraries. This function must be called
     * once (and only once) before calling any other C-SDK function. */
    if (IotSdk_Init() == true)
    {
        commonLibrariesInitialized = true;
    }
    else
    {
        IotLogInfo("Failed to initialize the common library.");
        status = EXIT_FAILURE;
    }

    IotLogInfo("AwsIotNetworkManager_Init");
    if (status == EXIT_SUCCESS)
    {
        if (AwsIotNetworkManager_Init() != pdTRUE)
        {
            IotLogError("Failed to initialize network manager library.");
            status = EXIT_FAILURE;
        }
    }

    IotLogInfo("IotSemaphore_Create( &demoNetworkSemaphore, 0, 1 ) != true )");
    if (status == EXIT_SUCCESS)
    {
        /* Create semaphore to signal that a network is available for the demo. */
        if (IotSemaphore_Create(&demoNetworkSemaphore, 0, 1) != true)
        {
            IotLogError("Failed to create semaphore to wait for a network connection.");
            status = EXIT_FAILURE;
        }
        else
        {
            semaphoreCreated = true;
        }
    }

    IotLogInfo("AwsIotNetworkManager_SubscribeForStateChange");

    if (status == EXIT_SUCCESS)
    {
        /* Subscribe for network state change from Network Manager. */
        if (AwsIotNetworkManager_SubscribeForStateChange(pContext->networkTypes,
                                                         _onNetworkStateChangeCallback,
                                                         pContext,
                                                         &subscription) != pdTRUE)
        {
            IotLogError("Failed to subscribe network state change callback.");
            status = EXIT_FAILURE;
        }
    }

    IotLogInfo("AwsIotNetworkManager_EnableNetwork");
    /* Initialize all the  networks configured for the device. */
    if (status == EXIT_SUCCESS)
    {
        if (AwsIotNetworkManager_EnableNetwork(configENABLED_NETWORKS) != AWSIOT_NETWORK_TYPE_WIFI)
        {
            IotLogError("Failed to initialize all the networks configured for the device.");
            status = EXIT_FAILURE;
        }
    }

    if (status == EXIT_SUCCESS)
    {
        /* Wait for network configured for the demo to be initialized. */
        if (pContext->networkTypes != AWSIOT_NETWORK_TYPE_NONE)
        {
            demoConnectedNetwork = _getConnectedNetworkForDemo(pContext);

            if (demoConnectedNetwork == AWSIOT_NETWORK_TYPE_NONE)
            {
                /* Network not yet initialized. Block for a network to be initialized. */
                IotLogInfo("No networks connected for the demo. Waiting for a network connection. ");
                demoConnectedNetwork = _waitForDemoNetworkConnection(pContext);
            }
        }
    }

    if (status == EXIT_FAILURE)
    {
        if (semaphoreCreated == true)
        {
            IotSemaphore_Destroy(&demoNetworkSemaphore);
        }

        if (commonLibrariesInitialized == true)
        {
            IotSdk_Cleanup();
        }
    }

    return status;
}

appNetworkSetting_t getNetworkSetting()
{
    appNetworkSetting_t setting = {
        .pNetworkInterface = AwsIotNetworkManager_GetNetworkInterface(demoConnectedNetwork),
        .pConnectionParams = AwsIotNetworkManager_GetConnectionParams(demoConnectedNetwork),
        .pCredentials = AwsIotNetworkManager_GetCredentials(demoConnectedNetwork),
    };
    return setting;
}