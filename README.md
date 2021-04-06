# Smart Personal Box FreeRTOS Demo 

FreeRTOS code for prototyping a smart parsonal box (smart lock) with M5StickC.

The overall project, including mobile app and backend server is introduced in [this blog post (Japanese)](https://aws.amazon.com/jp/builders-flash/202101/smart-treasure-box/).

## Introduction
This is a demo designed to run on the M5StickC device. It can be used to make a quick prototype of smart lock.

It contains:
* Subscribe Device Shadow MQTT Topic.
* Parse Device Shadow delta and turn the lock on.
* Publish a MQTT message after unlocked. 

## Hardware

- M5StickC

### Wiring 
* Connect a Grove-Relay module to the M5StickC.
* Connect an actuator or a door lock module to the relay.

Even if you don't have those modules, you still can check if the program is working by checking device's LED. The LED will turn on when the device receives an unlock command.

## Run (Mac)

### Toolchain

```shell script
mkdir -p ~/esp
cd ~/esp
wget https://dl.espressif.com/dl/xtensa-esp32-elf-osx-1.22.0-96-g2852398-5.2.0.tar.gz
tar -xzf xtensa-esp32-elf-osx-1.22.0-96-g2852398-5.2.0.tar.gz
```

Add the following environment variable to `~/.profile` file.

```shell script
export PATH=$HOME/esp/xtensa-esp32-elf/bin:$PATH
```

### Install dependency libraries 
```shell script
brew install cmake ninja wget
pip install cryptography serial pyserial pyparsing==2.0.3
cd path/to/personal-box-freertos-demo
pip install --user -r amazon-freertos/vendors/espressif/esp-idf/requirements.txt
```

### Configure
* [Follow the instruction](https://docs.aws.amazon.com/freertos/latest/userguide/freertos-prereqs.html) and retrieve the following information 

   * Your AWS IoT endpoint
   * IoT thing name
   * aws_clientcredential_keys.h
* Open the amazon-freertos-configs/aws_clientcredential.h file by double-clicking on it. And change/set the following values:

```
...
#define clientcredentialMQTT_BROKER_ENDPOINT "[YOUR AWS IOT ENDPOINT]"
...
#define clientcredentialIOT_THING_NAME       "[YOUR IOT THING NAME]"
...
#define clientcredentialWIFI_SSID            "[WILL BE PROVIDED]"
...
#define clientcredentialWIFI_PASSWORD        "[WILL BE PROVIDED]"
...
#define clientcredentialWIFI_SECURITY        eWiFiSecurityWPA2
...
```

*  Copy aws_clientcredential_keys.h to amazon-freertos-configs directory by dragging it there.

 
### Compile 

```shell script
cmake -S . -B build -DIDF_SDKCONFIG_DEFAULTS=./sdkconfig -DCMAKE_TOOLCHAIN_FILE=amazon-freertos/tools/cmake/toolchains/xtensa-esp32.cmake -GNinja

cmake --build build
```

### Flash code

```shell script
ESPPORT=/dev/cu.usbserial-12345678 ESPBAUD=1500000 cmake --build build --target flash
```
 
### Monitoring

```shell script
screen /dev/cu.usbserial-12345678 115200 -L
```

### Control via the thing shadow

If the device has successfully run the demo code, it should create a shadow document like the followings:

```json
{
  "desired": {
    "lockState": 0
  },
  "reported": {
    "lockState": 0
  }
}
```

Edit this shadow document with the following:

```json
{
  "desired": {
    "lockState": 1
  },
  "reported": {
    "lockState": 0
  }
}
```

Modifying the shadow desired document, should instantly result in a compiled shadow document looking something like:

```json
{
  "desired": {
    "lockState": 1
  },
  "reported": {
    "lockState": 0
  },
  "delta": {
    "lockState": 1
  }
}
```

Now that the device's LED is turned ON and the lock is opened as well.

## Security

See [CONTRIBUTING](CONTRIBUTING.md#security-issue-notifications) for more information.

## License

This library is licensed under the MIT-0 License. See the LICENSE file.
