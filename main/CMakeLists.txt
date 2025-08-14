set(srcs "esp_hid_host_main.c"
         "esp_hid_gap.c"
		 "spi_lcd_touch_VSPI.c" 
		 "showPad.c"
         "main.c")
set(include_dirs "." "../include")

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${include_dirs}"
                       REQUIRES esp_event
                       REQUIRES esp_timer
                       REQUIRES esp_hid
                       REQUIRES lvgl
                       # REQUIRES esp_bt  #  Failed to resolve component 'esp_bt'.
                       PRIV_REQUIRES nvs_flash)
