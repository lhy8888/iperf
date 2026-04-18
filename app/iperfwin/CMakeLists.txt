add_executable(IperfWin WIN32
    IperfCoreBridge.cpp
    IperfCoreBridge.h
    IperfGuiTypes.h
    IperfJsonParser.cpp
    IperfJsonParser.h
    IperfJsonSink.cpp
    IperfJsonSink.h
    IperfPages.cpp
    IperfPages.h
    MainWindow.cpp
    MainWindow.h
    main.cpp
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

target_link_libraries(IperfWin
    PRIVATE
        Qt6::Widgets
        Threads::Threads
        libiperf
)

set_target_properties(IperfWin PROPERTIES
    OUTPUT_NAME "IperfWin"
)

if(WIN32 AND WINDEPLOYQT_EXECUTABLE)
    get_filename_component(_msys2_bin_dir "${CMAKE_C_COMPILER}" DIRECTORY)

    add_custom_command(TARGET IperfWin POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                --compiler-runtime
                --dir "$<TARGET_FILE_DIR:IperfWin>"
                "$<TARGET_FILE:IperfWin>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_msys2_bin_dir}/libgcc_s_seh-1.dll"
                "$<TARGET_FILE_DIR:IperfWin>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_msys2_bin_dir}/libstdc++-6.dll"
                "$<TARGET_FILE_DIR:IperfWin>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_msys2_bin_dir}/libwinpthread-1.dll"
                "$<TARGET_FILE_DIR:IperfWin>"
        VERBATIM
    )
endif()
