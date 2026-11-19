idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "../../../include"
                       REQUIRES pthread esp_hw_support vfs esp_event esp_netif lwip unity)
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++20>)
