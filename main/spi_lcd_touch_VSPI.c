/*******************************************************************************************
 * App using lvgl 9.3 and spi-lcd (IDF 5.3.1) display with touchscreen ILI9341
 * 
 * Modified and adapted spi_lcd_touch.c:
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 * Now merged halfway to IDF version 5.3.1 (5.5 had additional errors with HID)
 * 
 * Modified working version :
 *  Changed from HSPI to VSPI with LCD-CS=GPIO05, T-CS=GPIO21
 *  HSPI will be used for JTAG debugging.
 *  LED2 will show Backlight on/off
 * 
 * Attention LVGL:
 *  lv_conf.h is not really necessary because there is a Kconfig file in lvgl__lvgl.
 *  Otherwise copy lv_conf.h into managed component at same level as lvgl__lvgl\
 *  It is advised to put lvgl into own components for lasting changes
 *  lvgl is NOT thread safe, so any actions with lvgl widgets... should take place inside 
 *  example_lvgl_lock(-1) and example_lvgl_unlock that handles lvgl_mux semaphore (IDF 5.3.1)
 *  IDF 5.3.1 and LCGL 9.0 have new task lock scheme: see newlib sys/lock.h
 *  But this is mayby "oldschool": "Compatibility definitions for legacy newlib locking functions" 
 *  Though the template used "_lock_acquire" ... I stayed with that, not going to esp_openthread.
 *  I used "#ifndef LVGL9" for previous version and "#ifdef LVGL9" for lvgl  version >=9.0
 *  
 *  After disp_drv register: lv_disp_set_rotation(disp, LV_DISP_ROT_90); for landscape !
 * 
 * Touch Controller: xpt2046 initialized and setup but not used here! Mybe of help (without any warrenty), when used:
 *  LV_COLOR_16_SWAP = 1 (true)
 *      esp_lcd_touch_set_swap_xy(tp, true);
        esp_lcd_touch_set_mirror_y(tp, false);
        esp_lcd_touch_set_mirror_x(tp, false);
    With MY_BOX2 the origin is opposite left USB plug
    LV_DISP_ROT_NONE gives portrait mode with USB plug top.
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);
        Touch : no swap, only mirror y
    LV_DISP_ROT_90 gives landscape:
        esp_lcd_panel_swap_xy(panel_handle, true) : bottom when USB is right  
        mirror both true!
        Touch : no swap, only mirror y
    LV_DISP_ROT_180 and 270 : Touch not yet tested !
    LV_DISP_ROT_180 Portrait, USB bottom
        xy and mirror all true
        Touch : ???
    LV_DISP_ROT_270 gives landscape:
        esp_lcd_panel_swap_xy(panel_handle, false) : bottom when USB left
        mirror : both false
        Touch : ???

    esp_lvgl_port component: https://components.espressif.com/components/espressif/esp_lvgl_port
    This component supports LVGL8 and LVGL9 depending on idf_component.yml:  
    lvgl/lvgl:
        version: "^8"
    Please, be aware, that some draw and object functions are not compatible between LVGL8 and LVGL9.

 * xQueue (external) data is received from esp_hid_host_main.c
 * 
 * Some more externals from showPad.c are used to set bar values and init events for buttons
 * 
 * Attention DMA memory: in IDF 5.5 it is changed, see define "V5_5". I am using older 5.3.1 for more stability
 * 
 * I use two FreeRTOS tasks, both on CORE0
 * 1) lvgl_task:
 *      prescribed task for lvgl's timer handler.
 *      Calling lv_timer_handler enclosed within locks.
 *      It is still unclear, if there are some timeout ticks to observe like in version lvgl 8?
 *      There is a callback "inc_lvgl_tick" for esp_timer, but I do not know, if it is still of use ??
 * 2) RdBT_taks
 *      Read BlueTooth task.
 *      Waits for bBtReady and then receives xQueue data within an inifinite loop.
 *      Bar values from the joystick are directly changed in lvgl.
 *      Button data (either with ID2 or ID3) are communicated to showPad.c via send_event.
 *      As I do not save a timestamp for button presses I can not distinguish release messages.
 *      Thus it is not allowed to press more than one button at a time !
 * 
 * 
 * Problems:
 *  Still unknown if espressif__esp_lvgl_port-v2.6.0 will do as well (more recent version 9; source by lvgl.io)
 *  ESP-PROG : OpenOCD server: launch.json vermutlich fehlerhaft/unvollständig.
 *  Erste Adresse des FTDI Treiber is JTAG/Debug Schnittstelle, die ist zweite ist prog/flash.
 *  Anscheinend muss man Interface0 immer wieder neu einrichten:
 *  https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/jtag-debugging/configure-ft2232h-jtag.html
 * Debugged: Prog hält mit:
 *  Thread 4 "IDLE0" received signal SIGINT, Interrupt.
    0x40085ec2 in esp_cpu_wait_for_intr () at C:/Espressif/esp-idf-v5.3.1/components/esp_hw_support/cpu.c:64
 * FreeRTOS verwendet Timer0 und LVGL benutzt: esp-timer, also software timer
 * Buttons are for GUI, not for event control. Better use simple labels or colored rectangles

 * Solved problems:
 *  Heap size not all available/docu wrong, but heap_caps_get_free_size(MALLOC_CAP_DEFAULT) works
 *  RdBT_task hangs after some (up to 20) minutes.
 *  Reason: LVGL with prio 2 und RdBTmit prio 1. Now both prios == EXAMPLE_LVGL_TASK_PRIORITY
 *  Tested for 30 minutes.
 *  ESP-IDF version 5.5 has a lot of bugs with esp_bt_hid.c ... So returned to v5.3.1   .
 *  Some components have to be enclosed by: 
 *                     REQUIRES esp_event
                       REQUIRES esp_timer
                       REQUIRES esp_hid
                       REQUIRES lvgl

 * LV_LOG_WARN("%p style was not found on %p widget with %6lx selector", (void *)style, (void *)obj, selector);  before: %6x selector

 * Because app_main_lcd starts first, we have to wait with processing BT data, until esp_hidh_dev_open found our gamepad -> bBtReady==true
 * There is also an external simple bool var "bBtReady" used as a semaphore and set only from esp_hid_host_main!
 * 
 */

#include <stdio.h>
// three new in v5.5
#include <unistd.h>
#include <sys/lock.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"        // not in V5.3.1/V5.5

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
//#include "esp_heap_caps.h"        //@JB better later on, see below!
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_task_wdt.h"           // I have to cancel IDLE0 watchdog: esp_task_wdt_deinit
#include "esp_err.h"
#include "esp_log.h"

#include "esp_timer.h"              // lvgl need a millisecnd timer          
#include "showPad.h"
#include "lvgl.h"
#include "esp_hid_host_main.h"


#if CONFIG_EXAMPLE_LCD_CONTROLLER_ILI9341
#include "esp_lcd_ili9341.h"
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01
#include "esp_lcd_gc9a01.h"
#endif

#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_STMPE610
#include "esp_lcd_touch_stmpe610.h"
#endif

#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_XPT2046
#include "esp_lcd_touch_xpt2046.h"
#endif

#include "esp_heap_caps.h"      //@JB not <> for heap_caps_malloc 

static const char *TAG = "BT_MC";

#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wformat-truncation"

//#define V5_5    : not used due to problems with hidh.c

// ESP-IDF and LCGL 9.0 have new task lock scheme, not freertos semaphore anymore, see newlib sys/lock.h
#define LVGL9

#ifdef LVGL9
    #define lv_event_send lv_obj_send_event 
#endif

// Not using SPI2/HSPI as in the example, because connected to esp_prog debugging
//#define LCD_HOST  SPI2_HOST
// or SPI3/VSPI:
#define LCD_HOST  SPI3_HOST

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ     (20 * 1000 * 1000)
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL

#if LCD_HOST == SPI3_HOST
    #define EXAMPLE_PIN_NUM_SCLK           18
    #define EXAMPLE_PIN_NUM_MOSI           23
    #define EXAMPLE_PIN_NUM_MISO           19
    #define EXAMPLE_PIN_NUM_LCD_DC         22
    #define EXAMPLE_PIN_NUM_LCD_RST        4
    #define EXAMPLE_PIN_NUM_LCD_CS         5

#elif LCD_HOST == SPI2_HOST
    #define EXAMPLE_PIN_NUM_SCLK           14
    #define EXAMPLE_PIN_NUM_MOSI           13
    #define EXAMPLE_PIN_NUM_MISO           12
    #define EXAMPLE_PIN_NUM_LCD_DC         26
    #define EXAMPLE_PIN_NUM_LCD_RST        25
    #define EXAMPLE_PIN_NUM_LCD_CS         15
#endif

#define EXAMPLE_PIN_NUM_BK_LIGHT       2
// T-CS remains constant:
#define EXAMPLE_PIN_NUM_TOUCH_CS       21

// The pixel number in horizontal and vertical (portrait mode)
#if CONFIG_EXAMPLE_LCD_CONTROLLER_ILI9341
    #define EXAMPLE_LCD_H_RES              240
    #define EXAMPLE_LCD_V_RES              320
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01
    #define EXAMPLE_LCD_H_RES              240
    #define EXAMPLE_LCD_V_RES              240
#endif

// Bit number used to represent command and parameter
#define EXAMPLE_LCD_CMD_BITS           8
#define EXAMPLE_LCD_PARAM_BITS         8

#define EXAMPLE_LVGL_TICK_PERIOD_MS    2
// New for locking with semaphores, lvgl muxes
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1000 / CONFIG_FREERTOS_HZ

#define EXAMPLE_LVGL_TASK_STACK_SIZE   (4 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY     2

#define RDBTDELAY 100

#ifdef LVGL9
    // LVGL library is not thread-safe, this example will call LVGL APIs from different tasks, so use a mutex to protect it
    _lock_t lvgl_api_lock;
#else   // lvgl 8.x
    tatic SemaphoreHandle_t lvgl_mux = NULL;
#endif

#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
    esp_lcd_touch_handle_t tp = NULL;
#endif

// no need for extern :
static sBtInput_t BT_input;

// extenals from showPad
extern lv_obj_t *bar_up, *bar_ltr, *bar_down, *bar_rtl;         // bars 1..4
//extern lv_obj_t *label_ltr, *label_rtl;                       // bar labels
extern lv_obj_t *btnTop, *btnBot, *btnB, *btnC, *btnD;          // labels/buttons   button A eliminated, same as btnTop
//extern char *B1, *B2, /* *B3, */ *B4, *B5, *B6;               // button names
extern int numB1, numB2, numB4, numB5, numB6;                   // label numbers to send with events
//extern char *up_val, *ltr_val, *down_val, *rtl_val;           // bar's values 
extern lv_obj_t *label_up, *label_ltr, *label_down, *label_rtl; 
// marked or released labels/buttons
extern uint32_t event_mark, event_release;     

// from esp_hid_host_main.c:
extern QueueHandle_t xQueue;
// is queue open and ready to send?
extern bool bBtReady;


void my_lvgl_port_update_callback(lv_display_t *disp);


/******************************************************
 * callback for LVGL flush ready notification
*******************************************************/
bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
#ifdef LVGL9
    lv_display_t *disp_driver = (lv_display_t *)user_ctx;       // silly naming: no driver but a screen area from user context
    lv_disp_flush_ready(disp_driver);
    return false;
#else
    // missing !!!!
#endif
}
/*********************************************
 * CB to flush the given area into display
'''''''''''''''''''''''''''''''''''''''''''''*/
#ifdef LVGL9
void my_lvgl_flush_ready_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *color_map)
#else
void my_lvgl_flush_ready_cb(lv_display_t *disp, const lv_area_t *area, lv_color_t *color_map)
#endif
{
    // new in V5.5
    my_lvgl_port_update_callback(disp);
#ifdef LVGL9
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
#else
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) disp->user_data;
#endif
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    // @JB new in V5.5, but useless/contradictory because we swap by conf.h
    // because SPI LCD is big-endian, we need to swap the RGB bytes order
    // lv_draw_sw_rgb565_swap(px_map, (offsetx2 + 1 - offsetx1) * (offsety2 + 1 - offsety1));

    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
}

/* Rotate display and touch, when rotated screen in LVGL. Called when driver parameters are updated. */
void my_lvgl_port_update_callback(lv_display_t *disp)
{
#ifdef LVGL9
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    lv_display_rotation_t rotation = lv_display_get_rotation(disp);

    switch (rotation) 
#else   // a little risky
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) disp->user_data;

    switch (disp->rotated) 
#endif
    {
    //ESP_LOGD(TAG, "port_update called. Rotation=%d", disp->rotated);
    
    case LV_DISPLAY_ROTATION_0: // perv: LV_DISP_ROT_NONE: LV_DISP_ROT_90: ,,,
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);
#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
        // Rotate LCD touch
        esp_lcd_touch_set_swap_xy(tp, false);   // default, when H_RES = 240
        esp_lcd_touch_set_mirror_y(tp, true);   // improve calibration ! 
        esp_lcd_touch_set_mirror_x(tp, false);
#endif
        break;
    case LV_DISPLAY_ROTATION_90:    // clockwise ! USB port right
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true);
#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
        // Rotate LCD touch
        esp_lcd_touch_set_swap_xy(tp, false);    
        esp_lcd_touch_set_mirror_y(tp, true);   // improve calibration ! Button at 30|-30 from bottom left fires when x=190,y=210 ???
        esp_lcd_touch_set_mirror_x(tp, false);  // x vertical, y->left
#endif
        break;
    case LV_DISPLAY_ROTATION_180:           // USB bottom
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, true);
#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
        // Rotate LCD touch
        esp_lcd_touch_set_swap_xy(tp, false);
        esp_lcd_touch_set_mirror_y(tp, true);
        esp_lcd_touch_set_mirror_x(tp, true);
#endif
        break;
    case LV_DISPLAY_ROTATION_270:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);  // must be
        esp_lcd_panel_mirror(panel_handle, false, false); // original: USB is left
#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
        // Rotate LCD touch
        esp_lcd_touch_set_swap_xy(tp, true);
        esp_lcd_touch_set_mirror_y(tp, false);
        esp_lcd_touch_set_mirror_x(tp, false);
#endif
        break;
    }
}   /* my_lvgl_port_update_callback */


#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED

static void example_lvgl_touch_cb(lv_indev_t *drv, lv_indev_data_t * data)     // V5.5 indev_t, not indev_drv_t
{
    uint16_t touchpad_x[1] = {0};
    uint16_t touchpad_y[1] = {0};
    uint8_t touchpad_cnt = 0;

    /* Read touch controller data */
#ifdef LVGL9     // useless, because lv_indev_t still includes user_data
    esp_lcd_touch_handle_t touch_pad = lv_indev_get_user_data(drv);
#else
    esp_lcd_touch_handle_t touch_pad = drv->user_data;     // replayced by touch_pad in what follows:
#endif
    esp_lcd_touch_read_data(touch_pad);

    /* Get coordinates */
    bool touchpad_pressed = esp_lcd_touch_get_coordinates(touch_pad, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);

    if (touchpad_pressed && touchpad_cnt > 0) {
        data->point.x = touchpad_x[0];
        data->point.y = touchpad_y[0];
    
        data->state = LV_INDEV_STATE_PRESSED;
        ESP_LOGV(TAG, "Touched! x=%u, y=%u", touchpad_x[0], touchpad_y[0]);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
#endif

// this is oblivious, because we use lv_conf.h: LV_TICK_CUSTOM==1
static void inc_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

#ifndef LVGL9
// replayced in V5.5 by sys/lock.h with _lock_acquire ..
bool example_lvgl_lock(int timeout_ms)
{
    // Convert timeout in milliseconds to FreeRTOS ticks
    // If `timeout_ms` is set to -1, the program will block until the condition is met 
    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE;
}

void example_lvgl_unlock(void)
{
    xSemaphoreGiveRecursive(lvgl_mux);
}
#endif

/********************************************
 * prescribed task for lvgl's timer handler
*********************************************/
static void lvgl_task(void *arg)
{
#ifdef LVGL9    
    uint32_t time_till_next_ms = 0;
#else
    uint32_t task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
#endif
    
    ESP_LOGD(TAG, "Starting LVGL task");
    
    while (1) {
#ifdef LVGL9
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        // in case of triggering a task watch dog time out
        time_till_next_ms = MAX(time_till_next_ms, EXAMPLE_LVGL_TASK_MIN_DELAY_MS);
        // in case of lvgl display not ready yet
        time_till_next_ms = MIN(time_till_next_ms, EXAMPLE_LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
#else
        // Lock the mutex due to the LVGL APIs are not thread-safe
        if (example_lvgl_lock(1000)) {
            task_delay_ms = lv_timer_handler();
            // Release the mutex
            example_lvgl_unlock();
        }
        if (task_delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS) {
            task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS) {
            task_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
#endif  // LVGL9
    }

}


// perform coordinate transformation into screen coords.
// will be called by esp_lcd_touch.c/esp_lcd_touch_get_coordinates()
// now with pixel coordinates, not raw coords
#define XPT2046_X_MIN 235   //CONFIG_XPT2046_X_MIN
#define XPT2046_X_MAX 3850  //CONFIG_XPT2046_X_MAX
#define XPT2046_Y_MIN 345  //CONFIG_XPT2046_Y_MIN
#define XPT2046_Y_MAX 3885  //CONFIG_XPT2046_Y_MAX

void myTouchCalibration(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num) {
    //ESP_LOGI(TAG,"TouchCalibration: x,y,z,num,maxnum: %u,%u,%u,%u,%u", *x,*y,*strength, *point_num, max_point_num);
    //ESP_LOGI(TAG, "x,y=%u,%u", *x, *y);
    if((*x) > XPT2046_X_MIN)    (*x) -= XPT2046_X_MIN;
    else(*x) = 0;

    if((*y) > XPT2046_Y_MIN)    (*y) -= XPT2046_Y_MIN;
    else(*y) = 0;

    (*x) = (uint16_t)(((uint32_t)(*x) * EXAMPLE_LCD_H_RES) /
           (XPT2046_X_MAX - XPT2046_X_MIN));

    (*y) = (uint16_t)(((uint32_t)(*y) * EXAMPLE_LCD_V_RES) /
           (XPT2046_Y_MAX - XPT2046_Y_MIN));
    ESP_LOGD(TAG, "After calibration (%u,%u)", *x, *y);

}   /* myTouchCalibration */



/********************************************
 * Read BlueTooth task 
 * Reads gamepad data via BT classic
 * At this moment it is only posible
 * to recognize a single button at a time !
*********************************************/
static void RdBT_task(void *arg)
{
    uint8_t mask_updown, mask_rl, mask_btn;
    bool btnBset=false, btnCset=false, btnDset=false, btnTopset=false, btnBotset=false;

    ESP_LOGD(TAG, "Starting RdBT task");
    
    // wait until esp_hid_host_main has found and opened the gamepad
    while (!bBtReady)    {
        vTaskDelay(10);
    }
    // give some head start for showWaitStart start display, otherwise labels are not ready in time
    vTaskDelay(10);

    ESP_LOGD(TAG, "RdBT-task waiting for xQueue");

    if( xQueue != 0 ) 
    {
        while (1)   {
            // Receive a message on the created queue.  Block for 10 ticks if a
            // message is not immediately available.
            // Gamepad joystick input is mostly 0..8, but sometime and not stable also >8.
            // Joystick values are set here, not in showPad, because it is more complicate to create special events for all details
            if( xQueueReceive( xQueue, (void *)BT_input, ( TickType_t ) (1000/portTICK_PERIOD_MS)))  {
                if ((BT_input->reportId == 2) && (BT_input->len == 4))   // Mouse/joystick 
                {
                    ESP_LOGD(TAG, "BT data 4 received.");
                    mask_updown = BT_input->data[2];
                    if(mask_updown>0xf0)    // up !
                    {
                        mask_updown &= 0xF;
                        if (mask_updown>8) mask_updown=8;
                        _lock_acquire(&lvgl_api_lock);
                        lv_bar_set_value(bar_up, (int32_t)mask_updown , LV_ANIM_OFF);    // limit
                        lv_label_set_text_fmt(label_up, "%u", mask_updown );
                        _lock_release(&lvgl_api_lock);
                    }
                    else if(mask_updown>0) // down !
                    {   // here no problem with >8
                        _lock_acquire(&lvgl_api_lock);
                        lv_bar_set_value(bar_down, (int32_t)mask_updown, LV_ANIM_OFF);
                        lv_label_set_text_fmt(label_down, "%u", mask_updown);
                        _lock_release(&lvgl_api_lock);
                    }
                    else if(!mask_updown)    // release
                    {
                        _lock_acquire(&lvgl_api_lock);
                        lv_bar_set_value(bar_up, 0, LV_ANIM_OFF);
                        lv_bar_set_value(bar_down, 0, LV_ANIM_OFF);
                        lv_label_set_text(label_up, "0");
                        lv_label_set_text(label_down, "0");
                        _lock_release(&lvgl_api_lock);
                    }

                    mask_rl = BT_input->data[1];    
                     if(mask_rl>0xf0)    // left !
                    {
                        mask_rl &= 0xF;
                        if (mask_rl>8) mask_rl=8;
                        _lock_acquire(&lvgl_api_lock);
                        lv_bar_set_value(bar_rtl, (int32_t)(mask_rl & 0xF), LV_ANIM_OFF);
                        lv_label_set_text_fmt(label_rtl, "%u", (mask_rl & 0xF));
                        _lock_release(&lvgl_api_lock);
                    }
                    else if(mask_rl>0) // right !
                    {
                        _lock_acquire(&lvgl_api_lock);
                        lv_bar_set_value(bar_ltr, (int32_t)mask_rl, LV_ANIM_OFF);
                        lv_label_set_text_fmt(label_ltr, "%u", mask_rl);
                        _lock_release(&lvgl_api_lock);
                    }
                    else if(!mask_rl)
                    {
                        _lock_acquire(&lvgl_api_lock);
                        lv_bar_set_value(bar_ltr, 0, LV_ANIM_OFF);
                        lv_bar_set_value(bar_rtl, 0, LV_ANIM_OFF);
                        lv_label_set_text(label_ltr, "0");
                        lv_label_set_text(label_rtl, "0");
                        _lock_release(&lvgl_api_lock);
                    }


                    if(BT_input->data[0] == 0x01) // firebutton TO and A 
                    {
                        
                        lv_event_send(btnTop, event_mark , (void*)&numB1);  // user event
                        btnTopset = true;
                        
                    }
                    else if(BT_input->data[0] == 0x08) // button B 
                    {
                        lv_event_send(btnB, event_mark , (void*)&numB4); 
                        btnBset = true;
                    }
                    else if(!BT_input->data[0])
                    {
                        // which one was set?
                        if(btnTopset)   { 
                            lv_event_send(btnTop, event_release , (void*)&numB1);
                            btnTopset=false; 
                        }
                        if(btnBset)   { 
                            lv_event_send(btnB, event_release , (void*)&numB4);  
                            btnBset=false; 
                        }
                    }
                    
                }   // Mouse/joystick 

                else if ((BT_input->reportId == 3) && (BT_input->len == 2))  // buttons 
                {
                    ESP_LOGD(TAG, "BT data 2 received.");
                    mask_btn = BT_input->data[0];
                    if(mask_btn==0xe9)   {    // btn C
                        lv_event_send(btnC, event_mark , (void*)&numB5);
                        btnCset = true;
                    }
                    else if(mask_btn==0x46)   {    // btn TU
                        lv_event_send(btnBot, event_mark , (void*)&numB2);
                        btnBotset = true;
                    }
                    else if(mask_btn==0xea)   {  // btn D
                        lv_event_send(btnD, event_mark , (void*)&numB6);
                        btnDset = true;
                    }
                    else  if(!mask_btn)   {  // release
                        if(btnCset) {
                            lv_event_send(btnC, event_release , (void*)&numB5);
                            btnCset = false;
                        }
                        if(btnBotset) {
                            lv_event_send(btnBot, event_release , (void*)&numB2);
                            btnBotset = false;
                        }
                        if(btnDset) {
                            lv_event_send(btnD, event_release , (void*)&numB6);
                            btnDset = false;
                        }
                    }
                }   // buttons
            }   // if xQueueReceive
            // not added to wdt : esp_task_wdt_reset(); 
            vTaskDelay(1);
        }   // while
    }   // if( xQueue != 0 ) 
    vTaskDelete(NULL);
}

/********************************************
 * Main of project
*********************************************/
void app_main_lcd(void)
{
    static lv_display_t *display;      // contains callback functions  V5.5

    // Get all chip , CPU and heap info
    // chipInfo();
    // always 0 : printf("Total heap at startup=%u\n", heap_caps_get_total_size(MALLOC_CAP_INVALID));
    printf("Total def. heap at startup=%u\n", heap_caps_get_total_size(MALLOC_CAP_DEFAULT));
    // always 0 : printf("Heap free at startup=%u\n", heap_caps_get_free_size(MALLOC_CAP_INVALID));
    printf("Def. heap free at startup=%u\n", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));

    // disable task-wdt for IDEL0 (CPU0)
    esp_task_wdt_deinit();

    ESP_LOGD(TAG, "Turn off blue connect LCD");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL);

    // get mem for Bt
    BT_input = (sBtInput_t)malloc(sizeof(struct sBtInput));
    assert(BT_input);

    ESP_LOGD(TAG, "Initialize SPI bus");
    spi_bus_config_t buscfg = {
        .sclk_io_num = EXAMPLE_PIN_NUM_SCLK,
        .mosi_io_num = EXAMPLE_PIN_NUM_MOSI,
        .miso_io_num = EXAMPLE_PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        //.max_transfer_sz = EXAMPLE_LCD_H_RES * 80 * sizeof(uint16_t),       // H_RES=240 : 38400,   H_RES=320 : 51200
        .max_transfer_sz = 0,    // Maximum transfer size, in bytes. Defaults to 4092 if 0 when DMA enabled: it is with ESP32!
    };  
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));    // DMA enabled!

    ESP_LOGD(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = EXAMPLE_PIN_NUM_LCD_DC,
        .cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS,
        .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
        .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        /* moved behind tick timer setupt
        .on_color_trans_done = notify_lvgl_flush_ready,
        .user_ctx = &display,
        */
    };  
    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    /* Version 5_5 example does this extra:
    ESP_LOGI(TAG, "Register io panel event callback for LVGL flush ready notification");
    const esp_lcd_panel_io_callbacks_t cbs = {.on_color_trans_done = example_notify_lvgl_flush_ready, };
    // Register done callback 
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, display));
    @JB I do think, that this gives a little more security, but directly within io_config will do.
    V 5.5 is not stable enough as of July 2025
    */

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order /*.rgb_endian depreciated in V5.5 */ = LCD_RGB_ENDIAN_BGR,
        .bits_per_pixel = 16,
    };
#if CONFIG_EXAMPLE_LCD_CONTROLLER_ILI9341
    ESP_LOGD(TAG, "Install ILI9341 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01
    ESP_LOGD(TAG, "Install GC9A01 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle));
#endif

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
#if CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));

    // user can flush pre-defined pattern to the screen before we turn on the screen or backlight
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    
// in V5.5 lvgl is init here !

#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    // Attach the TOUCH to the SPI bus
#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_STMPE610
    esp_lcd_panel_io_spi_config_t tp_io_config = ESP_LCD_TOUCH_IO_SPI_STMPE610_CONFIG(EXAMPLE_PIN_NUM_TOUCH_CS);
#endif
#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_XPT2046
    esp_lcd_panel_io_spi_config_t tp_io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(EXAMPLE_PIN_NUM_TOUCH_CS);
#endif
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,  
        },
        .process_coordinates = myTouchCalibration,
    };

#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_STMPE610
    ESP_LOGD(TAG, "Initialize touch controller STMPE610");
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_stmpe610(tp_io_handle, &tp_cfg, &tp));
#endif // CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_STMPE610
#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_XPT2046
    ESP_LOGD(TAG, "Initialize touch controller XPT2046");
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_cfg, &tp));
#endif
#endif // CONFIG_EXAMPLE_LCD_TOUCH_ENABLED


    // always 0 : printf("free before lvgl=%d\n", heap_caps_get_free_size(0x7FFFF));   // MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM | MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL));

    ESP_LOGD(TAG, "Initialize LVGL library");
    lv_init();
    // alloc draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    size_t draw_buffer_sz = EXAMPLE_LCD_H_RES * 32 * sizeof(lv_color16_t);     // 16 bit for colors
    
#ifdef LVGL9
    // create a lvgl display
    display = lv_display_create(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
    #ifdef V5_5
        void *buf1 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);     // new DMA allocation
        assert(buf1);
        void *buf2 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);
        assert(buf2);
    #else
        void *buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);   // 3*8 bit for colors
        assert(buf1);
        void *buf2 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);
        assert(buf2);
    #endif
        
    // initialize LVGL draw buffers
    lv_display_set_buffers(display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // associate the mipi panel handle to the display
    lv_display_set_user_data(display, panel_handle);
    // set color depth
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    // set the callback which can copy the rendered image to an area of the display
    lv_display_set_flush_cb(display, my_lvgl_flush_ready_cb);   // lv_display_flush_cb_t is void * ???

    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_90);   // LV_DISPLAY_ROTATION_0
    ESP_LOGD(TAG,"Rotation disp_drv:%d\n", lv_display_get_rotation(display));

#else // old 8.3 version
    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
    
    lv_color_t *buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);   // 3*8 bit for colors
    assert(buf1);
    lv_color_t *buf2 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);
    assert(buf2);
    // initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, EXAMPLE_LCD_V_RES * 20);

    ESP_LOGD(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);    // hor_res=320 as default
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;   // because rotation==0 as default with USB top
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb = my_lvgl_flush_ready_cb;
    disp_drv.drv_update_cb = my_lvgl_port_update_callback;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    // disp_drv.rotated = LV_DISP_ROT_;  : implicitly by lv_disp_set_rotation
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    lv_disp_set_rotation(disp, LV_DISP_ROT_90);    //rotation == 1
    //lv_disp_set_rotation(disp, LV_DISP_ROT_NONE);     //rotation == 0
    /*  same results:
    printf("Horizontal resolution:%d\n",lv_disp_get_hor_res(disp));
    printf("Full phys.hor. res.:%d\n",lv_disp_get_physical_hor_res(disp));
    */
    ESP_LOGD(TAG,"Rotation disp_drv:%d\n",lv_disp_get_rotation(disp));
    //ESP_LOGD(TAG,"Rotation disp==NULL:%d\n",lv_disp_get_rotation(NULL));

#endif // LVGL9


    ESP_LOGD(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &inc_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGD(TAG, "Register io panel event callback for LVGL flush ready notification");
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    /* Register done callback */
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, display));


#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
    #ifdef LVGL9
        static lv_indev_t *indev;
        indev = lv_indev_create(); // Input device driver (Touch)
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(indev, display);
        lv_indev_set_user_data(indev, tp);
        lv_indev_set_read_cb(indev, example_lvgl_touch_cb);
    #else
        static lv_indev_drv_t indev_drv;    // Input device driver (Touch)
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.disp = disp;
        indev_drv.read_cb = example_lvgl_touch_cb;
        indev_drv.user_data = tp;
        lv_indev_drv_register(&indev_drv);
    #endif //LVGL9
#endif

    // debugging heap:
    ESP_LOGI(TAG, "free before lvgl_task=%d\n", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));   // MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM | MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL));

#ifdef LVGL9
    //
#else
    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mux);
#endif

    ESP_LOGI(TAG, "Create LVGL task");
    BaseType_t xRet =
        xTaskCreatePinnedToCore(lvgl_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL, 0);
    if (xRet != pdPASS) ESP_LOGE(TAG, "LVGL task not started!");

    ESP_LOGD(TAG, "Create RdBT task");
    xRet = xTaskCreatePinnedToCore(RdBT_task, "RdBT", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL, 0);
    if (xRet != pdPASS) ESP_LOGE(TAG, "RdBT task not started!");

    ESP_LOGI(TAG, "Prepare gamepad data display");

#ifdef LVGL9
    _lock_acquire(&lvgl_api_lock);
        show_init();
    _lock_release(&lvgl_api_lock);

    showWaitStart();    // waits for bBtReady

        ESP_LOGI(TAG, "Show buttons");
    _lock_acquire(&lvgl_api_lock);
        showButtons();
        ESP_LOGD(TAG, "Show 4 bars");
        lv_example_bar_5();
    _lock_release(&lvgl_api_lock);
#else
    //lv_obj_t *scr = lv_disp_get_scr_act(disp);
    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (example_lvgl_lock(-1)) {
        show_init();
        showButtons();
        lv_example_bar_5();
        // Release the mutex
        example_lvgl_unlock();
    }
#endif

    // check heap
    heap_caps_check_integrity_all(true);
    ESP_LOGD(TAG,"heap_caps_check_integrity_all=%d", heap_caps_check_integrity_all(true));
    // always 0 : printf("free before RdBT_task=%d\n", heap_caps_get_free_size(MALLOC_CAP_DEFAULT)); // funkt nich:MALLOC_CAP_INVALID));

}
