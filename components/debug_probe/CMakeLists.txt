idf_component_register(SRCS "src/probe.cpp" "vendor/DAP.c" "vendor/SW_DP.c" INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "src" "vendor" REQUIRES app core board esp_timer esp_driver_gpio esp_hw_support esp_system lwip)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable)
