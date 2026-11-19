idf_component_register(SRCS "src/modules.cpp" "src/console.cpp" "src/pet_console.cpp" "src/piano_audio.cpp"
    "src/ui_request.cpp" "src/local_api.cpp" INCLUDE_DIRS "include"
    REQUIRES app core board display network debug_probe pc_monitor nvs_flash console esp_driver_gpio
        esp_driver_ledc esp_driver_usb_serial_jtag vfs esp_system esp_timer
    PRIV_REQUIRES esp_http_server lwip)

idf_build_get_property(idf_python PYTHON)
set(web_dir "${CMAKE_CURRENT_SOURCE_DIR}/web")
set(page "${CMAKE_CURRENT_BINARY_DIR}/control.html")
set(policy "${CMAKE_CURRENT_BINARY_DIR}/control.csp")
add_custom_command(OUTPUT "${page}" "${page}.gz" "${policy}"
    COMMAND "${idf_python}" "${web_dir}/pack.py" "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS "${web_dir}/index.html" "${web_dir}/app.js" "${web_dir}/pack.py"
    VERBATIM)
target_add_binary_data(${COMPONENT_LIB} "${page}" TEXT)
target_add_binary_data(${COMPONENT_LIB} "${page}.gz" BINARY)
target_add_binary_data(${COMPONENT_LIB} "${policy}" TEXT)
