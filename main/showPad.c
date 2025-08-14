/*******************************************************
 * Shows BT gamepad controls:
 * 5 buttons and two directional cursors (joystick)
 * using lvgl simplified "buttons" and bars.
 * The Gamepad has 6 Buttons, but in mode @D
 * button A and top firebutton are synchronized.
********************************************************/
#include <string.h>
#include "freertos/FreeRTOS.h"      // vTaskDelay
#include "lvgl.h"   //lvgl/lvgl.h"

#include "esp_err.h"
#include "esp_log.h"

/* from template: a 2.8 or a 3.5" display is small but not used here
typedef enum {
    DISP_SMALL,
    DISP_MEDIUM,
    DISP_LARGE,
} disp_size_t;
static disp_size_t disp_size;
static const lv_font_t *font_large;
static const lv_font_t *font_normal;
*/

static const char TAG[] = {"showPad"};

static lv_style_t style_bar;

// these are used externally:
lv_obj_t *bar_up, *bar_ltr, *bar_down, *bar_rtl;            // bars 1..4
lv_obj_t *label_up, *label_ltr, *label_down, *label_rtl;    // bar labels
lv_obj_t *btnTop, *btnBot, *btnB, *btnC, *btnD;             // labels/buttons A and TO are the same. Button A eliminated
char *B1 = {"FireTop"};
char *B2 = {"FireBot"};
//char *B3 = {"A"};
char *B4 = {"B"};
char *B5 = {"C"};
char *B6 = {"D"};
int numB1=1, numB2=2, numB4=4, numB5=5, numB6=6;

char *up_val, *ltr_val, *down_val, *rtl_val;    // values of the bars
uint32_t event_mark, event_release;             // marked or released labels/buttons
const uint32_t colDef = 0x115588, colExcit = 0x661100;
lv_color_t lvColDef ;
lv_color_t lvColExcit ;

// flag for RdBT and display not to start too early
extern bool bBtReady;
// LVGL library is not thread-safe, this example will call LVGL APIs from different tasks, so use a mutex to protect it
extern _lock_t lvgl_api_lock;

/***********************************
 * Init styles
************************************/
void show_init(void)    {

/* not used:
    disp_size = DISP_SMALL;
    font_large = LV_FONT_DEFAULT;
    font_normal = LV_FONT_DEFAULT;
#if LV_FONT_MONTSERRAT_18
    font_large     = &lv_font_montserrat_18;
#else
        //LV_LOG_WARN("LV_FONT_MONTSERRAT_18 is not enabled for the widgets demo. Using LV_FONT_DEFAULT instead.");
        ESP_LOGW(TAG,"LV_FONT_MONTSERRAT_18 is not enabled. Using LV_FONT_DEFAULT instead.");
#endif
#if LV_FONT_MONTSERRAT_12
    font_normal    = &lv_font_montserrat_12;
#else
        //LV_LOG_WARN("LV_FONT_MONTSERRAT_12 is not enabled for the widgets demo. Using LV_FONT_DEFAULT instead.");
        ESP_LOGW(TAG,"LV_FONT_MONTSERRAT_12 is not enabled. Using LV_FONT_DEFAULT instead.");
#endif

*/

    /*Create a simple bar style*/
    lv_style_init(&style_bar);
    lv_style_set_bg_grad_color(&style_bar, lv_palette_main(LV_PALETTE_DEEP_PURPLE));

    lvColDef = lv_color_hex(colDef);
    lvColExcit = lv_color_hex(colExcit);

}   /* show_init */

/*****************************************
 * Event handler for the "button" objects.
******************************************/
static void label_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    //char *ud = (char *)e->user_data;  
    int *ud = NULL;

    /*
    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG,"%s clicked", ud);
    }
    else if(code == LV_EVENT_PRESSED) {
        ESP_LOGI(TAG,"%s pressed", ud);
    }
    else if(code == LV_EVENT_RELEASED) {
        ESP_LOGI(TAG,"%s released", ud);
    }
    else if(code == LV_EVENT_VALUE_CHANGED) {
        ESP_LOGI(TAG,"%s toggled", ud);
    }
    */
    ud = (int *)lv_event_get_user_data(e);

    if(code == event_mark) {
        ud = (int *)lv_event_get_user_data(e);
        ESP_LOGD(TAG,"B%d marked", *ud);
        _lock_acquire(&lvgl_api_lock);
        switch(*ud) {
            case 1:
                lv_obj_set_style_bg_color(btnTop, lvColExcit, 0);
                break;
            case 2:
                lv_obj_set_style_bg_color(btnBot, lvColExcit, 0);
                break;
            case 4:
                lv_obj_set_style_bg_color(btnB, lvColExcit, 0);
                break;
            case 5:
                lv_obj_set_style_bg_color(btnC, lvColExcit, 0);
                break;
            case 6:
                lv_obj_set_style_bg_color(btnD, lvColExcit, 0);
                break;
            default:
                break;
        }
        _lock_release(&lvgl_api_lock);
    }
    else if(code == event_release) {
        ud = (int *)lv_event_get_user_data(e);
        ESP_LOGD(TAG,"B%d released", *ud);
        _lock_acquire(&lvgl_api_lock);
        switch(*ud) {
            case 1:
                lv_obj_set_style_bg_color(btnTop, lvColDef, 0);
                break;
            case 2:
                lv_obj_set_style_bg_color(btnBot, lvColDef, 0);
                break;
            case 4:
                lv_obj_set_style_bg_color(btnB, lvColDef, 0);
                break;
            case 5:
                lv_obj_set_style_bg_color(btnC, lvColDef, 0);
                break;
            case 6:
                lv_obj_set_style_bg_color(btnD, lvColDef, 0);
                break;
            default:
                break;
        }
        _lock_release(&lvgl_api_lock);
    }
    
    else {
        //ESP_LOGV(TAG,"Button event %d unknown", code);
    }
}   /* label_handler */


/*************************************************************
  Show wait screen until bBtReady signals gamepad found.
  Then display this information and @D tipp.
**************************************************************/
void showWaitStart(void)
{
    lv_obj_t * label;

    _lock_acquire(&lvgl_api_lock);
        label = lv_label_create(lv_screen_active());
        lv_label_set_text(label,"Please wait for connection");
        lv_obj_set_style_text_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN);
        lv_obj_center(label);
    _lock_release(&lvgl_api_lock);

    // now wait for bluetooth connected
    while(!bBtReady)    {
        vTaskDelay(10);
    }

    // now we are ready
    _lock_acquire(&lvgl_api_lock);
        lv_label_set_text(label,"Gamepad connected: use @D !");
        //lv_obj_center(label);
    _lock_release(&lvgl_api_lock);
    
    // delay for user's eye
    vTaskDelay(pdMS_TO_TICKS(2000));

    // clear screen
    _lock_acquire(&lvgl_api_lock);
        lv_obj_clean(lv_scr_act());
    _lock_release(&lvgl_api_lock);
    
}

/*********************************************************************************************
    Does not display "buttons" but "labels" with some surrounding 
    that are easier to change (background) by events.
    I use two custom events: event_mark, event_release .
    Any number of custom event codes can be registered by uint32_t MY_EVENT_1 = lv_event_register_id()
**********************************************************************************************/
void showButtons(void)      // lv_obj_t * parent)
{
    //lv_obj_t * label;
    uint16_t event_filter1, event_filter2;

    // register two special user events in order to change the labels/"buttons"
    event_mark = lv_event_register_id();
    event_release = lv_event_register_id();
    ESP_LOGI(TAG,"user events %lu, %lu", event_mark, event_release );
    event_filter1 = event_mark;
    event_filter2 = event_release;

    // create a style for the rectangle around each label
    static lv_style_t style_label;
    lv_style_init(&style_label);
    //lv_style_set_radius(&style_label, 5);
    lv_style_set_width(&style_label, 70);
    lv_style_set_height(&style_label, 30);
    /* Properties to describe spacing between the parent's sides and the children and among the children. */
    lv_style_set_pad_ver(&style_label, 10);
    lv_style_set_pad_left(&style_label, 10);
    lv_style_set_bg_color(&style_label, lvColDef);
    lv_style_set_text_color(&style_label, lv_color_white());

    // now 5 labels for two firebuttons and buttons B,C,D. "A" is syncro with a firebutton, so eliminated.

    btnTop = lv_obj_create(lv_scr_act());    // create(parent);    
    lv_obj_add_style(btnTop, &style_label, 0);
    lv_obj_add_event_cb(btnTop, label_handler, event_filter1, (void*)&numB1);
    lv_obj_add_event_cb(btnTop, label_handler, event_filter2, (void*)&numB1);
    lv_obj_align(btnTop, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_t * label1 = lv_label_create(btnTop);
    lv_label_set_text(label1, B1);
    lv_obj_center(label1);

    btnBot = lv_obj_create(lv_scr_act());
    lv_obj_add_style(btnBot, &style_label, 0);
    lv_obj_add_event_cb(btnBot, label_handler, event_filter1, (void*)&numB2);
    lv_obj_add_event_cb(btnBot, label_handler, event_filter2, (void*)&numB2);
    lv_obj_align(btnBot, LV_ALIGN_TOP_RIGHT, -10, 48);
    lv_obj_t * label2 = lv_label_create(btnBot);
    lv_label_set_text_static(label2, B2);
    lv_obj_center(label2);

    btnB = lv_obj_create(lv_scr_act());    
    lv_obj_add_style(btnB, &style_label, 0);
    lv_obj_add_event_cb(btnB, label_handler, event_filter1, (void*)&numB4);
    lv_obj_add_event_cb(btnB, label_handler, event_filter2, (void*)&numB4);
    lv_obj_align(btnB, LV_ALIGN_TOP_RIGHT, -10, 124);
    lv_obj_t * label4 = lv_label_create(btnB);
    lv_label_set_text_static(label4, B4);
    lv_obj_center(label4);    

    btnC = lv_obj_create(lv_scr_act());    
    lv_obj_add_style(btnC, &style_label, 0);
    lv_obj_add_event_cb(btnC, label_handler, event_filter1, (void*)&numB5);
    lv_obj_add_event_cb(btnC, label_handler, event_filter2, (void*)&numB5);
    lv_obj_align(btnC, LV_ALIGN_TOP_RIGHT, -10, 162);
    lv_obj_t * label5 = lv_label_create(btnC);
    lv_label_set_text_static(label5, B5);
    lv_obj_center(label5);

    btnD = lv_obj_create(lv_scr_act());    
    lv_obj_add_style(btnD, &style_label, 0);
    lv_obj_add_event_cb(btnD, label_handler, event_filter1, (void*)&numB6);
    lv_obj_add_event_cb(btnD, label_handler, event_filter2, (void*)&numB6);
    lv_obj_align(btnD, LV_ALIGN_TOP_RIGHT, -10, 200);
    lv_obj_t * label6 = lv_label_create(btnD);
    lv_label_set_text_static(label6, B6 );
    lv_obj_center(label6); 

}   /* showButtons */

/*******************************************************
 * Bar values are directly set in RdBT
********************************************************/
/* NOT USED !
static void bar_hnd(lv_event_t * e)
{
    // lv_event_code_t code = lv_event_get_code(e);
    // char *ud = (char *)lv_event_get_user_data(e);
}
*/

/******************************************************************
 * Horizontal and vertical bars with LTR and RTL base direction.
 * Each bar has a label showing it's value.
 * Because lvgl did not "RTL" vertical bars, I had to change the
 * given lv_bar.c (just in one line).
 * Bar events are not used. Values and labels ar set in RdBT_task.
*******************************************************************/
void lv_example_bar_5(void)
{
    char val[10];

    // first up bar
    strncpy(val,"1",4);
    up_val = val;
    bar_up = lv_bar_create(lv_scr_act());
    //lv_obj_add_event_cb(bar_up, bar_hnd, LV_EVENT_KEY, up_val);     // Get the key with `lv_indev_get_key(lv_indev_get_act());
    lv_obj_add_style(bar_up, &style_bar, LV_PART_INDICATOR);
    //lv_obj_add_style(bar_up, &style_bar, LV_PART_MAIN);           // LV_PART_MAIN==0    
    //lv_obj_set_style_base_dir(bar_up, LV_BASE_DIR_RTL, 0);        // no visible effect
    lv_bar_set_range(bar_up, 0, 8);
    lv_obj_set_size(bar_up, 20, 90);
    lv_bar_set_value(bar_up, 0, LV_ANIM_OFF);
    lv_obj_align(bar_up, LV_ALIGN_TOP_MID, -40, 10);

    label_up = lv_label_create(lv_scr_act());
    lv_label_set_text(label_up, "3");
    lv_obj_align_to(label_up, bar_up, LV_ALIGN_OUT_LEFT_MID, -5, 0);

    // 2nd bar to the right
    strncpy(val,"2",4);
    ltr_val = val;
    bar_ltr = lv_bar_create(lv_scr_act());
    //lv_obj_add_event_cb(bar_ltr, bar_hnd, LV_EVENT_KEY, ltr_val);   // Get the key with `lv_indev_get_key(lv_indev_get_act());
    lv_obj_add_style(bar_ltr, &style_bar, LV_PART_INDICATOR);
    //lv_obj_add_style(bar_ltr, &style_bar, LV_PART_MAIN);          // LV_PART_MAIN==0
    lv_bar_set_range(bar_ltr, 0, 8);
    lv_obj_set_size(bar_ltr, 90, 20);
    lv_bar_set_value(bar_ltr, 0, LV_ANIM_OFF);
    lv_obj_align(bar_ltr, LV_ALIGN_LEFT_MID, 140, 0);

    label_ltr = lv_label_create(lv_scr_act());
    lv_label_set_text(label_ltr, "5");
    lv_obj_align_to(label_ltr, bar_ltr, LV_ALIGN_OUT_TOP_MID, 0, -5);

    // third bar down
    strncpy(val,"3",3);
    down_val = val;
    bar_down = lv_bar_create(lv_scr_act());
    //lv_obj_add_event_cb(bar_down, bar_hnd, LV_EVENT_KEY, down_val);     // Get the key with `lv_indev_get_key(lv_indev_get_act());
    lv_obj_add_style(bar_down, &style_bar, LV_PART_INDICATOR);          // with 5.5. Before with 3.x: LV_BAR_DRAW_PART_INDICATOR);
    //lv_obj_add_style(bar_down, &style_bar, LV_PART_MAIN);             // LV_PART_MAIN==0
    //lv_obj_set_style_base_dir(bar_down, LV_BASE_DIR_RTL, 0);
    lv_bar_set_range(bar_down, 0, 8);
    lv_obj_set_size(bar_down, 20, 90);
    lv_bar_set_value(bar_down, 0, LV_ANIM_OFF);
    lv_obj_align(bar_down, LV_ALIGN_BOTTOM_MID, -40, -10);

    label_down = lv_label_create(lv_scr_act());
    lv_label_set_text(label_down, "11");
    lv_obj_align_to(label_down, bar_down, LV_ALIGN_OUT_LEFT_MID, -5, 0);

    // forth bar to the left
    strncpy(val,"4",3);
    rtl_val = val;
    bar_rtl = lv_bar_create(lv_scr_act());
    //lv_obj_add_event_cb(bar_rtl, bar_hnd, LV_EVENT_KEY, rtl_val);
    lv_obj_add_style(bar_rtl, &style_bar, LV_PART_INDICATOR);
    lv_bar_set_range(bar_rtl, 0, 8);
    lv_obj_set_style_base_dir(bar_rtl, LV_BASE_DIR_RTL, 0);
    lv_obj_set_size(bar_rtl, 90, 20);
    lv_bar_set_value(bar_rtl, 0, LV_ANIM_OFF);
    lv_obj_align(bar_rtl, LV_ALIGN_LEFT_MID, 10, 0);

    label_rtl = lv_label_create(lv_scr_act());
    lv_label_set_text(label_rtl, "7");
    lv_obj_align_to(label_rtl, bar_rtl, LV_ALIGN_OUT_TOP_MID, 0, -5);

}   /* lv_example_bar_5 */
