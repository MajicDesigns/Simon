// Simon Game by MajicDesigns April 2020
//
// Creates a sequence of tones and lights for the player to repeat.
// As the game progresses, the sequence becomes progressively longer and more complex.
// Once the user fails or the player time limit runs out, the game is over.
//
// Reference: https://en.wikipedia.org/wiki/Simon_(game)
// Game rules: https://www.ultraboardgames.com/simon/game-rules.php
//
// Library dependencies:
// MD_UISwitch located at https://github.com/MajicDesigns/MD_UISwitch
//

#include <MD_UISwitch.h>
#include "pitches.h"

#define DEBUG 0

#if DEBUG
#define PRINT(s, v)   { Serial.print(F(s)); Serial.print(v); }
#define PRINTS(s)     { Serial.print(F(s)); }
#define PRINTX(s, v)  { Serial.print(F(s)); Serial.print(v, HEX); }
#else
#define PRINT(s, v)
#define PRINTS(s)
#define PRINTX(s, v)
#endif

const uint8_t MAX_CODE_LENGTH = 32;     // maximum length of codes generated per move
const uint8_t NUM_COLORS = 4;           // number of colors in the game

// Time values
const uint32_t TONE_TIME = 500;         // duration for a tone in ms
const uint32_t TONE_TIME_DELTA = 75;    // solo play display reduction tiome in ms per stage
const uint16_t TUNE_TIME = 200;         // note base time for tunes in ms

const uint32_t SELECT_TIME = 800;       // mode select blink rate in ms
const uint16_t BETWEEN_TIME = 500;      // pause between actions by cpu in ms

const uint32_t PLAYER_TIMEOUT = 5000;   // solo player timeout in ms
const uint32_t REACT_TIME_PRESET = 1000;// reaction starting time in ms
const uint16_t REACT_TIME_DELTA = 20;   // reaction time reduction in ms per round

// Tone parameters
const uint8_t TONE_PIN = 3;             // pin for the piezo buxxer

// Switch, LED and musical note parameters grouped in the structure
typedef struct
{
  uint8_t pinSwitch;  // pin for the switch
  uint8_t pinLED;     // pin for the LED
  uint16_t note;      // note for this color (from pitches.h)
} boardColor_t;

const boardColor_t board[NUM_COLORS] =
{
  // S, L, N
  {  8, 4, NOTE_E4 },  // Green
  {  9, 5, NOTE_CS5 }, // Yellow
  { 10, 6, NOTE_E5 },  // Blue
  { 11, 7, NOTE_A5 },  // Red
};

const uint8_t GREEN = 0;
const uint8_t YELLOW = 1;
const uint8_t BLUE = 2; 
const uint8_t RED = 3;

uint8_t switchPin[NUM_COLORS];      // copy of color switch pins for MD_UISwitch
MD_UISwitch_Digital S(switchPin, NUM_COLORS);

// Define some 'tunes' to play when milestones are reached
typedef struct
{
  uint8_t color;
  uint8_t time_multiple;
} note_t;

const note_t PROGMEM welcome[] =
{
  {GREEN, 1}, {RED, 1}, {BLUE, 1 }, {YELLOW, 1}, {GREEN, 1}, 
  {YELLOW, 1 }, {RED, 1 }, {BLUE, 2}, {GREEN, 1}, {YELLOW, 1}, 
//  {RED, 1}, {GREEN, 1}, {YELLOW, 1}, {BLUE, 3}, {RED, 1},
//  {GREEN, 1}, {BLUE, 1}, {RED, 2}, {YELLOW, 1}, {GREEN, 1}, 
//  {RED, 1}, {BLUE, 1}, {YELLOW, 1}, 
  {0, 0} // end of tune marker
};

const note_t PROGMEM celebrate[] =
{
  {GREEN, 1}, {YELLOW, 1}, {BLUE, 1}, {RED, 1},
  {GREEN, 1}, {YELLOW, 1}, {BLUE, 1}, {RED, 1},
  {0, 0} // end of tune marker
};

void clear(void)
{
  // clear the board
  noTone(TONE_PIN);
  for (uint8_t i = 0; i < NUM_COLORS; i++)
    digitalWrite(board[i].pinLED, LOW);
}

void showColor(uint8_t c, uint32_t duration = TONE_TIME)
// sow a LED and play the associated tone
{
  digitalWrite(board[c].pinLED, HIGH);
  tone(TONE_PIN, board[c].note);
  delay(duration);
  noTone(TONE_PIN);
  digitalWrite(board[c].pinLED, LOW);
}

void showTimeout(uint8_t c)
{
  showColor(c, TONE_TIME * 2);
}

void showFail(uint8_t c)
{
  showColor(c, TONE_TIME * 4);
}

void playTune(const note_t* tune)
// Play an array of note_t terminated with a 0 time multiple
// The tune is stored in PROGMEM (flash) memory
{
  uint16_t duration;
  note_t n;

  do
  {
    memcpy_P((void*)&n, (const void*)tune, sizeof(note_t));
    duration = TUNE_TIME * n.time_multiple;
    if (duration != 0) showColor(n.color, duration);
    tune++;
  } while (duration != 0);
}

int pin2idx(uint8_t pin)
// give a pin return the index of the board element
// -1 if not found
{
  int idx = -1;

  for (uint8_t i = 0; i < NUM_COLORS; i++)
  {
    if (board[i].pinSwitch == pin)
    {
      idx = i;
      break;
    }
  }

  return(idx);
}

bool gameSolo(void)
// This is a single or multiplayer game.
// This is the classic game where the computer selects a secret code and the 
// player has to repeat the code incrementally until a mistake is made or the
// entire code (up to MAX_CODE_LENGTH) is repeated successfully. After each 
// successful cycle the number of notes to rememeber increases by 1.
// The player has up to USER_TIMEOUT time to make their guess.
// The game speeds uip after the 5th, 9th and 13th guess in the sequence.
{
  static enum :uint8_t { STARTUP, MAKECODE, SHOWCODE, USER_MOVE, CYCLE_COMPLETE, ALL_DONE, RESET } state = STARTUP;
  static uint8_t idxCode;       // index of successful guesses
  static uint8_t idxProgress;   // current size of code guess
  static uint8_t code[MAX_CODE_LENGTH];      // hold the secret code when generated!
  static uint32_t timeLast;     // to track time
  static uint32_t tone_time;    // speeds up during the game

  bool b = true;

  switch (state)
  {
  case STARTUP:
    clear();
    idxProgress = 1;
    state = MAKECODE;
    break;

  case MAKECODE:
    // make the random code in an array
    PRINTS("\n=>MAKECODE\nCode:");
    randomSeed(millis());
    for (uint8_t i = 0; i < MAX_CODE_LENGTH; i++)
    {
      code[i] = random(NUM_COLORS);
      PRINT(" ", code[i]);
    }
    tone_time = TONE_TIME;
    state = SHOWCODE;
    break;

  case SHOWCODE:
    // now show the code to the user
    for (uint8_t i = 0; i < idxProgress; i++)
    {
      showColor(code[i], tone_time);
      delay(tone_time/2);
    }
    idxCode = 1;     // start user input all over again
    timeLast = millis();
    state = USER_MOVE;
    break;

  case USER_MOVE:
    // have we timed out?
    if (millis() - timeLast >= PLAYER_TIMEOUT)
    {
      PRINTS("\nTimeout!");
      showTimeout(code[idxCode - 1]);
      state = RESET;
    }

    // now check for user input
    if (S.read() == MD_UISwitch::KEY_PRESS)
    {
      int idx = pin2idx(S.getKey());

      timeLast = millis();
      // we found the pin number if changed from -1
      if (idx != -1)
      {
        // is this a good guess?
        if (idx != code[idxCode - 1])
        {
          // bad guess
          PRINTS("\nBad guess");
          showFail(idx);  // fail indicator!
          state = RESET;
        }
        else
        {
          // good guess
          showColor(idx);

          // is this the last guess?
          PRINT("\nOK idx=", idxCode);
          PRINT(" prog=", idxProgress);
          if (idxCode >= idxProgress)
          {
            delay(BETWEEN_TIME);    // delay for LED animation break
            state = CYCLE_COMPLETE;
          }
          idxCode++;
        }
      }
    }
    break;

  case CYCLE_COMPLETE:
    PRINTS("\n=>CYCLE_COMPLETE");
    idxProgress++;   // make it one cycle longer
    if (idxProgress == 5 || idxProgress == 9 || idxProgress == 13)
      tone_time -= TONE_TIME_DELTA;
    state = (idxProgress > MAX_CODE_LENGTH ? ALL_DONE : SHOWCODE);
    break;

  case ALL_DONE:
    PRINTS("\n=>ALL_DONE");
    // do something to celebrate
    playTune(celebrate);
    state = RESET;
    break;

  case RESET:
    PRINTS("\n=>RESET");
    state = STARTUP;
    b = false;
    break;
  }

  return(b);
}

bool gameAdd(void)
// This is a multiplayer game.
// In this game the computer selects the first color and displays that. 
// The first player mimics that color and adds another. The second player 
// mimics the first 2 and then adds another, and so on. The first player to 
// make a mistake loses the game.
// The player has up to USER_TIMEOUT time to make their guess.
{
  static enum :uint8_t { INIT, START_INPUT, USER_MOVE, USER_ADD, ALL_DONE, RESET } state = INIT;
  static uint8_t idxCode;       // index of successful guesses
  static uint8_t idxProgress;   // current size of code guess
  static uint8_t code[MAX_CODE_LENGTH];      // hold the secret code when generated!
  static uint32_t timeLast;     // to track time
  bool b = true;

  switch (state)
  {
  case INIT:
    PRINTS("\n=>INIT");
    clear();
    idxProgress = 1;

    // make the random code in an array
    randomSeed(millis());
    code[0] = random(NUM_COLORS);
    PRINT("\nCode:", code[0]);
    // now show the code to the user
    showColor(code[0]);
    state = START_INPUT;
    break;

  case START_INPUT:
    PRINTS("\n=>START_INPUT");
    idxCode = 1;     // start user input tracking
    timeLast = millis();
    state = USER_MOVE;
    break;

  case USER_MOVE:
    // have we timed out?
    if (millis() - timeLast >= PLAYER_TIMEOUT)
    {
      PRINTS("\nMove timeout!");
      showTimeout(code[idxCode - 1]);
      state = RESET;
    }

    // now check for user input
    if (S.read() == MD_UISwitch::KEY_PRESS)
    {
      int idx = pin2idx(S.getKey());

      timeLast = millis();
      // we found the pin number if changed from -1
      if (idx != -1)
      {
        // is this a good guess?
        if (idx != code[idxCode - 1])
        {
          // bad guess
          PRINTS("\nBad guess");
          showFail(idx);  // fail indicator!
          state = RESET;
        }
        else
        {
          // good guess
          showColor(idx);

          // is this the last guess?
          PRINT("\nOK idx=", idxCode);
          PRINT(" prog=", idxProgress);
          if (idxCode == idxProgress)
            state = (idxProgress == MAX_CODE_LENGTH) ? ALL_DONE : USER_ADD;
          idxCode++;
        }
      }
    }
    break;

  case USER_ADD:
    // have we timed out?
    if (millis() - timeLast >= PLAYER_TIMEOUT)
    {
      PRINTS("\nAdd timeout!");
      showTimeout(code[idxCode]);
      state = RESET;
    }

    // now check for user input
    if (S.read() == MD_UISwitch::KEY_PRESS)
    {
      int idx = pin2idx(S.getKey());

      timeLast = millis();
      // we found the pin number if changed from -1
      if (idx != -1)
      {
        // good guess
        showColor(idx);

        // is this the last guess?
        PRINT("\nAdd idx=", idxCode);
        PRINT(" prog=", idxProgress);
        code[idxCode-1] = idx;
        idxProgress++;   // make it one cycle longer
        state = START_INPUT;
      }
    }
    break;

  case ALL_DONE:
    PRINTS("\n=>ALL_DONE");
    // do something to celebrate
    playTune(celebrate);
    state = RESET;
    break;

  case RESET:
    PRINTS("\n=>RESET");
    state = INIT;
    b = false;
    break;
  }

  return(b);
}

bool gameReact(void)
// This is a single or multiplayer game.
// In this each player is allocated a color. The computer selects a color
// at random and the player must press their switch within a preset time. 
// As the game progresses the time reaction time is reduced. If a player 
// does not press within the reaction time they lose the game.
// The round can only finish on a timeout or error.
//
{
  static enum :uint8_t { STARTUP, SHOWCODE, USER_MOVE, RESET } state = STARTUP;
  static uint8_t code;          // hold the current color
  static uint32_t timeReact;     // current reaction time
  static uint32_t timeLast;     // to track time
  bool b = true;

  switch (state)
  {
  case STARTUP:
    clear();
    delay(BETWEEN_TIME);      // allow some settling time for users
    timeReact = REACT_TIME_PRESET;
    randomSeed(millis());
    state = SHOWCODE;
    break;

  case SHOWCODE:
    // make the random code in an array
    PRINTS("\n=>SHOWCODE\nCode:");
    code = random(NUM_COLORS);
    PRINT(" ", code);
    showColor(code);
    timeLast = millis();
    state = USER_MOVE;
    break;

  case USER_MOVE:
    // have we timed out?
    if (millis() - timeLast > timeReact)
    {
      PRINTS("\nTimeout!");
      showFail(code); // show what they timed out on!
      state = RESET;
    }

    // now check for user input
    if (S.read() == MD_UISwitch::KEY_PRESS)
    {
      int idx = pin2idx(S.getKey());

      // we found the pin number if changed from -1
      if (idx != -1)
      {
        // is this a good guess?
        if (idx == code)
        {
          timeReact -= REACT_TIME_DELTA;    // adjust reaction time
          state = SHOWCODE;
        }
        else
        {
          // bad guess
          PRINTS("\nWrong guess");
          showFail(idx);  // fail indicator!
          state = RESET;
        }
      }
    }
    break;

  case RESET:
    PRINTS("\n=>RESET");
    state = STARTUP;
    b = false;
    break;
  }

  return(b);
}

void setup(void)
{
#if DEBUG
  Serial.begin(57600);
#endif
  PRINTS("\n[Simon Game]");

  // initalize miscellaneous hardware
  pinMode(TONE_PIN, OUTPUT);
  for (uint8_t i = 0; i < NUM_COLORS; i++)
  {
    pinMode(board[i].pinLED, OUTPUT);
    switchPin[i] = board[i].pinSwitch;
  }

  // initialize the color switch object
  S.begin();
  S.enableLongPress(false);
  S.enableDoublePress(false);
  S.enableRepeat(false);

  playTune(welcome);
}


void loop(void)
{
  static uint32_t timeLast = 0;
  static enum:uint8_t { SOLO = 0, ADD = 1, REACT = 2, SELECT = 5 } mode = SELECT;   // current game mode
  static uint8_t modeIdx = 0;   // index of the LED for the current mode

  switch (mode)
  {
  case SOLO:  if (!gameSolo()) mode = SELECT;  break;
  case ADD:   if (!gameAdd()) mode = SELECT;   break;
  case REACT: if (!gameReact()) mode = SELECT; break;

  case SELECT:
    // Select the gameplay mode.
    // Blink the current mode LED until it is selected (start play) 
    // or another mode is selected.

    // check if the blink time is expired and toggle the LED
    if (millis() - timeLast >= SELECT_TIME)
    {
      timeLast = millis();
      digitalWrite(board[modeIdx].pinLED, !digitalRead(board[modeIdx].pinLED));
    }

    // now see if a key was pressed
    if (S.read() == MD_UISwitch::KEY_PRESS)
    {
      int idx = pin2idx(S.getKey());

      if (idx == modeIdx)   // pressed current selection - run it
      {
        switch (modeIdx)
        {
        case 0: mode = SOLO;   PRINTS("\nPlay Solo"); break;
        case 1: mode = ADD;  PRINTS("\nPlay Learn");  break;
        case 2: mode = REACT;  PRINTS("\nPlay Reaction");  break;
        }
      }
      else if (idx >= SOLO && idx <= REACT)   // new selection
      {
        digitalWrite(board[modeIdx].pinLED, LOW);   // switch off current LED
        modeIdx = idx;
        timeLast = 0;     // make sure we show this immediately
      }
    }
    break;
  }
}
