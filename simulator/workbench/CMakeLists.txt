cmake_minimum_required(VERSION 3.21)
project(apl_workbench VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickWidgets)
qt_standard_project_setup()
get_filename_component(APL_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
file(TO_CMAKE_PATH "${APL_QT_PREFIX}" APL_QT_PREFIX)
set(APL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_library(apl STATIC "${APL_ROOT}/src/apl.c" "${APL_ROOT}/src/apl_core.c" "${APL_ROOT}/src/apl_hmi.c")
target_include_directories(apl PUBLIC "${APL_ROOT}/include" "${APL_ROOT}/config")
target_compile_options(apl PRIVATE -Wall -Wextra -finput-charset=UTF-8)
qt_add_executable(apl_workbench
    src/main.cpp
    src/apl_platform.h src/apl_platform.cpp
    src/apl_memory.h src/apl_memory.cpp
    src/frontend_catalog.h src/frontend_catalog.cpp
    src/workbench_window.h src/workbench_window.cpp
)
qt_add_resources(apl_workbench panels
    PREFIX "/panels"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/frontends"
    FILES frontends/round/RoundPanel.qml frontends/bench/KeyBench.qml
)
target_link_libraries(apl_workbench PRIVATE Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickWidgets apl)
target_compile_options(apl_workbench PRIVATE -Wall -Wextra -Werror -finput-charset=UTF-8 -fexec-charset=UTF-8)
target_compile_definitions(apl_workbench PRIVATE "APL_QT_PREFIX=\"${APL_QT_PREFIX}\"")
set_target_properties(apl_workbench PROPERTIES WIN32_EXECUTABLE TRUE)
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${APL_QT_PREFIX}/bin")
if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET apl_workbench POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/frontends"
                --compiler-runtime "$<TARGET_FILE:apl_workbench>"
        COMMENT "Deploy APL workbench Qt runtime"
    )
endif()
include(CTest)
if(BUILD_TESTING)
    add_executable(apl_core_test "${APL_ROOT}/tests/test_apl.c")
    target_link_libraries(apl_core_test PRIVATE apl)
    add_test(NAME apl_core COMMAND apl_core_test)
    add_executable(apl_optional_key_test "${APL_ROOT}/tests/test_hmi_key_optional.c"
        "${APL_ROOT}/src/apl.c" "${APL_ROOT}/src/apl_core.c" "${APL_ROOT}/src/apl_hmi.c")
    target_include_directories(apl_optional_key_test PRIVATE "${APL_ROOT}/include" "${APL_ROOT}/config")
    target_compile_definitions(apl_optional_key_test PRIVATE APL_HMI_COMPILE_KEY_LIGHT=0)
    add_test(NAME apl_optional_key COMMAND apl_optional_key_test)
    qt_add_executable(apl_memory_test tests/apl_memory_test.cpp src/apl_memory.h src/apl_memory.cpp)
    target_include_directories(apl_memory_test PRIVATE src)
    target_link_libraries(apl_memory_test PRIVATE Qt6::Core)
    add_test(NAME apl_memory COMMAND apl_memory_test)
    foreach(mode IN ITEMS both purify humidify)
        add_executable(apl_link_${mode}_test "${APL_ROOT}/tests/test_link.c"
            "${APL_ROOT}/src/apl.c" "${APL_ROOT}/src/apl_core.c" "${APL_ROOT}/src/apl_hmi.c")
        target_include_directories(apl_link_${mode}_test PRIVATE "${APL_ROOT}/include" "${APL_ROOT}/config")
        target_compile_options(apl_link_${mode}_test PRIVATE -Wall -Wextra -Werror -finput-charset=UTF-8)
        if(mode STREQUAL "purify")
            target_compile_definitions(apl_link_${mode}_test PRIVATE APL_COMPILE_HUMIDIFY=0 APL_DEFAULT_HUMIDIFY=0)
        elseif(mode STREQUAL "humidify")
            target_compile_definitions(apl_link_${mode}_test PRIVATE APL_COMPILE_PURIFY=0 APL_DEFAULT_PURIFY=0)
        endif()
        add_test(NAME apl_link_${mode} COMMAND apl_link_${mode}_test)
    endforeach()
    add_test(NAME workbench COMMAND apl_workbench --self-test)
endif()
