// Alarm clock board
// 
// Author:  Nick Gammon
// Date:    9th June 2011
// Version: 1.1
 
//  Version 1.1: Changed period to frequency so you can specify alarm sound in Hz
 
/*
 
Permission is granted to use this code for any purpose.
 
The code is provided "as is" with no warranty as to whether you wake up or not, nor any other warranty.
 
---
 
Note: I have observed that digits look slightly brighter if they have less segments.
(eg. 1 compared to 8, or the decimal point on its own).
 
I think this is because the more segments, the more the current is "shared" between
them. You could conceivably compensate by counting the on segments and lighting them
for slightly longer if there are more of them, but I haven't done that.
 
*/
 
#include <Wire.h>
#include "RTClib.h"
 
// when alarm is to sound (24-hour clock)
 
const byte ALARM_HOUR = 6;
const byte ALARM_MINUTE = 15;
 
// cancel alarm after these many minutes
const long RING_FOR = 15; 
                
// what days to ring on (0 to 6)               
const boolean wantedDays [7] = { false, true, true, true, true, true, false };
//                                SUN    MON   TUE   WED   THU   FRI   SAT
 
// alarm frequency
#define FREQUENCY 600  // Hz
#define PERIOD 500000L / FREQUENCY  // (1 / frequency) * 1e6 / 2
 
// Make true, recompile and upload quickly to set the clock chip to the compile time.
// Then make false, recompile and upload again, or it will keep resetting the clock
// to the last compile time every time you turn it on.
const boolean ADJUST_TIME = false;
 
// the pins the anodes are connected to
 
/*
   
    --A--
  |       |
  F       B
  |       |
    --G--
  |       |
  E       C
  |       |
    --D--
            (DP)
 
*/
 
// make true if you are using common-anode - reverse sense of voltages
const byte COMMON_ANODE = false;
 
// bring digit low to activate cathode
#define DIG1 0
#define DIG2 1
#define DIG3 4
#define DIG4 6
 
// segments
#define SEGA A3
#define SEGB 12
#define SEGC A2
#define SEGD 7
#define SEGE A0
#define SEGF A1
#define SEGG 13
#define SEGDP 5  // decimal point
 
// buttons/switches
#define CANCEL_BUTTON 2  // press to cancel alarm
#define ALARM_ON 3  // want alarm to ring?
#define DST 11  // daylight savings time?
 
// LED to flash when alarm rings
#define ALARM_LED 8      // flash so you can find the button
 
// pins buzzer is connected to
#define BUZZER1 9
#define BUZZER2 10
 
// clock chip class
RTC_DS1307 RTC;
 
// patterns for the digits
const byte pat0 [7] = { 1, 1, 1, 1, 1, 1, 0 };  // 0
const byte pat1 [7] = { 0, 1, 1, 0, 0, 0, 0 };  // 1
const byte pat2 [7] = { 1, 1, 0, 1, 1, 0, 1 };  // 2
const byte pat3 [7] = { 1, 1, 1, 1, 0, 0, 1 };  // 3
const byte pat4 [7] = { 0, 1, 1, 0, 0, 1, 1 };  // 4
const byte pat5 [7] = { 1, 0, 1, 1, 0, 1, 1 };  // 5
const byte pat6 [7] = { 1, 0, 1, 1, 1, 1, 1 };  // 6
const byte pat7 [7] = { 1, 1, 1, 0, 0, 0, 0 };  // 7
const byte pat8 [7] = { 1, 1, 1, 1, 1, 1, 1 };  // 8
const byte pat9 [7] = { 1, 1, 1, 1, 0, 1, 1 };  // 9
const byte patspace [7] = {   0 };  // space
const byte pathyphen [7] = {  0, 0, 0, 0, 0, 0, 1 };  // -
const byte patA [7] = { 1, 1, 1, 0, 1, 1, 1 };  // A
const byte patF [7] = { 1, 0, 0, 0, 1, 1, 1 };  // F
const byte patL [7] = { 0, 0, 0, 1, 1, 1, 0 };  // L
 
// 0 to 9 are themselves in the patterns array
//  there are the other entries:
#define SHOW_SPACE 10
#define SHOW_HYPHEN 11
#define SHOW_A 12
#define SHOW_F 13
#define SHOW_L 14
 
// which pins which segments are on (including decimal place)
const byte pins [8] = { SEGA, SEGB, SEGC, SEGD, SEGE, SEGF, SEGG, SEGDP };
const byte * patterns [] = { pat0, pat1, pat2, pat3, pat4, pat5, pat6, pat7, pat8, pat9, 
                             patspace, pathyphen, patA, patF, patL };
 
// the pins each digit is connected to
const byte digits [4] = { DIG1, DIG2, DIG3, DIG4 };
 
// our variables
boolean tick, pm, rang, alarm, dim;
unsigned long ms;          // keep track of when we last drew the clock
unsigned long alarm_time;  // when the alarm sounded
volatile boolean button_down;  // true if cancel button pressed
 
// entered when "cancel" button is pressed
void button_isr ()
  {
  button_down = true;
  }  // end of button_isr
 
void setup ()
{
 
  // clock uses I2C
  Wire.begin();
  
  // activate clock
  RTC.begin();
 
  // set time in clock chip if not set before
  if (! RTC.isrunning() || ADJUST_TIME) 
    {
    // following line sets the RTC to the date & time this sketch was compiled
    RTC.adjust(DateTime(__DATE__, __TIME__));
    }
 
  // set up pins - one for each digit
  for (byte i = 0; i < 4; i++)
    pinMode (digits [i], OUTPUT);
 
  // and one for each segment
  for (byte i = 0; i < 8; i++)
    pinMode (pins [i], OUTPUT);
 
  pinMode (BUZZER1, OUTPUT);   // piezo buzzer pins
  pinMode (BUZZER2, OUTPUT);
  
  // for flashing the LED
  pinMode (ALARM_LED, OUTPUT);
  
  // input mode, but high gives us a pull-up for the switch
  digitalWrite (CANCEL_BUTTON, HIGH);  // pull-up
  digitalWrite (DST, HIGH);            // pull-up
  digitalWrite (ALARM_ON, HIGH);      // pull-up
 
  // interrupt routine to quickly detect button presses
  attachInterrupt (0, button_isr, FALLING);
  
  beep (10);  // beep to show we started up
 
  // show "-AL-"
  ms = millis ();
  while (millis () - ms < 2000)
    {
    digit (1, SHOW_HYPHEN);
    digit (2, SHOW_A);  
    digit (3, SHOW_L);
    digit (4, SHOW_HYPHEN);
    }
  
  // show alarm set time for 5 seconds (24-hour time)
  ms = millis ();
  pm = ALARM_HOUR >= 12;
  while (millis () - ms < 5000)
    show_time (ALARM_HOUR, ALARM_MINUTE);  
 
  // show "OFF" if alarm off
  if (digitalRead (ALARM_ON) == LOW)
    {
    ms = millis ();
    while (millis () - ms < 2000)
      {
      digit (1, 0);
      digit (2, SHOW_F);  
      digit (3, SHOW_F);
      digit (4, SHOW_SPACE);
      }
    }
    
  ms = millis ();
  beep (20);  // second beep
    
  // clear any spurious button-press
  button_down = false;
     
}  // end of setup
 
void digit (const byte segment, const byte which)
{
  byte adjust = 0;
  
  // common anode uses opposite polarity
  if (COMMON_ANODE)
    adjust = 0xFF;
    
  // get correct digit pin, zero-relative
  byte pin = digits [segment - 1];
 
  const byte * pat = patterns [which];
  
  for (byte i = 0; i < 7; i++)
    digitalWrite (pins [i], pat [i] ^ adjust);
 
  // decimal point flags
  switch (segment)
    {
    case 1: digitalWrite (SEGDP, digitalRead (ALARM_ON) ^ adjust); break;
    case 2: digitalWrite (SEGDP, tick ^ adjust);     break;
    case 3: digitalWrite (SEGDP, LOW ^ adjust);      break;
    case 4: digitalWrite (SEGDP, pm ^ adjust);       break;
    } // end of switch
    
 
  // bring appropriate digit low to activate it
 
  digitalWrite (pin, LOW ^ adjust);
 
  // make dimmer at night
  if (dim)
    delayMicroseconds (50);
  else
    delay (2);  // milliseconds
 
  // back to high 
 
  digitalWrite (pin, HIGH ^ adjust);
  for (byte i = 0; i < 7; i++)
    digitalWrite (pins [i], LOW ^ adjust);
  digitalWrite (SEGDP, LOW ^ adjust);
}  // end of digit
 
 
void show_time (const byte hour, const byte minute)
{
  digit (1, (hour / 10) ? (hour / 10) : SHOW_SPACE);  // leading space if zero
  digit (2, hour % 10);
  digit (3, minute / 10);
  digit (4, minute % 10);
}  // end of show_time
 
// Annoying beep for duration times
void beep (const int duration)
{
 
  for(int i = 0 ; i < duration ; i++)
  {
    digitalWrite (BUZZER1, HIGH);
    digitalWrite (BUZZER2, LOW);
    delayMicroseconds(PERIOD);
 
    digitalWrite (BUZZER1, LOW);
    digitalWrite (BUZZER2, HIGH);
    delayMicroseconds(PERIOD);
  }
 
  digitalWrite (BUZZER1, LOW);
  digitalWrite (BUZZER2, LOW);
 
}  // end of beep
 
void loop()
{
  // find the time  
  DateTime now = RTC.now();
  byte hour = now.hour ();
  byte minute = now.minute ();
  
  // daylight savings? one hour later
  if (digitalRead (DST))
    hour++;
  if (hour > 23)
    hour = 0;
  
  // make dimmer early in the morning (before 6 am) or late at night (after 9 pm)
  dim = hour < 6 || hour >= 21;
 
  // if not in alarm hour, reset alarm (ready for next day)
  if (hour != ALARM_HOUR)
    rang = false;
    
  // if RING_FOR minutes are up, or alarm switch turned off, cancel alarm
  if (alarm && ((millis () - alarm_time) > (RING_FOR * 60L * 1000L) || 
                 digitalRead (ALARM_ON) == LOW))
    {
    alarm = false; 
    digitalWrite (ALARM_LED, LOW);
    }
  
  // if we reach the alarm time, and we have't rang it yet, sound alarm
  // (only on Monday to Friday)
  if (hour == ALARM_HOUR &&       // correct hour
      minute == ALARM_MINUTE &&   // correct minute
      wantedDays [now.dayOfWeek ()] &&  // this day of week wanted
      digitalRead (ALARM_ON) &&   // they have alarm switched on
      !rang)                      // haven't already rung it today
      {
      alarm = true;
      alarm_time = millis ();
      }
  
  // check for button press  
  if (button_down)
    {
    // she cancelled it!
    if (alarm)
      {
      alarm = false;
      rang = true;
      digitalWrite (ALARM_LED, LOW);
      }
    else
      {
      // alarm wasn't on - just do alarm test
      if (digitalRead (ALARM_ON))
        {
        alarm = true;  // test alarm 
        alarm_time = millis ();
        }
      }
      
     // wait until let go of button
     while (digitalRead (CANCEL_BUTTON) == LOW)
      {}
      
     delay (100);  // debounce
     button_down = false;
    }  // end button pressed
    
  // work out AM/PM decimal point
  pm = hour >= 12;
 
  // make 12-hour time  
  if (hour > 12)
    hour -= 12;
 
  // don't dim if alarm ringing
  
  if (alarm)
    dim = false;
    
  show_time (hour, minute);
 
  unsigned long diff = millis () - ms;
 
  // "tick" the decimal place every 1/2 second
  if (diff > 500)
  {
    tick = !tick;
    ms = millis ();
    
    // beep away if alarm sounding
    if (alarm)
      {
      beep (100);
      digitalWrite (ALARM_LED, !digitalRead (ALARM_LED));
      }
      
  }
 
  delay (2);  // 2 ms delay for multiplexing
  
}  // end of loop

