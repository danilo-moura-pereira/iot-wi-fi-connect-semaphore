// Environment variables to access them from menuconfig (see Kconfig.projbuild file)
/*
 * Standard input-output header of C language
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
/*
 * Vanilla FreeRTOS allows ports and applications to configure the kernel by adding macros
 * Allows dinamic and static memory allocation for tasks and system tools
*/
#include "freertos/FreeRTOS.h"

/*
 * Allow tasks management in FreeRTOS 
*/
#include "freertos/task.h"

/*
 * Events management in FreeRTOS
*/
#include "freertos/event_groups.h"

/*
 * Lightweight TCP/IP stack error handling
*/
#include "lwip/err.h"

/*
 * Lightweight TCP/IP stack
*/
#include "lwip/sys.h"


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
 * Event loop library
*/
#include "esp_event.h"

/*
 * Making HTTP/S requests from ESP-IDF applications.
*/
#include "esp_http_client.h"

/*
 * Logging library
*/
#include "esp_log.h"

/*
 * Allows internal calls to the ESP32 processor
*/
#include "esp_system.h"

#define WIFI_AP_SSID CONFIG_ESP_WIFI_SSID
#define WIFI_AP_PASS CONFIG_ESP_WIFI_PASSWORD
#define WIFI_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_RETRY

// Bits for event group management
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Event group handle
static EventGroupHandle_t s_wifi_event_group;

// TAG for log register (debugging)
#define TAG "Wi-Fi"

//Call the wifiSemaphore handle fro main.c
extern xSemaphoreHandle wifiSemaphore;

// Connection re-tries max number
static int s_retry_num = 0;
const int MAX_RETRY = WIFI_MAXIMUM_RETRY;


// Default event handler function
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    /* 
     * Verify the event base type  (WIFI_EVENT)
     * Verify the event_id (STACK STEP - START -> CONNECT -> GOT IP -> DISCONNECT)
     * Do the corresponding task
    */
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP... trying %d, total of %d tries configured", s_retry_num, MAX_RETRY);
        } else {
            /* If wifi connect don't suceeds, include wifi event into a bit group. 
               Automatically unblock tasks that are blocked waiting for the bits.
            */
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "Connect to the AP fail");
    /* 
     * Verify the event base type  (IP_EVENT)
     * Verify the event_id (IP_EVENT_STA_GOT_IP)
     * Do the corresponding task
    */
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        // Unpack IP event data in a variable (event_ip)
        ip_event_got_ip_t* event_ip = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP address received:" IPSTR, IP2STR(&event_ip->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        
        //Notify semaphore activating
        xSemaphoreGive(wifiSemaphore);
    }
}

// Initialize the wifi resource 
void wifi_start() {
    // Event gruop initilize
    s_wifi_event_group = xEventGroupCreate();

    // TCP/IP network interface initialize
    ESP_ERROR_CHECK(esp_netif_init());

    // Default event loop task initialize 
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Netif of type "Wi-Fi Station" default initialize
    esp_netif_create_default_wifi_sta();

    // Wi-Fi default struct
    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();

    // Load and initialize the Wi-Fi Station
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_config));

    // Wi-Fi event handler register
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    
    // Network IP event handler register
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    // Configure data structure "config" with wi-fi connect params
    wifi_config_t config = {
        .sta = {
            .ssid = WIFI_AP_SSID,
            .password = WIFI_AP_PASS
        }
    };

    // Configure Wi-Fi initialization mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    // Set Wi-Fi connect params
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &config));

    // Switch on Wi-Fi resource
    ESP_ERROR_CHECK(esp_wifi_start());

    /* 
     * Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) 
    */
    // ! BLOCKING CALL
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* 
     * xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. 
    */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to ap SSID: %s, Password: %s", WIFI_AP_SSID, WIFI_AP_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID: %s, Password: %s", WIFI_AP_SSID, WIFI_AP_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* 
     * The event will not be processed after unregister 
       ! This part of the code NEEDS to be DELETED in a PRODUCTION ENVIRONMENT
       ! The EventBit needs to run ALL THE TIME
    */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler));
    vEventGroupDelete(s_wifi_event_group);
}
