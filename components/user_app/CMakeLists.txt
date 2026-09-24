idf_component_register(
  SRCS "user_app.cpp" "pet.c"
  PRIV_REQUIRES main lvgl_port_bsp i2c_bsp driver nvs_flash esp_timer esp_adc
  REQUIRES lcd_touch_bsp
  INCLUDE_DIRS "./")
