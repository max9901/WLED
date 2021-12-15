#pragma once

#include "wled.h"
#include <deque>

#define USER_WLED_DEBOUNCE_THRESHOLD 50    //only consider button input of at least 50ms as valid (debouncing)
#define USER_WLED_LONG_PRESS 600           //long press if button is released after held for at least 600ms
#define USER_WLED_DOUBLE_PRESS 350         //double press if another press within 350ms after a short press
#define USER_WLED_LONG_REPEATED_ACTION 300 //how often a repeated action (e.g. dimming) is fired on long press on button IDs >0
#define USER_WLED_LONG_AP 6000             //how long the button needs to be held to activate WLED-AP
#define REDPIN 26
#define RESETTIME 360 * 1000  //(360 sec - 6 min)

IPAddress serverIP(192, 168, 2, 16);
const int kNetworkTimeout = 30 * 1000;
const int kNetworkDelay = 1000;

//class name. Use something descriptive and leave the ": public Usermod" part :)
class MaxKnop : public Usermod
{
private:
  //Private class members. You can declare variables and functions only accessible to your usermod here
  unsigned long lastTime = 0;
  unsigned long Red_button_timer = 0;
  bool checklight = false;
  
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
    Serial.println("Hello starLED KNOP reporting!");
    pinMode(REDPIN, OUTPUT);
  }

  void loop()
  {
    if (checklight && millis() - Red_button_timer > RESETTIME){
      Serial.println("timer reset");
      checklight = false;

      WS2812FX::Segment& seg =   strip.getSegment(1);
      seg.setOption(SEG_OPTION_ON, 1, 1);
      seg.setOption(SEG_OPTION_SELECTED, 1, 1);
      seg.setColor(0, 255, 1);
      colorUpdated(CALL_MODE_DIRECT_CHANGE);

    }

    // 20 fps
    if (millis() - lastTime > 15)
    {
      lastTime = millis();
      if (effect_on)
      {
        //toggle the button
        
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

        //end with the segments off.

        WS2812FX::Segment* segments = strip.getSegments();
        for (int i = 0; i < MAX_NUM_SEGMENTS; i++, segments++) {
          if (!segments->isActive()) {
            break;
          }
          col[0]          = 0;
          col[1]          = 0;
          col[2]          = 0;
          colSec[0]       = 0;
          colSec[1]       = 0;
          colSec[2]       = 0;
          col[3]          = 0;
          colSec[3]       = 0;
          segments->setOption(SEG_OPTION_SELECTED,true);
          colorUpdated(CALL_MODE_DMX_MULTI_SEG);
          //clear segment selection
          segments->setOption(SEG_OPTION_SELECTED,false);
        }
        effect_on = false;
      }
    }

  }

  void handleOverlayDraw()
  {
    //fix that we can change it in the ui with the white channel
    if((busses.getPixelColor(0))){
      digitalWrite(REDPIN, true);
    }else{
      digitalWrite(REDPIN, false);
    }

    if (effect_on){
        checklight = true;
        digitalWrite(REDPIN, false);
        
        //1 pixel offset because the first pixel is the red button
        for (int i = 0+1; i < 55+1; i++)
        {
          strip.setPixelColor(i     , ((strip1[i]<<16) + (strip1[i]<<8) + (strip1[i]) ));
          strip.setPixelColor(i + 55, ((strip1[i]<<16) + (strip1[i]<<8) + (strip1[i]) ));
        }
        colorUpdated(CALL_MODE_BUTTON);
      }
  }

  void ShortPress()
  {
    //pixel action
    {
      newpress = 5;
      effect_on = true;

      //reset the segements toch
      WS2812FX::Segment& seg =   strip.getSegment(1);
      seg.setOption(SEG_OPTION_ON, 1, 1);
      seg.setOption(SEG_OPTION_SELECTED, 1, 1);
      seg.setColor(0, 0, 1);
      colorUpdated(CALL_MODE_DIRECT_CHANGE);

      //turn off button.
      Red_button_timer = millis();
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