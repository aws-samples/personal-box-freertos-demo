cmake_minimum_required(VERSION 3.13)

project(freertos_examples)

file(GLOB SOURCES "src/*.c")
add_executable(afr_workshop ${SOURCES})

include_directories(afr_workshop PRIVATE "amazon-freertos/demos/network_manager")
include_directories(afr_workshop PRIVATE "amazon-freertos/demos/dev_mode_key_provisioning/include")
include_directories(afr_workshop PRIVATE "amazon-freertos/libraries/freertos_plus/standard/utils/include")
include_directories(afr_workshop PRIVATE "amazon-freertos/libraries/c_sdk/standard/serializer/include")

include_directories(afr_workshop PRIVATE include)

# Tell IDF build to link against this target.
set(IDF_PROJECT_EXECUTABLE afr_workshop)

# Add some extra components. IDF_EXTRA_COMPONENT_DIRS is an variable used by ESP-IDF
# to collect extra components.
list(APPEND IDF_EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS})
get_filename_component(
    EXTRA_COMPONENT_DIRS
    "components/afr-m5stickc-bsp" ABSOLUTE
)
list(APPEND IDF_EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS})

# As of now there's no offical way to redefine config files outside of Amazon FreeRTOS source tree.
# This is a temporary approach to inject an include path so that this takes precedence over the
# config file directory inside Amazon FreeRTOS.
include_directories(BEFORE amazon-freertos-configs)

# Add amazon freertos as an subdirectory. AFR_BOARD tells which board to target.
set(AFR_BOARD espressif.esp32_devkitc CACHE INTERNAL "")
add_subdirectory(amazon-freertos)

target_link_libraries(afr_workshop 
    PRIVATE
        AFR::core_mqtt
        AFR::backoff_algorithm
        AFR::core_json
        AFR::transport_interface_secure_sockets
        AFR::secure_sockets
        AFR::pkcs11_helpers
        AFR::demo_wifi_provisioning
        AFR::demo_numeric_comparison
        AFR::mqtt_demo_helpers
        AFR::device_shadow
)


