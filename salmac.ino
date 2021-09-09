#include <Adafruit_SH110X.h>
#include <Adafruit_NeoPixel.h>
#include <RotaryEncoder.h>
#include <Wire.h>
#include <Adafruit_TinyUSB.h>
#include <array>
// Create the neopixel strip with the built in definitions NUM_NEOPIXEL and PIN_NEOPIXEL
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUM_NEOPIXEL, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// Create the OLED display
Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &SPI1, OLED_DC, OLED_RST, OLED_CS);

// Create the rotary encoder
RotaryEncoder encoder(PIN_ROTA, PIN_ROTB, RotaryEncoder::LatchMode::FOUR3);
void checkPosition() {  encoder.tick(); } // just call tick() to check the state.
// our encoder position state
int encoder_pos = 0;
uint8_t j = 0;
bool i2c_found[128] = {false};

// https://github.com/adafruit/Adafruit_TinyUSB_Arduino/blob/master/examples/HID/hid_composite/hid_composite.ino
Adafruit_USBD_HID usb_hid;
// HID report descriptor using TinyUSB's template
// Report ID
enum
{
    RID_KEYBOARD = 1,
    RID_MOUSE,
    RID_CONSUMER_CONTROL, // Media, volume etc ..
};
static uint8_t const desc_hid_report[] =
{
    TUD_HID_REPORT_DESC_KEYBOARD( HID_REPORT_ID(RID_KEYBOARD) ),
    TUD_HID_REPORT_DESC_MOUSE   ( HID_REPORT_ID(RID_MOUSE) ),
    TUD_HID_REPORT_DESC_CONSUMER( HID_REPORT_ID(RID_CONSUMER_CONTROL) )
};

static constexpr char KEY_TO_CHAR[128][2] = {
    HID_KEYCODE_TO_ASCII
};

enum class ColourMode
{
    OFF = 0,
    CYCLE,
    WHITE,
    RED,
    GREEN,
    BLUE,
} colour_mode = ColourMode::OFF;
int brightness = 80;

static constexpr int KEY_CNT = 12;
static bool prev_key_state[KEY_CNT] = {false};
static bool next_key_state[KEY_CNT] = {false};
static bool prev_encoder_button_state = false;
static int mode_index = 0;

static inline bool key_released(int i) { return prev_key_state[i] && !next_key_state[i]; }

using mode_loop_callback = void (*)();
static bool pressed_something = false;
static void generic_keyboard_loop([[maybe_unused]]const bool (&prev)[KEY_CNT],
                                  const bool (&next)[KEY_CNT],
                                  const uint8_t (&map)[KEY_CNT])
{
    uint8_t keycode[6] = {};
    int codeidx = 0;
    for (int i = 0; i < KEY_CNT && codeidx < 6; i++)
    {
        if (next[i])
            keycode[codeidx++] = map[i];
    }

    // if ( TinyUSBDevice.suspended() && count )
    // {
    //     // Wake up host if we are in suspend mode
    //     // and REMOTE_WAKEUP feature is enabled by host
    //     TinyUSBDevice.remoteWakeup();
    // }

    if ( !usb_hid.ready() ) return;

    if (codeidx > 0)
    {
        usb_hid.keyboardReport(RID_KEYBOARD, 0, keycode);
        pressed_something = true;
    }
    else if (pressed_something)
    {
        usb_hid.keyboardRelease(RID_KEYBOARD);
        pressed_something = false;
    }

    for (int i = 0; i < 12; i++)
    {
        display.setCursor((i % 3) * 48, 32 + (i / 3) * 8);
        if (next[i])
        { // switch pressed!
            pixels.setPixelColor(i, 0xFFFFFF);  // make white
            // move the text into a 3x4 grid
            display.print("[");
        }
        else
        {
            display.print(" ");
        }
        display.print(KEY_TO_CHAR[map[i]][0]);
        if (next[i])
        {
            display.print("]");
        }
    }
}

static struct {
    const char *name;
    mode_loop_callback cb;
} modes[] = {
    {
        "numpad",
        [](){
            static constexpr uint8_t KEY_MAP[12] = {
                HID_KEY_7, HID_KEY_8, HID_KEY_9,
                HID_KEY_4, HID_KEY_5, HID_KEY_6,
                HID_KEY_1, HID_KEY_2, HID_KEY_3,
                HID_KEY_EQUAL, HID_KEY_0, HID_KEY_EQUAL,
            };
            generic_keyboard_loop(prev_key_state, next_key_state, KEY_MAP);
        },
    },
    {
        "light",
        [](){
            // First row
            if (key_released(0)) colour_mode = ColourMode::OFF;
            else if (key_released(1)) colour_mode = ColourMode::CYCLE;
            else if (key_released(2)) colour_mode = ColourMode::WHITE;
            // Second row
            else if (key_released(3)) colour_mode = ColourMode::RED;
            else if (key_released(4)) colour_mode = ColourMode::GREEN;
            else if (key_released(5)) colour_mode = ColourMode::BLUE;
            // Fourth row
            else if (key_released(9))
            {
                brightness -= 10;
                if (brightness < 0) brightness = 0;
                pixels.setBrightness(brightness);
            }
            else if (key_released(10))
            {
                brightness = 100;
            }
            else if (key_released(11))
            {
                brightness += 10;
                if (brightness > 255) brightness = 255;
                pixels.setBrightness(brightness);
            }
        },
    },
};
static constexpr int MODE_COUNT = sizeof(modes) / sizeof(modes[0]);

void led_loop()
{
    if (colour_mode == ColourMode::OFF)
    {
        for (int i = 0; i < pixels.numPixels(); i++)
        {
            pixels.setPixelColor(i, 0);
        }
    }
    else if (colour_mode == ColourMode::CYCLE)
    {
        for (int i = 0; i < pixels.numPixels(); i++)
        {
            pixels.setPixelColor(i, Wheel(((i * 256 / pixels.numPixels()) + j) & 255));
        }
    }
    else if (colour_mode == ColourMode::WHITE)
    {
        for (int i = 0; i < pixels.numPixels(); i++)
        {
            pixels.setPixelColor(i, 0xFFFFFF);
        }
    }
    else if (colour_mode == ColourMode::RED)
    {
        for (int i = 0; i < pixels.numPixels(); i++)
        {
            pixels.setPixelColor(i, 0xFF0000);
        }
    }
    else if (colour_mode == ColourMode::GREEN)
    {
        for (int i = 0; i < pixels.numPixels(); i++)
        {
            pixels.setPixelColor(i, 0x00FF00);
        }
    }
    else if (colour_mode == ColourMode::BLUE)
    {
        for (int i = 0; i < pixels.numPixels(); i++)
        {
            pixels.setPixelColor(i, 0x0000FF);
        }
    }
}

void setup()
{
    Serial.begin(115200);
    //while (!Serial) { delay(10); }     // wait till serial port is opened
    delay(100);  // RP2040 delay is not a bad idea

    Serial.println("Adafruit Macropad with RP2040");

    // start pixels!
    pixels.begin();
    pixels.setBrightness(255);
    pixels.show(); // Initialize all pixels to 'off'

    // Start OLED
    display.begin(0, true); // we dont use the i2c address but we will reset!
    display.display();

    // set all mechanical keys to inputs
    for (uint8_t i = 0; i <= 12; i++)
    {
        pinMode(i, INPUT_PULLUP);
    }

    // set rotary encoder inputs and interrupts
    pinMode(PIN_ROTA, INPUT_PULLUP);
    pinMode(PIN_ROTB, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_ROTA), checkPosition, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ROTB), checkPosition, CHANGE);  

    // We will use I2C for scanning the Stemma QT port
    Wire.begin();

    // text display tests
    display.setTextSize(1);
    display.setTextWrap(false);
    display.setTextColor(SH110X_WHITE, SH110X_BLACK); // white text, black background

    pinMode(PIN_SPEAKER, OUTPUT);
    digitalWrite(PIN_SPEAKER, LOW);
    // tone(PIN_SPEAKER, 988, 100);  // tone1 - B5
    // delay(100);
    // tone(PIN_SPEAKER, 1319, 200); // tone2 - E6
    // delay(200);

    // Setup HID
    usb_hid.setPollInterval(2);
    usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
    usb_hid.setStringDescriptor("TinyUSB HID Composite");
    usb_hid.begin();
}


bool last_was_vol = false;

void loop() {
    display.clearDisplay();

    encoder.tick();          // check the encoder
    int newPos = encoder.getPosition();
    auto direction = encoder.getDirection();
    if (encoder_pos != newPos)
    {
        Serial.print("Encoder:");
        Serial.print(newPos);
        Serial.print(" Direction:");
        Serial.println((int)direction);
        encoder_pos = newPos;

        if (direction == RotaryEncoder::Direction::CLOCKWISE)
        {
            usb_hid.sendReport16(RID_CONSUMER_CONTROL, HID_USAGE_CONSUMER_VOLUME_DECREMENT);
        }
        else
        {
            usb_hid.sendReport16(RID_CONSUMER_CONTROL, HID_USAGE_CONSUMER_VOLUME_INCREMENT);
        }
        last_was_vol = true;
    }
    else if (last_was_vol)
    {
        last_was_vol = false;
        usb_hid.sendReport16(RID_CONSUMER_CONTROL, 0);
    }

    // Scanning takes a while so we don't do it all the time
    // if ((j & 0x3F) == 0) {
    //     Serial.println("Scanning I2C: ");
    //     Serial.print("Found I2C address 0x");
    //     for (uint8_t address = 0; address <= 0x7F; address++) {
    //         Wire.beginTransmission(address);
    //         i2c_found[address] = (Wire.endTransmission () == 0);
    //         if (i2c_found[address]) {
    //             Serial.print("0x");
    //             Serial.print(address, HEX);
    //             Serial.print(", ");
    //         }
    //     }
    //     Serial.println();
    // }

    // display.setCursor(0, 16);
    // display.print("I2C Scan: ");
    // for (uint8_t address=0; address <= 0x7F; address++) {
    //     if (!i2c_found[address]) continue;
    //     display.print("0x");
    //     display.print(address, HEX);
    //     display.print(" ");
    // }

    display.setCursor(0, 0);
    display.print("Mode: ");
    display.print(mode_index + 1);
    display.print("/");
    display.print(MODE_COUNT);
    display.print(" Bright: ");
    display.print(brightness);
    display.print("\n");
    display.print(modes[mode_index].name);

    // check encoder press
    display.setCursor(0, 16);
    if (!digitalRead(PIN_SWITCH)) {
        Serial.println("Encoder button");
        display.print("Encoder pressed ");
        pixels.setBrightness(255);     // bright!
        if (!prev_encoder_button_state)
        {
            Serial.println("Switching mode");
            mode_index = (mode_index + 1) % MODE_COUNT;
            if (pressed_something)
            {
                usb_hid.keyboardRelease(RID_KEYBOARD);
                pressed_something = false;
            }
        }
        prev_encoder_button_state = true;
    } else {
        pixels.setBrightness(brightness);
        prev_encoder_button_state = false;
    }

    led_loop();

    for (int i=1; i<=12; i++)
        next_key_state[i - 1] = !digitalRead(i);

    auto &mode = modes[mode_index];
    if (mode.cb)
        mode.cb();

    memcpy(prev_key_state, next_key_state, sizeof(next_key_state));

    // show neopixels, incredment swirl
    pixels.show();
    j++;

    // display oled
    display.display();
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
    if(WheelPos < 85) {
        return pixels.Color(255 - WheelPos * 3, 0, WheelPos * 3);
    } else if(WheelPos < 170) {
        WheelPos -= 85;
        return pixels.Color(0, WheelPos * 3, 255 - WheelPos * 3);
    } else {
        WheelPos -= 170;
        return pixels.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
    }
}
