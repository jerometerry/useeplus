# Useeplus V4L2 Linux Driver and LibUSB Multi-Platform Driver

There are several cheap borescope cameras available on Amazon that require a proprietary app on Android and iOS, and don't have readily available drivers.

Here is one such camera:

[Kinpthy Endoscope Camera](https://www.amazon.ca/dp/B0C9JR3N4W) - with Light, 1920P HD Borescope Tools with 8 Adjustable LED Lights, Endoscope with Semi-Rigid Snake Camera, Inspection Borescope for iOS and Android-16.4ft Single Lens

If you dig into your USB device details, these cameras have the following vendor and product IDs:

| vendor id | product id |
| --------- | ---------- |
| 2ce3      | 3828       |
| 0329      | 2022       |

The device name that comes up for my camera is "Geek szitman supercamera".

The app that the instructions say to use is called Sup-Anesok: https://play.google.com/store/apps/details?id=com.i4season.supanesok&hl=en_CA.

## Existing Hobbyist Drivers

If you do a quick Google search, you'll likely come across these GitHub repositories:

- https://github.com/hbens/geek-szitman-supercamera
- https://github.com/jmz3/EndoscopeCamera
- https://github.com/MAkcanca/useeplus-linux-driver
- https://libraries.io/pypi/supercamera

hbens posted the original proof of concept, as far as I can tell. He did some great detective work to reverse engineer the custom protocol and figure out the commands to control the camera to get it streaming video.

The supercamera defaults to 640x480, and no existing drivers could get a higher resolution. Until now.

## Useeplus Reverse Engineering

I started from where hbens and jmz3 left things, and dug deep to figure out how this camera really works.

The foundational reverse-engineering of the `com.useeplus.protocol` was primarily done by hbens. Building upon that proof-of-concept, I expanded the protocol decoding to handle specific quirks and enable the camera to operate in 3 different resolutions: 640x480, 320x240, and 1280x960.

## V4L2 Compliance Testing

I was able to build a V4L2 Linux Driver that passes all the v4l2-compliance tests.

## What's Included

- V4L2 Linux Driver
- LibUSB Driver
- High performance MJPEG streaming servers, based on uSockets / uWebSockets
- Zero-allocation design after startup, by reserving 128 bytes at the beginning of all buffers to write HTTP headers
- Code profiled with bpftrace scripts to identify and resolve memory allocations
- C++ Implementation of the Java LMAX Disruptor pattern
- Makefile for building, testing, and benchmarking
- Makefile configured to use Zig compiler. Can be switched to GCC or Clang easily
- Make targets for building, testing, running checkpatch.pl, and clang-format
- Configured to use static analysis tools Cppcheck, IWYU (Include What You Use), and clang-tidy
- Google Test based test suite
- Google Benchmark with benchmarks for the Disruptor lockless ringbuffer and MJPEG pipeline
- MIT License
- Reusable classes for building custom applications
- Useeplus decoder C module

## ⚠️ Notice Regarding AI-Assisted Generation & Upstreaming

Please note that while the core architecture and overarching design of this project are my own, the foundational Useeplus protocol reverse-engineering was primarily done by [hbens](https://github.com/hbens/geek-szitman-supercamera). Additionally, the specific implementations of the Video4Linux (V4L2) and VideoBuffer2 (VB2) subsystems were heavily assisted by AI (Google Gemini). AI was also utilized for Makefile automation and restructuring tests to ensure the code passed `v4l2-compliance` and `checkpatch.pl`.

**If you fork this project with the intent to submit a patch to the Linux Kernel Mailing List (LKML):**
You assume all responsibility for the codebase. Do not blindly submit this repository as a patch. The Linux kernel community has strict, justified standards regarding AI provenance, and AI suggestions can introduce subtle kernel-level regressions (e.g., memory management, locking, or subsystem-specific behaviors).

You must thoroughly audit, understand, and be prepared to independently defend every line of the V4L2/VB2 integration before upstreaming.

## 🧠 Acknowledgements & Copyright Notice

The Useeplus protocol reverse-engineering work was primarily done by [hbens](https://github.com/hbens/geek-szitman-supercamera), as documented in the `geek-szitman-supercamera` repository. The original proof-of-concept code (`supercamera_poc.cpp`) upon which the decoding logic is based is distributed under the **Creative Commons Zero v1.0 Universal (CC0-1.0)** license.

This project was also heavily inspired by the work of [jmz3](https://github.com/jmz3/EndoscopeCamera) and [doctormo](https://github.com/doctormo). Their efforts laid the groundwork for this project and making `com.useeplus.protocol` accessible.

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

For information regarding the third-party libraries used in this project, please see the [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES.md) file.
