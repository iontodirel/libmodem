# libmodem

**NOTE**: _libmodem is a work in progress, the implementation is not yet complete_

A(FSK) modem library for APRS and packet radio.

libmodem provides a reusable platform for experimenting with AFSK/FSK modulation and demodulation, and for building and evaluating new demodulators.

libmodem includes the components needed to implement a complete software modem: AX.25 and FX.25 bitstream encode/decode, an audio interface with support for WASAPI, ALSA, and Core Audio, I/O routines for communication over sockets and serial ports, and a collection of modulators and demodulators.

The library is designed to be modular: you can swap bitstream encoders, audio interfaces, modulators, and demodulators to configure different pipelines. Components are intentionally loosely coupled so pieces can be reused independently, down to small functions.

libmodem is cross platform and is supported natively on Windows, Linux and OSX. The build system and code is seamless to integrate on all platforms.

libmodem is fully testable, and is very extensivly tested, with thousands of lines of test code and over 50 tests, including FFTs for frequency accuracy, using Direwolf to test the modulator, and using the WA8LMF TNC Test CDs and Direwolf generated bitstreams.

A complete, end to end reference modem implementation is included, with support for 300, 1200, 2400, and 9600 bps operation and AX.25 plus FX.25 framing. The modem is configured as a composable pipeline: modulators and demodulators are wired to named audio streams, data streams, and PTT streams, and each stage can be swapped independently.

The configuration supports many to many routing. A single modulator can transmit to multiple audio outputs at the same time, for example a live sound card and a WAV renderer, while ingesting packets from multiple inputs such as TCP, serial, stdin, or built in test sources. Audio streams can be shared across pipelines and reused as many times as needed. The audio layer supports native devices (WASAPI and ALSA), TCP audio transport with separate audio and control ports, WAV input and output for repeatable testing, and null or synthetic audio sources for demos and validation.

On the data side, the modem can expose multiple interfaces concurrently, including multi client TCP servers, serial KISS, stdout, rotating log and JSON file streams, and an external dynamic library interface for custom integration. Output formats are selectable per stream and can be mixed across consumers, including KISS, TNC2 style text, JSON, AX.25 hex or binary, raw bitstream, and telemetry augmented variants where supported. This makes it easy to do workflows like writing decoded bitstreams to disk for analysis, streaming decoded frames to multiple clients, or generating synthetic traffic using random and repeat packet sources.

Transmit control is equally flexible. PTT can be driven over serial using RTS or DTR with configurable polarity, via GPIO on platforms like Raspberry Pi with optional pre and post delays, or through a plugin library so you can integrate with custom keying hardware. Modulator parameters such as mark and space frequencies, TX delay and tail, gain, preemphasis, and leading and trailing silence are all configurable, and the same base modulator can be cloned and specialized via inheritance for variants like a WAV only renderer or a multi output transmitter.

### Features
- Support for both AFSK and FSK modulation
- High-quality audio output
