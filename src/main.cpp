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

const int LENGTH_OPTIONS[] = {1, 2, 3, 4, 5, 6, 7, 8}; // 1/16, 1/8, 3/16, 1/4, 3/8, 1/2
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
unsigned long oledLastUpdatedAt = 0;
const unsigned long oledUpdateInterval = 1000 / 30; // = 30Hz.

void drawDisplay()
{
  display.clear();
  display.drawLine(63, 16, 47, 48);
  display.display();
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

    Serial.print("Note: ");
    Serial.println(noteValues[0]);
    Serial.print("Length: ");
    Serial.println(lengthValues[0]);
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
      if (clockCount % lengthValues[i] == 0)
      {
        sendNoteOn(noteValues[i]);
        activeNotes[i] = true;
        activeNoteValues[i] = noteValues[i];
        activeNoteLengths[i] = lengthValues[i];
      }

      int offClock = (activeNoteLengths[i] + 1) / 2;
      if (activeNotes[i] && (clockCount % activeNoteLengths[i] == offClock % activeNoteLengths[i]))
      {
        sendNoteOff(activeNoteValues[i]);
        activeNotes[i] = false;
      }
    }
  }

  delay(1); // To avoid busy loop.
}

// =====================
// External gate input handling
// =====================
// void gateISR() {
//   gatePulse = true;
// }