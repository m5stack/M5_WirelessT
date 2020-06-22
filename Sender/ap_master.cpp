#include "ap_master.h"
#include "esp_system.h"
#include "M5StickC.h"

const char *PASSWORD = "12345678";

WiFiUDP udp_1;

#define M5LogErr(fmt, ...) Serial.printf("M5[ERROR] %s(%d): " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define M5LogWarn(fmt, ...) Serial.printf("M5[ WARN] %s(%d): " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define M5LogInfo(fmt, ...) Serial.printf("M5[ INFO] %s(%d): " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define M5LogTrace(fmt, ...) Serial.printf("M5[TRACE] %s(%d): " fmt, __func__, __LINE__, ##__VA_ARGS__)

int8_t AP_MasterInit(void)
{
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
					  IPAddress(192, 168, 4, 1),
					  IPAddress(255, 255, 255, 0));

	uint8_t baseMac[6];
	esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
	char ssid_buf[30];
	sprintf(ssid_buf, "StickT_AP_%02X%02X", baseMac[4], baseMac[5]);
	WiFi.softAP(ssid_buf, PASSWORD);

	IPAddress myIP = WiFi.softAPIP();
	Serial.print("AP IP address: ");
	Serial.println(myIP);

	udp_1.begin(1001);

	return 0;
}
