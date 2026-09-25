idf_component_register(
  SRCS "user_app.cpp" "pet.c" "art.c"
  PRIV_REQUIRES main lvgl_port_bsp i2c_bsp driver nvs_flash esp_timer esp_adc esp_driver_usb_serial_jtag
  REQUIRES lcd_touch_bsp
  INCLUDE_DIRS "./")
