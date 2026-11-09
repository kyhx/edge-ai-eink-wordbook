idf_component_register(SRCS "main.c" "epd.c" "gfx.c" "audio_test.c"
                            "fonts/font_word.c" "fonts/font_ipa.c"
                            "fonts/font_small.c" "fonts/font_cjk.c"
                            "fonts/vocab_data.c"
                       INCLUDE_DIRS "." "fonts"
                       PRIV_REQUIRES esp_driver_i2s esp_driver_gpio esp_driver_spi
                                     spi_flash esp_psram esp_timer)
