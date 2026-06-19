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
        "hal/mic.cpp"
        "hal/wakeword.cpp"
        "scene/listen.c"
        "scene/vad.c"
        "hal/recorder.cpp"
        "net/gemini.cpp"
        "net/gemini_wav.c"
        "net/net.cpp"
        "net/provcfg.cpp"
        "net/wifi.cpp"
        "net/http_server.cpp"
        "net/captive_dns.cpp"
    INCLUDE_DIRS "." "scene" "gfx" "render" "hal" "net"
    REQUIRES
        esp_driver_i2c
        esp_timer
        esp_lcd
        esp_mm
        waveshare__esp32_s3_touch_amoled_1_75
        waveshare__esp_lcd_touch_cst9217
        # Voice: wake-word detector + audio codec capture (BSP mic).
        # ESPMicroSpeechFeatures is pulled in transitively because
        # micro_wake_word.h includes its frontend_util.h header.
        micro_wake_word
        ESPMicroSpeechFeatures
        espressif__esp-tflite-micro
        espressif__esp_codec_dev
        espressif__esp_lcd_touch
        glm
        # Networking: WiFi STA/AP, captive portal, POST listener, mDNS.
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_http_server
        esp_http_client
        esp-tls
        lwip
        mdns
)

# Allow C++ in firmware (glm requires C++14 minimum). Pure-C files (framebuffer.c)
# compile as C; everything else as gnu++17 with no exceptions / no RTTI.
target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17 -fno-exceptions -fno-rtti>
)
