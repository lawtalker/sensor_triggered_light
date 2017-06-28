/***************************************************************************

   ===========================
   =  Sensor triggered light =
   ===========================

   Copyright 2017 Michael Kwun

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

 ***************************************************************************/

/*
    This sketch is used to control a light using two sensors:

    (1) PIR (passive infrared) sensor (light on if motion detected); and

    (2) ultrasonic range finder (light on if body passes close).

    The controlled light is kept on until neither condition is met for a
    chosen interval (lightInterval).

    I use this to control a light in the middle of a long run of outside
    stairs that lead to my sidewalk. Using a standard motion-detecting
    light fixture doesn't work well, because it is hard to set it to turn on
    when someone starts walking up the stairs without also having it turn on
    when people pass by on the sidewalk.

    I place the ultrasonic range finder near the bottom step, perpendicular
    to the direction of travel. So placed, it is a precise way to trigger
    the light at a specific location, but doesn't trigger the light if you
    are walking down the stairs. For that, I use the PIR sensor. At the top
    of the stairs, a bit of imprecision about where to trigger the light does
    not cause any problems, because random passers-by do not walk around the
    top of the stairs.

    Note that nothing in this system keeps the light from turning on during
    the daytime. I instead use a separate switch that turns the entire
    system  off from sunrise to sunset - a switch that is not part of the
    microcontroller or this sketch. A prior version of this system used a
    photoresistor to detect daylight, but the external timer switch yields
    better results for me. If you decide to modify this sketch to incorporate
    a photoresistor, keep in mind that sunlight does not smoothly sweep from
    daylight to night, so perhaps use one threshold for sensing dawn (and
    thus preventing the light from turning on) and a higher threshold for
    sensing dusk (and thus allowing the light to turn on). This will keep the
    light from flickering if a body is sensed when it is near dusk or dawn.

    One final note: My setup does not use a dimmable light. But in case
    one might later want to add dimming capabilities, it makes sense to use
    a PWM-capable pin to control the light.

*/

#define DEBUG

// I/O pins
#define ledPin LED_BUILTIN  // for debugging (D13 on most boards)
#define pirPin 8            // PIR sensor
#define triggerPin 10       // ultrasonic ping trigger
#define echoPin 11          // ultrasonic echo
#define lightPin 9          // output pin to control power to light

// light timeout in seconds
#define lightInterval 90

// minimum distance (inches) to trigger light for ultrasonic proximity sensor
#define pingInches 24

void setup() {
  pinMode(ledPin, OUTPUT);
  pinMode(lightPin, OUTPUT);
  pinMode(triggerPin, OUTPUT);
}

void loop() {

  // This variable tracks our last sensor event, and it is initialized
  // with a value that will ensure that at startup the light is off.
  static unsigned long bodyTime = 0 - 1000UL * lightInterval;

  /*
      (1) Turn the light on or off

      The light is HIGH (on) if movement has been detected within the
      interval lightInterval, else light is LOW (off).

  */

  if ((millis() - bodyTime) < (1000UL * lightInterval)) {
    digitalWrite(lightPin, HIGH);
  }
  else {
    digitalWrite(lightPin, LOW);

    /*
       If the light is off, we set bodyTime to the recent past. Without
       this, if no movement was detected in about 50 straight days, the
       light would spuriously turn on.

    */
    bodyTime = millis() - 1000UL * lightInterval;
  }

#ifdef DEBUG
  byte sensor = LOW;
#endif

  /*
      (2) Check PIR (HC-SR501 or compatible) for movement

      The PIR sensor reads HIGH if movement is detected.

      The PIR sensor also has a "delay" setting that adjusts how long the
      sensor continues to read HIGH. Because the program logic keeps the
      light on for a chosen interval after the PIR no longer reads HIGH, I
      set the delay adjustment on the PIR sensor itself to the minimum. For
      my sensor, this is about 2 seconds.

      I also set the PIR sensor to single-trigger (aka L) mode, which means
      that after the sensor is held HIGH, it will return to LOW for about
      three seconds, during which time motion is not detected. After this
      period of time, the sensor will again read HIGH if motion is detected.
      The sensor's other mode (retriggering, aka H) will continuously detect
      motion, and keep the sensor HIGH during that time. For my purposes,
      either mode would actually be fine.

      Finally, the PIR sensor has a sensitivity (range) adjustment. My PIR
      sensor is not reliable if the range is set to the maximum. Instead, I
      set the range to the minimum (clockwise), which is a range of about 1
      meter. I then get reliable triggering within this range. For my
      purposes, a 1 meter range is fine.

  */

  // ignore PIR for first 60 seconds to let it adjust to ambient conditions
  static byte pirWarm;
  if ((! pirWarm) && (millis() > 60000)) {
    pirWarm = 1;
  }

  if (pirWarm && digitalRead(pirPin)) {
    bodyTime = millis();

#ifdef DEBUG
    sensor = HIGH;
#endif

  }

  /*
      (3) Check ultrasonic sensor (HC-SR04 or compatible) for body

      The ultrasonic range finder uses two pins. The trigger pin is raised
      HIGH for 10 microseconds to send a ping, at which point the sensor will
      send eight 40 kHz pulses. Once a ping has been sent, the echo pin
      is held HIGH until a reflection of the ping is detected, at which point
      echo will return to LOW. If no reflection is detected after 38 
      milliseconds, echo will return to low.
      
      Sound takes approximately 888 microseconds to travel 1 foot, and thus
      if the echo pin is raised high for 888 * 2 microseconds, that indicates
      the object is 1 foot away (i.e. the ping took 888 microseconds to
      reach the object, and then 888 microseconds to return as an echo).

      The sensor's maximum range is about 13 feet. At that point, one would
      expect echo to be held HIGH for about 23 milliseconds. So the 38
      millisecond timeout is well beyond that range.

      If there is no obstruction within the sensor's range, this entire cycle
      will take less than 40 milliseconds. In my application, there is a wall
      that is less than 4 feet from the sensor, which means that the cycle
      should take less than 8 milliseconds when there is no closer
      obstruction than the wall.

      So this is our approach:

      (0) Raise the trigger high for 10 microseconds to trigger a ping.
      (1) After we send a ping, wait for echo to be held HIGH.
      (2) Wait for echo to go LOW, and once it does if it was HIGH for a
          short enough period of time, conclude that there was some movement.

      Example code often uses pulseIn() to check the length of the echo, but
      pulseIn() is blocking - potentially for 76ms (38ms until the echo
      starts, if no obstruction is detected, and then 38ms for th echo
      itself). The code below is nonblocking, except for 10 microseconds to
      trigger the ping. It presumably is not as accurate as using interrupts
      but for my purposes (detecting when a body is walking by) I don't care
      about the precise distance, merely that there is something closer
      to the sensor than the wall.

      And the 10 microsecond blocking of loop() when a ping is triggered is
      on the order of one loop through loop() (i.e. when it is time to send
      a ping, the loop takes about twice as long as usual), which is not too
      bad.

  */

  static byte pingState;
  static unsigned long echoTime;
  static byte pongState;

  switch (pingState) {

    // send ping
    case 0:
      pingState = 1;
      digitalWrite(triggerPin, HIGH);
      delayMicroseconds(10);
      digitalWrite(triggerPin, LOW);
      break;

    // wait for echo
    case 1:
      if (digitalRead(echoPin)) {
        pingState = 2;
        echoTime = micros();
      }
      break;

    // time echo length and process
    case 2:
      if (! digitalRead(echoPin)) {
        pingState = 0;
        if ((micros() - echoTime) < (2L * 74 * pingInches)) {
          pongState = HIGH;
        }
        else {
          pongState = LOW;
        }
      }
      break;
  }

  if (pongState) {
    bodyTime = millis();

#ifdef DEBUG
    sensor = HIGH;
#endif

  }

#ifdef DEBUG
  digitalWrite(ledPin, sensor);
#endif

} /* end loop() */
