// Environment variables to access them from menuconfig (see Kconfig.projbuild file)
/*
 * Standard input-output header of C language
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

#if CONFIG_EXAMPLE_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif


// Bits for event group management
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Event group handle
static EventGroupHandle_t x_wifi_event_group;

// TAG for log register (debugging)
static const char* TAG = "WIFI";

//Call the wifiSemaphore handle fro main.c
extern xSemaphoreHandle xWifiSemaphore;

// Connection re-tries max number
static int i_retry_num = 0;
const int i_MAX_RETRY = WIFI_MAXIMUM_RETRY;


// Default wifi event handler function
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    /* 
     * Verify the event base type  (WIFI_EVENT)
     * Verify the event_id (STACK STEP - START -> CONNECT -> GOT IP -> DISCONNECT)
     * Do the corresponding task
    */
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (i_retry_num < i_MAX_RETRY) {
            esp_wifi_connect();
            i_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP... trying %d, total of %d tries configured", i_retry_num, i_MAX_RETRY);
        } else {
            /* If wifi connect don't suceeds, include wifi event into a bit group. 
               Automatically unblock tasks that are blocked waiting for the bits.
            */
            xEventGroupSetBits(x_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "Connect to the AP fail");

    /* 
     * Verify the event base type  (IP_EVENT)
     * Verify the event_id (IP_EVENT_STA_GOT_IP)
     * Do the corresponding task
    */
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        // Unpack IP event data in a variable (x_event_ip)
        ip_event_got_ip_t* x_event_ip = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP address received: " IPSTR, IP2STR(&x_event_ip -> ip_info.ip));
        i_retry_num = 0;
        xEventGroupSetBits(x_wifi_event_group, WIFI_CONNECTED_BIT);
        
        //Notify semaphore activating
        xSemaphoreGive(xWifiSemaphore);
    }
}

// Start wifi resource 
void wifi_init_AP(char *wifi_ssid, char *wifi_passwd) {
    // Event gruop initilize
    x_wifi_event_group = xEventGroupCreate();

    // TCP/IP network interface initialize
    ESP_ERROR_CHECK(esp_netif_init());

    // Default event loop task initialize 
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Netif of type "Wi-Fi Station" default initialize
    esp_netif_create_default_wifi_sta();

    // Wi-Fi default struct
    wifi_init_config_t x_wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();

    // Load and initialize the Wi-Fi Station
    ESP_ERROR_CHECK(esp_wifi_init(&x_wifi_init_config));

    // Wi-Fi event handler register
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    
    // Network IP event handler register
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t x_wifi_config = {
        .sta = {
            .ssid = WIFI_AP_SSID,
            .password = WIFI_AP_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
	         * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
        },
    };

    // Configure data structure "config" with default wi-fi connect params if no params are passed to the function
    // If no ssid was informed, set default "menuconfig" parameters
    if (wifi_ssid == NULL || strcmp(wifi_ssid, "") == 0) {
        wifi_ssid = WIFI_AP_SSID;
        wifi_passwd = WIFI_AP_PASS;
    } 
    else {
        const char *ssid_char = wifi_ssid;
        const char *passwd_char = wifi_passwd;

        strcpy((char*)x_wifi_config.sta.ssid, ssid_char);
        strcpy((char*)x_wifi_config.sta.password, passwd_char);
    }

    // Configure Wi-Fi initialization to STATION mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    // Set Wi-Fi connect params
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &x_wifi_config));

    // Switch on Wi-Fi resource
    ESP_ERROR_CHECK(esp_wifi_start());

    /* 
     * Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) 
    */
    // ! BLOCKING CALL (portMAX_DELAY -> open-ended)
    EventBits_t bits = xEventGroupWaitBits(x_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* 
     * xEventGroupWaitBits() returns the bits before the call returned, as such we can test which event actually
     * happened. 
    */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to ap SSID: %s, Password: %s", wifi_ssid, wifi_passwd);
		ESP_LOGI(TAG, "Wifi init in STATION mode finished.");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID: %s, Password: %s", wifi_ssid, wifi_passwd);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* 
     * The event will not be processed after unregister 
       ! This part of the code NEEDS to be DELETED in a PRODUCTION ENVIRONMENT
       ! The EventBit needs to run ALL THE TIME
    */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler));
    vEventGroupDelete(x_wifi_event_group);
}
