if(KRKR2_TEST_SCOPE STREQUAL "full")
if(KRKR2_BUILD_APP)
    find_package(cocos2dx CONFIG REQUIRED)
    set(SOURCES NodeOrderTest.cpp CocosPointerCoordinatesTest.cpp CocosInputFocusTest.cpp CocosTextInputTest.cpp)
    add_executable(cocos_window_tests ${SOURCES})
    target_include_directories(cocos_window_tests PRIVATE ${CMAKE_SOURCE_DIR}/cpp/core/environ)
    target_link_libraries(cocos_window_tests PRIVATE Catch2::Catch2WithMain cocos2dx::cocos2d core_input)
    catch_discover_tests(cocos_window_tests)
else()
    # The cocos window tests link the Cocos backend, which the SDL3-only
    # configuration (KRKR2_BUILD_APP=OFF) does not build.
    message(STATUS "cocos_window_tests skipped: KRKR2_BUILD_APP=OFF")
endif()
endif()

set(INPUT_SOURCES InputStateTest.cpp InputModifiersTest.cpp PointerCoordinatesTest.cpp InputCancellationTest.cpp TouchButtonTest.cpp TouchButtonPeer.cpp TextInputTest.cpp)
add_executable(input_state_tests ${INPUT_SOURCES})
target_link_libraries(input_state_tests PRIVATE Catch2::Catch2WithMain core_input)
catch_discover_tests(input_state_tests)

# Compile the backend-independent hook source without the legacy event host.
set(FRAME_HOOK_SOURCES PostUpdateTest.cpp PostUpdateRegistration.cpp
    ${CMAKE_SOURCE_DIR}/cpp/core/event/PostUpdateEvent.cpp)
add_executable(frame_hook_tests ${FRAME_HOOK_SOURCES})
target_include_directories(frame_hook_tests PRIVATE ${CMAKE_SOURCE_DIR}/cpp/core/event)
target_link_libraries(frame_hook_tests PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(frame_hook_tests)
