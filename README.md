IperfWin: Windows 11 GUI for libiperf
=====================================

IperfWin is a Windows 11 Qt 6 Widgets front-end for libiperf. This
repository keeps the libiperf core, but the mainline UX, build, release,
and documentation flow now center on the Windows application.

Current release: `IperfWin-v1.1.1`

What is included
----------------

* Client and Server modes in one GUI
* Windows-specific build and packaging flow using MSYS2 UCRT64, CMake, and Ninja
* Share-safe exports for external reporting
* Windows smoke coverage for JSON parsing, export safety, address selection,
  functional end-to-end tests, validation matrix checks, and stop-cycle checks
* GitHub Actions artifacts for a portable ZIP bundle and an installer

Building IperfWin
-----------------

    cmake --preset windows-ucrt64
    cmake --build --preset windows-ucrt64

The GUI executable is `build/windows-ucrt64/bin/IperfWin.exe` and the
smoke test binary is `build/windows-ucrt64/smoke/IperfSmoke.exe`.

For the Windows workflow, packaging layout, and runtime architecture notes,
see `docs/windows-port.rst`.

Release downloads
-----------------

The latest GitHub Release is:

* [IperfWin-v1.1.1](https://github.com/lhy8888/iperf/releases/tag/IperfWin-v1.1.1)

The release assets are:

* [IperfWin-v1.1.1-windows-ucrt64.zip](https://github.com/lhy8888/iperf/releases/download/IperfWin-v1.1.1/IperfWin-v1.1.1-windows-ucrt64.zip)
* [IperfWin-v1.1.1-Setup.exe](https://github.com/lhy8888/iperf/releases/download/IperfWin-v1.1.1/IperfWin-v1.1.1-Setup.exe)
