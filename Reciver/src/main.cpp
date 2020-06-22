#include <M5Stack.h>
#include "img_table.h"
#include "ap_slave.h"
#include "bmp.h"
#include "face_joy_stick.h"
#include "img/ColorT.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <vector>

typedef std::vector<String> StringVector;

uint32_t bmp_counter = 0;
uint8_t	disp_mode;
uint8_t	mes_mode;
uint8_t	last_disp_mode = 0xFF;
uint8_t	last_mes_mode = 0xFF;
TFT_eSprite img_buffer = TFT_eSprite(&M5.Lcd);
WiFiUDP udp;

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

QueueHandle_t xQueue_DispBuffer = xQueueCreate(3, sizeof(int32_t));
uint8_t recv_frame_buffer[19220];
uint8_t (*raw_full_buffer)[240];

void UDPRecv(void *pvParameters)
{
    uint8_t last_frame = 0xFF;
    uint16_t parts_flag = 0x8000;
    while(1)
	{
        uint32_t len = udp.parsePacket();
        if(len)
        {
            unsigned long T = micros();
            uint8_t data[len];
            udp.read(data, len);

            // uint32_t current_fraame = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
            uint8_t current_frame = data[0];
            uint8_t part = data[1];
            // Serial.printf("%d[%d]\n", current_frame, part);

            if(last_frame != current_frame)
            {
                last_frame = current_frame;
                parts_flag = 0x8000;
            }

            if(part == 14)
            {
                memcpy(recv_frame_buffer + (part * 1280), (data + 2), 1280 + 19);
            }
            else
            {
                memcpy(recv_frame_buffer + (part * 1280), (data + 2), 1280);
            }
            
            parts_flag |= 0x0001 << part;

            if(parts_flag == 0xFFFF)
            {
                parts_flag = 0x8000;
                uint8_t* disp_buffer = (uint8_t *) malloc(19220 * sizeof(uint8_t));
                if(disp_buffer != NULL) 
                {
                    memcpy(disp_buffer, recv_frame_buffer, 19220);
                    if(xQueueSend(xQueue_DispBuffer, &disp_buffer, 0) == 0)
                    {
                        free(disp_buffer);
                    }
                }
            }
             Serial.printf("%d[%d] len = %d, recv_time = %ld us\n", current_frame, part, len, micros() - T);
        }
        //Serial.println();
    }
}

void DisplayImage(const uint16_t* palette)
{
    uint16_t x, y;
    for (y = 0; y < 240; y++)
    {
        for (x = 0; x < 320; x++)
        {
            img_buffer.drawPixel(x, y, palette[raw_full_buffer[x][y]]);
        }
    }
}

void IRAM_ATTR DisplayCursor(uint16_t x, uint16_t y)
{
    img_buffer.drawCircle(x, y, 6, TFT_WHITE);
    img_buffer.drawLine(x, y - 10, x, y + 10, TFT_WHITE);
    img_buffer.drawLine(x - 10, y, x + 10, y, TFT_WHITE);
}

String SelectAP(void)
{
    StringVector result;
    unsigned long T = 0;
    uint16_t select = 0;
    while(1)
    {
        //if(millis() - T > 1000)
        if(T == 0)
        {
            T = millis();
            result.clear();
            int8_t scanResults = WiFi.scanNetworks();
            for (int32_t i = 0; i < scanResults; ++i)
            {
                String SSID = WiFi.SSID(i);

                if (SSID.indexOf("StickT_AP_") == 0)
                {
                    result.push_back(SSID);
                }
            }
            WiFi.scanDelete();
        }

        img_buffer.setTextDatum(CC_DATUM);
        img_buffer.fillSprite(0);
        img_buffer.setTextSize(2);
        img_buffer.fillRect(0, 0, 320, 20, TFT_RED);
        img_buffer.drawString("StickT Select", 160, 10);
        img_buffer.drawString("<-", 65, 230);
        img_buffer.drawString("OK", 160, 230);
        img_buffer.drawString("->", 255, 230);

        img_buffer.setTextDatum(TL_DATUM);
        M5.update();
        if(M5.BtnA.wasReleased())
        {
            if(select > 0) select--;
        }
        if(M5.BtnC.wasReleased())
        {
            if(select < result.size() - 1) select++;
            else select = result.size() - 1;
        }
        if(M5.BtnB.wasReleased())
        {
            if(result.size() != 0)
            {
                M5.Lcd.fillScreen(0);
                M5.Lcd.drawString("Connecting...", 160, 95);
                M5.Lcd.setTextSize(1);
                M5.Lcd.drawString("If there is no response for more than 5 seconds", 160, 125);
                M5.Lcd.drawString("please restart StickT and this device", 160, 140);
                return result.at(select);
            }
        }
        if(result.size() == 0)
        {
            img_buffer.setTextDatum(CC_DATUM);
            img_buffer.drawString("StickT Not Found.", 160, 120);
        }
        else
        {
            uint16_t x = 0, y = 0, index = 0;
            if(select >= result.size())
            {
                select = result.size() - 1;
            }
            Serial.printf("%d\n", select);
            for(index = 0; index < result.size(); index++)
            {
                String mac = result.at(index).substring(10);

                uint16_t bx, by;
                bx = x * 64 + 2;
                by = y * 24 + 24;
                if(select == index)
                {
                    img_buffer.fillRect(bx, by, 60, 20, TFT_RED);
                }
                else
                {
                    img_buffer.fillRect(bx, by, 60, 20, TFT_BLUE);
                }
                img_buffer.drawString(mac, bx + 6, by + 3);
                x++;
                if(x == 5)
                {
                    x = 0;
                    y ++;
                }
            }
        }
        img_buffer.pushSprite(0, 0);
    }
}

void setup() 
{
    raw_full_buffer = (uint8_t(*)[240])malloc(sizeof(uint8_t)*320*240);
    if(raw_full_buffer == NULL)
    {
        ESP.restart();
    }

    M5.begin();
    M5.Lcd.setTextDatum(CC_DATUM);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_WHITE);
    M5.Lcd.fillRect(0, 0, 320, 20, TFT_RED);
    M5.Lcd.drawString("StickT Select", 160, 10);
    M5.Lcd.drawString("<-", 65, 230);
    M5.Lcd.drawString("OK", 160, 230);
    M5.Lcd.drawString("->", 255, 230);
    M5.Lcd.drawString("Scanning...", 160, 120);

    img_buffer.createSprite(320, 240);
    img_buffer.setTextSize(1);
    img_buffer.setTextColor(TFT_WHITE);
    JoyStickInit();

    AP_SlaveInit(SelectAP());
    udp.begin(2000);

    disableCore0WDT();
    xTaskCreatePinnedToCore(
        UDPRecv,   // Function to implement the task
        "UDPRecv", // Name of the task
        4096,         // Stack size in words
        NULL,         // Task input parameter
        1,            // Priority of the task
        NULL,         // Task handle.
        0);           // Core where the task should run
}

const uint8_t kCenterRange = 100;
int8_t servo_delta = 0;
int8_t encoder_delta = 0;

void loop() 
{
    //Serial.println(esp_get_free_heap_size());
    uint8_t* recv_buffer;
    if(xQueueReceive(xQueue_DispBuffer, &recv_buffer, 0))
    {
        uint16_t x, y, i = 0;
        uint8_t appendix_buf[19];

        // Decode appendix data
        memcpy(appendix_buf, recv_buffer + 160*120, 19);
        uint8_t recv_disp_mode = appendix_buf[0];
        uint8_t recv_mes_mode = appendix_buf[1];
        if(recv_disp_mode != last_disp_mode)
        {
            last_disp_mode = recv_disp_mode;
            disp_mode = recv_disp_mode;
        }
        if(recv_mes_mode != last_mes_mode)
        {
            last_mes_mode = recv_mes_mode;
            mes_mode = recv_mes_mode;
        }
        uint16_t max_x = appendix_buf[5] << 8 | appendix_buf[6];
        uint16_t max_y = appendix_buf[7] << 8 | appendix_buf[8];
        uint16_t min_x = appendix_buf[9] << 8 | appendix_buf[10];
        uint16_t min_y = appendix_buf[11] << 8 | appendix_buf[12];
        uint16_t i_max_temp = appendix_buf[13] << 8 | appendix_buf[14];
        uint16_t i_min_temp = appendix_buf[15] << 8 | appendix_buf[16];
        uint16_t i_center_temp = appendix_buf[17] << 8 | appendix_buf[18];
        max_x <<= 1;
        max_y <<= 1;
        min_x <<= 1;
        min_y <<= 1;
        //Serial.printf("disp_mode %d, mes_mode %d, raw_cursor %04X, dir_flag %d, max_x %d, max_y %d, min_x %d, min_y %d, i_max_temp %d, i_min_temp %d, i_center_temp %d\n", disp_mode,mes_mode,raw_cursor,dir_flag,max_x,max_y,min_x,min_y,i_max_temp,i_min_temp,i_center_temp);

        // Image interpolation amplification
        // Green
        for (y = 0; y < 240; y+=2)
        {
            for (x = 0; x < 320; x+=2)
            {
                raw_full_buffer[x][y] = recv_buffer[i];
                i++;
            }
        }

        // Red
        for (y = 1; y < 238; y+=2)
        {
            for (x = 1; x < 318; x+=2)
            {
                raw_full_buffer[x][y] = (raw_full_buffer[x-1][y-1] + raw_full_buffer[x+1][y-1] + 
                                        raw_full_buffer[x-1][y+1] + raw_full_buffer[x+1][y+1]) >> 2;
            }
        }

        // Red bottom line
        for (x = 1; x < 318; x+=2)
        {
            raw_full_buffer[x][239] =  (raw_full_buffer[x-1][y-1] + raw_full_buffer[x+1][y-1]) >> 1;
        }

        // Red right line
        for (y = 1; y < 238; y+=2)
        {
            raw_full_buffer[319][y] = (raw_full_buffer[x-1][y-1] + raw_full_buffer[x-1][y+1]) >> 1;
        }

        // Red bottom-right corner
        raw_full_buffer[319][239] = raw_full_buffer[318][238];

        // Yellow
        for (y = 2; y < 239; y+=2)
        {
            for (x = 1; x < 318; x+=2)
            {
                raw_full_buffer[x][y] = (raw_full_buffer[x-1][y] + raw_full_buffer[x+1][y] + 
                                        raw_full_buffer[x][y+1] + raw_full_buffer[x][y-1]) >> 2;
            }
        }

        // Yellow top line
        for (x = 1; x < 318; x+=2)
        {
            raw_full_buffer[x][0] = (raw_full_buffer[x-1][0] + raw_full_buffer[x+1][0]) >> 1;
        }

        // Yellow right line
        for (y = 2; y < 239; y+=2)
        {
            raw_full_buffer[319][y] = (raw_full_buffer[319][y-1] + raw_full_buffer[319][y+1]) >> 1;
        }

        // Yellow top-right corner
        raw_full_buffer[319][0] = (raw_full_buffer[318][0] + raw_full_buffer[319][1]) >> 1;

        // White
        for (y = 1; y < 238; y+=2)
        {
            for (x = 2; x < 320; x+=2)
            {
                raw_full_buffer[x][y] = (raw_full_buffer[x-1][y] + raw_full_buffer[x+1][y] + 
                                        raw_full_buffer[x][y+1] + raw_full_buffer[x][y-1]) >> 2;
            }
        }

        // White bottom line
        for (x = 2; x < 320; x+=2)
        {
            raw_full_buffer[x][239] = (raw_full_buffer[x-1][239] + raw_full_buffer[x+1][239]) >> 1;
        }

        // White left line
        for (y = 1; y < 238; y+=2)
        {
            raw_full_buffer[0][y] = (raw_full_buffer[0][y-1] + raw_full_buffer[0][y+1]) >> 1;
        }

        // White bottom-left corner
        raw_full_buffer[0][239] = (raw_full_buffer[0][238] + raw_full_buffer[1][239]) >> 1;
        
        // Display
        switch (disp_mode)
        {
            case DISP_MODE_GRAY: DisplayImage(colormap_grayscale); break;
            case DISP_MODE_GOLDEN: DisplayImage(colormap_golden); break;
            case DISP_MODE_RAINBOW: DisplayImage(colormap_rainbow); break;
            case DISP_MODE_IRONBLACK: DisplayImage(colormap_ironblack); break;
            default: DisplayImage(colormap_cam);
        }

        switch (mes_mode)
        {
            case MES_AUTO_MIN:
                DisplayCursor(min_x, min_y);
                x = min_x + 5;
                y = min_y + 5;
                if (min_x > 320 - 65)
                    x = min_x - 65;
                if (min_y > 240 - 25)
                    y = min_y - 25;
                img_buffer.setCursor(x, y);
                img_buffer.printf("%2d.%02d", i_min_temp / 100, i_min_temp % 100);
                break;

            case MES_CENTER:
                DisplayCursor(160, 120);
                img_buffer.setCursor(160 + 5, 120 + 5);
                img_buffer.printf("%2d.%02d", i_center_temp / 100, i_center_temp % 100);
                break;

            default:
                DisplayCursor(max_x, max_y);
                x = max_x + 5;
                y = max_y + 5;
                if (max_x > 320 - 65)
                    x = max_x - 65;
                if (max_y > 240 - 25)
                    y = max_y - 25;
                img_buffer.setCursor(x, y);
                img_buffer.printf("%2d.%02d", i_max_temp / 100, i_max_temp % 100);
        }

        img_buffer.pushSprite(0, 0);
        free(recv_buffer);
    }

    M5.update();
    joy_stick_t joy_stick = JoyStickUpdate();
    if(joy_stick.y > _joy_stick_initial_state.y + kCenterRange) //up
    {
        int32_t _x = joy_stick.y - _joy_stick_initial_state.y;
        _x = _x * _x;
        servo_delta = int8_t(0.0005f * _x);
        JoyStickLed(3, 0, 0, int(0.002 * _x));
        JoyStickLed(1, 0, 0, 0);
    }
    else if(joy_stick.y < _joy_stick_initial_state.y - kCenterRange) //down
    {
        int32_t _x = joy_stick.y - _joy_stick_initial_state.y;
        _x = _x * _x;
        servo_delta = int8_t(-0.0005f * _x);
        JoyStickLed(1, 0, 0, int(0.002 * _x));
        JoyStickLed(3, 0, 0, 0);
    }
    else //center
    {
        JoyStickLed(3, 0, 0, 0);
        JoyStickLed(1, 0, 0, 0);
        servo_delta = 0;
    }

    if(joy_stick.x > _joy_stick_initial_state.x + kCenterRange) //up
    {
        int32_t _x = joy_stick.x - _joy_stick_initial_state.x;
        _x = _x * _x;
        encoder_delta = int8_t(0.0005f * _x);
        JoyStickLed(2, 0, int(0.002 * _x), 0);
        JoyStickLed(0, 0, 0, 0);
    }
    else if(joy_stick.x < _joy_stick_initial_state.x - kCenterRange) //down
    {
        int32_t _x = joy_stick.x - _joy_stick_initial_state.x;
        _x = _x * _x;
        encoder_delta = int8_t(-0.0005f * _x);
        JoyStickLed(0, 0, int(0.002 * _x), 0);
        JoyStickLed(2, 0, 0, 0);
    }
    else //center
    {
        JoyStickLed(0, 0, 0, 0);
        JoyStickLed(2, 0, 0, 0);
        encoder_delta = 0;
    }
    //Serial.printf("encoder_delta = %d, servo_delta = %d, btn = %d\n", encoder_delta, servo_delta, joy_stick.btn);
    udp.beginPacket(IPAddress(192,168,4,1), 1001);
    udp.write((uint8_t)encoder_delta);
    udp.write((uint8_t)servo_delta);
    udp.write((uint8_t)joy_stick.btn);
    udp.endPacket();

    if (M5.BtnA.wasReleased())
    {
        disp_mode++;
        if (disp_mode > DISP_MODE_IRONBLACK)
        {
            disp_mode = DISP_MODE_CAM;
        }
    }

    if (M5.BtnC.wasReleased())
    {
        mes_mode++;
        if (mes_mode > MES_CENTER)
        {
            mes_mode = MES_AUTO_MAX;
        }
    }

    if (M5.BtnB.wasPressed())
    {
        int32_t x, y;

        char file_name[20];
        sprintf(file_name, "/snapshot_%04d.bmp", ++bmp_counter);

        String info("Saving ");
        info += file_name;
        M5.Lcd.drawString(info, 160, 120);

        File file = SD.open(file_name, FILE_WRITE);
        if(!file)
        {
            Serial.println("Failed to open file for writing");
            return;
        }

        Serial.printf("Write %s\n", file_name);
        file.write(bmp_head, 54);

        switch (disp_mode)
        {
            case DISP_MODE_CAM: file.write(bmp_palette_rgb, 1024); break;
            case DISP_MODE_GRAY: file.write(bmp_palette_gray, 1024); break;
            case DISP_MODE_GOLDEN: file.write(bmp_palette_golden, 1024); break;
            case DISP_MODE_RAINBOW: file.write(bmp_palette_rainbow, 1024); break;
            case DISP_MODE_IRONBLACK: file.write(bmp_palette_ironblack, 1024); break;
            default: file.write(bmp_palette_rgb, 1024);
        }

        for (y = 239; y >= 0; y--)
        {
            for (x = 319; x >= 0; x--)
            {
                file.write(255 - raw_full_buffer[x][y]);
            }
        }
        file.close();
    }
}