# M5 Cardputer ADV WAV Sampler

A 10-slot audio sampler for the M5 Cardputer ADV that records and plays back WAV files directly from an SD card.

![M5_Cardputer_ADV Sampler](https://img.shields.io/badge/Platform-M5_Cardputer_ADV-blue)
![Arduino](https://img.shields.io/badge/Arduino-IDE-00979D)
![License](https://img.shields.io/badge/License-MIT-green)

## 🎵 Features

- **10 Sample Slots** - Record and store up to 10 different audio samples
- **WAV Format** - Records in standard 48kHz, 16-bit mono WAV format
- **High-ish Quality Audio** - 1024-sample buffer with 2x recording gain
- **Up to 10 Seconds** - Each sample can be up to 10 seconds long
- **Looping Playback** - Samples loop continuously until stopped
- **Live Volume Control** - Adjust playback volume using `,` and `/` keys

## 🚀 Installation

- Use the sampler.ino for a standalone installation
- Use the samplerM5.ino to flash with [M5 Launcher](https://github.com/bmorcelli/Launcher)


## 🎮 Controls

### Menu Navigation
| Key | Action |
|-----|--------|
| `;` | Navigate up |
| `.` | Navigate down |
| `Space` | Start recording to selected slot |
| `Enter` | Play selected sample (loops until stopped) |
| `Del` | Delete selected sample |
| `,` | Decrease volume |
| `/` | Increase volume |

### During Recording/Playback
| Key | Action |
|-----|--------|
| `Enter` | Stop recording or playback |
| `,` / `/` | Adjust volume (playback only) |

## 🛠️ Hardware Requirements

- **M5Cardputer ADV**
- **SD Card** (formatted as FAT32)

## 📋 Technical Specifications

| Specification | Value |
|--------------|-------|
| Sample Rate | 48kHz |
| Bit Depth | 16-bit |
| Format | Mono WAV |
| Recording Gain | 2x amplification with soft clipping |
| Buffer Size | 1024 samples per chunk |
| Max Duration | 10 seconds per sample |
| Storage Location | `/samples/` directory on SD card |




## 🔧 Customization

```cpp
static constexpr const size_t max_duration_seconds = 10;  // Maximum recording duration
static constexpr const float recording_gain = 2.0f;       // Recording amplification
```

## Authors

- [@riosma](https://www.github.com/riosma)


## 📝 License

MIT License - feel free to modify and distribute
