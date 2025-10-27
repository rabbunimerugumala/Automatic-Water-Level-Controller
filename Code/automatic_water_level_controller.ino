// Include necessary libraries
#include <EEPROM.h>            // EEPROM: store small values persistently (e.g., saved thresholds)
#include <Wire.h>              // Wire: I2C communication library required by many LCD backpacks
#include <LiquidCrystal_I2C.h> // LiquidCrystal_I2C: control an I2C-connected 16x2 LCD

// Initialize LCD with I2C address 0x27 and dimensions 16x2
LiquidCrystal_I2C lcd(0x27, 16, 2); // lcd object used for all display operations

// Variables for sensor readings and display
long duration;  // raw pulse duration in microseconds (placeholder/global)
int cm;         // measured distance in centimeters from ultrasonic sensor
int set_val;    // value stored/read from EEPROM (e.g., calibration or saved level)
int percentage; // water level percentage computed from distance

// State flags
bool state = false;          // used to debounce/track manual button press state
bool pump = false;           // current pump state (true = ON)
bool lowWaterAlert = false;  // prevents repeated low-water buzzer sequences
bool highWaterAlert = false; // prevents repeated high-water buzzer sequences

// Pin definitions (change these if wiring differs)
#define TRIGGER_PIN 9  // ultrasonic sensor trigger (output)
#define ECHO_PIN 10    // ultrasonic sensor echo (input)
#define RELAY_PIN 3    // relay module controlling the water pump
#define BUZZER_PIN 4   // buzzer for audible alerts
#define MODE_SWITCH 6  // mode switch (AUTO/MANUAL) input with internal pullup
#define MOTOR_SWITCH 7 // manual motor ON/OFF switch input with internal pullup
#define GREEN_LED 5    // LED indicating normal/OK/filled
#define YELLOW_LED 8   // LED indicating warning/low water

// Tank level calibration constants (distances in cm)
#define EMPTY_LEVEL 15 // measured distance when tank is empty (adjust to your tank)
#define FULL_LEVEL 2   // measured distance when tank is full (adjust to your tank)

void setup()
{
    Serial.begin(9600);                             // start serial for debugging at 9600 baud
    lcd.init();                                     // initialize the LCD library
    lcd.backlight();                                // turn on the LCD backlight
    scrollText("Automatic Water Level Controller"); // show a startup message

    lcd.clear();                  // clear LCD before printing static labels
    lcd.print("WATER LEVEL:");    // print label on first line
    lcd.setCursor(0, 1);          // go to start of second line
    lcd.print("PUMP:OFF MANUAL"); // initial status text (pump off, manual)

    // Configure pins
    pinMode(TRIGGER_PIN, OUTPUT);        // trigger pin sends ultrasonic pulse
    pinMode(ECHO_PIN, INPUT);            // echo pin receives pulse reflection
    pinMode(MOTOR_SWITCH, INPUT_PULLUP); // use internal pull-up, switch to ground when pressed
    pinMode(MODE_SWITCH, INPUT_PULLUP);  // same for mode switch
    pinMode(BUZZER_PIN, OUTPUT);         // buzzer control
    pinMode(RELAY_PIN, OUTPUT);          // relay to control pump
    pinMode(GREEN_LED, OUTPUT);          // normal/OK indicator
    pinMode(YELLOW_LED, OUTPUT);         // warning/attention indicator

    // Read a previously stored value from EEPROM (address 0)
    set_val = EEPROM.read(0);
    if (set_val > EMPTY_LEVEL)
        set_val = EMPTY_LEVEL; // basic clamp in case EEPROM holds bad data
}

void loop()
{
    cm = getFilteredReading(); // read sensor and filter out outliers
    Serial.print("Measured Distance: ");
    Serial.print(cm); // print measured distance
    Serial.println(" cm");

    // Convert measured distance to a percentage for display (map and clamp)
    percentage = map(cm, EMPTY_LEVEL, FULL_LEVEL, 0, 100); // map distance range to 0-100
    percentage = constrain(percentage, 0, 100);            // ensure percent stays within bounds

    lcd.setCursor(12, 0);  // place cursor near right side on first row
    lcd.print(percentage); // display numeric percentage
    lcd.print("%   ");     // show '%' and clear any leftover chars

    // Automatic pump control when MODE_SWITCH is in AUTO position (pulled HIGH)
    if (percentage < 30 && digitalRead(MODE_SWITCH))
        pump = true; // start pump when low
    if (percentage >= 95)
        pump = false; // stop pump when nearly full

    digitalWrite(RELAY_PIN, !pump);    // drive relay (inverted logic used here)
    lcd.setCursor(5, 1);               // position for pump ON/OFF text
    lcd.print(pump ? "ON  " : "OFF "); // print current pump state

    lcd.setCursor(9, 1);                                         // position for mode label
    lcd.print(digitalRead(MODE_SWITCH) ? "AUTO   " : "MANUAL "); // display current mode

    // LED and buzzer alert logic
    if (percentage >= 90)
    {                                  // high-water warning/limit
        digitalWrite(GREEN_LED, HIGH); // show green LED
        digitalWrite(YELLOW_LED, LOW); // hide yellow LED
        if (!highWaterAlert)
        {                          // only trigger once on transition
            beepBuzzer(1);         // single beep for high water
            highWaterAlert = true; // set flag to avoid repeating
            lowWaterAlert = false; // clear opposite flag
        }
    }
    else if (percentage < 20)
    {                                   // low-water warning
        digitalWrite(GREEN_LED, LOW);   // turn off green LED
        digitalWrite(YELLOW_LED, HIGH); // turn on yellow LED
        if (!lowWaterAlert)
        {                           // only trigger once on transition
            beepBuzzer(2);          // two beeps for low water
            lowWaterAlert = true;   // set flag
            highWaterAlert = false; // clear opposite flag
        }
    }
    else
    {                                 // normal water level
        digitalWrite(GREEN_LED, LOW); // ensure LEDs are off
        digitalWrite(YELLOW_LED, LOW);
    }

    // Manual switch handling: when motor switch pressed in AUTO mode, save current level
    if (!digitalRead(MOTOR_SWITCH) && !state && digitalRead(MODE_SWITCH))
    {
        state = true;             // mark we've handled the press
        set_val = cm;             // store current measured distance
        EEPROM.write(0, set_val); // write to EEPROM address 0
    }

    // In MANUAL mode, a press toggles the pump
    if (!digitalRead(MOTOR_SWITCH) && !state && !digitalRead(MODE_SWITCH))
    {
        state = true; // mark handled
        pump = !pump; // toggle pump state
    }

    if (digitalRead(MOTOR_SWITCH))
        state = false; // reset state when switch released

    delay(500); // main loop delay to reduce sensor polling rate and bounce
}

// Function to get the median value from multiple sensor readings (reduces spikes)
long getFilteredReading()
{
    long readings[5]; // small sample set for median filter
    for (int i = 0; i < 5; i++)
    {
        readings[i] = getUltrasonicReading(); // take a raw ultrasonic reading
        delay(10);                            // short delay between samples
    }
    return medianFilter(readings, 5); // return median of samples
}

// Perform a single ultrasonic distance measurement and convert to cm
long getUltrasonicReading()
{
    digitalWrite(TRIGGER_PIN, LOW);  // ensure trigger starts LOW
    delayMicroseconds(2);            // short delay for stability
    digitalWrite(TRIGGER_PIN, HIGH); // send 10us pulse
    delayMicroseconds(10);
    digitalWrite(TRIGGER_PIN, LOW); // stop pulse

    long duration = pulseIn(ECHO_PIN, HIGH); // measure echo pulse duration in microseconds
    return microsecondsToCm(duration);       // convert to centimeters
}

// Convert pulse duration (microseconds) to distance in centimeters
long microsecondsToCm(long microseconds)
{
    return microseconds / 29 / 2; // speed of sound conversion: ~29 microsec per cm for round trip
}

// Simple median filter implementation (sort small array and return middle)
long medianFilter(long arr[], int size)
{
    for (int i = 0; i < size - 1; i++)
    {
        for (int j = 0; j < size - i - 1; j++)
        {
            if (arr[j] > arr[j + 1])
            { // basic bubble sort step to order elements
                long temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
    return arr[size / 2]; // return the middle element (median)
}

// Buzzer helper to beep specified number of times
void beepBuzzer(int times)
{
    for (int i = 0; i < times; i++)
    {
        digitalWrite(BUZZER_PIN, HIGH); // turn buzzer on
        delay(300);                     // beep duration
        digitalWrite(BUZZER_PIN, LOW);  // turn buzzer off
        delay(300);                     // gap between beeps
    }
}

// Scroll a message across the LCD by repeatedly shifting the substring
void scrollText(String message)
{
    for (int i = 0; i < message.length(); i++)
    {
        lcd.clear();                     // clear before writing next frame
        lcd.setCursor(0, 0);             // start at first column
        lcd.print(message.substring(i)); // print the remaining substring
        delay(250);                      // short pause between frames
    }
}
