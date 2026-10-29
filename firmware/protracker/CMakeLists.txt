cmake_minimum_required(VERSION 3.20)
project(fm1_protracker C)
enable_testing()
add_library(fm1_mod mod.c usb.c library.c view.c controls.c bank.c history.c session.c routing.c)
target_include_directories(fm1_mod PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(fm1_mod PUBLIC c_std_11)
add_executable(mod_tests tests/test_mod.c)
target_link_libraries(mod_tests PRIVATE fm1_mod)
add_test(NAME mod_document_editor_player_usb COMMAND mod_tests)
add_test(NAME mod_starter_bank COMMAND mod_tests ${CMAKE_CURRENT_SOURCE_DIR}/samples/FM1START.MOD)

add_executable(controls_tests tests/test_controls.c)
target_link_libraries(controls_tests PRIVATE fm1_mod)
add_test(NAME tracker_hardware_controls COMMAND controls_tests)

add_executable(history_tests tests/test_history.c)
target_link_libraries(history_tests PRIVATE fm1_mod)
add_test(NAME tracker_edit_history COMMAND history_tests)

add_executable(session_tests tests/test_session.c)
target_link_libraries(session_tests PRIVATE fm1_mod)
add_test(NAME tracker_complete_workflows COMMAND session_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/FM1BREAK.FMB)

add_executable(library_save_tests tests/test_library_save.c)
target_link_libraries(library_save_tests PRIVATE fm1_mod)
add_test(NAME tracker_pc_save_requests COMMAND library_save_tests)

add_executable(routing_tests tests/test_routing.c)
target_link_libraries(routing_tests PRIVATE fm1_mod)
add_test(NAME tracker_stereo_speaker_routing COMMAND routing_tests)

foreach(profile native padded)
    add_executable(tracker_capture_${profile}_tests tests/test_tracker_capture.c
        capture.c ../usb-video/uvc_stream.c)
    target_include_directories(tracker_capture_${profile}_tests PRIVATE ../usb-video)
    target_link_libraries(tracker_capture_${profile}_tests PRIVATE fm1_mod)
    if(profile STREQUAL padded)
        target_compile_definitions(tracker_capture_${profile}_tests PRIVATE FM1_UVC_IPAD_PAD=1)
    endif()
    add_test(NAME tracker_screen_${profile} COMMAND tracker_capture_${profile}_tests)
endforeach()

add_executable(view_tests tests/test_view.c)
target_link_libraries(view_tests PRIVATE fm1_mod)
add_test(NAME tracker_field_block_view COMMAND view_tests)

add_executable(bank_tests tests/test_bank.c)
target_link_libraries(bank_tests PRIVATE fm1_mod)
add_test(NAME active_bank_parser_import COMMAND bank_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/FM1BREAK.FMB)

add_test(NAME breakbeat_demo_mod COMMAND mod_tests --probe
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/FM1BREAK.MOD)

add_executable(mod_view_preview tools/render_view_host.c)
target_link_libraries(mod_view_preview PRIVATE fm1_mod)

add_library(tracker_sim SHARED tools/simulator.c)
target_link_libraries(tracker_sim PRIVATE fm1_mod)

add_executable(mod_demo_preview tools/render_demo_host.c)
target_link_libraries(mod_demo_preview PRIVATE fm1_mod)
