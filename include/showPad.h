
/* showPad.h
Use these externals with lcgl.h
 lv_obj_t *bar_ltr, *bar_rtl;        // bars
 lv_obj_t *label_ltr, *label_rtl;    // bar labels
 lv_obj_t *btnTop, *btnBot, *btnB, *btnC, *btnD;            // labels/buttons

These events should be called:
 bar_ltr: LV_EVENT_KEY with userdata: xy, x is 2, y is value
 bar_rtl: LV_EVENT_KEY with userdata: xy, x is 4, y is value
To manually send events to an object, use lv_event_send(obj, <EVENT_CODE> &some_data).
in lvgl v9 use lv_obj_send_event(...) instead!
*/
void show_init(void);
void showWaitStart(void);
void showButtons(void);
void lv_example_bar_5(void);