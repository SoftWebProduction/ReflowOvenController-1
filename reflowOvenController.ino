/*******************************************************************************
* Title: Reflow Oven Controller
* Version: 1.9
* Date: 29-01-2015
* Author: Mark Fink
* Website: www.rocketscream.com
* 
* Brief
* =====
* This is an example firmware for our Arduino compatible reflow oven controller. 
* The reflow curve used in this firmware is meant for lead-free profile 
* (it's even easier for leaded process!). You'll need to use the MAX31855 
* library for Arduino if you are having a shield of v1.68.
*
* Temperature (Degree Celcius)                   Magic Happens Here!
* 250-|                                                   
*     |                         Liquidus + 20°C   x|x x x x x x x|x
*     |                                          x |             |  x
*     |                                         x  |             |    x
* 200-|                                        x   |             |      x
*     |                                   |   x    |             |        x   
*     |                                   |  x     |             |          x
*     |                                   | x      |             |
* 150-|               x  x  x  x  x  x  x x        |             |
*     |             x |                   |        |             |
*     |           x   |                   |        |             | 
*     |         x     |                   |        |             | 
*     |       x       |                   |        |             | 
*     |     x         |                   |        |             |
*     |   x           |                   |        |             |
* 30 -| x             |                   |        |             |
*     |    <3°C/sec   |<    60 - 120 sec >|<3°C/sec|< 30-60 sec >|
*     |    Preheat    |        Soak       | Reflow |     Peak    | Cool
*  0  |_ _ _ _ _ _ _ _|_ _ _ _ _ _ _ _ _ _|_ _ _ _ |_ _ _ _ _ _ _| _ _ _ _ _ _ _ 
*                                                                Time (Seconds)
*
* Disclaimer
* ==========
* Dealing with high voltage is a very dangerous act! Please make sure you know
* what you are dealing with and have proper knowledge before hand. Your use of 
* any information or materials on this reflow oven controller is entirely at 
* your own risk, for which we shall not be liable. 
*
* Licences
* ========
* This reflow oven controller hardware and firmware are released under the 
* Creative Commons Share Alike v3.0 license
* http://creativecommons.org/licenses/by-sa/3.0/ 
* You are free to take this piece of code, use it and modify it. 
* All we ask is attribution including the supporting libraries used in this 
* firmware. 
*
* Required Libraries
* ==================
* - MAX31855 Library (for board v1.60 & above): 
*   >> https://github.com/rocketscream/MAX31855
*
* Revision  Description
* ========  ===========
* 1.9       removed PID controller plus a conservative rampup for v 1.80 reflow board
* *******************************************************************************/

// ***** INCLUDES *****
#include <LiquidCrystal.h>
// Newer board version starts from v1.60 using MAX31855KASA+ chip 
#include <MAX31855.h>

// ***** TYPE DEFINITIONS *****
typedef enum REFLOW_PHASE
{
  REFLOW_PHASE_IDLE,
  REFLOW_PHASE_PREHEAT,
  REFLOW_PHASE_SOAK,
  REFLOW_PHASE_REFLOW,
  REFLOW_PHASE_PEAK,
  REFLOW_PHASE_COOL,
  REFLOW_PHASE_COMPLETE,
  REFLOW_PHASE_TOO_HOT,
  REFLOW_PHASE_ERROR
} reflowPhase_t;

// ***** LCD MESSAGES *****
const char* lcdMessagesReflowPhase[] = {
  "Ready",
  "Preheat",
  "Soak",
  "Reflow",
  "Peak",
  "Cool",
  "Complete",
  "Wait,hot",
  "Error"
};

// ***** DEGREE SYMBOL FOR LCD *****
unsigned char degree[8]  = {140,146,146,140,128,128,128,128};

typedef enum OVEN_STATUS
{
  OVEN_OFF,
  OVEN_ON
} ovenStatus_t;

typedef enum BUTTON
{
  BUTTON_NONE,
  BUTTON_1, 
  BUTTON_2
} button_t;

typedef enum DEBOUNCE_STATE
{
  DEBOUNCE_STATE_IDLE,
  DEBOUNCE_STATE_CHECK,
  DEBOUNCE_STATE_RELEASE
} debounceState_t;

// ***** CONSTANTS *****
#define RAMPUP 3.0
#define ROOM_TEMPERATURE 50
#define SOAK_TEMPERATURE 150
#define SOAK_PERIOD 90000
#define PEAK_TEMPERATURE 237
#define PEAK_PERIOD 50000
#define COOL_TEMPERATURE 100

#define READ_INTERVAL_TIME 200
#define DISPLAY_INTERVAL_TIME 1000
#define DEBOUNCE_PERIOD_MIN 50

// ***** PIN ASSIGNMENT *****
int ssrPin = 5;
int thermocoupleSOPin = A3;
int thermocoupleCSPin = A2;
int thermocoupleCLKPin = A1;
int lcdRsPin = 7;
int lcdEPin = 8;
int lcdD4Pin = 9;
int lcdD5Pin = 10;
int lcdD6Pin = 11;
int lcdD7Pin = 12;
int ledRedPin = 4;
int buzzerPin = 6;
int buttonPin = A0;

//unsigned long nextCheck;
unsigned long nextRead;
unsigned long nextDisplay;

unsigned long phaseStartTime;
double temp;
double phaseStartTemp;

unsigned long buzzerPeriod;

reflowPhase_t reflowPhase;  // Reflow controller state machine variable
ovenStatus_t ovenStatus;    // Reflow oven status

debounceState_t debounceState; // BUTTON debounce state
long lastDebounceTime; // BUTTON debounce timer
button_t buttonStatus; // BUTTON press status
int timerSeconds;      // in seconds

// Specify LCD interface
LiquidCrystal lcd(lcdRsPin, lcdEPin, lcdD4Pin, lcdD5Pin, lcdD6Pin, lcdD7Pin);
// Specify thermocouple interface
MAX31855 thermocouple(thermocoupleSOPin, thermocoupleCSPin, 
                      thermocoupleCLKPin);


void setup()
{
  // SSR pin initialization to ensure reflow oven is off
  digitalWrite(ssrPin, LOW);
  pinMode(ssrPin, OUTPUT);

  // Buzzer pin initialization to ensure annoying buzzer is off
  digitalWrite(buzzerPin, LOW);
  pinMode(buzzerPin, OUTPUT);

  // LED pins initialization and turn on upon start-up (active low)
  digitalWrite(ledRedPin, LOW);
  pinMode(ledRedPin, OUTPUT);

  // Start-up splash
  digitalWrite(buzzerPin, HIGH);
  lcd.begin(8, 2);
  lcd.createChar(0, degree);
  lcd.clear();
  lcd.print("Reflow");
  lcd.setCursor(0, 1);
  lcd.print("Oven 1.9");
  digitalWrite(buzzerPin, LOW);
  delay(2500);
  lcd.clear();

  // Serial communication at 57600 bps
  Serial.begin(57600);

  // Turn off LED (active low)
  digitalWrite(ledRedPin, HIGH);
  nextRead = millis();
  nextDisplay = millis();
}


void loop()
{
  // necessary to read the thermocouple?
  if (millis() > nextRead)
  {
    // Read thermocouple next sampling period
    nextRead += READ_INTERVAL_TIME;
    // Read current temperature
    temp = thermocouple.readThermocouple(CELSIUS);
    
    // If thermocouple problem detected
    if((temp == FAULT_OPEN) || (temp == FAULT_SHORT_GND) || 
       (temp == FAULT_SHORT_VCC))
    {
      // Illegal operation
      reflowPhase = REFLOW_PHASE_ERROR;
      ovenStatus = OVEN_OFF;
    }
  }
  
  if (millis() > nextDisplay)
  {
    // Check temp in the next seconds
    nextDisplay += DISPLAY_INTERVAL_TIME;
    // If reflow process is on going
    if (ovenStatus == OVEN_ON)
    {
      Serial.println(temp);
    }

    // Clear LCD
    lcd.clear();
    // Print current system state
    lcd.print(lcdMessagesReflowPhase[reflowPhase]);
    // Move the cursor to the 2 line
    lcd.setCursor(0, 1);

    // If currently in error state
    if (reflowPhase == REFLOW_PHASE_ERROR)
    {
      // No thermocouple wire connected
      lcd.print("TC Error!");
    }
    else
    {
      // Print current temperature
      lcd.print(temp);

      #if ARDUINO >= 100
        // Print degree Celsius symbol
        lcd.write((uint8_t)0);
      #else
        // Print degree Celsius symbol
        lcd.print(0, BYTE);
      #endif
      lcd.print("C ");
    }
  }  // end nextDisplay

  // Reflow oven controller state machine
  switch (reflowPhase)
  {
  case REFLOW_PHASE_IDLE:
    ovenStatus = OVEN_OFF;
    // If oven temperature is above room temperature
    if (temp >= ROOM_TEMPERATURE)
    {
      reflowPhase = REFLOW_PHASE_TOO_HOT;
    }
    else
    {
      // If BUTTON is pressed to start reflow process
      if (buttonStatus == BUTTON_1)
      {
        Serial.println("temp");
        reflowPhase = REFLOW_PHASE_PREHEAT;
        phaseStartTime = millis();
        phaseStartTemp = temp;
      }
    }
    break;

  case REFLOW_PHASE_PREHEAT:
    ovenStatus = OVEN_ON;
    // If minimum soak temperature is achieved      
    if (temp >= SOAK_TEMPERATURE)
    {
      // Proceed to soaking state
      phaseStartTime = millis();
      phaseStartTemp = temp;
      reflowPhase = REFLOW_PHASE_SOAK; 
    }
    else
    {
      if ((temp - phaseStartTemp) < ((float)(millis() - phaseStartTime) * RAMPUP / 1000.0))
      {
        // temp is lower than max rampup
        digitalWrite(ssrPin, HIGH); 
      }
      else
      {
        digitalWrite(ssrPin, LOW); 
      }
    }
    break;

  case REFLOW_PHASE_SOAK:     
    ovenStatus = OVEN_ON;
    // If micro soak temperature is achieved       
    if (millis() > phaseStartTime + SOAK_PERIOD)
    {
      phaseStartTime = millis();
      phaseStartTemp = temp;
      reflowPhase = REFLOW_PHASE_REFLOW;
    }
    else
    {
      if (temp < SOAK_TEMPERATURE)
      {
        digitalWrite(ssrPin, HIGH); 
      }
      else
      {
        digitalWrite(ssrPin, LOW); 
      }
    }
    break;

  case REFLOW_PHASE_REFLOW:
    ovenStatus = OVEN_ON;
    // If minimum peak temperature is achieved      
    if (temp >= PEAK_TEMPERATURE)
    {
      // Proceed to peak phase
      phaseStartTime = millis();
      phaseStartTemp = temp;
      reflowPhase = REFLOW_PHASE_PEAK; 
    }
    else
    {
      if ((temp - phaseStartTemp) < ((float)(millis() - phaseStartTime) * RAMPUP / 1000.0))
      {
        // temp is lower than max rampup
        digitalWrite(ssrPin, HIGH); 
      }
      else
      {
        digitalWrite(ssrPin, LOW); 
      }
    }
    break;

  case REFLOW_PHASE_PEAK:     
    ovenStatus = OVEN_ON;
    if (millis() > phaseStartTime + PEAK_PERIOD)
    {
      digitalWrite(ssrPin, LOW); // switch off oven
      ovenStatus = OVEN_OFF;                
      phaseStartTime = millis();
      phaseStartTemp = temp;
      reflowPhase = REFLOW_PHASE_COOL;
    }
    else
    {
      if (temp < PEAK_TEMPERATURE)
      {
        digitalWrite(ssrPin, HIGH); 
      }
      else
      {
        digitalWrite(ssrPin, LOW); 
      }
    }
    break; 

  case REFLOW_PHASE_COOL:
    ovenStatus = OVEN_OFF;                
    // If minimum cool temperature is achieve       
    if (temp <= COOL_TEMPERATURE)
    {
      // Retrieve current time for buzzer usage
      buzzerPeriod = millis() + 1000;
      // Turn on buzzer to indicate completion
      digitalWrite(buzzerPin, HIGH);
      // Proceed to reflow Completion state
      reflowPhase = REFLOW_PHASE_COMPLETE; 
    }         
    break;    

  case REFLOW_PHASE_COMPLETE:
    ovenStatus = OVEN_OFF;                
    if (millis() > buzzerPeriod)
    {
      // Turn off buzzer and green LED
      digitalWrite(buzzerPin, LOW);
      // Reflow process ended
      reflowPhase = REFLOW_PHASE_IDLE; 
    }
    break;
  
  case REFLOW_PHASE_TOO_HOT:
    ovenStatus = OVEN_OFF;                
    // If oven temperature drops below room temperature
    if (temp < ROOM_TEMPERATURE)
    {
      // Ready to reflow
      reflowPhase = REFLOW_PHASE_IDLE;
    }
    break;
    
  case REFLOW_PHASE_ERROR:
    ovenStatus = OVEN_OFF;                
    // If thermocouple problem is still present
    if((temp == FAULT_OPEN) || (temp == FAULT_SHORT_GND) || 
       (temp == FAULT_SHORT_VCC))
    {
      // Wait until thermocouple wire is connected
      reflowPhase = REFLOW_PHASE_ERROR; 
    }
    else
    {
      // Clear to perform reflow process
      reflowPhase = REFLOW_PHASE_IDLE; 
    }
    break;    
  } // end switch

  // If BUTTON 1 is pressed
  if (buttonStatus == BUTTON_1)
  {
    // If currently reflow process is on going
    if (ovenStatus == OVEN_ON)
    {
      // Button press is for cancelling
      // Turn off reflow process
      ovenStatus = OVEN_OFF;
      digitalWrite(ssrPin, LOW);   
      // Reinitialize state machine
      reflowPhase = REFLOW_PHASE_IDLE;
    }
  } 

  // Simple BUTTON debounce state machine
  switch (debounceState)
  {
    case DEBOUNCE_STATE_IDLE:
      // No valid BUTTON press
      buttonStatus = BUTTON_NONE;
      // If BUTTON #1 is pressed
      if (analogRead(buttonPin) == 0)
      {
        // Intialize debounce counter
        lastDebounceTime = millis();
        // Proceed to check validity of button press
        debounceState = DEBOUNCE_STATE_CHECK;
      } 
      break;

    case DEBOUNCE_STATE_CHECK:
      if (analogRead(buttonPin) == 0)
      {
        // If minimum debounce period is completed
        if ((millis() - lastDebounceTime) > DEBOUNCE_PERIOD_MIN)
        {
          // Proceed to wait for button release
          debounceState = DEBOUNCE_STATE_RELEASE;
        }
      }
      // False trigger
      else
      {
        // Reinitialize button debounce state machine
        debounceState = DEBOUNCE_STATE_IDLE; 
      }
      break;

    case DEBOUNCE_STATE_RELEASE:
      if (analogRead(buttonPin) > 0)
      {
        // Valid BUTTON 1 press
        buttonStatus = BUTTON_1;
        // Reinitialize button debounce state machine
        debounceState = DEBOUNCE_STATE_IDLE; 
      }
      break;
  }

  // make sure ssr is low
  if (ovenStatus == OVEN_OFF)
  {
    digitalWrite(ssrPin, LOW);   
  }

}  // end loop

