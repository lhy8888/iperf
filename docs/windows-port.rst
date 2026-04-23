Windows GUI Port
================

This branch turns iperf3 into a Windows 11 GUI application while
keeping the libiperf core intact. The goal is to ship a single installer
that contains ``IperfWin.exe``, the Qt 6 runtime, and the MinGW/UCRT
support DLLs required to run on a clean machine.

What is in scope
-----------------

* CMake/Ninja builds via MSYS2 UCRT64
* A Qt 6 Widgets front-end under ``app/iperfwin``
* Client and server modes in the same GUI
* Windows socket, runtime, entropy, and buffer compatibility wrappers
* JSON-based history and smoke-test coverage
* GitHub Actions packaging for both a ZIP artifact and an installer

How the port is structured
--------------------------

* ``src/`` still owns the iperf3 core library
* ``src/platform/win/`` contains the Windows compatibility layer
* ``app/iperfwin/`` contains the GUI pages, bridge, JSON parser, and smoke tests
* ``installer/`` contains the Inno Setup script used by CI

User workflow
-------------

1. Open ``IperfWin.exe``.
2. Use ``Quick Test`` for a simple client session or ``Server`` to listen.
3. Adjust protocol, host, port, duration, parallel streams, bitrate, and advanced options as needed.
4. Click ``Start`` and watch live interval data update in the GUI.
5. Use ``Stop`` to end a run and ``History`` to export the JSON session record.

Build and package
-----------------

The Windows preset is named ``windows-ucrt64``. It configures a Ninja
build in ``build/windows-ucrt64`` and produces:

* ``bin/IperfWin.exe`` for the GUI
* ``smoke/IperfSmoke.exe`` for local validation
* ``package/IperfWin-windows-ucrt64.zip`` for a portable bundle
* ``installer/IperfWinSetup.exe`` for end users

The ``windows-ucrt64.yml`` CI workflow runs the build, executes smoke
tests, and uploads both artifacts. The tag-triggered ``release.yml``
workflow reuses the same build chain for formal releases and uploads the
ZIP plus installer assets to GitHub Releases.
