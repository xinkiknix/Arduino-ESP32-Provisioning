/* Code by Johan terryn
* tested on ESP32S3 Wroom-1 & Wroom-2
***********************************************************************
!!! ESP board manager 3.3.7 has a bug, works fine with version 3.3.6 !!
***********************************************************************
* Example generic provisioning solution 
* Uses Espressif BLE Provisioning tool (Android /Apple) to enter/select SSID and network Password
* After provisioning ESP reboots and can be accessed using "http//:ESP32.local"
* On the web page the device IP address can be set aswel as Gateway, Subnet and mDNS name 
* Server reboots and will connect to local SSID using local IP address. Server is accessible through http//:<mDNS>.local
* To reset or set to other SSID: Press button 0 (Boot button on esp32 dev modules) to restart cycle
*/
#include "WiFi.h"
#include "WiFiProv.h"     //provisioning interface
#include <Preferences.h>  //to store local wifi definitions
#include <ESPAsyncWebServer.h>
#include "esp_wifi.h"
#include <ESPmDNS.h>
#include <mdns.h>

#define RO_MODE true
#define RW_MODE false

AsyncWebServer server(80);
Preferences prefs;
// prov_uuid should be an optional parameter, but sometimes on some boards it might fail without this parameter
// the provided prov_uuid is the default one
uint8_t prov_uuid[16] = { 0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf, 0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02 }; 
// --- Web Page HTML ---
const char* pref_page = R"=====(
<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>
<style>body{font-family:sans-serif;padding:20px;} input{width:100%;padding:10px;margin:10px 0;}</style></head>
<body><h2>Site Network Config</h2>
<form action='/save' method='POST'>
IP: <input name='ip' placeholder='192.168.1.50'>
Gateway: <input name='gw' value='192.168.1.1'>
Subnet: <input name='sn' value='255.255.255.0'>
mDNS name: <input name='mDNS' placeholder='Living'>.local
<input type='submit' value='Save & Reboot' style='background:#007bff;color:white;border:none;'>
</form></body></html>
)=====";
// --- Web Page HTML ---
const char* index_page = R"=====(
<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>
<style>body{font-family:sans-serif;padding:20px;} input{width:100%;padding:10px;margin:10px 0;}</style></head>
<body><h2>Welcome</h2>
<div>This is your start page</div>
</body></html>
)=====";


/*Call back function, provides details of wifi connection status*/
void SysProvEvent(arduino_event_t* sys_event) {
  switch (sys_event->event_id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("\nConnected IP address : ");
      Serial.println(IPAddress(sys_event->event_info.got_ip.ip_info.ip.addr));
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: Serial.println("\nDisconnected. Connecting to the AP again... "); break;
    case ARDUINO_EVENT_PROV_START: Serial.println("\nProvisioning started\nGive Credentials of your access point using smartphone app"); break;
    case ARDUINO_EVENT_PROV_CRED_RECV:
      {
        Serial.println("\nReceived Wi-Fi credentials");
        Serial.print("\tSSID : ");
        Serial.println((const char*)sys_event->event_info.prov_cred_recv.ssid);
        Serial.print("\tPassword : ");
        Serial.println((char const*)sys_event->event_info.prov_cred_recv.password);
        break;
      }
    case ARDUINO_EVENT_PROV_CRED_FAIL:
      {
        Serial.println("\nProvisioning failed!\nPlease reset to factory and retry provisioning\n");
        if (sys_event->event_info.prov_fail_reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR) {
          Serial.println("\nWi-Fi AP password incorrect");
        } else {
          Serial.println("\nWi-Fi AP not found....Add API \" nvs_flash_erase() \" before beginProvision()");
        }
        break;
      }
    case ARDUINO_EVENT_PROV_CRED_SUCCESS: Serial.println("\nProvisioning Successful"); break;
    case ARDUINO_EVENT_PROV_END: Serial.println("\nProvisioning Ends"); break;
    default: break;
  }
}

/*Remove stored credentials*/
void ClearPreferences() {
  prefs.begin("site-cfg", RW_MODE);
  prefs.clear();
  prefs.end();
  Serial.println("[RESET] Old Static IP wiped for new provisioning.");
}

/*Web server-page handling*/
void handleIndex(AsyncWebServerRequest* request) {
  request->send(200, "text/html", index_page);
}

void handlePref(AsyncWebServerRequest* request) {
  request->send(200, "text/html", pref_page);
}

void handleSave(AsyncWebServerRequest* request) {
  String data = request->arg("ip") + "," + request->arg("gw") + "," + request->arg("sn") + "," + request->arg("mDNS");
  if (request->arg("ip").length() < 7) {
    request->send(400, "text/plain", "Invalid IP Address");
    return;
  }
  Serial.println(data);
  prefs.begin("site-cfg", RW_MODE);
  prefs.putString("net_info", data);
  prefs.end();
  request->send(200, "text/plain", "Saved! Rebooting to " + request->arg("ip")+" mDNS: " + request->arg("mDNS") +".local");
  delay(2000);
  ESP.restart();
}
/*End web server*/

bool WifiConnect() {
  // Initialize WiFi in Station Mode
  WiFi.mode(WIFI_STA);
  // register callback
  WiFi.onEvent(SysProvEvent);
  prefs.begin("site-cfg", RO_MODE);
  String savedIP = prefs.getString("net_info", "");
  prefs.end();
  // FORCE the driver to return saved credentials
  // Using a low-level ESP-IDF check because WiFi.SSID() fails in current ESP32 for arduino release 
  wifi_config_t conf;
  bool hasCredentials = (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK && strlen((char*)conf.sta.ssid) > 0);
  /*SSID is known and valid and preferences are stored*/
  if (hasCredentials && savedIP.length() > 0) {
    Serial.printf("[STATUS] Deployed Mode. SSID found: %s\n", (char*)conf.sta.ssid);

    char buf[64];
    savedIP.toCharArray(buf, 64);
    char* ip_p = strtok(buf, ",");
    char* gw_p = strtok(NULL, ",");
    char* sn_p = strtok(NULL, ",");
    char* mDNS = strtok(NULL, ",");

    IPAddress ip, gw, sn;
    if (ip.fromString(ip_p) && gw.fromString(gw_p) && sn.fromString(sn_p)) {
      IPAddress dns(8, 8, 8, 8);  // Google DNS
      if (!WiFi.config(ip, gw, sn, dns)) {
        Serial.println("[ERROR] Static IP Configuration Failed!");
      } else {
        Serial.println("[WIFI] Static IP Locked: " + String(ip_p));
      }
    }
    WiFi.begin();
    if (mDNS) {
      if (mDNS[strlen(mDNS) - 1] == ' ') {  // on phone browser spell-checker might add " " (space) if the chosen mDNS is a dictionary word, so remove it
        mDNS[strlen(mDNS) - 1] = '\0';
      } else {
        mDNS[strlen(mDNS)] = '\0';
      }
      setmDNS(mDNS);
    }  // Connected using the saved credentials and Static Config
    return true;
  } else {
    Serial.println("[STATUS] Factory Mode. No credentials in NVS. Starting BLE...");
    ClearPreferences();
    WiFiProv.beginProvision(
      NETWORK_PROV_SCHEME_BLE, NETWORK_PROV_SCHEME_HANDLER_FREE_BTDM,
      NETWORK_PROV_SECURITY_1, "12345678", "PROV_ESP32", NULL, prov_uuid, false);
  }
  setmDNS("ESP32");  //default mdns name : http//:ESP32.local
  return false;
}

void setmDNS(char* mDNSName) {
  if (!MDNS.begin(mDNSName)) {
    Serial.println("[mDNS]Error setting up MDNS responder!");
  } else {
    if (!MDNS.addService("_http", "_tcp", 80)) {
      Serial.println("[mDNS] responder failed:" + String(mDNSName));
    } else {
      Serial.println("[mDNS] responder started. Access your ESP32 at http://" + String(mDNSName) + ".local");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(0, INPUT_PULLUP);
  Serial.println("\n[SYSTEM] Booting...");
  if (WifiConnect()) { // have Access to network with local credentials
    server.on("/", handleIndex);
    // add other server handling here
  } else { //access with ESP32.local only right after provisioning
    server.on("/", handlePref); 
    server.on("/save", HTTP_POST, handleSave);
  }
 
  server.begin();
}

void loop() {
  // Reset Logic
  if (digitalRead(0) == LOW) {
    delay(3000);
    if (digitalRead(0) == LOW) {
      Serial.println("[RESET] Performing Factory Wipe...");
      WiFi.disconnect(true, true);
      ClearPreferences();
      delay(500);
      ESP.restart();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    static bool reported = false;
    if (!reported) {
      Serial.print("[Status] Online! URL: http://");
      Serial.println(WiFi.localIP());
      char actualHostname[MDNS_NAME_BUF_LEN];  // MDNS_NAME_BUF_LEN is 64 from mdns.h
      esp_err_t err = mdns_hostname_get(actualHostname);
      if (err == ESP_OK) {
        Serial.println("[Status]Current hostname: http://" + String(actualHostname) + ".local");
      }
      reported = true;
    }
  }
}
