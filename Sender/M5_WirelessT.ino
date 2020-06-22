#include <M5StickC.h>
#include <Wire.h>
#include <SPI.h>
#include "Lepton.h"
#include "img_table.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "img/ColorT.h"
#include "ap_master.h"
#include "driver/mcpwm.h"
#include "soc/mcpwm_reg.h"
#include "soc/mcpwm_struct.h"

#define ENCODER_ADDR    0x30

#define SCREEN_X 240
#define SCREEN_Y 135
#define FLIR_X 160
#define FLIR_Y 120
#define MAX_FLIR_RAW_BUFFER (FLIR_X * FLIR_Y - 1)

#define FLIR_WINDOW_X1 5
#define FLIR_WINDOW_Y1 4
#define FLIR_WINDOW_X2 (FLIR_WINDOW_X1 + FLIR_X)
#define FLIR_WINDOW_Y2 (FLIR_WINDOW_Y1 + FLIR_Y)
#define FLIR_WINDOW_CENTER_X (FLIR_WINDOW_X1 + FLIR_X / 2)
#define FLIR_WINDOW_CENTER_Y (FLIR_WINDOW_Y1 + FLIR_Y / 2)

#define HIST_HEIGHT 70
#define HIST_WINDOWS_X1 171
#define HIST_WINDOWS_Y1 34
#define HIST_WINDOWS_X2 (HIST_WINDOWS_X1 + 64)
#define HIST_WINDOWS_Y2 (HIST_WINDOWS_Y1 + HIST_HEIGHT)

#define MES_MODE_X 171
#define MES_MODE_Y 8

#define DISP_MODE_X 171
#define DISP_MODE_Y 25

#define SERVO_DEFAULT_DUTY  1040
#define SERVO_MAX_DUTY      1600
#define SERVO_MIN_DUTY      500

//SDA SCL CS VSYNC
Lepton lepton(21, 22, 0, 38);
extern uint16_t fpa_temp, aux_temp;
extern const uint16_t camColors[];
extern const uint16_t GrayLevel[];
extern uint16_t smallBuffer[FLIR_X * FLIR_Y];
extern uint16_t raw_max, raw_min;
extern uint16_t max_x, max_y, min_x, min_y;
TFT_eSprite img_buffer = TFT_eSprite(&M5.Lcd);
uint16_t servo_value = SERVO_DEFAULT_DUTY;

enum modes
{
    MES_AUTO_MAX = 0,
    MES_AUTO_MIN,
    MES_CENTER,
    DISP_MODE_CAM = 0,
    DISP_MODE_GRAY,
    DISP_MODE_GOLDEN,
    DISP_MODE_RAINBOW,
    DISP_MODE_IRONBLACK,
};

typedef struct
{
    float data;
    int16_t increment;
    uint8_t sw;
}encoder_t;

encoder_t encoder = {0};
uint16_t hist_buffer[64] = {0};
uint8_t mes_mode = MES_AUTO_MAX;
uint8_t disp_mode = DISP_MODE_CAM;

uint16_t Read_Buffer(uint16_t x, uint16_t y)
{
    return smallBuffer[y * 160 + x];
}

uint8_t I2CSetReadReg(uint8_t reg_addr)
{
    Wire1.beginTransmission(ENCODER_ADDR);
    Wire1.write(reg_addr);
    uint8_t err = Wire1.endTransmission();
    return err;
}

uint8_t I2CWriteReg(uint8_t reg_addr, uint8_t value)
{
    Wire1.beginTransmission(ENCODER_ADDR);
    Wire1.write(reg_addr);
    Wire1.write(value);
    uint8_t err = Wire1.endTransmission();
    return err;
}

//500 ~ 2500
void ServoWrite(uint16_t val)
{
    mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, val);
}

void ServoInit(uint16_t default_val)
{
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, 9);
    mcpwm_config_t pwm_config;
    pwm_config.frequency = 50;    //frequency = 50Hz, i.e. for every servo motor time period should be 20ms
    pwm_config.cmpr_a = 0;    //duty cycle of PWMxA = 0
    pwm_config.cmpr_b = 0;    //duty cycle of PWMxb = 0
    pwm_config.counter_mode = MCPWM_UP_COUNTER;
    pwm_config.duty_mode = MCPWM_DUTY_MODE_0;
    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);    //Configure PWM0A & PWM0B with above settings
    ServoWrite(default_val);
}

void UpdateEncoder(encoder_t *encoder)
{
    I2CSetReadReg(0x10);
    Wire1.requestFrom(ENCODER_ADDR, 1);
    encoder->sw = Wire1.read();

    if(encoder->sw != 0)
    {
        encoder->data = 0;
        I2CWriteReg(0x20, 0xFF);
    }
    else
    {
        I2CSetReadReg(0x00);
        Wire1.requestFrom(ENCODER_ADDR, 2);
        encoder->increment = Wire1.read() << 8;
        encoder->increment |= Wire1.read();

        if (encoder->increment != 0)
        {
            //Serial.printf("encoder->increment = %d\n", encoder->increment);
            encoder->data += ((encoder->increment) * 0.05f);
            I2CWriteReg(0x20, 0xFF);
        }
    }
}

void IRAM_ATTR Display_Cursor(uint16_t x, uint16_t y)
{
    img_buffer.drawCircle(x, y, 6, TFT_WHITE);
    img_buffer.drawLine(x, y - 10, x, y + 10, TFT_WHITE);
    img_buffer.drawLine(x - 10, y, x + 10, y, TFT_WHITE);
}


QueueHandle_t xQueue_RemoteImgTransfer = xQueueCreate(1, sizeof(int32_t));

void RemoteImgTransfer(void *pvParameters)
{
    uint8_t* pst;
    uint8_t part = 0;
    uint8_t send_frame_counter = 0;
    while(1)
    {
        if(WiFi.softAPgetStationNum() != 0)
        {
            if(xQueueReceive(xQueue_RemoteImgTransfer, &pst, 0))
            {
                //unsigned long T = micros();
                for(part = 0; part < 14; part++)
                {
                    udp_1.beginPacket(IPAddress(192,168,4,101), 2000);
                    udp_1.write(send_frame_counter);
                    udp_1.write(part);
                    udp_1.write(pst + (part * 1280), 1280);
                    udp_1.endPacket();
                    delay(5);
                }

                udp_1.beginPacket(IPAddress(192,168,4,101), 2000);
                udp_1.write(send_frame_counter);
                udp_1.write(14);
                udp_1.write(pst + (14 * 1280), 1280 + 19);
                udp_1.endPacket();
                delay(5);
                free(pst);
                //Serial.printf("[%d] send_time = %ld us\n", send_frame_counter, micros() - T);
                send_frame_counter++;
            }
        }
    }
}

void DisplayImage(float raw_diff, uint16_t raw_cursor, const uint16_t *palette, bool dir_flag, uint8_t* data)
{
    uint8_t* remote_img_buffer = (uint8_t *) malloc((19200 + 19) * sizeof(uint8_t));
    if(remote_img_buffer == NULL)
    {
        return;
    }
    uint16_t x, y, i = 0;
    uint16_t index = 0;

    memcpy(remote_img_buffer + 19200, data, 19);

    if (dir_flag)
    {
        for (y = FLIR_WINDOW_Y1; y < FLIR_WINDOW_Y2; y++)
        {
            for (x = FLIR_WINDOW_X1; x < FLIR_WINDOW_X2; x++)
            {
                if(smallBuffer[i] < raw_cursor)
                {
                    index = 0;
                }
                else
                {
                    index = (smallBuffer[i] - raw_cursor) * raw_diff;
                }
                if(index > 255) index = 255;
                remote_img_buffer[i] = index;
                hist_buffer[(index >> 2)]++;
                img_buffer.drawPixel(x, y, *(palette + index));
                i++;
            }
        }
    }
    else
    {
        for (y = FLIR_WINDOW_Y1; y < FLIR_WINDOW_Y2; y++)
        {
            for (x = FLIR_WINDOW_X1; x < FLIR_WINDOW_X2; x++)
            {
                if(smallBuffer[i] > raw_cursor)
                {
                    index = 255;
                }
                else
                {
                    index = (smallBuffer[i] - raw_min) * raw_diff;
                }
                if(index > 255) index = 255;
                remote_img_buffer[i] = index;
                hist_buffer[(index >> 2)]++;
                img_buffer.drawPixel(x, y, *(palette + index));
                i++;
            }
        }
    }
    if(xQueueSend(xQueue_RemoteImgTransfer, &remote_img_buffer, 0) == 0)
    {
        free(remote_img_buffer);
    }
}

void DrawBattery(uint16_t x, uint16_t y, float vol)
{
    const uint8_t w = 18;
    const uint8_t h = 7;
    img_buffer.drawLine(x + 1, y, x + w, y, TFT_WHITE); // -
    img_buffer.drawLine(x, y + 1, x, y + h, TFT_WHITE);  // |
    img_buffer.drawLine(x + 1, y + h + 1, x + w, y + h + 1, TFT_WHITE); //_
    img_buffer.drawLine(x + w + 1, y + 1, x + w + 1, y + h, TFT_WHITE); //   |
    img_buffer.drawLine(x + w + 3, y + 4, x + w + 3, y + h - 3, TFT_WHITE);
    img_buffer.drawPixel(x + w + 2, y + 3, TFT_WHITE);
    img_buffer.drawPixel(x + w + 2, y + h - 2, TFT_WHITE);
    
    float rate = (vol - 3.4) / (4.1 - 3.4);
    if(rate > 1.0)
    {
        img_buffer.fillRect(x + 2, y + 2, w - 2, h - 2, TFT_GREEN);
    }
    else if(rate <= 0.05)
    {
        img_buffer.drawLine(x + 2, y + 2, x + 2, y + h - 1, TFT_GREEN);
    }
    else
    {
        img_buffer.fillRect(x + 2, y + 2, uint16_t(rate * (w - 2)), h - 2, TFT_GREEN);
    }
}

void IRAM_ATTR Update_Flir()
{
    UpdateEncoder(&encoder);
    uint16_t i, raw_cursor = raw_max;
    int32_t x, y;
    uint8_t index;
    float raw_diff = 0;
    img_buffer.fillRect(0, 0, SCREEN_X, SCREEN_Y, TFT_BLACK);

    //convert temp
    float fpa_temp_f = fpa_temp / 100.0f - 273.15;
    //float max_temp = 0.0465f * raw_max - 349.44;
    //float min_temp = 0.0465f * raw_min - 349.44;
    //float center_temp = 0.0465f * smallBuffer[MAX_FLIR_RAW_BUFFER/2] - 349.44;
    float max_temp = 0.0217f * raw_max + fpa_temp_f - 177.77f;
    float min_temp = 0.0217f * raw_min + fpa_temp_f - 177.77f; //177.77
    float center_temp = 0.0217f * smallBuffer[9519] + fpa_temp_f - 177.77f;
    //Serial.printf("FPA = %.2f\n", fpa_temp_f);

    uint32_t udplen = udp_1.parsePacket();
	if(udplen)
	{
		uint8_t data[udplen];
		udp_1.read(data, udplen);
        //Serial.printf("%02X, %02X, %02X\n", data[0], data[1], data[2]);
        if(data[2] == 0)
        {
            servo_value = SERVO_DEFAULT_DUTY;
            encoder.data = 0;
        }
        else
        {
            servo_value += ((int8_t)data[1]);
            encoder.data += ((int8_t)data[0]) * 0.001f;
            if(servo_value > SERVO_MAX_DUTY)
            {
                servo_value = SERVO_MAX_DUTY;
            }
            else if(servo_value < SERVO_MIN_DUTY)
            {
                servo_value = SERVO_MIN_DUTY;
            }
        }
        ServoWrite(servo_value);
    }

    float cursor_temp = max_temp;
    bool dir_flag = encoder.data >= 0;
    if (dir_flag)
    {
        if(encoder.data > 0.95)
        {
            encoder.data = 0.95;
        }

        cursor_temp = min_temp + (max_temp - min_temp) * encoder.data;
        raw_cursor = (cursor_temp + 177.77 - fpa_temp_f) / 0.0217f;
        raw_diff = 256.0f / (raw_max - raw_cursor);
    }
    else
    {
        if(encoder.data < -0.95)
        {
            encoder.data = -0.95;
        }

        cursor_temp = max_temp - ((max_temp - min_temp) * (-encoder.data));
        raw_cursor = (cursor_temp + 177.77 - fpa_temp_f) / 0.0217f;
        raw_diff = 256.0f / (raw_cursor - raw_min);
    }
    
    uint16_t i_max_temp = max_temp * 100;
    uint16_t i_min_temp = min_temp * 100;
    uint16_t i_center_temp = center_temp * 100;
    uint8_t appendix_data[19] = {
                disp_mode,
                mes_mode,
                0x00,
                0x00,
                0x00,
                max_x >> 8,
                max_x,
                max_y >> 8,
                max_y,
                min_x >> 8,
                min_x,
                min_y >> 8,
                min_y,
                i_max_temp >> 8,
                i_max_temp,
                i_min_temp >> 8,
                i_min_temp,
                i_center_temp >> 8,
                i_center_temp
            };

    max_x += FLIR_WINDOW_X1;
    max_y += FLIR_WINDOW_Y1;
    min_x += FLIR_WINDOW_X1;
    min_y += FLIR_WINDOW_Y1;

    //display mode switch
    i = 0;
    switch (disp_mode)
    {
    case DISP_MODE_CAM:
        DisplayImage(raw_diff, raw_cursor, colormap_cam, dir_flag, appendix_data);
        break;

    case DISP_MODE_GRAY:
        DisplayImage(raw_diff, raw_cursor, colormap_grayscale, dir_flag, appendix_data);
        break;

    case DISP_MODE_GOLDEN:
        DisplayImage(raw_diff, raw_cursor, colormap_golden, dir_flag, appendix_data);
        break;

    case DISP_MODE_RAINBOW:
        DisplayImage(raw_diff, raw_cursor, colormap_rainbow, dir_flag, appendix_data);
        break;

    case DISP_MODE_IRONBLACK:
        DisplayImage(raw_diff, raw_cursor, colormap_ironblack, dir_flag, appendix_data);
        break;
    }

    //measure mode switch
    switch (mes_mode)
    {
    case MES_AUTO_MAX:
        Display_Cursor(max_x, max_y);
        x = max_x + 5;
        y = max_y + 5;
        if (max_x > FLIR_WINDOW_X2 - 35)
            x = max_x - 35;
        if (max_y > FLIR_WINDOW_Y2 - 15)
            y = max_y - 15;
        img_buffer.setCursor(x, y);
        img_buffer.printf("%.2f", max_temp);
        break;

    case MES_AUTO_MIN:
        Display_Cursor(min_x, min_y);
        x = min_x + 5;
        y = min_y + 5;
        if (min_x > FLIR_WINDOW_X2 - 35)
            x = min_x - 35;
        if (min_y > FLIR_WINDOW_Y2 - 15)
            y = min_y - 15;
        img_buffer.setCursor(x, y);
        img_buffer.printf("%.2f", min_temp);
        break;

    case MES_CENTER:
        Display_Cursor(FLIR_WINDOW_CENTER_X, FLIR_WINDOW_CENTER_Y);
        img_buffer.setCursor(FLIR_WINDOW_CENTER_X + 5, FLIR_WINDOW_CENTER_Y + 5);
        img_buffer.printf("%.2f", center_temp);
        break;
    }

    //Histogram
    uint16_t max_hist = 0;
    for (i = 0; i < 64; i++)
    {
        if (hist_buffer[i] > max_hist)
        {
            max_hist = hist_buffer[i];
        }
    }

    uint16_t hist_div = max_hist / HIST_HEIGHT;

    i = 0;
    switch (disp_mode)
    {
    case DISP_MODE_CAM:
        for (x = HIST_WINDOWS_X1; x < HIST_WINDOWS_X2; x++)
        {
            img_buffer.drawLine(x, HIST_WINDOWS_Y2, x, HIST_WINDOWS_Y2 - hist_buffer[i] / hist_div, colormap_cam[i * 4]);
            hist_buffer[i] = 0;
            i++;
        }
        break;

    case DISP_MODE_GRAY:
        for (x = HIST_WINDOWS_X1; x < HIST_WINDOWS_X2; x++)
        {
            img_buffer.drawLine(x, HIST_WINDOWS_Y2, x, HIST_WINDOWS_Y2 - hist_buffer[i] / hist_div, colormap_grayscale[i * 4]);
            hist_buffer[i] = 0;
            i++;
        }
        break;

    case DISP_MODE_GOLDEN:
        for (x = HIST_WINDOWS_X1; x < HIST_WINDOWS_X2; x++)
        {
            img_buffer.drawLine(x, HIST_WINDOWS_Y2, x, HIST_WINDOWS_Y2 - hist_buffer[i] / hist_div, colormap_golden[i * 4]);
            hist_buffer[i] = 0;
            i++;
        }
        break;

    case DISP_MODE_RAINBOW:
        for (x = HIST_WINDOWS_X1; x < HIST_WINDOWS_X2; x++)
        {
            img_buffer.drawLine(x, HIST_WINDOWS_Y2, x, HIST_WINDOWS_Y2 - hist_buffer[i] / hist_div, colormap_rainbow[i * 4]);
            hist_buffer[i] = 0;
            i++;
        }
        break;

    case DISP_MODE_IRONBLACK:
        for (x = HIST_WINDOWS_X1; x < HIST_WINDOWS_X2; x++)
        {
            img_buffer.drawLine(x, HIST_WINDOWS_Y2, x, HIST_WINDOWS_Y2 - hist_buffer[i] / hist_div, colormap_ironblack[i * 4]);
            hist_buffer[i] = 0;
            i++;
        }
        break;
    }

    double bar_percentage = (double)(raw_cursor - raw_min) / (double)(raw_max - raw_min);
    uint8_t bar_len = bar_percentage * 64;
    img_buffer.drawRect(HIST_WINDOWS_X1, HIST_WINDOWS_Y2 + 5, 64, 4, TFT_WHITE);
    if(dir_flag)
    {
        img_buffer.fillRect(HIST_WINDOWS_X1 + bar_len, HIST_WINDOWS_Y2 + 5, 64 - bar_len, 4, TFT_WHITE);
    }
    else
    {
        img_buffer.fillRect(HIST_WINDOWS_X1, HIST_WINDOWS_Y2 + 5, bar_len, 4, TFT_WHITE);
    }
    
    img_buffer.setCursor(HIST_WINDOWS_X1, HIST_WINDOWS_Y2 + 12);
    img_buffer.printf("%.0f", min_temp);
    img_buffer.setCursor(HIST_WINDOWS_X2 - 12, HIST_WINDOWS_Y2 + 12);
    img_buffer.printf("%.0f", max_temp);

    uint8_t axp_button = M5.Axp.GetBtnPress();
    if(axp_button == 0x01)
    {
        M5.Axp.Write1Byte(0x32, 0x80);
    }
    else if(axp_button == 0x02)
    {
        mes_mode++;
        if (mes_mode > MES_CENTER)
        {
            mes_mode = MES_AUTO_MAX;
        }
    }

    M5.update();

    if (M5.BtnA.wasReleased())
    {
        disp_mode++;
        if (disp_mode > DISP_MODE_IRONBLACK)
        {
            disp_mode = DISP_MODE_CAM;
        }
    }

    //Setting info
    img_buffer.setTextDatum(TC_DATUM);
    float bat_voltage = M5.Axp.GetBatVoltage();
    DrawBattery(214, 4, bat_voltage);

    /*
    switch (mes_mode)
    {
    case MES_AUTO_MAX:
        img_buffer.drawString("AUTO(MAX)", 205, MES_MODE_Y);
        break;

    case MES_AUTO_MIN:
        img_buffer.drawString("AUTO(MIN)", 205, MES_MODE_Y);
        break;

    case MES_CENTER:
        img_buffer.drawString("CENTER", 205, MES_MODE_Y);
        break;
    }
    */
    switch (disp_mode)
    {
    case DISP_MODE_CAM:
        img_buffer.drawString("RGB", HIST_WINDOWS_X1 + 20, 5);
        break;

    case DISP_MODE_GRAY:
        img_buffer.drawString("GRAY", HIST_WINDOWS_X1 + 20, 5);
        break;

    case DISP_MODE_GOLDEN:
        img_buffer.drawString("GOLDEN", HIST_WINDOWS_X1 + 20, 5);
        break;

    case DISP_MODE_RAINBOW:
        img_buffer.drawString("RAINBOW", HIST_WINDOWS_X1 + 20, 5);
        break;

    case DISP_MODE_IRONBLACK:
        img_buffer.drawString("IRON", HIST_WINDOWS_X1 + 20, 5);
        break;
    }

    img_buffer.pushSprite(0, 0);
}

#define COLORT_Y    27
#define COLORT_X    160
bool start_anime_flag = true;
void Progress_Bar(void *pvParameters)
{
    uint8_t frame = 0;
    uint8_t round = 0;
    uintmax_t start = micros();
    while (1)
    {
        switch(frame)
        {
            case 0: M5.Lcd.drawBitmap(COLORT_X, COLORT_Y, 50, 73, (uint16_t *)ColorT_0000_16); break;
            case 1: M5.Lcd.drawBitmap(COLORT_X, COLORT_Y, 50, 73, (uint16_t *)ColorT_0001_15); break;
            case 2: M5.Lcd.drawBitmap(COLORT_X, COLORT_Y, 50, 73, (uint16_t *)ColorT_0002_14); break;
            case 3: M5.Lcd.drawBitmap(COLORT_X, COLORT_Y, 50, 73, (uint16_t *)ColorT_0003_13); break;
            case 4: M5.Lcd.drawBitmap(COLORT_X, COLORT_Y, 50, 73, (uint16_t *)ColorT_0004_12); break;
            case 5: M5.Lcd.drawBitmap(COLORT_X, COLORT_Y, 50, 73, (uint16_t *)ColorT_0005_11); break;
            case 6: M5.Lcd.drawBitmap(COLORT_X, COLORT_Y, 50, 73, (uint16_t *)ColorT_0006_10); break;
            case 7: M5.Lcd.drawBitmap(COLORT_X, COLORT_Y, 50, 73, (uint16_t *)ColorT_0007_9); break;
            case 8: M5.Lcd.drawBitmap(COLORT_X, COLORT_Y, 50, 73, (uint16_t *)ColorT_0008_8); break;
            case 9: M5.Lcd.drawBitmap(COLORT_X, COLORT_Y, 50, 73, (uint16_t *)ColorT_0009_7); break;
            case 10: M5.Lcd.drawBitmap(COLORT_X, COLORT_Y, 50, 73, (uint16_t *)ColorT_0010_6); break;
            case 11: M5.Lcd.drawBitmap(COLORT_X, COLORT_Y, 50, 73, (uint16_t *)ColorT_0011_5); break;
            case 12: M5.Lcd.drawBitmap(COLORT_X, COLORT_Y, 50, 73, (uint16_t *)ColorT_0012_4); break;
            case 13: M5.Lcd.drawBitmap(COLORT_X, COLORT_Y, 50, 73, (uint16_t *)ColorT_0013_3); break;
            case 14: M5.Lcd.drawBitmap(COLORT_X, COLORT_Y, 50, 73, (uint16_t *)ColorT_0014_2); break;
            case 15: M5.Lcd.drawBitmap(COLORT_X, COLORT_Y, 50, 73, (uint16_t *)ColorT_0015_1); break;
            case 16: M5.Lcd.drawBitmap(COLORT_X, COLORT_Y, 50, 73, (uint16_t *)ColorT_0016_0); break;
        }

        frame++;
        if(frame > 16)
        {
            frame = 0;
            round++;
            if(round > 4)
            {
                delay(1500);
                start_anime_flag = false;
                break;
            }
        }
        delay(45);
    }
    vTaskDelete(NULL);
}

void Key_Detect(void *pvParameters)
{
    while (1)
    {
        M5.update();
        //Serial.printf("AXP = %02X\n", M5.Axp.GetBtnPress());
        //delay(50);
        /*
        if(M5.Axp.GetBtnPress())
        {
            M5.Axp.Write1Byte(0x32, 0x80);
        }*/
        /*
        if (M5.BtnB.wasReleased())
        {
            mes_mode++;
            if (mes_mode > MES_CENTER)
            {
                mes_mode = MES_AUTO_MAX;
            }
        }*/
        if (M5.BtnA.wasReleased())
        {
            disp_mode++;
            if (disp_mode > DISP_MODE_IRONBLACK)
            {
                disp_mode = DISP_MODE_CAM;
            }
        }
    }
}

void setup()
{
    esp_timer_init();
    delay(100);
    TaskHandle_t xHandle;
    M5.begin();
    M5.Lcd.setRotation(1);
    M5.Lcd.fillScreen(TFT_WHITE);
    M5.Lcd.drawBitmap(29, 39, 132, 57, (uint16_t *)title);
    M5.Axp.Write1Byte(0x28, 0xcc);

    ServoInit(servo_value);
    AP_MasterInit();

    disableCore0WDT();
    xTaskCreatePinnedToCore(
        Progress_Bar,   /* Function to implement the task */
        "Progress_Bar", /* Name of the task */
        4096,           /* Stack size in words */
        NULL,           /* Task input parameter */
        1,              /* Priority of the task */
        &xHandle,       /* Task handle. */
        0);             /* Core where the task should run */

    img_buffer.createSprite(SCREEN_X, SCREEN_Y);
    img_buffer.setTextSize(1);
    img_buffer.setTextColor(TFT_WHITE);

    lepton.begin();
    lepton.syncFrame();
    uint16_t SYNC = 5, DELAY = 3;
    lepton.doSetCommand(0x4854, &SYNC, 1);
    lepton.doSetCommand(0x4858, &DELAY, 1);
    lepton.end();

    while(start_anime_flag)
    {
        delay(1);
    }

    xTaskCreatePinnedToCore(
        RemoteImgTransfer,   // Function to implement the task
        "RemoteImgTransfer", // Name of the task
        4096,         // Stack size in words
        NULL,         // Task input parameter
        1,            // Priority of the task
        NULL,         // Task handle.
        0);           // Core where the task should run
}

void loop()
{
    //unsigned long T = micros();
    lepton.getRawValues();
    Update_Flir();
    //printf("%d\n", micros() - T);
}
