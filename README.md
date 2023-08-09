### **about**

This is a copy of the EGADS source from the Engineering Sketch Pad (https://acdl.mit.edu/ESP) - the relevant sections from the `ESP` README are provided below.

The main difference between this repository and the original `EGADS` code is the way the library is configured. A `CMake` configuration file is included here to provide a more automatic installation process.
In particular, the `CMake` configuration file will automatically download the `OpenCASCADE` libraries for your system, and the `RPATH` will be set so that `LD_LIBRARY_PATH` is no longer needed to find the `OpenCASCADE` libraries. The `OpenCASCADE` libraries are attached in the "Releases" section of this repository. Currently, only version 7.6.0 has been uploaded.

The best way to use the `EGADS` library from this repository is to simply call

```
add_subdirectory(egads)
```

from your `CMake` configuration and link your program/library to the `egads` library target defined in this project. The main `EGADS` API headers are in `egads/include` but you may also want to include `egads/src` if you need access to lower-level `EGADS` functions. This project also defines the `EGADS_OCC_INCLUDE` variable in case you need to directly access the `OpenCASCADE` API.

### `ESP` README:

**ESP: The Engineering Sketch Pad**, _Rev 1.22 -- December 2022_

#### Preamble

Windows 7 & 8 are no longer supported, only Windows 10 is tested (we have
not begun testing against Windows 11). This also means that older versions
of MS Visual Studio are no longer being tested either. Only MSVS versions
2017 and up are fully supported.

This ESP release no longer works with Python 2.7. The minimum supported
version is now Python 3.8. Also, we now only support OpenCASCADE at Rev
7.4 or higher. And these must be the versions taken from the ESP website
(and not from elsewhere). At this point we recommend 7.6.0.

It is advisable to unblock browser tabs on the web browser in use.

The training material is no longer part of this distribution. The last
training was given for Rev 1.19 and can be found at the ESP website at
http://acdl.mit.edu/ESP/Training, which is in 2 parts. The first is on
ESP geometry construction and is found in the ESP subdirectory and the
second on analysis is found in the CAPS subdirectory. The PDFs and MP4s
of the lectures can be found in the (sub)subdirectory "lectures".
Do NOT apply the overlays -- they are specifically for ESP 1.19.

Apple notes:
(1) You CANNOT download the distributions using a browser. For instructions
on how to get ESP see MACdownloads.txt on the web site.
(2) You must have XQuartz at a minimum release of 2.8.1 for some supplied
executables to function.
(3) Big Sur and Monterey are now fully tested.
(4) Apple M1 computers are natively supported but require Rosetta2 for the
running of some legacy CAPS apps. Rosetta2 can be installed by
executing the following command: "softwareupdate --install-rosetta".
(5) M1 builds must be done in a "native" shell. That is, typing "arch"
must return "arm64".
(6) If Safari blocks a pop-up (for example, the flowchart in ESP),
you can press the rectangular button in the Smart Search field
and allow the file to be seen.

#### Prerequisites

The most significant prerequisite for this software is OpenCASCADE.
This ESP release only supports the prebuilt versions marked 7.4.1
and 7.6.0, which are available at http://acdl.mit.edu/ESP. Please DO
NOT report any problems with any other versions of OpenCASCADE, much
effort has been spent in "hardening" the OpenCASCADE code. It is advised
that all ESP users update to 7.4.1/7.6.0 because of better robustness and
performance. If you are still on a LINUX box with a version of gcc less
than 4.8, you will have to upgrade to a newer OS or version of gcc.

##### Release Notes

The significant updates made to EGADS from Rev 1.21 are:

- Added a Julia interface (jlEGADS)
- Refactor pyEGADS to support EGADSlite
- Added periodic support for EG_approximate
- Allow for senses to be flipped when applying EG_isEquivalent
- Can rule with sections of different number of Edges.

### **LICENSE**

The Engineering Sketch Pad is distributed under a LGPL license, which this repository inherits.
Please see the `LICENSE` file for complete licensing details.
