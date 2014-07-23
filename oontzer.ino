#include <Wire.h>
#include <Adafruit_Trellis.h>
#include <Adafruit_OONTZ.h>

Adafruit_Trellis T[4];
OONTZ oontz(&T[0], &T[1], &T[2], &T[3]);
const uint8_t addr[] = {
  0x70, // upper left
  0x71, // upper right
  0x72, // lower left
  0x73 // lower right
};

#define WIDTH ((sizeof(T) / sizeof(T[0])) * 2)
#define N_BUTTONS ((sizeof(T) / sizeof(T[0])) * 16)

uint16_t grid[4]; // Sequencer state, 4 tracks of 16 beats
uint8_t currentBeat = 15;
unsigned int bpm = 480; // Tempo
unsigned long beatInterval = 60000L / bpm, // ms/beat
              prevBeatTime = 0L, // Column step timer
              prevReadTime = 0L; // Keypad polling timer

// The note[] and channel[] tables are the MIDI note and channel numbers.
// bitmask[] is for efficient reading/writing bits to the grid[] array.
static const uint8_t note[4] = {29, 26, 30, 31}, channel[4] = {1, 1, 1, 1};
static const uint16_t beatmask[16] = {
  // TODO find out if its actually faster to dereference an array element
  // vs bit-shifting a uint16_t. if its not, this should be replaced with
  // a function which shifts given a beat (uint8_t).
  1, 2, 4, 8, 16, 32, 64, 128,
  256, 512, 1024, 2048, 4096, 8192, 16384, 32768
};

void setup() {
  oontz.begin(addr[0], addr[1], addr[2], addr[3]);
  #ifdef __AVR__
    // Default Arduino I2C speed is 100 KHz, but the HT16K33 supports
    // 400 KHz.  We can force this for faster read & refresh, but may
    // break compatibility with other I2C devices...so be prepared to
    // comment this out, or save & restore value as needed.
    TWBR = 12;
  #endif
  oontz.clear();
  oontz.writeDisplay();
  memset(grid, 0, sizeof(grid));
}


// highlight one beat on all tracks
void line(uint8_t beat, boolean forceShow) {
  // left off here: trying to refactor this function to
  // stop clearing LEDs while sweeping the line across
  // the sequencer.
  uint16_t mask = beatmask[beat];
  for (uint8_t track=0; track<4; track++) {
    uint8_t x = beat2x(beat);
    uint8_t y = beat2y(beat, track);
    uint8_t i = oontz.xy2i(x, y);
    if (forceShow || (grid[track] & mask)) {
      oontz.setLED(i);
    } else {
      oontz.clrLED(i);
    }
  }
}

void loop() {
  uint16_t mask;
  uint8_t track, beat;
  boolean refresh = false;
  unsigned long t = millis();

  if ((t - prevReadTime) >= 20L) { // 20ms = min Trellis poll time
    if (oontz.readSwitches()) { // Button state change?
      for (uint8_t i=0; i<N_BUTTONS; i++) { // For each button...
        uint8_t x, y;
        oontz.i2xy(i, &x, &y);
        track = y2track(y);
        beat = xy2beat(x, y);
        mask = beatmask[beat];
        if (oontz.justPressed(i)) {
          if (grid[track] & mask) { // Already set? Turn off...
            grid[track] &= ~mask;
            oontz.clrLED(i);
            usbMIDI.sendNoteOff(note[track], 127, channel[track]);
          } else { // Turn on
            grid[track] |= mask;
            oontz.setLED(i);
          }
          refresh = true;
        }
      }
    }
    prevReadTime = t;
  }

  if ((t - prevBeatTime) >= beatInterval) { // Next beat?
    // Turn off old column
    line(currentBeat, false);
    for (uint8_t track=0; track<4; track++) {
      if (grid[track] & beatmask[currentBeat]) {
        usbMIDI.sendNoteOff(note[track], 127, channel[track]);
      }
    }
    // Advance column counter, wrap around
    if (++currentBeat > 15) currentBeat = 0;
    // Turn on new column
    line(currentBeat, true);
    for (uint8_t track=0; track<4; track++) {
      if (grid[track] & beatmask[currentBeat]) {
        usbMIDI.sendNoteOn(note[track], 127, channel[track]);
      }
    }
    prevBeatTime = t;
    refresh = true;
    beatInterval = 60000L / bpm;
  }

  if (refresh) oontz.writeDisplay();

  while (usbMIDI.read()); // Discard incoming MIDI messages
}

uint8_t beat2x(uint8_t beat) {
  if (beat > 7) {
    return beat - 8;
  } else {
    return beat;
  }
}

uint8_t beat2y(uint8_t beat, uint8_t track) {
  if (beat > 7) {
    return (track * 2) + 1;
  } else {
    return (track * 2);
  }
}

uint8_t y2track(uint8_t y) {
  return y / 2;
}

uint8_t xy2beat(uint8_t x, uint8_t y) {
  if (y % 2 == 0) {
    return x;
  } else {
    return x + 8;
  }
}
