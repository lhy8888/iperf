set(IPERFWIN_CORE_SOURCES
    IperfCoreBridge.cpp
    IperfCoreBridge.h
    IperfGuiTypes.h
    IperfJsonParser.cpp
    IperfJsonParser.h
    IperfJsonSink.cpp
    IperfJsonSink.h
)

set(IPERFWIN_GUI_SOURCES
    IperfPages.cpp
    IperfPages.h
    IperfTestOrchestrator.cpp
    IperfTestOrchestrator.h
    MainWindow.cpp
    MainWindow.h
    main.cpp
)

add_executable(IperfWin WIN32
    ${IPERFWIN_CORE_SOURCES}
    ${IPERFWIN_GUI_SOURCES}
)

add_executable(IperfSmoke
    ${IPERFWIN_CORE_SOURCES}
    IperfSmoke.cpp
)

find_program(WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt6 windeployqt
    HINTS
        "$ENV{MINGW_PREFIX}/bin"
        "$ENV{MSYSTEM_PREFIX}/bin"
)

target_include_directories(IperfWin
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_BINARY_DIR}/src"
)

target_include_directories(IperfSmoke
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_BINARY_DIR}/src"
)

target_link_libraries(IperfWin
    PRIVATE
        Qt6::Widgets
        Qt6::Network
        Threads::Threads
        libiperf
)

target_link_libraries(IperfSmoke
    PRIVATE
        Qt6::Core
        Qt6::Network
        Threads::Threads
        libiperf
)

set_target_properties(IperfWin PROPERTIES
    OUTPUT_NAME "IperfWin"
)

set_target_properties(IperfSmoke PROPERTIES
    OUTPUT_NAME "IperfSmoke"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/smoke"
)

function(iperf_apply_windows_runtime_deploy target_name)
    if(WIN32 AND WINDEPLOYQT_EXECUTABLE)
        get_filename_component(_msys2_bin_dir "${CMAKE_C_COMPILER}" DIRECTORY)
        file(GLOB _msys2_runtime_dlls CONFIGURE_DEPENDS "${_msys2_bin_dir}/*.dll")

        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --compiler-runtime
                    --dir "$<TARGET_FILE_DIR:${target_name}>"
                    "$<TARGET_FILE:${target_name}>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    ${_msys2_runtime_dlls}
                    "$<TARGET_FILE_DIR:${target_name}>"
            VERBATIM
        )
    endif()
endfunction()

iperf_apply_windows_runtime_deploy(IperfWin)
iperf_apply_windows_runtime_deploy(IperfSmoke)

if(BUILD_TESTING)
    add_test(NAME IperfSmokeJsonParser
        COMMAND IperfSmoke --json-parser-selftest)
    add_test(NAME IperfSmokeExportSafety
        COMMAND IperfSmoke --export-safety-selftest)
    add_test(NAME IperfSmokeAddressSelection
        COMMAND IperfSmoke --address-selection-selftest)
    add_test(NAME IperfSmokeEndToEnd
        COMMAND IperfSmoke --end-to-end --port 5210 --timeout-ms 15000 --warmup-ms 500)
    add_test(NAME IperfSmokeValidationMatrix
        COMMAND IperfSmoke --validation-matrix --port 5230 --timeout-ms 20000 --warmup-ms 500)
    add_test(NAME IperfSmokeStopCycle
        COMMAND IperfSmoke --stop-cycle --port 5220 --iterations 20 --timeout-ms 30000 --stop-delay-ms 250 --warmup-ms 500)
    set_tests_properties(IperfSmokeEndToEnd IperfSmokeValidationMatrix IperfSmokeStopCycle PROPERTIES RUN_SERIAL TRUE)
endif()
