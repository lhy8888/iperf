include_guard(GLOBAL)

function(iperf_prepare_config_headers)
    set(IPERF_VERSION "3.21+")
    set(PACKAGE "iperf")
    set(PACKAGE_NAME "iperf")
    set(PACKAGE_TARNAME "iperf")
    set(PACKAGE_STRING "iperf ${IPERF_VERSION}")
    set(PACKAGE_URL "https://software.es.net/iperf/")
    set(PACKAGE_BUGREPORT "https://github.com/esnet/iperf")
    set(PACKAGE_VERSION "${IPERF_VERSION}")
    set(VERSION "${IPERF_VERSION}")

    check_symbol_exists(clock_gettime "time.h" HAVE_CLOCK_GETTIME)
    set(HAVE_CLOCK_NANOSLEEP 0)
    set(HAVE_CPUSET_SETAFFINITY 0)
    set(HAVE_CPU_AFFINITY 1)
    set(HAVE_DAEMON 0)
    set(HAVE_DLFCN_H 0)
    set(HAVE_DONT_FRAGMENT 1)
    check_include_file(endian.h HAVE_ENDIAN_H)
    set(HAVE_FLOWLABEL 0)
    check_function_exists(getline HAVE_GETLINE)
    check_include_file(inttypes.h HAVE_INTTYPES_H)
    set(HAVE_IPPROTO_MPTCP 0)
    set(HAVE_IP_BOUND_IF 0)
    set(HAVE_IP_DONTFRAG 0)
    set(HAVE_IP_DONTFRAGMENT 1)
    set(HAVE_IP_MTU_DISCOVER 0)
    set(HAVE_LINUX_TCP_H 0)
    set(HAVE_MSG_TRUNC 0)
    check_function_exists(nanosleep HAVE_NANOSLEEP)
    set(HAVE_NETINET_SCTP_H 0)
    check_include_file(poll.h HAVE_POLL_H)
    set(HAVE_PTHREAD 1)
    set(HAVE_PTHREAD_PRIO_INHERIT 0)
    set(HAVE_SCHED_SETAFFINITY 0)
    set(HAVE_SCTP_H 0)
    set(HAVE_SENDFILE 0)
    set(HAVE_SETPROCESSAFFINITYMASK 1)
    set(HAVE_SOCKET_SHUTDOWN_SHUT_WR 1)
    set(HAVE_SO_BINDTODEVICE 0)
    set(HAVE_SO_MAX_PACING_RATE 0)
    set(HAVE_SSL 0)
    check_include_file(stdatomic.h HAVE_STDATOMIC_H)
    check_include_file(stdint.h HAVE_STDINT_H)
    set(HAVE_STDIO_H 1)
    set(HAVE_STDLIB_H 1)
    check_include_file(strings.h HAVE_STRINGS_H)
    set(HAVE_STRING_H 1)
    set(HAVE_STRUCT_SCTP_ASSOC_VALUE 0)
    set(HAVE_SYS_ENDIAN_H 0)
    set(HAVE_SYS_SOCKET_H 0)
    check_include_file(sys/stat.h HAVE_SYS_STAT_H)
    set(HAVE_SYS_TYPES_H 1)
    set(HAVE_TCP_CONGESTION 0)
    set(HAVE_TCP_INFO_SND_WND 0)
    set(HAVE_TCP_KEEPALIVE 0)
    set(HAVE_TCP_USER_TIMEOUT 0)
    set(HAVE_UDP_GRO 0)
    set(HAVE_UDP_SEGMENT 0)
    check_include_file(unistd.h HAVE_UNISTD_H)
    set(CAN_BIND_TO_DEVICE 0)
    set(STDC_HEADERS 1)

    set(_bool_macros
        CAN_BIND_TO_DEVICE
        HAVE_CLOCK_GETTIME
        HAVE_CLOCK_NANOSLEEP
        HAVE_CPUSET_SETAFFINITY
        HAVE_CPU_AFFINITY
        HAVE_DAEMON
        HAVE_DLFCN_H
        HAVE_DONT_FRAGMENT
        HAVE_ENDIAN_H
        HAVE_FLOWLABEL
        HAVE_GETLINE
        HAVE_INTTYPES_H
        HAVE_IPPROTO_MPTCP
        HAVE_IP_BOUND_IF
        HAVE_IP_DONTFRAG
        HAVE_IP_DONTFRAGMENT
        HAVE_IP_MTU_DISCOVER
        HAVE_LINUX_TCP_H
        HAVE_MSG_TRUNC
        HAVE_NANOSLEEP
        HAVE_NETINET_SCTP_H
        HAVE_POLL_H
        HAVE_PTHREAD
        HAVE_PTHREAD_PRIO_INHERIT
        HAVE_SCHED_SETAFFINITY
        HAVE_SCTP_H
        HAVE_SENDFILE
        HAVE_SETPROCESSAFFINITYMASK
        HAVE_SOCKET_SHUTDOWN_SHUT_WR
        HAVE_SO_BINDTODEVICE
        HAVE_SO_MAX_PACING_RATE
        HAVE_SSL
        HAVE_STDATOMIC_H
        HAVE_STDINT_H
        HAVE_STDIO_H
        HAVE_STDLIB_H
        HAVE_STRINGS_H
        HAVE_STRING_H
        HAVE_STRUCT_SCTP_ASSOC_VALUE
        HAVE_SYS_ENDIAN_H
        HAVE_SYS_SOCKET_H
        HAVE_SYS_STAT_H
        HAVE_SYS_TYPES_H
        HAVE_TCP_CONGESTION
        HAVE_TCP_INFO_SND_WND
        HAVE_TCP_KEEPALIVE
        HAVE_TCP_USER_TIMEOUT
        HAVE_UDP_GRO
        HAVE_UDP_SEGMENT
        HAVE_UNISTD_H
        STDC_HEADERS
    )
    set(_string_macros
        PACKAGE
        PACKAGE_BUGREPORT
        PACKAGE_NAME
        PACKAGE_STRING
        PACKAGE_TARNAME
        PACKAGE_URL
        PACKAGE_VERSION
        VERSION
    )

    set(_config_in "${CMAKE_SOURCE_DIR}/src/iperf_config.h.in")
    set(_config_template "${CMAKE_BINARY_DIR}/src/iperf_config.h.in.cmake")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/src")
    file(READ "${_config_in}" _config_contents)
    string(REPLACE "\r\n" "\n" _config_contents "${_config_contents}")
    string(REPLACE "\r" "\n" _config_contents "${_config_contents}")
    string(REPLACE "\n" ";" _config_lines "${_config_contents}")

    set(_rendered "")
    foreach(_line IN LISTS _config_lines)
        if(_line MATCHES "^#undef[ \t]+([A-Za-z0-9_]+)[ \t]*$")
            set(_macro "${CMAKE_MATCH_1}")
            list(FIND _string_macros "${_macro}" _string_index)
            if(NOT _string_index EQUAL -1)
                string(APPEND _rendered "#cmakedefine ${_macro} \"@${_macro}@\"\n")
            else()
                list(FIND _bool_macros "${_macro}" _bool_index)
                if(NOT _bool_index EQUAL -1)
                    string(APPEND _rendered "#cmakedefine ${_macro}\n")
                else()
                    string(APPEND _rendered "${_line}\n")
                endif()
            endif()
        else()
            string(APPEND _rendered "${_line}\n")
        endif()
    endforeach()

    file(WRITE "${_config_template}" "${_rendered}")
    configure_file("${_config_template}" "${CMAKE_BINARY_DIR}/src/iperf_config.h" @ONLY)
    configure_file("${CMAKE_SOURCE_DIR}/src/version.h.in" "${CMAKE_BINARY_DIR}/src/version.h" @ONLY)
endfunction()
