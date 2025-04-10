// Move WiFi-related functions to a new file

#include "WiFiManager.h"
#include "ESP8266WebServer.h"
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP8266httpUpdate.h>

// Global variable declarations
extern time_t lastTimeSync;
extern float version;
struct TimeConfig {
    int32_t timezone_offset;  // Offset from UTC in seconds
    char timezone_name[32];   // Human readable timezone name
    bool manual_time_set;     // Flag to indicate if time was set manually
    time_t last_manual_set;   // Last manual time set timestamp
    
    TimeConfig() : timezone_offset(19800), manual_time_set(false), last_manual_set(0) {  // Default to IST (UTC+5:30)
        strncpy(timezone_name, "IST", sizeof(timezone_name));
    }
};

struct DisplayConfig {
    bool use_24h_format;     // true = 24h, false = AM/PM
    bool use_celsius;        // true = Celsius, false = Fahrenheit
    uint8_t date_duration;    // Duration to show date (0-60 seconds, 0 = don't show)
    uint8_t temp_duration;    // Duration to show temperature (0-60 seconds, 0 = don't show)
    uint8_t humidity_duration;// Duration to show humidity (0-60 seconds, 0 = don't show)
    bool auto_brightness;    // true = use auto brightness based on min/max
    uint8_t min_brightness;  // Minimum brightness level (0-15)
    uint8_t max_brightness;  // Maximum brightness level (0-15)
    uint8_t man_brightness;
    float temp_delta;       // Temperature adjustment in degrees (+/-)
    float humidity_delta;   // Humidity adjustment percentage (+/-)
    
    DisplayConfig() : use_24h_format(false), use_celsius(true),
                     date_duration(5), temp_duration(5), humidity_duration(5),
                     auto_brightness(false), min_brightness(0), max_brightness(15), 
                     man_brightness(0), temp_delta(0.0), humidity_delta(0.0) {}
};

struct MQTTConfig {
    char mqtt_server[40];
    int mqtt_port;
    char mqtt_user[32];
    char mqtt_password[32];
    
    bool isEmpty() const {
        return mqtt_server[0] == '\0' || mqtt_port == 0;
    }
};

struct DeviceConfig {
    char hostname[32];  // Device hostname for network identification
    
    DeviceConfig() {
        strcpy(hostname, "DeskClock");  // Default hostname
    }
};

struct SystemCommandConfig {
    char command[32];  // Store the binary command string
    
    SystemCommandConfig() {
        memset(command, '0', sizeof(command) - 1);  // Initialize with all zeros
        command[sizeof(command) - 1] = '\0';  // Ensure null termination
    }
};

struct FirmwareConfig {
    char update_url[512];  // URL for firmware updates
    
    FirmwareConfig() {
        update_url[0] = '\0';  // Initialize empty
    }
};

extern MQTTConfig mqttConfig;
extern TimeConfig timeConfig;
extern DisplayConfig displayConfig;
extern DeviceConfig deviceConfig;
extern SystemCommandConfig systemCommandConfig;  // Add the extern declaration
extern FirmwareConfig firmwareConfig;  // Add firmware config

// Declare the server object as extern to avoid multiple definitions
extern ESP8266WebServer server;

// Declare displaySetupMessage as an external function
extern void displaySetupMessage(const char* message);

// Declare mqttClient as an external object
extern PubSubClient mqttClient;

// Declare Telnet server and client objects as extern
extern WiFiServer telnetServer;
extern WiFiClient telnetClient;

void setupWiFi();
void resetWiFiSettings();
void setupWebServer();
void handleRoot();
void handleSave();

// Declare MQTT setup and reconnect functions
void setupMQTT();
void reconnectMQTT();

// Declare the publishMQTTData function
void publishMQTTData(float temperature, float humidity);

// Function declarations for Telnet
void setupTelnet();
void handleTelnet();
void printBoth(const char* message);
void printBoth(const String& message);
void printBothf(const char* format, ...);

// Add new function declarations
void loadMQTTConfig();
void saveMQTTConfig();
void setDefaultMQTTConfig();

// Add new function declarations for timezone configuration
void loadTimeConfig();
void saveTimeConfig();
void setDefaultTimeConfig();

// Add new function declarations for manual time setting
void handleManualTimeSet();
void setManualTime(int year, int month, int day, int hour, int minute);

// Add new function declarations for display configuration
void loadDisplayConfig();
void saveDisplayConfig();
void setDefaultDisplayConfig();

// Add new function declarations for device configuration
void loadDeviceConfig();
void saveDeviceConfig();
void setDefaultDeviceConfig();

// Add new function declarations for system command configuration
void loadSystemCommandConfig();
void saveSystemCommandConfig();
void setDefaultSystemCommandConfig();

// Add new function declarations for firmware configuration
void loadFirmwareConfig();
void saveFirmwareConfig();
void setDefaultFirmwareConfig();
void displayUpdateProgress(int progress, const char* status);
