#ifndef face_joy_stick_h
#define face_joy_stick_h
#include "M5Stack.h"
#define FACE_JOY_ADDR 0x5E

typedef struct
{
    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t btn = 0;
}joy_stick_t;

void JoyStickInit();
void JoyStickLed(int indexOfLED, int r, int g, int b);
joy_stick_t JoyStickUpdate(void);
extern joy_stick_t _joy_stick_initial_state;

#endif
