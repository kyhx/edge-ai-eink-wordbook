idf_component_register(SRCS "main.c" "epd.c" "audio_test.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_i2s esp_driver_gpio esp_driver_spi
                                     spi_flash esp_psram esp_timer)
