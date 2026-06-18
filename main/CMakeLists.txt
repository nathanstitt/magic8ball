idf_component_register(
    SRCS
        "app_main.cpp"
        "scene/statemachine.cpp"
        "scene/animation.cpp"
        "scene/answers.cpp"
        "gfx/framebuffer.c"
        "gfx/draw.cpp"
        "gfx/text.cpp"
        "render/render.cpp"
        "render/pyramid.cpp"
        "render/fx.cpp"
        "hal/display.cpp"
        "hal/imu.cpp"
        "hal/touch.cpp"
        "hal/power.cpp"
    INCLUDE_DIRS "." "scene" "gfx" "render" "hal"
    REQUIRES
        esp_driver_i2c
        esp_timer
        esp_lcd
        waveshare__esp32_s3_touch_amoled_1_75
        waveshare__esp_lcd_touch_cst9217
        espressif__esp_lcd_touch
        glm
)

# Allow C++ in firmware (glm requires C++14 minimum). Pure-C files (framebuffer.c)
# compile as C; everything else as gnu++17 with no exceptions / no RTTI.
target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17 -fno-exceptions -fno-rtti>
)
