idf_component_register(SRCS "weather_main.cpp" "weather_data.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_gpio esp_timer json 78__esp-ml307 78__uart-uhci)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
