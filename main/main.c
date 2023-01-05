/*
 * Standard input-output header of C language
*/
#include <stdio.h>

/*
 * Supporto to NVS (Non Volatile Storage - flash memory)
 * Store pairs "key-value" in flash memory of the ESP32
*/
#include "nvs_flash.h"

/*
 * Support for configuring and monitoring the ESP32 WiFi networking functionality
*/
#include "esp_wifi.h"

/*
 Event loop library
*/
#include "esp_event.h"

/*
 Making HTTP/S requests from ESP-IDF applications.
*/
#include "esp_http_client.h"

/*
 * Logging library
*/
#include "esp_log.h"

/*
 * Semaphore library
*/
#include "freertos/semphr.h"

/*
 * Own libraries
*/
#include "wifi.h"
#include "http_client.h"

// Semaphore Handle variable
xSemaphoreHandle wifiSemaphore;

// Processing HTTP Request function for the specific task
void  ProcessHTTPRequest(void * params) {
    while(true) {
        if (xSemaphoreTake(wifiSemaphore, portMAX_DELAY)) {
            ESP_LOGI("Main Task", "Make a HTTP request");
            http_request();
            https_request();
        }
    }
}

//Main function
void app_main(void)
{
    /*
     * Initilize the NVS (Non Volatile Storage - Flash memory) with default error handler from ESP32 documentation
     * Return error code to "esp_err_t" variable
    */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {\
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //Create semaphore for the wifi connection
    wifiSemaphore = xSemaphoreCreateBinary();
    // Start "own_wifi_start"  function and try to connect to the AP
    wifi_start();

    // Activate HTTP Task
    xTaskCreate(&ProcessHTTPRequest, "Process HTTP Request", 4096, NULL, 1, NULL);
}
