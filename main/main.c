/*********************************************************************
 * Bluetooth Classic Control with LVGL Display
 * 
 * This is only a simple main prog that collects th two parts:
 *  LCD driving via lvgl 9.3.0
 *  BT scanning and HID host receiving from HZ-2746 wireless controller
 * 
 * See spi_lcd_touch_VSPI.c for LCD details
 * esp_hid_host_main.c (example from IDF 5.5) for BT host details
 * 
 * After some irritation I decides to return to IDF 5.3.1
 * 
 * 2025 Jürgen Böhm
 *  
 * 
 * Check cmake compiler flags by:
 * cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON build   (or build/Release ???)
 * It may come out like this:  -O3 -DNDEBUG -std=gnu++20 -arch arm64
 * If you want to set, e.g.:
 * CMAKE_CXX_FLAGS  "-Ofast -DNDEBUG -std=c++20 -march=native -fpic -ftree-vectorize")
 *
*********************************************************************/
#include "esp_hid_host_main.h"

extern void app_main_lcd(void);

/********************************************
 * Main of project
*********************************************/
void app_main(void) {

    app_main_hid_host();
    app_main_lcd();
    

}