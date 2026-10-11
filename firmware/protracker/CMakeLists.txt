cmake_minimum_required(VERSION 3.20)
project(fm1_protracker C)
enable_testing()
add_library(fm1_mod mod.c usb.c library.c view.c controls.c bank.c)
target_include_directories(fm1_mod PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(fm1_mod PUBLIC c_std_11)
add_executable(mod_tests tests/test_mod.c)
target_link_libraries(mod_tests PRIVATE fm1_mod)
add_test(NAME mod_document_editor_player_usb COMMAND mod_tests)
add_test(NAME mod_starter_bank COMMAND mod_tests ${CMAKE_CURRENT_SOURCE_DIR}/samples/FM1START.MOD)

add_executable(controls_tests tests/test_controls.c)
target_link_libraries(controls_tests PRIVATE fm1_mod)
add_test(NAME tracker_hardware_controls COMMAND controls_tests)

add_executable(view_tests tests/test_view.c)
target_link_libraries(view_tests PRIVATE fm1_mod)
add_test(NAME tracker_field_block_view COMMAND view_tests)

add_executable(bank_tests tests/test_bank.c)
target_link_libraries(bank_tests PRIVATE fm1_mod)
add_test(NAME active_bank_parser_import COMMAND bank_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/FM1BREAK.FMB)

add_test(NAME breakbeat_demo_mod COMMAND mod_tests --probe
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/FM1BREAK.MOD)

foreach(profile native padded)
    add_executable(capture_${profile}_tests tests/test_capture.c
        ../usb-video/tracker_capture.c ../usb-video/uvc_stream.c)
    target_include_directories(capture_${profile}_tests PRIVATE ../usb-video)
    target_link_libraries(capture_${profile}_tests PRIVATE fm1_mod)
    if(profile STREQUAL padded)
        target_compile_definitions(capture_${profile}_tests PRIVATE FM1_UVC_IPAD_PAD=1)
    endif()
    if(MSVC)
        target_compile_options(capture_${profile}_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(capture_${profile}_tests PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME tracker_capture_${profile} COMMAND capture_${profile}_tests)
endforeach()

add_executable(mod_view_preview tools/render_view_host.c)
target_link_libraries(mod_view_preview PRIVATE fm1_mod)

add_executable(mod_demo_preview tools/render_demo_host.c)
target_link_libraries(mod_demo_preview PRIVATE fm1_mod)
