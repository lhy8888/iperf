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
