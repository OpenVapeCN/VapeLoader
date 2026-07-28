cmake_minimum_required(VERSION 3.21)
project(vape_v4_controller_rewrite LANGUAGES CXX)

add_executable(vape_v4_rewrite WIN32
    src/main.cpp
    src/controller_model.cpp
    src/controller_model.h
    src/controller_ui.cpp
    src/controller_ui.h
    src/injection_coordinator.cpp
    src/injection_coordinator.h
    src/local_controller_service.cpp
    src/local_controller_service.h
    app.manifest
)

target_compile_features(vape_v4_rewrite PRIVATE cxx_std_17)
target_compile_definitions(vape_v4_rewrite PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(vape_v4_rewrite PRIVATE
    gdiplus
    winhttp
    shell32
    shlwapi
    ws2_32
)

if(MSVC)
    target_compile_options(vape_v4_rewrite PRIVATE /W4 /permissive- /utf-8)
    target_link_options(vape_v4_rewrite PRIVATE
        "/MANIFESTUAC:level='highestAvailable' uiAccess='false'"
    )
endif()

add_custom_command(TARGET vape_v4_rewrite POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:vape_v4_rewrite>/assets"
)
