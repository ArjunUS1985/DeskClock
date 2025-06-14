#include <DHT.h>
#include <MD_Parola.h>
#include <MD_MAX72XX.h>
#include <SPI.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <Font3x5.h>
#include <WiFiManager.h>
#include <ESP8266WebServer.h>
#include "WiFiSetup.h"
#include <time.h>
#include <ESP8266HTTPClient.h>

// Global variables
time_t lastTimeSync = 0;               // Track last sync time
unsigned long lastDisplayChange = 0;   // Track when display was last changed
unsigned long lastBrightnessCheck = 0; // Track when brightness was last updated
uint8_t currentDisplay = 0;            // 0 = time, 1 = date, 2 = temp, 3 = humidity
bool unableToSetTime = false; // Flag to indicate if manual time was set
#define LDR_PIN A0                     // Analog pin for LDR
#define BRIGHTNESS_CHECK_INTERVAL 1000 // Check brightness every 1 second
#define MIN_ANALOG_VALUE 1             // Minimum analog reading (darkness)
#define MAX_ANALOG_VALUE 1024          // Maximum analog reading (brightness)
#define MIN_INTENSITY -2               // Minimum display intensity
#define MAX_INTENSITY 15               // Reduced maximum intensity for better night viewing
// #define SMOOTHING_FACTOR 0.3  // How much weight to give to new readings (0-1)

// Global variables for brightness control
static float currentIntensity = 0;   // Current smoothed intensity value
static uint8_t lastSetIntensity = 0; // Last intensity value actually set to displays

// Create a sequence of what to display based on user settings
uint8_t displaySequence[4];  // Will hold sequence of valid displays
uint8_t displayDurations[4]; // Will hold corresponding durations
uint8_t numDisplays = 0;     // Number of active displays

void updateDisplaySequence()
{
    numDisplays = 0; // Start from 0 since time is now on separate display

    if (displayConfig.date_duration > 0)
    {
        displaySequence[numDisplays] = 1;
        displayDurations[numDisplays] = displayConfig.date_duration;
        numDisplays++;
    }
    if (displayConfig.temp_duration > 0)
    {
        displaySequence[numDisplays] = 2;
        displayDurations[numDisplays] = displayConfig.temp_duration;
        numDisplays++;
    }
    if (displayConfig.humidity_duration > 0)
    {
        displaySequence[numDisplays] = 3;
        displayDurations[numDisplays] = displayConfig.humidity_duration;
        numDisplays++;
    }
}

// DHT22 settings
#define DHTPIN D2
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// MAX7219 settings
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define DATA_PIN 13  // D7 on NodeMCU
#define DATA_PIN2 12 // D6 on NodeMCU
#define CS_PIN 15    // D8 on NodeMCU
#define CLK_PIN 14   // D5 on NodeMCU
#define CLK_PIN2 5   // D1 on NodeMCU
MD_Parola myDisplay = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN2, CS_PIN, MAX_DEVICES);
// Create a separate instance of MD_Parola for setup messages
MD_Parola setupDisplay = MD_Parola(HARDWARE_TYPE, DATA_PIN2, CLK_PIN, CS_PIN, MAX_DEVICES);
// Create a dedicated display for time
MD_Parola timeDisplay = MD_Parola(HARDWARE_TYPE, DATA_PIN2, CLK_PIN, CS_PIN, MAX_DEVICES);

#define RESET_PIN D3 // Define the GPIO pin connected to the reset button

#define COLON_CHAR ':'
#define COLON_OFF_CHAR '.' // Using single period when colon is off

void displaySetupMessage(const char *message);

void checkResetButton()
{
    static bool buttonPressed = false;
    static unsigned long pressStartTime = 0;

    pinMode(RESET_PIN, INPUT_PULLUP);

    // Current button state
    bool currentlyPressed = (digitalRead(RESET_PIN) == LOW);

    // Button just pressed
    if (currentlyPressed && !buttonPressed)
    {
        buttonPressed = true;
        pressStartTime = millis();
    }

    // Button still being held
    if (currentlyPressed && buttonPressed)
    {
        if (millis() - pressStartTime > 5000)
        { // Held for 5 seconds
            printBoth("Reset button pressed. Clearing Wi-Fi settings...");
            displaySetupMessage("Resetting clock in 5 seconds");
            resetWiFiSettings(); // Clear Wi-Fi credentials and restart
        }
    }

    // Button released
    if (!currentlyPressed && buttonPressed)
    {
        buttonPressed = false;
    }
}

void displaySetupMessage(const char *message)
{
    setupDisplay.begin();
    setupDisplay.setFont(nullptr); // Use default font
    setupDisplay.displayClear();
    setupDisplay.setIntensity(0);
    setupDisplay.displayText(message, PA_CENTER, 25, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
    while (!setupDisplay.displayAnimate())
    {
        delay(10); // Small delay to ensure smooth animation
    }
}
void displaySetupMessageProgress(const char *message)
{

    setupDisplay.displayText(message, PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
    setupDisplay.displayAnimate(); // Single call without waiting
}
void abnormalLoop()
{
    // More comprehensive messages to help users connect
    const char *messages[] = {
        "Join WiFi Network",
        "SmartClock-AP",
        "Open browser",
        "IP: 192.168.4.1"};
    int currentMessage = 0;
    unsigned long lastChange = 0;
    const unsigned long MESSAGE_INTERVAL = 2500; // Switch message every 2.5 seconds

    while (true)
    {
        ArduinoOTA.handle();
        server.handleClient(); // Handle web server requests in AP mode

        // Switch messages periodically
        unsigned long currentMillis = millis();
        if (currentMillis - lastChange >= MESSAGE_INTERVAL)
        {
            displaySetupMessage(messages[currentMessage]);
            currentMessage = (currentMessage + 1) % 4; // Cycle through all 4 messages
            lastChange = currentMillis;
        }

        // Handle reset button during AP mode too
        checkResetButton();

        setupDisplay.displayAnimate();
        delay(10);
    }
}

void setupTime()
{
    // Load timezone configuration
    loadTimeConfig();

    // Try NTP sync first
    configTime(timeConfig.timezone_offset, 0, "pool.ntp.org", "time.nist.gov");
    printBothf("Setting up time with timezone %s (offset: %d seconds)", timeConfig.timezone_name, timeConfig.timezone_offset);
    // displaySetupMessage("Time sync...");

    // Add timeout for NTP sync (30 seconds)
    unsigned long startAttempt = millis();
    bool syncSuccess = false;

    while (millis() - startAttempt < 30000)
    {                        // 30 second timeout
        ArduinoOTA.handle(); // Add OTA handling while waiting
        if (time(nullptr) > 1600000000)
        { // Valid time received (after 2020)
            syncSuccess = true;
            break;
        }
        delay(100); // Reduced delay to check OTA more frequently
        if (telnetClient && telnetClient.connected())
        {
            telnetClient.print(".");
        }
    }

    if (syncSuccess)
    {
        printBoth("\nTime synchronized via NTP");
        displaySetupMessage("Time Synced!");
        lastTimeSync = time(nullptr);
    }
    else
    {
        printBoth("\nNTP sync failed - Please set time manually");
        displaySetupMessage("Set time manually");
        // Initialize with a default time if no manual time was previously set
        if (!timeConfig.manual_time_set)
        {
            // Set to 2024-01-01 00:00:00 as fallback
            setManualTime(2024, 1, 1, 0, 0);
            unableToSetTime = true; // Flag to indicate manual time was set
        }
    }
}

void syncTimeIfNeeded()
{
    time_t now = time(nullptr);
    struct tm *timeInfo = localtime(&now);

    // Check if it's 3 AM and if we haven't synced in the last hour
    if (timeInfo->tm_hour == 3 && (now - lastTimeSync) > 3600)
    {
        printBoth("Performing daily time sync...");
        displaySetupMessage("Daily Time Sync...");
        configTime(timeConfig.timezone_offset, 0, "pool.ntp.org", "time.nist.gov");
        lastTimeSync = now;
        printBoth("Time resynchronized");
    }
}

// Update the brightness method to include a check for auto brightness
void updateBrightness()
{
    if (!displayConfig.auto_brightness)
    {
        // If auto brightness is disabled, set manual brightness and return
        myDisplay.setIntensity(displayConfig.man_brightness);
        timeDisplay.setIntensity(displayConfig.man_brightness);
        return;
    }

    static int ldrValues[10] = {0};          // Array to store LDR values for the last 10 seconds
    static int currentIndex = 0;             // Current index in the array
    static unsigned long lastUpdateTime = 0; // Last time the brightness was updated
    static int targetBrightness = 0;         // Target brightness to gradually reach

    // Initialize the ldrValues array with the first LDR reading to avoid unassigned values affecting the average
    static bool isInitialized = false;
    if (!isInitialized)
    {
        int initialLdrValue = analogRead(LDR_PIN);
        for (int i = 0; i < 10; i++)
        {
            ldrValues[i] = initialLdrValue;
        }
        isInitialized = true;
    }

    // Read the LDR value
    int ldrValue = analogRead(LDR_PIN);
    if (telnetClient && telnetClient.connected())
    {
        telnetClient.print("ldr ");
        telnetClient.print(ldrValue);
    }
    ldrValues[currentIndex] = ldrValue;
    currentIndex = (currentIndex + 1) % 10; // Move to the next index, wrap around after 10

    // Calculate the running average of the last 10 seconds
    int sum = 0;
    for (int i = 0; i < 10; i++)
    {
        sum += ldrValues[i];
    }
    int averageLdrValue = sum / 10;

    // Adjust the mapping to allow brightness to reach 0 when LDR value is at its maximum
    targetBrightness = map(averageLdrValue, MIN_ANALOG_VALUE, MAX_ANALOG_VALUE,
                           displayConfig.max_brightness, displayConfig.min_brightness - 1);
    targetBrightness = constrain(targetBrightness, displayConfig.min_brightness - 1, displayConfig.max_brightness);

    // Gradually change the brightness over 3 seconds
    if (millis() - lastUpdateTime >= 300)
    { // Update every 300ms (3 seconds / 10 steps)
        if (telnetClient && telnetClient.connected())
        {
            telnetClient.print("last ");
            telnetClient.println(lastSetIntensity);
        }
        if (telnetClient && telnetClient.connected())
        {
            telnetClient.print("target");
            telnetClient.println(targetBrightness);
        }
        if (telnetClient && telnetClient.connected())
        {
            telnetClient.print("avg ");
            telnetClient.println(lastSetIntensity);
        }
        if (abs(targetBrightness - lastSetIntensity) > 0)
        {
            if (targetBrightness > lastSetIntensity)
            {
                lastSetIntensity++;
            }
            else if (targetBrightness < lastSetIntensity)
            {
                lastSetIntensity--;
            }

            myDisplay.setIntensity(lastSetIntensity);
            timeDisplay.setIntensity(lastSetIntensity);
        }
        lastUpdateTime = millis();
    }
}

#define MAX_COMMAND_LENGTH 31

// Helper function to check if a specific feature bit is enabled
bool isFeatureEnabled(uint8_t bitPosition)
{
    if (bitPosition >= MAX_COMMAND_LENGTH)
        return false;
    return (systemCommandConfig.command[bitPosition] == '1');
}

void setup()
{
    // Initialize Serial Monitor
    Serial.begin(9600);
    printBoth("DHT22 and MAX7219 Display");

    // Initialize MAX7219 display instances separately
    // Setup display for system messages (uses default font)
    setupDisplay.begin();
    setupDisplay.setIntensity(0);
    setupDisplay.setFont(newFont);
    setupDisplay.displayClear();

    // Main display for clock (uses custom font)
    myDisplay.begin();
    myDisplay.setIntensity(0);
    myDisplay.setFont(newFont);
    myDisplay.displayClear();

    // Apply vertical flip to the main display
    // You can now check for specific features using isFeatureEnabled()
    // For example, if bit 0 controls display flipping:

    // Initialize dedicated time display
    timeDisplay.begin();
    timeDisplay.setIntensity(0);
    timeDisplay.setFont(newFont);
    timeDisplay.displayClear();

    // Apply vertical flip to the time display if needed
    // Uncomment the next 3 lines if you want the time display flipped too
    // for (uint8_t i = 0; i < MAX_DEVICES; i++) {
    //     timeDisplay.getZoneDevice(0)->getGraphicDevice()->setTransform(MD_MAX72XX::TFUD);
    // }

    // Initialize SPIFFS
    if (!LittleFS.begin())
    {
        Serial.println("Failed to mount SPIFFS - Formatting filesystem...");
        if (LittleFS.format())
        {
            if (!LittleFS.begin())
            {
                Serial.println("Fatal: SPIFFS mount failed after formatting!");
                while (1)
                    delay(1000); // Halt
            }
            Serial.println("SPIFFS formatted successfully");
        }
        else
        {
            Serial.println("Fatal: SPIFFS format failed!");
            while (1)
                delay(1000); // Halt
        }
    }

    // Check for reset button press
    // checkResetButton();

    // Display message if WiFi is not connected and AP mode is starting
    displaySetupMessage("Connecting to wifi...");

    // Connect to Wi-Fi
    setupWiFi();


    // Initialize Telnet server
    setupTelnet();

    // Setup OTA
    ArduinoOTA.setHostname(deviceConfig.hostname);
    ArduinoOTA.onStart([]()
                       {
        setupDisplay.displayClear();
        setupDisplay.displayText("OTA", PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
        setupDisplay.displayAnimate(); });
    ArduinoOTA.onEnd([]()
                     {
        setupDisplay.displayText("Done", PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
        setupDisplay.displayAnimate(); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                          {
        static int lastShownPercentage = 0;
        int currentPercentage = (progress / (total / 100));
        
        // Only update display if percentage changed by 5% or more
        if (currentPercentage >= lastShownPercentage + 1 || currentPercentage == 100) {
            char progressMessage[5];
            snprintf(progressMessage, sizeof(progressMessage), "%u%%", currentPercentage);
            setupDisplay.displayText(progressMessage, PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
            setupDisplay.displayAnimate(); // Single call without waiting
            lastShownPercentage = currentPercentage;
        } });
    ArduinoOTA.onError([](ota_error_t error)
                       {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) displaySetupMessage("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) displaySetupMessage("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) displaySetupMessage("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) displaySetupMessage("Receive Failed");
        else if (error == OTA_END_ERROR) displaySetupMessage("End Failed"); });
    ArduinoOTA.begin();
    printBoth("OTA initialized");
    // displaySetupMessage("OTA ready");

    printBoth("Connected to WiFi");
    printBoth("WiFi IP Address: " + WiFi.localIP().toString());
    // displaySetupMessage(WiFi.localIP().toString().c_str());

    // Initialize DHT22 sensor
    dht.begin();

    // Load all configurations
    loadMQTTConfig();
    loadTimeConfig();
    loadDisplayConfig();
    loadDeviceConfig();
    loadSystemCommandConfig(); // Load system command configuration
    // Load firmware configuration
    loadFirmwareConfig();
    // Print system command status (only to Serial/Telnet, not display)
    printBoth("System command configuration loaded");

    updateDisplaySequence();

    setupTime();
    setupWebServer();
    if (WiFi.status() == WL_CONNECTED)
    {

    // Send the IP address to ntfy.sh with the device's MAC address
    String macAddress = WiFi.macAddress();
    macAddress.replace(":", ""); // Remove colons from MAC address
    String ntfyUrl = "http://ntfy.sh/" + macAddress;
    String message = String(deviceConfig.hostname) + " connected as IP: " + WiFi.localIP().toString();
    
    static WiFiClient wifiClient;  // Make it static so it persists
    HTTPClient http;
    if (http.begin(wifiClient, ntfyUrl)) {  // Check if begin was successful
        http.addHeader("Content-Type", "text/plain");
        int httpResponseCode = http.POST(message);
        if (httpResponseCode > 0) {
            Serial.printf("Message sent to ntfy.sh with response code: %d\n", httpResponseCode);
        } else {
            Serial.printf("Failed to send message to ntfy.sh. Error: %s\n", http.errorToString(httpResponseCode).c_str());
        }
        http.end();
    } else {
        Serial.println("Failed to begin HTTP client");
    }
        setupMQTT();
}
    else
    {
        printBoth("WiFi not connected. Skipping MQTT setup.");
    }
    // displaySetupMessage("Setup complete");

    pinMode(LDR_PIN, INPUT);
    if (isFeatureEnabled(0))
    {
        // Enable display flipping

        for (uint8_t i = 0; i < MAX_DEVICES; i++)
        {
            myDisplay.setZoneEffect(0, true, PA_FLIP_UD);
            myDisplay.setZoneEffect(0, true, PA_FLIP_LR);
        }
    }

    // lastSetIntensity=-1;
}

void loop()
{
    ArduinoOTA.handle();   // Handle OTA updates
    MDNS.update();         // Handle mDNS updates
    server.handleClient(); // Handle web server requests
    handleTelnet();        // Handle telnet connections
    syncTimeIfNeeded();

    // Reconnect MQTT if needed
    if (!mqttClient.connected())
    {
        reconnectMQTT();
    }
    mqttClient.loop();

    static unsigned long lastReadTime = 0;
    static unsigned long lastTimeUpdate = 0;
    static float lastTemp = 0;
    static float lastHumidity = 0;
    unsigned long currentMillis = millis();

    // Add brightness control
    if (currentMillis - lastBrightnessCheck >= BRIGHTNESS_CHECK_INTERVAL)
    {
        updateBrightness();
        lastBrightnessCheck = currentMillis;
    }
    // Update time display every second
    if (currentMillis - lastTimeUpdate >= 1000)
    {
        time_t now = time(nullptr);
        struct tm *timeinfo = localtime(&now);
        char timeStr[10];
        if (displayConfig.use_24h_format)
        {
            strftime(timeStr, sizeof(timeStr), "%H:%M", timeinfo);
        }
        else
        {
            strftime(timeStr, sizeof(timeStr), "%I:%M", timeinfo);
            if (timeStr[0] == '0')
                timeStr[0] = ' '; // Remove leading zero
            // Add A or P for AM/PM
            char ampm = (timeinfo->tm_hour < 12) ? 'A' : 'P';
            size_t len = strlen(timeStr);
            timeStr[len] = ' ';
            timeStr[len + 1] = ampm;
            timeStr[len + 2] = '\0';
        }
        timeDisplay.displayText(timeStr, PA_CENTER, 25, 0, PA_NO_EFFECT, PA_NO_EFFECT);

        while (!timeDisplay.displayAnimate())
        {
            delay(10);
        }
        lastTimeUpdate = currentMillis;
    }

    // Read DHT sensor every 2 seconds
    if (currentMillis - lastReadTime >= 2000)
    {
        float humidity = dht.readHumidity();
        float temperature = dht.readTemperature(!displayConfig.use_celsius); // true = Fahrenheit

        if (!isnan(humidity) && !isnan(temperature))
        {
            // Apply calibration adjustments
            temperature += displayConfig.temp_delta;
            humidity += displayConfig.humidity_delta;

            // Constrain humidity to valid range (0-100%)
            humidity = constrain(humidity, 0.0, 100.0);

            lastTemp = temperature;
            lastHumidity = humidity;
            publishMQTTData(temperature, humidity);
        }
        lastReadTime = currentMillis;
    }

    // Update display based on sequence
    if (currentMillis - lastDisplayChange >= (displayDurations[currentDisplay] * 1000))
    {
        // Check WiFi connectivity and display CUT WiFi sign if disconnected

        if (numDisplays > 0)
        { // Only change display if we have items to show
            currentDisplay = (currentDisplay + 1) % numDisplays;

            // Skip displays with duration set to 0
            if (displayDurations[currentDisplay] == 0)
            {
                currentDisplay = (currentDisplay + 1) % numDisplays;
                return;
            }

            lastDisplayChange = currentMillis;

            myDisplay.displayClear();
            updateDisplaySequence();
            switch (displaySequence[currentDisplay])
            {
            case 1:
            { // Date
                time_t now = time(nullptr);
                struct tm *timeinfo = localtime(&now);
                char dateStr[10];
                strftime(dateStr, sizeof(dateStr), "%b %d", timeinfo);
                // Convert month to uppercase
                if (dateStr[0] != '\0')
                {
                    dateStr[0] = toupper(dateStr[0]);
                }

                myDisplay.displayText(dateStr, PA_CENTER, 25, 0, PA_NO_EFFECT, PA_NO_EFFECT);
                while (!myDisplay.displayAnimate())
                {
                    delay(10);
                }
                break;
            }
            case 2:
            { // Temperature
                char tempStr[9];
                snprintf(tempStr, sizeof(tempStr), "%.1f%c", lastTemp, displayConfig.use_celsius ? 'C' : 'F');
                myDisplay.displayText(tempStr, PA_CENTER, 25, 0, PA_NO_EFFECT, PA_NO_EFFECT);
                while (!myDisplay.displayAnimate())
                {
                    delay(10);
                }
                break;
            }
            case 3:
            { // Humidity
                char humStr[9];
                snprintf(humStr, sizeof(humStr), "%.1f%%", lastHumidity);
                myDisplay.displayText(humStr, PA_CENTER, 25, 0, PA_NO_EFFECT, PA_NO_EFFECT);
                while (!myDisplay.displayAnimate())
                {
                    delay(10);
                }

                break;
            }
            }
        }
    }

    // Add logic to show WiFi disconnected message every 30 seconds
    static unsigned long lastWiFiCheckTime = 0;
    if (millis() - lastWiFiCheckTime >= 30000)
    { // Check every 10 seconds
        if (WiFi.status() != WL_CONNECTED)
        {
            delay(2000); // Wait for 2 seconds before showing the message
            myDisplay.displayClear();
            myDisplay.displayText("WIFI X", PA_CENTER, 25, 0, PA_NO_EFFECT, PA_NO_EFFECT);
            while (!myDisplay.displayAnimate())
            {
                delay(10);
            }
        }
        lastWiFiCheckTime = millis(); // Update the last check time
    }

    // Add logic to check unableToSetTime and call retryWiFiSetup every 1 minute
    static unsigned long lastTimeCheck = 0;
    if (millis() - lastTimeCheck >= 600000)
    { // Check every 1 minute
        if (unableToSetTime)
        {
//check if date is 01-01-2024
            time_t now = time(nullptr);
            struct tm *timeinfo = localtime(&now);
            if (timeinfo->tm_year == 124 && timeinfo->tm_mon == 0 && timeinfo->tm_mday == 1)
            {
                //reset esp
                ESP.restart();

            }
        }
        lastTimeCheck = millis(); // Update the last check time
    }
}