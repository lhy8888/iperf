lib_LTLIBRARIES         = libiperf.la                                   # Build and install an iperf library
bin_PROGRAMS            = iperf3                                        # Build and install an iperf binary
if ENABLE_PROFILING
noinst_PROGRAMS         = iperf3_profile   # Build, but don't install a profiled version of iperf3
else
noinst_PROGRAMS         =
endif
include_HEADERS         = iperf_api.h                                   # Defines the headers that get installed with the program


# Specify the source files and flags for the iperf library
libiperf_la_SOURCES     = \
                        cjson.c \
                        cjson.h \
                        flowlabel.h \
                        iperf.h \
                        iperf_api.c \
                        iperf_api.h \
                        iperf_error.c \
                        iperf_auth.h \
                        iperf_auth.c \
                        iperf_client_api.c \
                        iperf_locale.c \
                        iperf_locale.h \
                        iperf_server_api.c \
                        iperf_tcp.c \
                        iperf_tcp.h \
                        iperf_udp.c \
                        iperf_udp.h \
                        iperf_sctp.c \
                        iperf_sctp.h \
                        iperf_util.c \
                        iperf_util.h \
                        iperf_time.c \
                        iperf_time.h \
                        iperf_pthread.c \
                        iperf_pthread.h \
			dscp.c \
                        net.c \
                        net.h \
                        platform/win/buffer_alloc.c \
                        platform/win/buffer_alloc.h \
                        platform/win/socket_compat.c \
                        platform/win/socket_compat.h \
                        platform/win/win_runtime.c \
                        platform/win/win_runtime.h \
                        portable_endian.h \
                        queue.h \
                        tcp_info.c \
                        timer.c \
                        timer.h \
                        units.c \
                        units.h \
                        version.h

# Specify the sources and various flags for the iperf binary
iperf3_SOURCES          = main.c
iperf3_CFLAGS           = -g
iperf3_LDADD            = libiperf.la
iperf3_LDFLAGS          = -g

if ENABLE_PROFILING
# If the iperf-profiled-binary is enabled
# Specify the sources and various flags for the profiled iperf binary. This
# binary recompiles all the source files to make sure they are all profiled.
iperf3_profile_SOURCES  = main.c \
                          $(libiperf_la_SOURCES)

iperf3_profile_CFLAGS   = -pg -g
iperf3_profile_LDADD    = libiperf.la
iperf3_profile_LDFLAGS  = -pg -g
endif

install-exec-hook:
	if test -n "$(LDCONFIG)"; then $(LDCONFIG) || true; fi
