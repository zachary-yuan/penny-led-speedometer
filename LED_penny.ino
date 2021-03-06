#include "FastLED.h"

const float pi = 3.1416;
const int WHEEL_DIA = 59; // millimeters
const int BUTTON_PIN = 2;
const int HALL_PIN = 3;
const int LEDS_PIN = 6;

const int NUM_MODES = 3;
int mode = 1;
volatile bool buttonPressed = false;
int hallState = 0;

// calculate speed variables
unsigned long oldPulseTime = 0;
volatile unsigned long newPulseTime = 0;
volatile unsigned long newPulseTime_temp = 0;
volatile bool newPulse = false;
double prevRPM = -1;
double RPM = 0;
const int MAX_RPM = 800; // expected max RPM
const int MIN_RPM = 120; // minimum allowable RPM before zeroing out
const int TIMEOUT = 1000 / (MIN_RPM / 60);
int smoothedRPM = 0; // RPM after applying EMA filter

// LED settings
int speedFactor;
int brightness;
const int NUM_LEDS = 63;
const uint8_t MAX_V = 127; // max allowable brightness
CRGB leds[NUM_LEDS];

// Exponential Moving Average Filter
class EMA {
  public:
    // Constructor: Initialize constants
    EMA(uint8_t shiftFac)
      : shiftFac(shiftFac), fixedPointAHalf(1 << ((shiftFac * 2) - 1)) {}

    // Filter a new raw input value x[n] and return the filtered output y[n].
    // Should be called at regular intervals for best results.
    int32_t filter(int32_t value) {
      value = value << (shiftFac * 2);
      filtered = filtered + ((value - filtered) >> shiftFac);
      return (filtered + fixedPointAHalf) >> (shiftFac * 2);
    }

  private:
    // Member variables
    const uint8_t shiftFac;
    const int32_t fixedPointAHalf;
    int32_t filtered = 0;
};
// Filtering for each mode, higher parameter means more smoothing but slower reaction
EMA ema2(2);
EMA ema3(6);

/* ============================== Setup ============================== */

void setup() { 
  // Serial.begin(9600);

  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), ISR_button, FALLING);
  attachInterrupt(digitalPinToInterrupt(HALL_PIN), ISR_RPM, FALLING); 

  FastLED.addLeds<NEOPIXEL, LEDS_PIN>(leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5,400);
}

void loop() { 
  if (buttonPressed) {
    noInterrupts();
      buttonPressed = false;
      mode++;
      if (mode > NUM_MODES) mode = 1;
    interrupts();
  }

  RPM = calculate_RPM();
  if (prevRPM == -1) 
    prevRPM = RPM;

  // Set cutoff RPM, low values get sent to zero
  if (RPM < MIN_RPM || millis() - newPulseTime > TIMEOUT)
    RPM = 0;
  
  // Serial.println(RPM);

  // Play LED mode depending on which mode specified by button
  switch (mode) {
    case 1:
      LED_mode_1(RPM);
      break;
    case 2:
      LED_mode_2(RPM);
      break;
    case 3:
      LED_mode_3(RPM);
      break;
    default:
      break;
  }
}

/* ============================== Animations ============================== */

/* ===== Mode 1 ===== * /
 *  Rainbow snakes down both sides of board
 */
int leftIndex = 48; // runs from pixel 48 - 63 -> 0 - 9 (hardcoded values)
int rightIndex = 40; // runs from pixel 40 - 15 (hardcoded values)
const int SNAKE_LENGTH = 17; // length of rainbow snake
void LED_mode_1(double RPM)
{
  // Idle animation, just a simple scrolling rainbow
  if (RPM == 0) {
    uint8_t hue_1 = beat8(10, 255);
    fill_rainbow(leds, NUM_LEDS, hue_1, 8);
    FastLED.setBrightness(MAX_V);
    FastLED.show();
  }
  // Rainbow runs down sides of the board at speed of wheel
  else {
    FastLED.clear();
    
    int delayTime = map(RPM, MIN_RPM, MAX_RPM, 50, 5);
    if (delayTime < 5) delayTime = 5;
    
    if (leftIndex > 63) leftIndex = 0;
    // Reset to front when snake completely disappears
    if (leftIndex < 48 && leftIndex - SNAKE_LENGTH >= 9) leftIndex = 48;
    if (rightIndex + SNAKE_LENGTH <= 15) rightIndex = 40;
    
    for (int j = 0; j < SNAKE_LENGTH; j++) {
      if (rightIndex + j > 40) continue;
      if (rightIndex + j < 15) continue;
      leds[rightIndex + j] = CHSV(j * 16, 255, MAX_V);
    }

    for (int j = 0; j < SNAKE_LENGTH; j++) {
      if (leftIndex - j < 0) {
        int fixedIndex = 64 + (leftIndex - j);
        leds[fixedIndex] = CHSV(j * 16, 255, MAX_V);
        continue;
      }
      
      if (leftIndex >= 48 && leftIndex - j < 48) continue;
      if (leftIndex < 48 && leftIndex - j > 9) continue;
      leds[leftIndex - j] = CHSV(j * 16, 255, MAX_V);
    }

    // Front (41 - 47) will match color of pixel 40
    for (int j = 41; j <= 47; j++) {
      leds[j] = leds[40];
    }

    // Back will match color of pixel 15
    for (int j = 10; j <= 14; j++) {
      leds[j] = leds[15];
    }
    FastLED.show();
    
    leftIndex++;
    rightIndex--;
    delay(delayTime);
  }
}


/* ===== Mode 2 ===== * /
 *  2 lengths of pixels "chase" each other 
 *  around the board, at speed of wheels
 */
int i = 0; // index to cycle through strip
int delayTime;
const uint8_t hueBounce_1 = 160; // default color to start from
const uint8_t hueBounce_2 = 25; // default color for second length
const uint8_t hueRange = 20; // hueBounce ranges from +- hueRange
bool hueInc = true;
uint8_t hue_2_1 = hueBounce_1;
uint8_t hue_2_2 = hueBounce_2;
const int NUM_LEDS_LIT = 25; // number of LEDs lit
const int MAX_V_2 = 255; // max brightness for mode 2, since only a few LEDs on at a time
void LED_mode_2(double RPM)
{
  FastLED.clear();
  smoothedRPM = ema2.filter(RPM);
  
  delayTime = map(smoothedRPM, 0, MAX_RPM, 150, 5);
  if (delayTime < 5) delayTime = 5;
  if (i >= NUM_LEDS) i = 0;

  // Bounce both colors between the specified range
  if (hueInc) {
    hue_2_1++;
    hue_2_2++;
    if (hue_2_1 > hueBounce_1 + hueRange) hueInc = false;
  }
  else {
    hue_2_1--;
    hue_2_2--;
    if (hue_2_1 < hueBounce_1 - hueRange) hueInc = true;
  }

  // Light the LEDs in front of index i, specified by NUM_LEDS_LIT
  for (int j = 0; j < NUM_LEDS_LIT; j++) {
    int distanceFromBrightest = abs(NUM_LEDS_LIT / 2 - j);
    int brightness_2 = map(distanceFromBrightest, 0, NUM_LEDS_LIT / 2, MAX_V_2, 16);
    leds[(i + j) % NUM_LEDS] = CHSV(hue_2_1, 255, brightness_2);
  }

  // Light the other length, on the opposite side of the first, i.e. NUM_LEDS / 2 pixels away
  for (int j = 0; j < NUM_LEDS_LIT; j++) {
    int distanceFromBrightest = abs(NUM_LEDS_LIT / 2 - j);
    int brightness_2 = map(distanceFromBrightest, 0, NUM_LEDS_LIT / 2, MAX_V_2, 16);
    leds[(i + j + NUM_LEDS / 2) % NUM_LEDS] = CHSV(hue_2_2, 255, brightness_2);
  }

  FastLED.show();
  i++;
  delay(delayTime);
}

/* ===== Mode 3 ===== * /
 *  Color changes based on speed
 *  Uses EMA filtered RPM for smoother transitions
 */
void LED_mode_3(double RPM)
{
  smoothedRPM = ema3.filter(RPM);
  // Serial.println(smoothedRPM);
  
  speedFactor = map(smoothedRPM, 0, MAX_RPM, 0, 60);
  brightness = map(smoothedRPM, 0, MAX_RPM, 63, MAX_V);
  if (brightness > MAX_V)  {
    speedFactor = 150;
    brightness = MAX_V;
  }
  
  FastLED.showColor(CHSV(180 - speedFactor, 255, brightness));
}

/* =========================== Helper functions =========================== */

int calculate_RPM()
{
  if (newPulse) {
    noInterrupts();
      newPulseTime_temp = newPulseTime;
      newPulse = false;
    interrupts();
    // discard interrupts that are unreasonably fast
    // dt of 60 ms corresponds to 1000 RPM
    if (newPulseTime_temp - oldPulseTime < 60) return prevRPM;
    
    double rpm = 60.0 / ((newPulseTime_temp - oldPulseTime) / 1000.0); // 2.23694;
    oldPulseTime = newPulseTime_temp;

    // discard garbage values where speed is way too high
    if (prevRPM == 0 && RPM > 600) return prevRPM;
    
    prevRPM = rpm;
    return rpm;
  }
}

void ISR_RPM()
{
  newPulseTime = millis();
  newPulse = true;
}

void ISR_button()
{
  buttonPressed = true;
}
