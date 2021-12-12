#pragma once

#include "wled.h"
#include <deque>

#define USER_WLED_DEBOUNCE_THRESHOLD 50    //only consider button input of at least 50ms as valid (debouncing)
#define USER_WLED_LONG_PRESS 600           //long press if button is released after held for at least 600ms
#define USER_WLED_DOUBLE_PRESS 350         //double press if another press within 350ms after a short press
#define USER_WLED_LONG_REPEATED_ACTION 300 //how often a repeated action (e.g. dimming) is fired on long press on button IDs >0
#define USER_WLED_LONG_AP 6000             //how long the button needs to be held to activate WLED-AP
#define REDPIN 19

IPAddress serverIP(192, 168, 2, 16);
const int kNetworkTimeout = 30 * 1000;
const int kNetworkDelay = 1000;

//class name. Use something descriptive and leave the ": public Usermod" part :)
class MaxKnop : public Usermod
{
private:
  //Private class members. You can declare variables and functions only accessible to your usermod here
  unsigned long lastTime = 0;
  bool effect_on = false;
  std::deque<byte> strip1 = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  int newpress = 0;

public:
  /*
     * setup() is called once at boot. WiFi is not yet connected at this point.
     * You can use it to initialize variables, sensors or similar.
     */

  void setup()
  {
    Serial.println("Hello from my usermod!");
    pinMode(REDPIN, OUTPUT);
  }

  void loop()
  {
    // 20 fps
    if (millis() - lastTime > 15)
    {
      lastTime = millis();
      if (effect_on)
      {
        //toggle the button
        digitalWrite(REDPIN, false);

        //pop 1 pixel
        strip1.pop_back();

        //add new pixel
        if (newpress)
        {
          strip1.push_front(100);
          newpress--;
          if (newpress < 0)
            newpress = 0;
        }
        else
        {
          strip1.push_front(0);
        }
        
        //show the effect 
        strip.show();

        // check if there is still light in the strip
        for (int i = 0; i < 55; i++)
        {
          if (strip1[i])
            return;
        }
        effect_on = false;
        
      }
      else
      {
        digitalWrite(REDPIN, true);
      }
    }

  }
  void handleOverlayDraw()
  {
    if (effect_on){
        for (int i = 0; i < 55; i++)
        {
          strip.setPixelColor(i     , ((strip1[i]<<16) + (strip1[i]<<8) + (strip1[i]) ));
          strip.setPixelColor(i + 55, ((strip1[i]<<16) + (strip1[i]<<8) + (strip1[i]) ));
        }
      }
  }

  void ShortPress()
  {
    //pixel action
    {
      newpress = 5;
      effect_on = true;
      // shortPressAction();
    }
    //networking
    {
      if (Network.isConnected())
      {

        WiFiClient client;
        if (client.connect(serverIP, 8000))
        {
          Serial.println("connected");
          client.println("GET /api/buttonpress HTTP/1.0");
          client.println();
        } else{
          newpress = 20;
          Serial.println("connection to server failed");
        }
      }
      else
      {
        Serial.print("no wifi connection");
      }
    }
  }
  void LongPress()
  {
    longPressAction();
  }
  void DoublePress(){
    doublePressAction();
  }

  bool handleButton(uint8_t b){

    //return false to let wled handle it when settings are wrong.
    if ((b || buttonType[b] == BTN_TYPE_ANALOG || buttonType[b] == BTN_TYPE_ANALOG_INVERTED || buttonType[b] == BTN_TYPE_SWITCH || buttonType[b] == BTN_TYPE_PIR_SENSOR))
    {
      return false;
    }

    //momentary button logic
    if (isButtonPressed(b))
    { //pressed

      if (!buttonPressedBefore[b])
        buttonPressedTime[b] = millis();
      buttonPressedBefore[b] = true;

      if (millis() - buttonPressedTime[b] > USER_WLED_LONG_PRESS)
      { //long press
        if (!buttonLongPressed[b])
        {
          LongPress();
        }
        else if (b)
        { //repeatable action (~3 times per s) on button > 0
          LongPress();
          buttonPressedTime[b] = millis() - USER_WLED_LONG_REPEATED_ACTION; //300ms
        }
        buttonLongPressed[b] = true;
      }
    }
    else if (!isButtonPressed(b) && buttonPressedBefore[b])
    { //released

      long dur = millis() - buttonPressedTime[b];
      if (dur < USER_WLED_DEBOUNCE_THRESHOLD)
      {
        buttonPressedBefore[b] = false;
        return true;
      }                                     //too short "press", debounce
      bool doublePress = buttonWaitTime[b]; //did we have a short press before?
      buttonWaitTime[b] = 0;

      if (b == 0 && dur > USER_WLED_LONG_AP)
      { //long press on button 0 (when released)
        WLED::instance().initAP(true);
      }
      else if (!buttonLongPressed[b])
      { //short press
        if (b == 0 && !macroDoublePress[b])
        { //don't wait for double press on button 0 if no double press macro set
          ShortPress();
        }
        else
        { //double press if less than 350 ms between current press and previous short press release (buttonWaitTime!=0)
          if (doublePress)
          {
            DoublePress();
          }
          else
          {
            buttonWaitTime[b] = millis();
          }
        }
      }
      buttonPressedBefore[b] = false;
      buttonLongPressed[b] = false;
    }

    //if 350ms elapsed since last short press release it is a short press
    if (buttonWaitTime[b] && millis() - buttonWaitTime[b] > USER_WLED_DOUBLE_PRESS && !buttonPressedBefore[b])
    {
      buttonWaitTime[b] = 0;
      ShortPress();
    }
    return true;
  }

  uint16_t getId()
  {
    return USERMOD_ID_MAX_KNOP;
  }

  //More methods can be added in the future, this example will then be extended.
  //Your usermod will remain compatible as it does not need to implement all methods from the Usermod base class!
};