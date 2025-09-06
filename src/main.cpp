// Arduino BLE-MIDI - Ref: https://github.com/lathoub/Arduino-BLE-MIDI
#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_ESP32_NimBLE.h>

#include <SSD1306Wire.h>

#include <JC_Button.h> // Ref: https://github.com/JChristensen/JC_Button
#include <jled.h>      // Ref: https://github.com/jandelgado/jled

// Pin Definitions.
// Safe GPIO pins for switch/button input on ESP32:
// 2, 4, 5, 13, 14, 15, 16, 17, 18, 19, 21, 23, 24, 25, 26, 27, 32, 33
// const int PIN_RX = 2;
// const int PIN_TX = 3;
const int PIN_TRACK_A = 5;

// Safe ADC pins:
// 32, 33, 34, 35, 36, 39
const int PIN_TRACK_A_NOTE_POT = 32;
const int PIN_TRACK_A_LENGTH_POT = 33;
const int PIN_TRACK_B_NOTE_POT = 34;
const int PIN_TRACK_B_LENGTH_POT = 35;
const int PIN_TRACK_C_NOTE_POT = 36;
const int PIN_TRACK_C_LENGTH_POT = 39;

const int NUM_TRACKS = 3;

const int NOTE_POTS[NUM_TRACKS] = {
    PIN_TRACK_A_NOTE_POT,
    PIN_TRACK_B_NOTE_POT,
    PIN_TRACK_C_NOTE_POT};

const int LENGTH_POTS[NUM_TRACKS] = {
    PIN_TRACK_A_LENGTH_POT,
    PIN_TRACK_B_LENGTH_POT,
    PIN_TRACK_C_LENGTH_POT};

int noteValues[NUM_TRACKS];
int lengthValues[NUM_TRACKS];

bool activeNotes[NUM_TRACKS] = {false};
int activeNoteValues[NUM_TRACKS] = {0};
int activeNoteLengths[NUM_TRACKS] = {0};
unsigned long noteOnTime[NUM_TRACKS] = {0};

const int LENGTH_OPTIONS[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0}; // 1/16, 1/8, 3/16, 1/4, 3/8, 1/2
const int NUM_LENGTHS = sizeof(LENGTH_OPTIONS) / sizeof(LENGTH_OPTIONS[0]);

Button trackAButton(PIN_TRACK_A, 50);

const unsigned long analogReadInterval = 100; // ms
unsigned long lastAnalogReadAt = 0;

// Tempo Source
enum TempoSource
{
  SOFTWARE_BPM,
  EXTERNAL_GATE
};
TempoSource tempoSource = SOFTWARE_BPM;

// External Gate Input - Not implemented yet
volatile bool gatePulse = false; // Set to true when a gate pulse is detected.

float BPM = 80.0;
unsigned long lastClockTime = 0;
unsigned long clockCount = 0;

float sixteenthMs()
{
  float beatMs = 60000.0 / BPM; // = length of 1/4 (ms)
  return beatMs / 4.0;          // = length of 1/16 (ms)
}

// MIDI
const int MIDI_CH = 1;
BLEMIDI_CREATE_INSTANCE("MS. STEPS", MIDI);
const char* NOTE_NAMES[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

// Send a NoteOn message for the specified note (MIDI note number).
void sendNoteOn(int note)
{
  MIDI.sendNoteOn(note, 127, MIDI_CH);
  Serial.print("NoteOn: ");
  Serial.println(note);
}

void sendNoteOff(int note)
{
  MIDI.sendNoteOff(note, 127, MIDI_CH);
  Serial.print("NoteOff: ");
  Serial.println(note);
}

// OLED
SSD1306Wire display(0x3c, SDA, SCL);
const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
unsigned long oledLastUpdatedAt = 0;
const unsigned long oledUpdateInterval = 1000 / 30; // = 30Hz.

String midiNoteToName(int noteNumber) {
  int noteIndex = noteNumber % 12;
  int octave = noteNumber / 12 - 1; // MIDI 0 = C-1
  return String(NOTE_NAMES[noteIndex]) + String(octave);
}

void drawDisplay()
{
  unsigned long now = millis();
  if (now - oledLastUpdatedAt < oledUpdateInterval)
  {
    return;
  }

  display.clear();

  // Draw vertical thirds
  int thirdWidth = SCREEN_WIDTH / 3;
  display.drawLine(thirdWidth, 0, thirdWidth, SCREEN_HEIGHT - 12);
  display.drawLine(thirdWidth * 2, 0, thirdWidth * 2, SCREEN_HEIGHT - 12);

  // Draw note name, note length and sendNodeOn/Off indicators
  for (int i = 0; i < NUM_TRACKS; i++)
  {
    int centerX = thirdWidth * i + thirdWidth / 2;
    
    // Draw note name
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_16);
    display.drawString(centerX, 0, midiNoteToName(noteValues[i]));

    // Draw note length
    display.setFont(ArialMT_Plain_10);
    display.drawString(centerX, 17, String(lengthValues[i])+"/16");
  
    if (activeNotes[i])
    {
      display.fillCircle(centerX, 41, 10); // これが 1/16 の時だけ常時点灯になっちゃう
    }
  }

  display.drawString(SCREEN_WIDTH/2, SCREEN_HEIGHT - 10, "MS. STEPS");

  display.display();

  oledLastUpdatedAt = now;
}

void handleBLEMIDIConnected()
{
}

void handleBLEMIDIOnDisonnected()
{
}

void updatePots()
{
  if (millis() - lastAnalogReadAt >= analogReadInterval)
  {

    // Read pots and update noteValues and lengthValues arrays.
    // for (int i = 0; i < NUM_TRACKS; i++) {
    //   noteValues[i] = map(analogRead(NOTE_POTS[i]), 0, 4095, 60, 84); // 2 Octaves: C4 to B5;
    //   int index = map(analogRead(LENGTH_POTS[i]), 0, 4095, 0, NUM_LENGTHS-1);
    //   lengthValues[i] = LENGTH_OPTIONS[index];
    // }

    // For testing - Read only Track A pots.
    noteValues[0] = map(analogRead(PIN_TRACK_A_NOTE_POT), 0, 4095, 60, 84); // 2 Octaves: C4 to B5;
    int index = map(analogRead(PIN_TRACK_A_LENGTH_POT), 0, 4095, 0, NUM_LENGTHS - 1);
    lengthValues[0] = LENGTH_OPTIONS[index];

    // For testing - Set fixed values for other tracks.
    noteValues[1] = 67;
    noteValues[2] = 74;
    lengthValues[1] = 5;
    lengthValues[2] = 7;

    // Serial.print("Note: ");
    // Serial.println(noteValues[0]);
    // Serial.print("Length: ");
    // Serial.println(lengthValues[0]);
    lastAnalogReadAt = millis();
  }
}

void setup()
{
  Serial.begin(115200);
  trackAButton.begin();

  BLEMIDI.setHandleConnected(handleBLEMIDIConnected);
  BLEMIDI.setHandleDisconnected(handleBLEMIDIOnDisonnected);
  MIDI.begin();

  for (int i = 0; i < NUM_TRACKS; i++)
  {
    noteValues[i] = 60;
    lengthValues[i] = 2; // Initail value = 1/8
  }

  lastClockTime = millis();

  // External gate input - Not implemented yet
  // pinMode(GATE_PIN, INPUT);
  // attachInterrupt(digitalPinToInterrupt(GATE_PIN), gateISR, RISING);

  display.init();
  display.flipScreenVertically();
  drawDisplay();
}

void loop()
{

  updatePots();

  unsigned long now = millis();
  bool advanceClock = false;

  // Advance clock based on tempo source
  if (tempoSource == SOFTWARE_BPM)
  {
    if (now - lastClockTime >= sixteenthMs())
    {
      lastClockTime += sixteenthMs();
      advanceClock = true;
    }
  }
  else if (tempoSource == EXTERNAL_GATE)
  {
    if (gatePulse)
    {
      gatePulse = false;
      advanceClock = true;
    }
  }

  // Check clock
  if (advanceClock)
  {
    clockCount++;

    for (int i = 0; i < NUM_TRACKS; i++)
    {
      if(lengthValues[i] == 0)
      {
        if (activeNotes[i]) {
          sendNoteOff(activeNoteValues[i]);
          activeNotes[i] = false;
        }
        continue;
      }

      if (!activeNotes[i] && clockCount % lengthValues[i] == 0)
      {
        // Note on handling
        sendNoteOn(noteValues[i]);
        activeNotes[i] = true;
        activeNoteValues[i] = noteValues[i];
        activeNoteLengths[i] = lengthValues[i];
        noteOnTime[i] = millis();
      }

      if (activeNotes[i]) {
        // Note off handling
        unsigned long durationMs = sixteenthMs() * activeNoteLengths[i] / 2;
        if (millis() - noteOnTime[i] >= durationMs) {
          sendNoteOff(activeNoteValues[i]);
          activeNotes[i] = false;
        }
      }
    }
  }

  drawDisplay();
  delay(1); // To avoid busy loop.
}

// =====================
// External gate input handling
// =====================
// void gateISR() {
//   gatePulse = true;
// }