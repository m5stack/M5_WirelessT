#include "face_joy_stick.h"
joy_stick_t _joy_stick_initial_state;

void JoyStickInit()
{
    Wire.begin();
    for (int i = 0; i < 256; i++)
    {
        Wire.beginTransmission(FACE_JOY_ADDR);
        Wire.write(i % 4);
        Wire.write(random(256) * (256 - i) / 256);
        Wire.write(random(256) * (256 - i) / 256);
        Wire.write(random(256) * (256 - i) / 256);
        Wire.endTransmission();
        delay(2);
    }
    JoyStickLed(0, 0, 0, 0);
    JoyStickLed(1, 0, 0, 0);
    JoyStickLed(2, 0, 0, 0);
    JoyStickLed(3, 0, 0, 0);
    _joy_stick_initial_state = JoyStickUpdate();
}

void JoyStickLed(int indexOfLED, int r, int g, int b)
{
    Wire.beginTransmission(FACE_JOY_ADDR);
    Wire.write(indexOfLED);
    Wire.write(r);
    Wire.write(g);
    Wire.write(b);
    Wire.endTransmission();
}

joy_stick_t JoyStickUpdate(void)
{
    joy_stick_t data;
    Wire.requestFrom(FACE_JOY_ADDR, 5);
    if (Wire.available())
    {
        data.y = Wire.read();
        data.y |= Wire.read() << 8;
        data.x = Wire.read();
        data.x |= Wire.read() << 8;
        data.btn = Wire.read();
    }
    return data;
}


