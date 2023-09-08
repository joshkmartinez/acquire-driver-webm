# Acquire Webm Driver

This is an Acquire Driver that supports streaming to [webm](https://www.webmproject.org/about/)



## Devices

### Storage

- **webm** - Streams to a webm file encoded with VP9.



## 3rd Party Libraries

`acquire-driver-webm` depends on libwebm, for the webm container format, and libvpx, for the VP9 codec.

- [livbpx](https://chromium.googlesource.com/webm/libvpx)
  - v1.13.0 (as of 9/3/2023)
  - To build `./configure && make`

- [libwebm](https://chromium.googlesource.com/webm/libwebm)
  - v1.0.0.30 (as of 9/5/2023)
  - To build `cd libs/libwebm/build && cmake .. && make`
