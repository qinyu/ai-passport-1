idf_component_register(
    SRCS "main.c"
         "codex_protocol.c"
         "codex_model.c"
         "codex_ble.c"
         "codex_prefs.c"
         "codex_pet.c"
         "codex_ui.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt nvs_flash esp_timer esp_hw_support esp_app_format
)

target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/codex_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/codex_font_20.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/images/pi_pet.c"
)
