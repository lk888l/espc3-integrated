idf_component_register(SRCS "src/network.cpp" "src/pairing.cpp" INCLUDE_DIRS "include" REQUIRES app core esp_wifi esp_event esp_netif nvs_flash esp_timer esp_hw_support)
