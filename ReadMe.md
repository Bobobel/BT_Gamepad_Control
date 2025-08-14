# Gamepad Controller : Drive an ESP32 via Bluetooth Classic

## What is it?
I bought a simple and cheap Bluetouth VR controller (HZ2746) and use it to drive some ESP32 (devkit V4) applications.
Here is the basic template in c 2017 slang that displays controller actions on a LCD screen (ILI9341).

## Used toolchain and libs
.vscode directory for VSCode is included. I use ESP-IDF V. 5.3.1 (and not the more recent 5.5 that has some problems with hidh) and the included xtensa GCC.
The GUI lib LVGL V. 9.3.0 is used as a private lib/component because I changed lv_bar.c in order to have vertical bars with downward orientation.
Nothing seriously changed in default sdkconfig, see sdkconfig.defaults.
I only used BT classic, so you probably have to select BLE/NIMBLE... in menuconfig if you use that.
> [!NOTE] 
> Only Bluetooth Classic is used here and tested!

## Used examples and templates
spi_lcd_touch.c with modifications from IDF 5.5
esp_hid_host example also with modification from IDF 5.5

## Errors and open problems
See HZ-2746.txt and entry of esp_hid_host_main.c
There are assert and hid errors in the serial log, but I could not find there reason or impact.

## Possible adaptions and easy modifications
You may use the ESP_LOG serial output from scanning devices in order to find your gamepad or other BT input devices.
The LCD screen is very popular. If you are using another type look for those, that are mentioned at https://components.espressif.com/components?q=esp_lcd
In order to adapting the BT message IDs and length of records:
    1. see esp_hid_host_main.c callback hidh_callback(...): For case "ESP_HIDH_INPUT_EVENT" you will select the IDs of your choice.
        Eventually you will have to increase "struct sBtInput" in esp_hid_host_main.h
    2. In spi_lcd_touch_VSPI.c you will may choose the HSPI channel. 
    3. In this template I did not use touch features, but I included it from the original source.
    4. spi_lcd_touch_VSPI.c FreeRTOS task RdBT_task is the place where you can implement your own control functions below "xQueueReceive".

If you can use this software with a BLE device, please give me a note!

> [!NOTE] 
> As this software is provided as it is, so I will not help you with modifications.

## Credits and license
ESP-IDF 1.10.3 extension of VS-Code, see [VSCode](https://code.visualstudio.com/download) , 
LVGL 9.3.0 https://lvgl.io

Licensed under GPL v2 [GPL V3](https://www.gnu.org/licenses/gpl-3.0.html.en)
> All links without any accountability! Use it on your own authority.   


