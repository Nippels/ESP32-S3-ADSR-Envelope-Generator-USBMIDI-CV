# ESP32-S3-ADSR-Envelope-Generator-USBMIDI-CV
ESP32-S3 MIDI to CV/Gate Converter with Web UI ADSR Envelope Generator and PWM output

[![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Arduino](https://img.shields.io/badge/Arduino-ESP32--S3-green)](https://github.com/espressif/arduino-esp32)
[![License](https://img.shields.io/badge/license-MIT-red)](LICENSE)

A powerful MIDI to Control Voltage (CV) converter for the ESP32-S3 that generates ADSR envelopes with real-time parameter control via web interface. Perfect for analog synthesizers, Eurorack modules, and DIY synth projects.

## Features

- 🎹 **MIDI Input** - USB MIDI support for note on/off and control changes
- 🎛️ **ADSR Envelope Generator** - Attack, Decay, Sustain, Release with real-time updates
- 🌐 **Web Interface** - Control parameters via WiFi with instant feedback
- ⚡ **Real-time Updates** - ADSR changes take effect immediately, even during active notes
- 🔌 **CV Output** - 14-bit PWM on pin 39 (0-3.3V, configurable)
- 🔘 **Gate Output** - V-Trigger (3.3V) on pin 40 for DFAM/Eurorack compatibility
- 🎚️ **Velocity Sensitive** - Note velocity affects envelope amplitude
- 📊 **Live Status** - WebUI shows current state, active note, and gate status

## Hardware Requirements

- ESP32-S3 DevKit (or any ESP32-S3 board)
- Optional: Level shifter for 0-10V CV (if needed for your synth)
- Optional: MIDI interface (built-in USB works directly)

### Pin Connections

| Pin | Function | Voltage | Description |
|-----|----------|---------|-------------|
| 39 | CV Out | 0-3.3V | 14-bit PWM, ADSR envelope output |
| 40 | Gate Out | 0/3.3V | V-Trigger, DFAM standard |

## Software Requirements

- Arduino IDE 2.0+ or PlatformIO
- ESP32 Arduino Core 3.0.0 or higher
- Required libraries:
  - Control Surface (for MIDI handling)
  - WebServer (built-in)
  - WebSocketsServer (built-in)

## Installation

1. Clone this repository:
```bash
git clone https://github.com/YOUR_USERNAME/ESP32-S3-MIDI-ADSR-CV.git
