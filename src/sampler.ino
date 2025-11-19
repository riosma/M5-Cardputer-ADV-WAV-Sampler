#include <M5Cardputer.h>
#include <SPI.h>
#include <SD.h>

// SD pins for M5CardputerADV
#define SD_SPI_SCK_PIN  40
#define SD_SPI_MISO_PIN 39
#define SD_SPI_MOSI_PIN 14
#define SD_SPI_CS_PIN   12

static constexpr const size_t record_length = 1024;  // samples per chunk
static constexpr const size_t record_samplerate = 48000;  // 48kHz sample rate
static constexpr const size_t max_duration_seconds = 10;  // max rec duration
static constexpr const size_t max_record_size = record_samplerate * max_duration_seconds;
static constexpr const float recording_gain = 2.0f;  // amplify rec volume

// WAV file headers
static char riff[4] = {'R','I','F','F'};
static char wave[4] = {'W','A','V','E'};
static char fmt[4] = {'f','m','t',' '};
static char data[4] = {'d','a','t','a'};

// WAV file header
struct WAVHeader {
  uint32_t file_size;
  uint32_t fmt_size = 16;
  uint16_t audio_format = 1; // PCM
  uint16_t num_channels = 1; // mono
  uint32_t sample_rate = record_samplerate;
  uint32_t byte_rate = record_samplerate * 2; // sample_rate * num_channels * bits_per_sample/8
  uint16_t block_align = 2; // num_channels * bits_per_sample/8
  uint16_t bits_per_sample = 16;
  uint32_t data_size;
};

static int16_t *rec_buffer;  // buffer for rec chunks
static size_t recorded_samples = 0;
static bool is_recording = false;
static bool is_playing = false;
static int volume = 255;  // (0-255)
static File recording_file;

enum Mode {
  MODE_MENU,
  MODE_RECORDING,
  MODE_PLAYING
};

static Mode current_mode = MODE_MENU;
static int menu_selection = 0;  // selected sample slot
static int menu_scroll = 0;  // menu position
static const int NUM_SAMPLES = 10;  // sample slots
static const int VISIBLE_SAMPLES = 6;  // sample slots visible

// WAV header to file
void writeWAVHeader(File &file, uint32_t data_size) {
  WAVHeader header;
  header.data_size = data_size;
  header.file_size = 36 + data_size; // remove header
  
  file.seek(0);
  file.write((uint8_t*)riff, 4);
  file.write((uint8_t*)&header.file_size, 4);
  file.write((uint8_t*)wave, 4);
  file.write((uint8_t*)fmt, 4);
  file.write((uint8_t*)&header.fmt_size, 4);
  file.write((uint8_t*)&header.audio_format, 2);
  file.write((uint8_t*)&header.num_channels, 2);
  file.write((uint8_t*)&header.sample_rate, 4);
  file.write((uint8_t*)&header.byte_rate, 4);
  file.write((uint8_t*)&header.block_align, 2);
  file.write((uint8_t*)&header.bits_per_sample, 2);
  file.write((uint8_t*)data, 4);
  file.write((uint8_t*)&header.data_size, 4);
}

// get sample duration
float getWAVDuration(String filename) {
  File f = SD.open(filename, FILE_READ);
  if (!f) return 0.0;
  
  // skip header
  f.seek(40);
  uint32_t data_size;
  f.read((uint8_t*)&data_size, 4);
  f.close();
  
  size_t samples = data_size / sizeof(int16_t);
  return (float)samples / record_samplerate;
}

// main menu
void drawMenu() {
  M5Cardputer.Display.clear();
  M5Cardputer.Display.setTextDatum(top_left);
  M5Cardputer.Display.setTextColor(WHITE);
  M5Cardputer.Display.setFont(&fonts::FreeSans9pt7b);
  
  M5Cardputer.Display.drawString("marios", 5, 5);
  
  // volume and battery
  int battery = M5Cardputer.Power.getBatteryLevel();
  M5Cardputer.Display.drawString("vol:" + String(volume) + " bat:" + String(battery) + "%", 90, 5);
  
  // menu selection
  if (menu_selection < menu_scroll) {
    menu_scroll = menu_selection;
  } else if (menu_selection >= menu_scroll + VISIBLE_SAMPLES) {
    menu_scroll = menu_selection - VISIBLE_SAMPLES + 1;
  }
  
  // current menu view
  for (int i = 0; i < VISIBLE_SAMPLES && (i + menu_scroll) < NUM_SAMPLES; i++) {
    int sample_idx = i + menu_scroll;
    String filename = "/samples/sample_" + String(sample_idx + 1) + ".wav";
    bool exists = SD.exists(filename);
    
    int y = 30 + (i * 15);
    if (sample_idx == menu_selection) {
      M5Cardputer.Display.fillRect(5, y - 2, 230, 13, TFT_DARKGREY);
    }
    
    String label = String(sample_idx + 1) + ": ";
    if (exists) {
      float duration = getWAVDuration(filename);
      label += String(duration, 1) + "s";
    } else {
      label += "[empty]";
    }
    
    M5Cardputer.Display.drawString(label, 8, y);
  }
  
  // menu indicators
  if (menu_scroll > 0) {
    M5Cardputer.Display.fillTriangle(230, 30, 235, 35, 225, 35, WHITE);
  }
  if (menu_scroll + VISIBLE_SAMPLES < NUM_SAMPLES) {
    M5Cardputer.Display.fillTriangle(230, 115, 235, 110, 225, 110, WHITE);
  }
  
  M5Cardputer.Display.setFont(&fonts::Font0);
  M5Cardputer.Display.drawString(";/.: sel | enter: play", 5, 120);
  M5Cardputer.Display.drawString("space: rec | del: del | ,/: vol", 5, 128);
  M5Cardputer.Display.display();
}

void drawRecording() {
  M5Cardputer.Display.clear();
  M5Cardputer.Display.setTextDatum(top_center);
  M5Cardputer.Display.fillCircle(120, 30, 15, RED);
  M5Cardputer.Display.setFont(&fonts::FreeSansBoldOblique12pt7b);
  M5Cardputer.Display.drawString("recording", 120, 60);
  M5Cardputer.Display.setFont(&fonts::FreeSans9pt7b);
  M5Cardputer.Display.drawString("sample " + String(menu_selection + 1), 120, 85);
  
  float duration = (float)recorded_samples / record_samplerate;
  M5Cardputer.Display.drawString(String(duration, 1) + "s / " + String(max_duration_seconds) + "s", 120, 105);
  
  M5Cardputer.Display.setFont(&fonts::Font0);
  M5Cardputer.Display.drawString("press enter to stop", 120, 125);
  M5Cardputer.Display.display();
}

void drawPlaying() {
  M5Cardputer.Display.clear();
  M5Cardputer.Display.setTextDatum(top_center);
  M5Cardputer.Display.fillTriangle(100, 20, 100, 50, 130, 35, GREEN);
  M5Cardputer.Display.setFont(&fonts::FreeSansBoldOblique12pt7b);
  M5Cardputer.Display.drawString("playing", 120, 60);
  M5Cardputer.Display.setFont(&fonts::FreeSans9pt7b);
  M5Cardputer.Display.drawString("sample " + String(menu_selection + 1), 120, 85);
  M5Cardputer.Display.drawString("vol: " + String(volume), 120, 105);
  
  M5Cardputer.Display.setFont(&fonts::Font0);
  M5Cardputer.Display.drawString("enter: stop | ,/: vol", 120, 125);
  M5Cardputer.Display.display();
}

void deleteSample() {
  String filename = "/samples/sample_" + String(menu_selection + 1) + ".wav";
  if (SD.exists(filename)) {
    SD.remove(filename);
  }
}

void startRecording() {
  M5Cardputer.Speaker.end();
  M5Cardputer.Mic.end();
  delay(100);
  
  // start mic
  auto cfg = M5Cardputer.Mic.config();
  cfg.sample_rate = record_samplerate;
  M5Cardputer.Mic.config(cfg);
  M5Cardputer.Mic.begin();
  
  // delay for rec start
  delay(150);
  
  recorded_samples = 0;
  is_recording = true;
  current_mode = MODE_RECORDING;
  
  // filename
  String filename = "/samples/sample_" + String(menu_selection + 1) + ".wav";
  
  // overwrite existing
  if (SD.exists(filename)) {
    SD.remove(filename);
  }
  
  recording_file = SD.open(filename, FILE_WRITE);
  
  // WAV header
  if (recording_file) {
    writeWAVHeader(recording_file, 0);
  }
  
  drawRecording();
}

void stopRecording() {
  is_recording = false;
  
  // delay before rec stop
  delay(100);
  
  if (recording_file) {
    // update WAV header
    uint32_t data_size = recorded_samples * sizeof(int16_t);
    writeWAVHeader(recording_file, data_size);
    recording_file.flush();
    recording_file.close();
  }
  
  // close mic
  M5Cardputer.Mic.end();
  delay(100);
  
  current_mode = MODE_MENU;
  drawMenu();
}

void playSample() {
  String filename = "/samples/sample_" + String(menu_selection + 1) + ".wav";
  if (!SD.exists(filename)) return;
  
  File f = SD.open(filename, FILE_READ);
  if (!f) return;
  
  // skip WAV header
  f.seek(44);
  
  size_t file_size = f.size() - 44; // data without header
  size_t total_samples = file_size / sizeof(int16_t);
  f.close();
  
  if (total_samples == 0) return;
  
  // mic is stopped before playing
  
  M5Cardputer.Mic.end();
  delay(100);
  

  M5Cardputer.Speaker.begin();
  M5Cardputer.Speaker.setVolume(volume);
  
  delay(100);
  
  current_mode = MODE_PLAYING;
  is_playing = true;
  drawPlaying();
  
  // playback buffer
  const size_t playback_buffer_size = 4096;
  int16_t *playback_buffer = (int16_t*)malloc(playback_buffer_size * sizeof(int16_t));
  
  if (!playback_buffer) {
    M5Cardputer.Speaker.end();
    current_mode = MODE_MENU;
    drawMenu();
    return;
  }
  
  while (is_playing) {
    M5Cardputer.update();
    
    // playback stop and volume changes
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
      if (status.enter) {
        is_playing = false;
        break;
      }
      
      // volume change
      for (auto i : status.word) {
        if (i == ',') {
          volume = max(0, volume - 25);
          M5Cardputer.Speaker.setVolume(volume);
          drawPlaying();
        } else if (i == '/') {
          volume = min(255, volume + 25);
          M5Cardputer.Speaker.setVolume(volume);
          drawPlaying();
        }
      }
    }
    
    // sample playback
    f = SD.open(filename, FILE_READ);
    if (f) {
      f.seek(44); // skip WAV header
      
      while (f.available() && is_playing) {
        size_t samples_to_read = playback_buffer_size;
        size_t bytes_read = f.read((uint8_t*)playback_buffer, samples_to_read * sizeof(int16_t));
        size_t samples_read = bytes_read / sizeof(int16_t);
        
        if (samples_read > 0) {
          M5Cardputer.Speaker.playRaw(playback_buffer, samples_read, record_samplerate, false, 1, 0);
          
          while (M5Cardputer.Speaker.isPlaying() && is_playing) {
            M5Cardputer.update();
            
            // check for button press
            if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
              Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
              if (status.enter) {
                is_playing = false;
                break;
              }
              
              // volume change during playback
              for (auto i : status.word) {
                if (i == ',') {
                  volume = max(0, volume - 25);
                  M5Cardputer.Speaker.setVolume(volume);
                  drawPlaying();
                } else if (i == '/') {
                  volume = min(255, volume + 25);
                  M5Cardputer.Speaker.setVolume(volume);
                  drawPlaying();
                }
              }
            }
            delay(10);
          }
        }
      }
      f.close();
    }
    
    // still playing?
    if (!is_playing) break;
  }
  
  free(playback_buffer);
  
  // delay before speaker stop
  delay(100);
  
  
  M5Cardputer.Speaker.end();
  delay(100);
  
  current_mode = MODE_MENU;
  drawMenu();
}

void setup(void) {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg);
  
  M5Cardputer.Display.startWrite();
  M5Cardputer.Display.setRotation(1);
  
  // sd card init
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    M5Cardputer.Display.clear();
    M5Cardputer.Display.setTextDatum(middle_center);
    M5Cardputer.Display.drawString("SD card failed!", 120, 67);
    M5Cardputer.Display.display();
    while (1) delay(100);
  }
  
  // directory init
  if (!SD.exists("/samples")) {
    SD.mkdir("/samples");
  }
  
  // recording buffer
  rec_buffer = (int16_t*)malloc(record_length * sizeof(int16_t));
  if (!rec_buffer) {
    M5Cardputer.Display.clear();
    M5Cardputer.Display.setTextDatum(middle_center);
    M5Cardputer.Display.drawString("memory error!", 120, 67);
    M5Cardputer.Display.display();
    while (1) delay(100);
  }
  
  drawMenu();
}

void loop(void) {
  M5Cardputer.update();
  
  if (current_mode == MODE_MENU) {
    if (M5Cardputer.Keyboard.isChange()) {
      if (M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
        
        // menu buttons
        for (auto i : status.word) {
          if (i == ';') {  // menu up
            menu_selection = (menu_selection - 1 + NUM_SAMPLES) % NUM_SAMPLES;
            drawMenu();
          } else if (i == '.') {  // menu down
            menu_selection = (menu_selection + 1) % NUM_SAMPLES;
            drawMenu();
          } else if (i == ' ') {  // rec button
            startRecording();
          } else if (i == ',') {  // volume down
            volume = max(0, volume - 25);
            drawMenu();
          } else if (i == '/') {  // volume up
            volume = min(255, volume + 25);
            drawMenu();
          }
        }
        
        // play sample
        if (status.enter) {
          playSample();
        }
        // del sample
        else if (status.del) {
          deleteSample();
          drawMenu();
        }
      }
    }
  }
  else if (current_mode == MODE_RECORDING) {
    if (is_recording) {
      // rec max duration
      if (recorded_samples >= max_record_size) {
        stopRecording();
        return;
      }
      
      // Record a chunk
      if (M5Cardputer.Mic.record(rec_buffer, record_length, record_samplerate)) {
        // write chunk to SD
        if (recording_file) {
          size_t samples_to_write = record_length;
          if (recorded_samples + samples_to_write > max_record_size) {
            samples_to_write = max_record_size - recorded_samples;
          }
          
          recording_file.write((uint8_t*)rec_buffer, samples_to_write * sizeof(int16_t));
          recorded_samples += samples_to_write;
          
          // refresh display
          static unsigned long last_update = 0;
          if (millis() - last_update > 200) {
            drawRecording();
            last_update = millis();
          }
        }
      }
    }
    
    // stop rec button
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
      if (status.enter) {
        stopRecording();
      }
    }
  }
}