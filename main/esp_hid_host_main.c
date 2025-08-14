/*************************************************************************************
 * HID client for BT classic gamepad controller input
 * 
 * Adapted from the IDF 5.5 example:
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 * 
 * Dual mode BT & BLE client/host with special HID support.
 * Looks for servers and displays their services and chareteristics.
 * Can connect to free/unsecure BT servers like HID gamepad/joystick/mice.
 * 
 * Attention: Only BT classic tested !
 * 
 * FreeRTOS xQueue is used for communication to task RdBT (in spi_lcd_touch_VSPI.c)
 * There is also a simple bool var "bBtReady" used as a semaphore
 * that tells showPad.c/showWaitStart not to start too early
 * and RdBT_task not to wait for queued data before the queue is started here.
 * 
 * There is one FreeRTOS task "hid_task" on CORE0 that scans all Bluetooth devices in the vicinity
 * looking for my HZ-2746 and isplaying features of potential partners. 
 * When found tries to open the Gamepad with esp_hidh_dev_open.
 * Then it cycles with 1s outtime until connection is lost. 
 * Afterwards ESP32 will be restarted.
 * 
 * All data from the Gamepad as well as BT state changes will be processed within "hidh_callback"
 * 
 * Attention: IDF 5.5 corrects some 5.3.1 errors here: always checks bda before ESP_LOGI
 * 
 * Open problem:
 *  Frequently the ESP32 has to be reset before a first connection is astablished.
 *  Frequently the HZ-2746 is found but it's name is not recognized. 
 *  So "ESP_LOGI(TAG, "Gamepad %s found!", remote_device_name);" is not reached, 
 *  but gamepad interaction works.
 * 
 * Compiled and linked with IDF 5.3.1 in the end!
***************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h" 
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_bt.h"
#include "sdkconfig.h"

#if CONFIG_BT_NIMBLE_ENABLED    // only for BLE
    #include "host/ble_hs.h"
    #include "nimble/nimble_port.h"
    #include "nimble/nimble_port_freertos.h"
    #define ESP_BD_ADDR_STR         "%02x:%02x:%02x:%02x:%02x:%02x"
    #define ESP_BD_ADDR_HEX(addr)   addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]
#else
    #include "esp_bt_defs.h"     // classic BT
    #include "esp_gap_ble_api.h"
    #include "esp_gatts_api.h"
    #include "esp_gatt_defs.h"
    #include "esp_bt_main.h"
    #include "esp_bt_device.h"
#endif

#include "esp_hidh.h" 

#include "esp_hid_gap.h"            // local  ..\include
#include "esp_hid_host_main.h"      // local  ..\include

// These must be equal to defines in spi_lcd_touch_VSPI.c:
#define EXAMPLE_PIN_NUM_BK_LIGHT       2
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL

static const char *TAG = "ESP_HIDH";

QueueHandle_t xQueue;
struct sBtInput BT_input;
// flag for RdBT and display not to start too early
bool bBtReady = false;

// bGamepadFound==true (for local use) as long as BT connected to our Gamepad HZ...
static bool bGamepadFound=false;  

#if CONFIG_BT_HID_HOST_ENABLED
  static const char * remote_device_name = "HZ-2746";   // CONFIG_EXAMPLE_PEER_DEVICE_NAME;
#endif // CONFIG_BT_HID_HOST_ENABLED

#if !CONFIG_BT_NIMBLE_ENABLED
/*********************************** 
 * return hex address (6) from bda
************************************/
static char *bda2str(esp_bd_addr_t bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }

    uint8_t *p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}
#endif

/************************************************************* 
 * CB for BT classic esp_hidh
 * 
 * ESP_HIDH_OPEN_EVENT: sets bBtReady=true for external tasks
 * ESP_HIDH_INPUT_EVENT: signals incomming commands and data
 * For the "HZ-2746" I use only with "mode @D" two of them:
 * 1) CCONTROL   INPUT REPORT, ID:   3, Length:   2 
 *  for button presses
 * 2) MOUSE   INPUT REPORT, ID:   2, Length:   4
 *  for cursor movements
 * Because both length are unique, I do not check the IDs
 * 
 * Gamepad data arrives quite frequentyl (up to each 6 ms)
 * so I save previous data and compare it to actual data
 * in order to avoid overflow aof queue.
 * 
 * ESP_HIDH_CLOSE_EVENT: this will lead to a restart of ESP32
 * because it is owsome to recall all inits after a renewed connection
 * 
 * handler_args:?
 * 
**************************************************************/
void hidh_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidh_event_t event = (esp_hidh_event_t)id;
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;
    bool bSend = false;
    static esp_hidh_event_data_t *oldData2=NULL, *oldData4=NULL;

    if(!oldData4) oldData4 = (esp_hidh_event_data_t*)calloc(4, 1);
    if(!oldData2) oldData2 = (esp_hidh_event_data_t*)calloc(2, 1);

    switch (event) {
    case ESP_HIDH_OPEN_EVENT: {
        if (param->open.status == ESP_OK) {
            const uint8_t *bda = esp_hidh_dev_bda_get(param->open.dev);
            if(bda) {
                //tell RdBT task, that data is comming soon
                bBtReady = true;
                ESP_LOGD(TAG, ESP_BD_ADDR_STR " OPEN: %s", ESP_BD_ADDR_HEX(bda), esp_hidh_dev_name_get(param->open.dev));  //@JB no name found here
                esp_hidh_dev_dump(param->open.dev, stdout);
            }
        } else {
            ESP_LOGE(TAG, " OPEN failed!");
        }
        break;
    }
    case ESP_HIDH_BATTERY_EVENT: {
        const uint8_t *bda = esp_hidh_dev_bda_get(param->battery.dev);
        if(bda) ESP_LOGD(TAG, ESP_BD_ADDR_STR " BATTERY: %d%%", ESP_BD_ADDR_HEX(bda), param->battery.level);
        break;
    }
    case ESP_HIDH_INPUT_EVENT: {    // input events are the most relevant for a host application that is controlled by these events
        const uint8_t *bda = esp_hidh_dev_bda_get(param->input.dev);
        if (bda) {
            ESP_LOGD(TAG, ESP_BD_ADDR_STR " INPUT: %8s, MAP: %2u, ID: %3u, Len: %d, Data:", ESP_BD_ADDR_HEX(bda), esp_hid_usage_str(param->input.usage), param->input.map_index, param->input.report_id, param->input.length);
            //ESP_LOG_BUFFER_HEX(TAG, param->input.data, param->input.length);    // that is the actual/net HID data received 
            BT_input.reportId = param->input.report_id;
            BT_input.len = param->input.length;
            // queue is overflowing, when repeatedly filled with same joystick data (length==4) :
            if(BT_input.len == 4)   {
                if(memcmp(oldData4, param->input.data, 4))   {
                    memcpy(oldData4, param->input.data, 4);
                    memcpy(BT_input.data, oldData4, 4);
                    bSend = true;
                }
                else bSend=false;
            }
            else if(BT_input.len == 2)   {
                if(memcmp(param->input.data, oldData2, 2))   {
                    memcpy(oldData2, param->input.data, 2);
                    memcpy(BT_input.data, oldData2, 2);
                    bSend = true;
                }
                else bSend=false;
            }
            if(bSend)   {
                // do not block:
                if( xQueueSend(xQueue, ( void * )&BT_input   , ( TickType_t ) 0 ) != pdPASS )
                {
                    // Failed to post the message, even after 10 ticks.
                    ESP_LOGE(TAG,"Could not queue BT data");
                }
                else ESP_LOGD(TAG, "BT data queued");
            }
        }
        break;
    }
    case ESP_HIDH_FEATURE_EVENT: {
        const uint8_t *bda = esp_hidh_dev_bda_get(param->feature.dev);
        if (bda) {
            ESP_LOGD(TAG, ESP_BD_ADDR_STR " FEATURE: %8s, MAP: %2u, ID: %3u, Len: %d", ESP_BD_ADDR_HEX(bda),
                    esp_hid_usage_str(param->feature.usage), param->feature.map_index, param->feature.report_id,
                    param->feature.length);
            //ESP_LOG_BUFFER_HEX(TAG, param->feature.data, param->feature.length);
        }
        break;
    }
    case ESP_HIDH_CLOSE_EVENT: {        // HID device has closed down. Name is empty then
        const uint8_t *bda = esp_hidh_dev_bda_get(param->close.dev);
        if(bda) ESP_LOGI(TAG, ESP_BD_ADDR_STR " CLOSE: %s", ESP_BD_ADDR_HEX(bda), esp_hidh_dev_name_get(param->close.dev));
        // tell hid_task to restart the esp32
        bGamepadFound = false;
        ESP_LOGD(TAG, "Turn on blue connect LCD");
        gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL);
        break;
    }
    case ESP_HIDH_START_EVENT:  {   // introduced @JB
        ESP_LOGD(TAG, "hidh START");
        break;
    }
    default:
        ESP_LOGD(TAG, "Unhandled event: %d", event);
        break;
    }
}   /* hidh_callback */

#define SCAN_DURATION_SECONDS 5

/*****************************************************************
 * FreeRTOS task
 * This is the main loop over all found BT HID.
 * It scans them signalling features to stdout.
 * Actually it only opens the prescribed device remote_device_name
 * In case of a closed down connection it will restart the ESP32.
******************************************************************/
void hid_task(void *pvParameters)
{
    size_t results_len = 0;
    esp_hid_scan_result_t *results = NULL;
    bGamepadFound = false;

// no RESTART:
    while(!bGamepadFound)   {
        ESP_LOGI(TAG, "SCAN...");
        //start scan for HID devices. All at once
        esp_hid_scan(SCAN_DURATION_SECONDS, &results_len, &results);
        ESP_LOGI(TAG, "SCAN: %u results", results_len);
        if (results_len) {
            esp_hid_scan_result_t *r = results;     // results is a concatenated list
            esp_hid_scan_result_t *cr = NULL;       // current result to display

            // this while loop will end n case of no HID found/recognized
            while (r) {
                ESP_LOGI(TAG, "  %s: " ESP_BD_ADDR_STR ", ", (r->transport == ESP_HID_TRANSPORT_BLE) ? "BLE" : "BT ", ESP_BD_ADDR_HEX(r->bda));
                ESP_LOGI(TAG, "RSSI: %d, ", r->rssi);
                ESP_LOGI(TAG, "USAGE: %s, ", esp_hid_usage_str(r->usage));

#if CONFIG_BT_BLE_ENABLED
                if (r->transport == ESP_HID_TRANSPORT_BLE) {
                    //cr = r;
                    ESP_LOGI(TAG, "APPEARANCE: 0x%04x, ", r->ble.appearance);
                    ESP_LOGI(TAG, "ADDR_TYPE: '%s', ", ble_addr_type_str(r->ble.addr_type));
                }
#endif /* CONFIG_BT_BLE_ENABLED */
#if CONFIG_BT_NIMBLE_ENABLED
                if (r->transport == ESP_HID_TRANSPORT_BLE) {
                    cr = r;
                    ESP_LOGI(TAG, "APPEARANCE: 0x%04x, ", r->ble.appearance);
                    ESP_LOGI(TAG, "ADDR_TYPE: '%d', ", r->ble.addr_type);
                }
#endif /* CONFIG_BT_BLE_ENABLED */

#if CONFIG_BT_HID_HOST_ENABLED
                if (r->transport == ESP_HID_TRANSPORT_BT) {
                    //cr = r;
                    ESP_LOGI(TAG, "COD: %s[", esp_hid_cod_major_str(r->bt.cod.major));
                    // only with above ESP_LOG: esp_hid_cod_minor_print(r->bt.cod.minor, stdout);
                    ESP_LOGI(TAG, "] srv 0x%03x, ", r->bt.cod.service);
                    print_uuid(&r->bt.uuid);
                    ESP_LOGI(TAG, ", ");
                }
#endif /* CONFIG_BT_HID_HOST_ENABLED */

                ESP_LOGI(TAG, "NAME: %s ", r->name ? r->name : "");
                ESP_LOGI(TAG, "\n");

                // check if devise is my HZ-2746
                if(r->name) {
                    if(!strcmp(r->name, remote_device_name))    {
                        cr = r;
                        ESP_LOGI(TAG, "%s found!", remote_device_name);
                        break;
                    }
                } 
                r = r->next;
            }   /* while r*/

#if CONFIG_BT_HID_HOST_ENABLED
            if (cr) {   // && strncmp(cr->name, remote_device_name, strlen(remote_device_name)) == 0) {
                ESP_LOGI(TAG, "Gamepad %s found!", remote_device_name);
                esp_hidh_dev_t *devRet;
                devRet = esp_hidh_dev_open(cr->bda, cr->transport, cr->ble.addr_type); // return value is struct in esp_hidh_private.h
                if(devRet)   {
                    bGamepadFound = true;
                    ESP_LOGI(TAG, "Gamepad %s opened!", remote_device_name);
                    ESP_LOGD(TAG, "Turn on blue connect LCD");
                    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
                    while(bGamepadFound) vTaskDelay(pdMS_TO_TICKS(1000));
                }
                else ESP_LOGI(TAG, "esp_hidh_dev_open failed for Gamepad");
            }
            else{
                // restart scan ?
            }
#else
        if (cr) {
            //open the last result. In the end this calls from BT-API: esp_bt_hid_host_connect
            // if successfull CB function will get ESP_HIDH_OPEN_EVENT
            esp_hidh_dev_open(cr->bda, cr->transport, cr->ble.addr_type);
            // now we catch ESP_HIDH_INPUT_EVENT until we close the application or diconnect/close
        }
#endif // CONFIG_BT_HID_HOST_ENABLED

            //free the results
            esp_hid_scan_results_free(results);
            ESP_LOGI(TAG, "end of while with bGamepadFound=%d", bGamepadFound);
            results = NULL;
            break;
        }   // if results_len
        
        // a new run should not take place as long as BT connected
    }   /* while */

    
    // restart, because a new scan will not work korektly with other features
    ESP_LOGI(TAG, "Restarting after disconnect BT");
    esp_restart();

    vTaskDelete(NULL);
    ESP_LOGI(TAG, "hid_task ended.");

}   /* hid_task */


#if CONFIG_BT_NIMBLE_ENABLED
// This looks pretty incomplete !
void ble_hid_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}
void ble_store_config_init(void);
#endif



/**************************************************
 * Main routine
 * I do suppose, that nvs is used for BT internally
***************************************************/
void app_main_hid_host(void)
{
    esp_err_t ret;
#if HID_HOST_MODE == HIDH_IDLE_MODE
    ESP_LOGE(TAG, "Please turn on BT HID host or BLE!");
    return;
#endif

    // does BT/BLE use nvs????
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    // Init GAP for dual mode (HIDH_BTDM_MODE)
    // It seems impossible to just use BT witout BLE !??
    // But as far as I see, it ends with init_bt_gap() when CONFIG_BT_HID_HOST_ENABLED==1
    ESP_LOGD(TAG, "setting hid gap, mode:%d", HID_HOST_MODE);
    ESP_ERROR_CHECK( esp_hid_gap_init(HID_HOST_MODE) );

#if CONFIG_BT_BLE_ENABLED
    // GATTC only with BLE
    ESP_ERROR_CHECK( esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler) );
#endif /* CONFIG_BT_BLE_ENABLED */

    // now init HID host support
    esp_hidh_config_t config = {
        .callback = hidh_callback,
        .event_stack_size = 4096,
        .callback_arg = NULL,       // here we can provide an argument ("event data") that will be passed to the handler-CB, if called
    };
    ESP_ERROR_CHECK( esp_hidh_init(&config) );

#if !CONFIG_BT_NIMBLE_ENABLED
    char bda_str[18] = {0};
    ESP_LOGD(TAG, "Own address:[%s]", bda2str((uint8_t *)esp_bt_dev_get_address(), bda_str, sizeof(bda_str)));
#endif

#if CONFIG_BT_NIMBLE_ENABLED
    /* XXX Need to have template for store ??? */
    ble_store_config_init();

    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
	/* Starting nimble task after gatts is initialized*/
    ret = esp_nimble_enable(ble_hid_host_task);
    if (ret) {
        ESP_LOGE(TAG, "esp_nimble_enable failed: %d", ret);
    }
	
	vTaskDelay(200);

    uint8_t own_addr_type = 0;
    int rc;
    uint8_t addr_val[6] = {0};

    rc = ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, NULL, NULL);

    rc = ble_hs_id_infer_auto(0, &own_addr_type);

    if (rc != 0) {
        ESP_LOGI(TAG, "error determining address type; rc=%d\n", rc);
        return;
    }

    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);

    ESP_LOGI(TAG, "Device Address: ");
    ESP_LOGI(TAG, "%02x:%02x:%02x:%02x:%02x:%02x \n", addr_val[5], addr_val[4], addr_val[3],
		                                      addr_val[2], addr_val[1], addr_val[0])
#endif

    /*  xQueue communicates BT gamepad data to RdBT_task
    */
    xQueue = xQueueCreate( 200 , sizeof( struct sBtInput ) );
    if( xQueue == 0 )
    {
    // Queue was not created and must not be used.
        ESP_LOGE(TAG,"Could not create queue!");
        while(1) ;
    }

    /* We run only one task to handle all HID scan, open, close and featture display with no task handle.
        Return is pdPASS if the task was successfully created and added to a ready list,
        otherwise an error code defined in the file projdefs.h
    */
    BaseType_t xRet =
        xTaskCreatePinnedToCore(&hid_task, "hid_task", 6 * 1024, NULL, 2, NULL, 0);        
    if (xRet != pdPASS) ESP_LOGE(TAG, "hid_task not started!");
    // if this failed, nothing will show off
}
