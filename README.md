# libmodem

**NOTE**: _libmodem is a work in progress, the implementation is not yet complete_

A(FSK) modem library for APRS and packet radio.

It provides a reusable platform for experimenting with AFSK/FSK modulation and demodulation, and for building and evaluating new demodulators.

libmodem includes the components needed to implement a complete software modem: AX.25 and FX.25 bitstream encode/decode, an audio interface with support for WASAPI, ALSA, and Core Audio, I/O routines for communication over sockets and serial ports, and a collection of modulators and demodulators.

The library is designed to be modular: you can swap bitstream encoders, audio interfaces, modulators, and demodulators to configure different pipelines. Components are intentionally loosely coupled so pieces can be reused independently, down to small functions.

libmodem is cross platform and is supported natively on Windows, Linux and OSX. The build system and code is seamless to integrate on all platforms.

A end to end reference modem implementation is provided, with support for 300 / 1200 / 2400 / 9600bps, and support for AX.25 and FX.25 framing. It can directly interface with sound cards, or send and receive audio over TCP/IP, and drive PTT over serial ports. The modem is designed to be a pipeline, with each stage swappable. The modem is fully extensible, and can interface with the outside world using an external plugin model, stdin/stdout, TCP

### Features
- Support for both AFSK and FSK modulation
- High-quality audio output
