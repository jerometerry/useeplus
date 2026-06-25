# Useeplus V4L2 Linux Driver and LibUSB multi-platform Driver

There are several cheap borescope cameras available on Amazon that require a proprietary app on Android and iOS, and dont have readily available drivers.

Here's such a camera

[Kinpthy Endoscope Camera](https://www.amazon.ca/dp/B0C9JR3N4W) - with Light,1920P HD Borescope Tools with 8 Adjustable LED Lights, Endoscope with Semi-Rigid Snake Camera, Inspection Borescope for iOS and Android-16.4ft Single Lens

If you dig into your USB device details, these cameras have the following vendor / product ids.

| vendor id | product id |
| --------- | ---------- |
| 2ce3      | 3828       |
| 0329      | 2022       |

The device name that comes up for my camera is "Geek szitman supercamera".

The app that the instructions say to use is callec Sup-Anesok: https://play.google.com/store/apps/details?id=com.i4season.supanesok&hl=en_CA.

## Existing Hobbyist Drivers

If you do a quick google search, you'll like come across these github repositories

- https://github.com/hbens/geek-szitman-supercamera
- https://github.com/jmz3/EndoscopeCamera
- https://github.com/MAkcanca/useeplus-linux-driver
- https://libraries.io/pypi/supercamera

hbens postec the original proof of concept, as far as I can tell. He did some great detective work to reverse engineer the custom protocol and figure out the commands to control the camera to get it streaming video.

The supercamera defaults to 640x480, and no existing drivers could get a higher resolution. Until now.

## Useeplus Reverse Engineering

I started from where hbens and jmz3 left things, and dug deep to figure out how this camera really works.

I was able to get the camera to operate in 3 different resolutions: 640x480, 320x240, and 1280x960.

I also reverse engineered the custom protocol, including the quirks that make decodng the useeplus protocol challenging.

## V4L2 Compliance Testing

I was able to build a V4L2 Linux Driver that passes all the v4l2-compliance tests.

## Whats Included

- V4L2 Linux Driver
- LibUSB Driver
- High performance MJPEG streaming servers, based on uSockers / uWebSockets
- Zero-allocation design after startup, by reservng 128 bytes at the beginnng of all buffers to write HTTP headers
- Code profiled with bpftrace scripts to identify and resolve memory allcations
- C++ Implementation of the Java LMAX Disruptor patrern
- Makefile for building, testing, and benchmarking
- Makefile configured to use Zig compiler. Can be switched to GCC or Clang easily
- Make targets for building, testing, running checkpatch.pl, clang format
- Configured to use static analysis tools Cppcheck, include what you use, and clang tidy
- Google Test based test suite
- Google Benchmark with benchmarks for the Disruptor lockless ringbuffer and mjpeg pipeline
- MIT License
- Reusable classes for building custom applications
- Useeplus decoder C module

## 🧠 Acknowledgements

This project was heavily inspired by the reverse-engineering work of
[hbens](https://github.com/hbens/geek-szitman-supercamera), [jmz3](https://github.com/jmz3/EndoscopeCamera), and
[doctormo](https://github.com/doctormo). Their original proofs-of-concept laid the groundwork for decoding the
`com.useeplus.protocol`.

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

For information regarding the third-party libraries used in this project, please see the
[THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES.md) file.
