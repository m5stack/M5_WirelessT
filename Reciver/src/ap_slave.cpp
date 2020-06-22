#include "ap_slave.h"
#include "M5Stack.h"
#include "esp_system.h"

IPAddress LOCAL_IP(192, 168, 4, 101);
IPAddress GATEWAY(192, 168, 4, 1);
IPAddress SUBNET(255, 255, 0, 0);
IPAddress PRIMARY_DNS(8, 8, 8, 8); //optional
IPAddress SECOND_DNS(8, 8, 4, 4); //optional

const char *PASSWORD = "12345678";

#define M5LogErr(fmt, ...) Serial.printf("M5[ERROR] %s(%d): " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define M5LogWarn(fmt, ...) Serial.printf("M5[ WARN] %s(%d): " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define M5LogInfo(fmt, ...) Serial.printf("M5[ INFO] %s(%d): " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define M5LogTrace(fmt, ...) Serial.printf("M5[TRACE] %s(%d): " fmt, __func__, __LINE__, ##__VA_ARGS__)

String ScanWifi(void)
{
	int8_t scanResults = WiFi.scanNetworks();
	uint32_t stickt_count = 0;
	if (scanResults == 0)
    {
        Serial.println("No WiFi devices in AP Mode found");
    }
    else
    {
        for (int32_t i = 0; i < scanResults; ++i)
        {
            // Print SSID and RSSI for each device found
            String SSID = WiFi.SSID(i);
            int32_t RSSI = WiFi.RSSI(i);
            String BSSIDstr = WiFi.BSSIDstr(i);

            // Check if the current device starts with `Slave`
            if (SSID.indexOf("StickT_AP_") == 0)
            {
                stickt_count++;
                Serial.printf("Found StickT[%d] : %s (%d)\n", stickt_count, SSID.c_str(), RSSI);
				return SSID;
            }
        }
    }

	WiFi.scanDelete();
	return "NOT FOUND";
}

int8_t AP_SlaveInit(String ssid)
{
    if (!WiFi.config(LOCAL_IP, GATEWAY, SUBNET, PRIMARY_DNS, SECOND_DNS)) 
	{
    	M5LogErr("Failed to configure STA\n");
  	}

	if(ssid.indexOf("StickT_AP_") == 0)
	{
		WiFi.begin(ssid.c_str(), PASSWORD);
	}
    else
	{
		M5LogErr("StickT Not Found\n");
		return -1;
	}
	
    while (WiFi.status() != WL_CONNECTED) 
	{
		delay(500);
		Serial.print(".");
  	}

	Serial.print("\nWiFi connected with IP: ");
	Serial.println(WiFi.localIP());
	return 0;
}
