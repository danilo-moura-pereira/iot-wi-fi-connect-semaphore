/*
 * Standard input-output header of C language
*/
#include <stdio.h>

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
 * Supporto to NVS (Non Volatile Storage - flash memory)
 * Save pairs "key-value" in flash memory of the ESP32
*/
#include "nvs_flash.h"

/*
 * Semaphore library
*/
#include "freertos/semphr.h"

/*
 * Own libraries
*/
#include "wifi.h"
#include "http_client.h"

#define TAG_MAIN "MAIN TASK"

// Semaphore Handle variable
xSemaphoreHandle xWifiSemaphore;

// Processing HTTP Request function for the specific task
void  ProcessWifiRequest(void * params) {
    while(true) {
        if (xSemaphoreTake(xWifiSemaphore, portMAX_DELAY)) {
            ESP_LOGI(TAG_MAIN, "Make HTTP and HTTPS requests");
            http_request();
            https_request();
        }
    }
}

// Main function
void app_main(void) {
    //Initialize NVS
    esp_err_t x_return = nvs_flash_init();
    if (x_return == ESP_ERR_NVS_NO_FREE_PAGES || x_return == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      x_return = nvs_flash_init();
    }
    ESP_ERROR_CHECK(x_return);

    // Create semaphore for the wifi connection
    xWifiSemaphore = xSemaphoreCreateBinary();

    // Starting wifi on STATION mode and connecting to it
    wifi_init_AP(NULL, NULL);

    // Activate HTTP Task
    xTaskCreate(&ProcessWifiRequest, "Process WIFI Request", 4096, NULL, 1, NULL);
}
