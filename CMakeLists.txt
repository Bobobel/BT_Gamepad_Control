cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
# idf_build_set_property(MINIMAL_BUILD ON)

# Specify path to own LVGL config header
# from https://docs.lvgl.io/8.3/get-started/platforms/cmake.html?highlight=lv_conf%20h
#set(LV_CONF_PATH
    # ${CMAKE_CURRENT_SOURCE_DIR}/src/lv_conf.h
  #  ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h
  #  CACHE STRING "" FORCE)

project(BT_MotorControl)


