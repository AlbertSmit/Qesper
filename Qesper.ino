#include <Arduino.h>
#include <U8g2lib.h>
#include <RotaryEncoder.h>
#include <Gaussian.h>

#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

#include <AceButton.h>
using namespace ace_button;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE, /* clock=*/SCL, /* data=*/SDA); // All Boards without Reset of the Display

///////////////////////////////////////////////
///              BUTTON PINS                ///
///////////////////////////////////////////////
static const uint8_t BUTTON_PIN0 = 12;
static const uint8_t BUTTON_PIN1 = 14;

///////////////////////////////////////////////
///               LED PINS                  ///
///////////////////////////////////////////////
static const uint8_t LED_PIN = 16;
static const uint8_t YELLOW_LED = 0;
static const uint8_t GREEN_LED = 2;
static const uint8_t BLUE_LED = 1;

///////////////////////////////////////////////
///                 ENCODER                 ///
///////////////////////////////////////////////
static const uint8_t ENCODER_PIN0 = 13; // D7
static const uint8_t ENCODER_PIN1 = 15; // D8
RotaryEncoder *_encoder;
Encoded4To2ButtonConfig buttonConfig(BUTTON_PIN0, BUTTON_PIN1);
long _value = 0;

///////////////////////////////////////////////
///                 ACE BUTTON              ///
///////////////////////////////////////////////
AceButton b1(&buttonConfig, 1);
AceButton b2(&buttonConfig, 2);
AceButton b3(&buttonConfig, 3);
void handleEvent(AceButton *, uint8_t, uint8_t);

///////////////////////////////////////////////
///                 BPM STATE               ///
///////////////////////////////////////////////
int cursorIndex = 0;
long previousMillis = 0;
uint8_t BPM[8] = {80, 100, 120, 160, 168, 184, 192, 200};
int tempoIndex = 0;
long granularMillis = 0;
int granularIndex = 0;

///////////////////////////////////////////////
///              GLOBAL VALUES              ///
///////////////////////////////////////////////
int incrementialValue = 16;

///////////////////////////////////////////////
///                  MENU                   ///
///////////////////////////////////////////////
int menuIndex = 0;
String menuLetters[4] = {"Play", "Edit", "Visual", "Granular"};

///////////////////////////////////////////////
///            SEQUENCER STATE              ///
///////////////////////////////////////////////
int sequenceHitIndex = 0;
int sequenceLineIndex = 0;
uint8_t sequence[4][16] = {
    {true, false, false, false, true, false, false, false, true, false, false, false, true, false, false, false},
    {true, false, false, false, true, false, false, false, true, false, false, false, true, false, false, false},
    {true, false, false, false, true, false, false, false, true, false, false, false, true, false, false, false},
    {true, false, false, false, true, false, false, false, true, false, false, false, true, false, false, false}};
int sequenceNestedLength = 16;

/**
   @brief The interrupt service routine to check the signals from the rotary encoder
*/
ICACHE_RAM_ATTR void _checkPosition()
{
  _encoder->tick(); // just call tick() to check the state.
}

///////////////////////////////////////////////
///               HELPERS                   ///
///////////////////////////////////////////////

void resetOrAdd(int &value, int qualifier)
{
  qualifier ? value = 0 : value++;
}

void resetOrSubstract(int &value, int qualifier, int newMax)
{
  qualifier ? value = newMax : value--;
}

void setup(void)
{
  Serial.begin(115200);

  // Start display
  u8g2.begin();
  u8g2.setPowerSave(0);

  // Set all LED pins
  pinMode(LED_PIN, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);

  // Configure the real pins for pullup wiring
  pinMode(BUTTON_PIN0, INPUT_PULLUP);
  pinMode(BUTTON_PIN1, INPUT_PULLUP);

  // Encoder setup
  _encoder = new RotaryEncoder(ENCODER_PIN0, ENCODER_PIN1);
  pinMode(ENCODER_PIN0, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN0), _checkPosition, CHANGE);
  pinMode(ENCODER_PIN1, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN1), _checkPosition, CHANGE);

  // Configure the ButtonConfig with the event handler,
  // and enable all higher level events.
  buttonConfig.setEventHandler(handleEvent);
  buttonConfig.setFeature(ButtonConfig::kFeatureClick);
  buttonConfig.setFeature(ButtonConfig::kFeatureLongPress);
  buttonConfig.setFeature(ButtonConfig::kFeatureSuppressAfterLongPress);
}

int tempCount = 0;
void loop(void)
{
  u8g2.firstPage();
  do
  {
    b1.check();
    b2.check();
    b3.check();

    if (tempCount != 0)
    {
      drawLevel();
      tempCount--;
    }

    // BPM
    unsigned long interval = 60000UL / BPM[tempoIndex]; //algorithm to convert tempo into BPM
    unsigned long currentMillis = millis();

    handleBPM(interval, currentMillis);

    // Controls
    handleEncoder();

    // Blink LEDs
    blinkLEDsToSequence();

    // Drawers
    drawBPM();
    drawModeSelection();
    drawSequencerDots();
    drawMetronomeDots();

    if (menuLetters[menuIndex].equals("Visual"))
      drawVisuals();

  } while (
      u8g2.nextPage());
}

///////////////////////////////////////////////
///             DRAWER HELPERS              ///
///////////////////////////////////////////////

// Following 2 functinos are orrowed from this source:
// https://p3dt.net/post/2019/09/17/simple-ui-elements.html
void rotX(int cx, int r, int degrees)
{
  return cx + (r - 2) * cos(2 * PI * (degrees / 360) - PI);
}
void rotY(int cy, int r, int degrees)
{
  return cy + (r - 2) * sin(2 * PI * (degrees / 360) - PI);
}

void drawRotatableLine()
{
  uint8_t rx = x + r;
  uint8_t ry = y + r;

  uint8_t px = rotX(rx, r - r / 8, percent);
  uint8_t py = rotY(ry, r - r / 8, percent);
  u8g2.drawLine(rx, ry, px, py);
}

void drawCross(int x, int y, int size)
{
  u8g2.drawLine(x - (size / 2), y - (size / 2), x + (size / 2), y + (size / 2));
  u8g2.drawLine(x + (size / 2), y + (size / 2), x - (size / 2), y - (size / 2));
}

// TODO
void drawCube(int x, int y, int size)
{
  // Left side
  // u8g2.drawLine(x - (size / 2), y + (size / 2), x + (size / 2), y - (size / 2));

  int halfSize = size / 2;
  // Roof
  u8g2.drawLine(x - (size / 2), y + (size / 2), x, y + size);
  u8g2.drawLine(x, y, x + (size / 2), y - size);
}

///////////////////////////////////////////////
///               DRAWERS                   ///
///////////////////////////////////////////////

void drawBPM()
{
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.setCursor(5, (5 + 4));
  u8g2.print(BPM[tempoIndex]);
}

int counter;
void drawVisuals()
{
  u8g2.clearBuffer();

  resetOrAdd(counter, counter == 30);

  int base = 5;
  int radius = 1;

  for (int i = 0; i < sizeof(sequence) / sizeof(sequence[0] - 1); i++)
  {
    if (sequence[i][granularIndex] == true)
    {
      Gaussian g = Gaussian(0, (i * 2) + granularIndex);
      int randval = g.random();

      // u8g2.drawPixel(
      //     0 + base + (granularIndex * width) + randval,
      //     32 + randval);

      // u8g2.drawPixel(
      //     0 + base + (granularIndex * width) - randval,
      //     32 + randval);

      u8g2.drawDisc(
          64 + (randval),
          32 + randval, radius, U8G2_DRAW_ALL);

      u8g2.drawDisc(
          64 + randval,
          32 - randval, radius, U8G2_DRAW_ALL);
    }
  }
  u8g2.sendBuffer();
}

void blinkLEDsToSequence()
{
  // Map the array of length 3 to array of length 15.
  // This is the BPM divided by 4 times.

  sequence[0][granularIndex] ? digitalWrite(LED_PIN, HIGH) : digitalWrite(LED_PIN, LOW);
  sequence[1][granularIndex] ? digitalWrite(YELLOW_LED, HIGH) : digitalWrite(YELLOW_LED, LOW);
  sequence[2][granularIndex] ? digitalWrite(GREEN_LED, HIGH) : digitalWrite(GREEN_LED, LOW);
  sequence[3][granularIndex] ? digitalWrite(BLUE_LED, HIGH) : digitalWrite(BLUE_LED, LOW);
}

int timer;
void drawModeSelection()
{
  // Decrement timer
  if (timer != 0)
    timer--;

  // Placeholder dots
  int base = 5;
  int offset = 1;
  int width = incrementialValue;
  int radius = 1;
  int startAtWidth = 128 - base - (3 * width);
  int amount = 4;

  for (int i = 0; i < amount; i++)
  {
    if (menuIndex != i && timer != 0 && timer >= 4)
      u8g2.drawDisc(startAtWidth + (width * i) - offset, base, radius, U8G2_DRAW_ALL);
    if (menuIndex != i && timer != 0 && timer < 4)
      u8g2.drawPixel(startAtWidth + (width * i) - offset, base);
  }

  // Letter.
  String firstChar = menuLetters[menuIndex].substring(0, 1);
  const char *displayLetter = firstChar.c_str();
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(startAtWidth + (width * menuIndex) - offset, (5 + 4), displayLetter);
}

void drawSequencerDots()
{
  int base = 5;
  int spacing = 10;

  int width = incrementialValue / 4;
  int startAtWidth = 128 - base - (12 * width);

  int height = 2;
  int startAtHeight = 64 - base - (spacing * 3);

  for (int i = 0; i < sizeof(sequence) / sizeof(sequence[0] - 1); i++)
  {
    int pY = startAtHeight + (spacing * i);

    for (int z = 0; z < sequenceNestedLength - 1; z++)
    {
      if (sequence[i][z])
        u8g2.drawLine(startAtWidth + (width * z), pY, startAtWidth + (width * z), pY + height);
    }
  }

  // Cursor
  int posX = startAtWidth + (width * (sequenceHitIndex * 4));
  int posY = startAtHeight + (spacing * sequenceLineIndex) + 1;
  if (menuLetters[menuIndex].equals("Edit") || menuLetters[menuIndex].equals("Granular"))
    u8g2.drawLine(posX - 1, posY, posX + 1, posY);
}

void drawLevel()
{
  u8g2.clearBuffer();

  u8g2.drawLine((128 / 2), (64 / 2 - (_value)), (128), (64 / 2 - (_value)));
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.setCursor(5, (5 + (3 * 4)));
  u8g2.print(_value);

  u8g2.sendBuffer();
}

void drawMetronomeDots()
{
  int base = 5;
  int startAtHeight = 64 - base;
  int width = incrementialValue;

  int amount = 4;
  for (int i = 0; i < amount; i++)
  {
    // u8g2.drawLine(base + (incrementialValue * i), 59, base + (incrementialValue * i), 62);
    u8g2.drawPixel(base + (width * i), startAtHeight);
  }

  // Current * cursorIndex
  int spacing = 8;
  int height = 3;
  int pX = (base + (width * cursorIndex));
  int pX2 = (base + ((width / 4) * granularIndex));

  u8g2.drawLine(pX, startAtHeight - spacing, pX, startAtHeight - spacing - height);
  //  u8g2.drawLine(pX2, startAtHeight - (spacing / 4), pX2, startAtHeight - (spacing / 4) - 2);
}

///////////////////////////////////////////////
///                 BPM                     ///
///////////////////////////////////////////////

void handleBPM(unsigned long &intrvl, unsigned long &curr)
{
  // 120 BPM tick.
  if (curr - previousMillis >= intrvl)
  {
    previousMillis += intrvl;

    // Ticker on OLED.
    resetOrAdd(cursorIndex, cursorIndex == 3);
  }

  // Granular speed BPM tick.
  if (curr - granularMillis >= (intrvl / 4))
  {
    granularMillis += intrvl / 4;

    // Ticker on OLED.
    resetOrAdd(granularIndex, granularIndex == ((4 * 4) - 1));
  }
}

///////////////////////////////////////////////
///               CONTROLS                  ///
///////////////////////////////////////////////

void handleEncoder()
{
  _encoder->tick();
  long newPos = _encoder->getPosition();
  if (newPos != _value)
  {
    // Not in edit mode bro
    if (!menuLetters[menuIndex].equals("Edit") && !menuLetters[menuIndex].equals("Granular"))
      tempCount = 50;

    // If lower, reset to high
    if (menuLetters[menuIndex].equals("Edit"))
    {
      if (newPos < _value)
      {
        resetOrSubstract(sequenceHitIndex, sequenceHitIndex < 0, 3);
      }

      // If higher, reset to low
      if (newPos > _value)
      {
        resetOrAdd(sequenceHitIndex, sequenceHitIndex > 3);
      }
    }

    _value = newPos;
  }
}

///////////////////////////////////////////////
///               HANDLERS                  ///
///////////////////////////////////////////////

void handleEvent(AceButton *button, uint8_t eventType, uint8_t buttonState)
{
  Serial.print(F("handleEvent(): "));
  Serial.print(F("virtualPin: "));
  Serial.print(button->getPin());
  Serial.print(F("; eventType: "));
  Serial.print(eventType);
  Serial.print(F("; buttonState: "));
  Serial.println(buttonState);

  switch (eventType)
  {
  case AceButton::kEventReleased:
    if (button->getPin() == 1)
    {
      timer = 12;
      resetOrAdd(menuIndex, menuIndex == sizeof(menuLetters) / sizeof(menuLetters[0]) - 1);
    }

    if (button->getPin() == 2)
    {
      if (menuLetters[menuIndex].equals("Play"))
      {
        resetOrAdd(tempoIndex, tempoIndex == 7);
      }

      if (menuLetters[menuIndex].equals("Edit") || menuLetters[menuIndex].equals("Granular"))
      {
        resetOrAdd(sequenceLineIndex, sequenceLineIndex == sizeof(sequence) / sizeof(sequence[0]) - 1);
      }
    }

    if (button->getPin() == 3)
    {
      if (menuLetters[menuIndex].equals("Edit") || menuLetters[menuIndex].equals("Granular"))
      {
        sequence[sequenceLineIndex][sequenceHitIndex * 4] == false
            ? sequence[sequenceLineIndex][sequenceHitIndex * 4] = true
            : sequence[sequenceLineIndex][sequenceHitIndex * 4] = false;
      }
    }

    break;

  case AceButton::kEventLongPressed:
    if (button->getPin() == 2)
    {
      if (menuLetters[menuIndex].equals("Edit") || menuLetters[menuIndex].equals("Granular"))
      {
        sequence[sequenceLineIndex][(sequenceHitIndex * 4) + 1] == false
            ? sequence[sequenceLineIndex][(sequenceHitIndex * 4) + 1] = true
            : sequence[sequenceLineIndex][(sequenceHitIndex * 4) + 1] = false;
      }
    }

    if (button->getPin() == 3)
    {
      if (menuLetters[menuIndex].equals("Edit") || menuLetters[menuIndex].equals("Granular"))
      {
        sequence[sequenceLineIndex][(sequenceHitIndex * 4) - 1] == false
            ? sequence[sequenceLineIndex][(sequenceHitIndex * 4) - 1] = true
            : sequence[sequenceLineIndex][(sequenceHitIndex * 4) - 1] = false;
      }
    }
    break;

    // case AceButton::kEventReleased:
    //   digitalWrite(LED_PIN, LOW);
    //   break;
  }
}
