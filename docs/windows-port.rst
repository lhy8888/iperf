Windows Port
============

This branch adds the CMake/Ninja build scaffold for the Windows UCRT64
port, plus the first compatibility-layer split for sockets, entropy,
buffer allocation, and runtime information.

Planned stages:

* libiperf CMake build with ``main.c`` excluded from the GUI product
* Qt 6 Widgets GUI shell under ``app/iperfwin``
* Windows socket/runtime compatibility wrappers
* Installer and CI packaging
