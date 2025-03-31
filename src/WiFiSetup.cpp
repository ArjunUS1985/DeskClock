#include "WiFiSetup.h"
#include <PubSubClient.h>
#include <ESP8266mDNS.h>

// Define the global configs
MQTTConfig mqttConfig;
TimeConfig timeConfig;
DisplayConfig displayConfig;
DeviceConfig deviceConfig;  // Add DeviceConfig variable

// Declare the mqttCallback function
void mqttCallback(char* topic, byte* payload, unsigned int length);

ESP8266WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Initialize Telnet server and client
WiFiServer telnetServer(23);
WiFiClient telnetClient;

void setupWiFi() {
    WiFiManager wifiManager;
    bool wifiConnected = false;
    
    // Configure WiFiManager callbacks to handle display messages
    wifiManager.setAPCallback([](WiFiManager* mgr) {
        printBoth("Entered config mode");
        String apIP = WiFi.softAPIP().toString();
        printBoth("AP IP address: " + apIP);
        
        // Display the AP name and IP on the LED display
        displaySetupMessage("Join: SmartClock-AP");
        delay(2000);
        displaySetupMessage(("IP: " + apIP).c_str());
        delay(2000);
        displaySetupMessage("To configure");
    });
    
    // Set custom AP mode timeout
    wifiManager.setConfigPortalTimeout(300); // 5 minutes timeout for better user experience
    
    // Try to connect using saved credentials
    if (wifiManager.autoConnect("SmartClock-AP")) {
        // If we get here, we're connected
        printBoth("Connected to WiFi");
        displaySetupMessage("WiFi Connected!");
        wifiConnected = true;
    } else {
        printBoth("Failed to connect to WiFi or timeout reached");
        
        // Enter configuration portal explicitly but with a shorter timeout
        if (wifiManager.startConfigPortal("SmartClock-AP")) {
            printBoth("WiFi configured through portal");
            displaySetupMessage("WiFi Configured!");
            wifiConnected = true;
        } else {
            printBoth("Failed to configure WiFi, continuing without WiFi");
            displaySetupMessage("No WiFi");
            // We don't restart here - just continue without WiFi
        }
    }
    
    delay(1000); // Give time for the message to be displayed
    
    if (wifiConnected) {
        // Additional setup that depends on WiFi can go here
        String localIP = WiFi.localIP().toString();
        printBoth("IP: " + localIP);
        
        // Display IP address on the LED display
       // displaySetupMessage("IP:" + localIP);
        //delay(2000);
        
        // Set up mDNS responder with the hostname
        loadDeviceConfig(); // Make sure hostname is loaded
        if (MDNS.begin(deviceConfig.hostname)) {
            // Add service to mDNS
            MDNS.addService("http", "tcp", 80);  // Web server on port 80
            MDNS.addService("telnet", "tcp", 23); // Telnet on port 23
            printBothf("mDNS responder started: %s.local", deviceConfig.hostname);
            
            // Show the hostname.local address
            displaySetupMessage((String(deviceConfig.hostname) + ".local").c_str());
            delay(2000);
        } else {
            printBoth("Error setting up mDNS responder");
        }
    } else {
        // Set a flag or take actions for offline mode
        printBoth("Running in offline mode");
    }
    
    // Final message to indicate we're done with setup
    displaySetupMessage("Ready!");
}

void resetWiFiSettings() {
    WiFi.disconnect(true); // Disconnect from Wi-Fi
    ESP.eraseConfig(); // Erase all Wi-Fi and network-related settings
    
    // Clear all configuration files
    if (LittleFS.begin()) {
        // Remove all configuration JSON files
        if (LittleFS.exists("/mqtt_config.json")) {
            LittleFS.remove("/mqtt_config.json");
            printBoth("MQTT configuration cleared");
        }
        
        if (LittleFS.exists("/time_config.json")) {
            LittleFS.remove("/time_config.json");
            printBoth("Time configuration cleared");
        }
        
        if (LittleFS.exists("/display_config.json")) {
            LittleFS.remove("/display_config.json");
            printBoth("Display configuration cleared");
        }
        
        if (LittleFS.exists("/device_config.json")) {
            LittleFS.remove("/device_config.json");
            printBoth("Device configuration cleared");
        }
        
        // Also remove any HTML template files that might have been created
        if (LittleFS.exists("/head.html")) {
            LittleFS.remove("/head.html");
        }
        
        if (LittleFS.exists("/config.html")) {
            LittleFS.remove("/config.html");
        }
        
        if (LittleFS.exists("/error.html")) {
            LittleFS.remove("/error.html");
        }
        
        if (LittleFS.exists("/success.html")) {
            LittleFS.remove("/success.html");
        }
        
        LittleFS.end();
    }
    
    printBoth("All settings erased. Restarting...");
    displaySetupMessage("System Reset.. Restarting..."); // Display message before restart
    delay(1000);
    ESP.restart(); // Restart the ESP8266 to re-enter AP mode
}

void setDefaultMQTTConfig() {
    memset(&mqttConfig, 0, sizeof(MQTTConfig));
}

void setDefaultTimeConfig() {
    timeConfig = TimeConfig(); // This will use the constructor's default values
}

void setDefaultDisplayConfig() {
    displayConfig = DisplayConfig(); // This will use constructor's default values
}

void setDefaultDeviceConfig() {
    deviceConfig = DeviceConfig(); // Use the constructor's default values ("DeskClock")
}

void loadMQTTConfig() {
    if (!LittleFS.begin()) {
        printBoth("Failed to mount file system");
        setDefaultMQTTConfig();
        return;
    }

    if (!LittleFS.exists("/mqtt_config.json")) {
        printBoth("No MQTT config file found");
        setDefaultMQTTConfig();
        return;
    }

    File configFile = LittleFS.open("/mqtt_config.json", "r");
    if (!configFile) {
        printBoth("Failed to open MQTT config file");
        setDefaultMQTTConfig();
        return;
    }

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error) {
        printBoth("Failed to parse config file");
        setDefaultMQTTConfig();
        return;
    }

    if (doc.containsKey("server") && doc.containsKey("port")) {
        strncpy(mqttConfig.mqtt_server, doc["server"], sizeof(mqttConfig.mqtt_server) - 1);
        mqttConfig.mqtt_port = doc["port"].as<int>();
        if (doc.containsKey("user")) {
            strncpy(mqttConfig.mqtt_user, doc["user"], sizeof(mqttConfig.mqtt_user) - 1);
        }
        if (doc.containsKey("password")) {
            strncpy(mqttConfig.mqtt_password, doc["password"], sizeof(mqttConfig.mqtt_password) - 1);
        }
    } else {
        setDefaultMQTTConfig();
    }
}

void loadTimeConfig() {
    if (!LittleFS.begin()) {
        printBoth("Failed to mount file system");
        setDefaultTimeConfig();
        return;
    }

    if (!LittleFS.exists("/time_config.json")) {
        printBoth("No time config file found");
        setDefaultTimeConfig();
        return;
    }

    File configFile = LittleFS.open("/time_config.json", "r");
    if (!configFile) {
        printBoth("Failed to open time config file");
        setDefaultTimeConfig();
        return;
    }

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error) {
        printBoth("Failed to parse time config file");
        setDefaultTimeConfig();
        return;
    }

    if (doc.containsKey("timezone_offset") && doc.containsKey("timezone_name")) {
        timeConfig.timezone_offset = doc["timezone_offset"].as<int32_t>();
        strncpy(timeConfig.timezone_name, doc["timezone_name"], sizeof(timeConfig.timezone_name) - 1);
    } else {
        setDefaultTimeConfig();
    }
}

void loadDisplayConfig() {
    if (!LittleFS.begin()) {
        printBoth("Failed to mount file system");
        setDefaultDisplayConfig();
        return;
    }

    if (!LittleFS.exists("/display_config.json")) {
        printBoth("No display config file found");
        setDefaultDisplayConfig();
        return;
    }

    File configFile = LittleFS.open("/display_config.json", "r");
    if (!configFile) {
        printBoth("Failed to open display config file");
        setDefaultDisplayConfig();
        return;
    }

    StaticJsonDocument<400> doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error) {
        printBoth("Failed to parse display config file");
        setDefaultDisplayConfig();
        return;
    }

    displayConfig.use_24h_format = doc["use_24h_format"] | false;
    displayConfig.use_celsius = doc["use_celsius"] | true;
    displayConfig.date_duration = doc["date_duration"] | 5;
    displayConfig.temp_duration = doc["temp_duration"] | 5;
    displayConfig.humidity_duration = doc["humidity_duration"] | 5;
    displayConfig.auto_brightness = doc["auto_brightness"] | false;
    displayConfig.min_brightness = doc["min_brightness"] | 0;
    displayConfig.max_brightness = doc["max_brightness"] | 15;
    displayConfig.man_brightness = doc["man_brightness"] | 8; // Default to 8 if not set

    printBothf("Display config loaded: 24h=%d, C=%d, dates=%d, temp=%d, hum=%d, auto=%d, min=%d, max=%d, man=%d", 
        displayConfig.use_24h_format, displayConfig.use_celsius, 
        displayConfig.date_duration, displayConfig.temp_duration, 
        displayConfig.humidity_duration, displayConfig.auto_brightness, 
        displayConfig.min_brightness, displayConfig.max_brightness,
        displayConfig.man_brightness);
}

void loadDeviceConfig() {
    if (!LittleFS.begin()) {
        printBoth("Failed to mount file system");
        setDefaultDeviceConfig();
        return;
    }

    if (!LittleFS.exists("/device_config.json")) {
        printBoth("No device config file found");
        setDefaultDeviceConfig();
        return;
    }

    File configFile = LittleFS.open("/device_config.json", "r");
    if (!configFile) {
        printBoth("Failed to open device config file");
        setDefaultDeviceConfig();
        return;
    }

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error) {
        printBoth("Failed to parse device config file");
        setDefaultDeviceConfig();
        return;
    }

    if (doc.containsKey("hostname")) {
        strncpy(deviceConfig.hostname, doc["hostname"], sizeof(deviceConfig.hostname) - 1);
        printBothf("Loaded hostname: %s", deviceConfig.hostname);
    } else {
        setDefaultDeviceConfig();
    }
}

void saveMQTTConfig() {
    if (!LittleFS.begin()) {
        printBoth("Failed to mount file system");
        return;
    }

    StaticJsonDocument<200> doc;
    doc["server"] = mqttConfig.mqtt_server;
    doc["port"] = mqttConfig.mqtt_port;
    doc["user"] = mqttConfig.mqtt_user;
    doc["password"] = mqttConfig.mqtt_password;

    File configFile = LittleFS.open("/mqtt_config.json", "w");
    if (!configFile) {
        printBoth("Failed to open config file for writing");
        return;
    }

    if (serializeJson(doc, configFile) == 0) {
        printBoth("Failed to write config file");
    }
    configFile.close();
}

void saveTimeConfig() {
    if (!LittleFS.begin()) {
        printBoth("Failed to mount file system");
        return;
    }

    StaticJsonDocument<200> doc;
    doc["timezone_offset"] = timeConfig.timezone_offset;
    doc["timezone_name"] = timeConfig.timezone_name;

    File configFile = LittleFS.open("/time_config.json", "w");
    if (!configFile) {
        printBoth("Failed to open time config file for writing");
        return;
    }

    if (serializeJson(doc, configFile) == 0) {
        printBoth("Failed to write time config file");
    }
    configFile.close();
  
}

void saveDisplayConfig() {
    if (!LittleFS.begin()) {
        printBoth("Failed to mount file system");
        return;
    }

    StaticJsonDocument<400> doc;
    doc["use_24h_format"] = displayConfig.use_24h_format;
    doc["use_celsius"] = displayConfig.use_celsius;
    doc["date_duration"] = displayConfig.date_duration;
    doc["temp_duration"] = displayConfig.temp_duration;
    doc["humidity_duration"] = displayConfig.humidity_duration;
    doc["auto_brightness"] = displayConfig.auto_brightness;
    doc["min_brightness"] = displayConfig.min_brightness;
    doc["max_brightness"] = displayConfig.max_brightness;
    doc["man_brightness"] = displayConfig.man_brightness;

    File configFile = LittleFS.open("/display_config.json", "w");
    if (!configFile) {
        printBoth("Failed to open display config file for writing");
        return;
    }

    if (serializeJson(doc, configFile) == 0) {
        printBoth("Failed to write display config file");
    }
    configFile.close();
    
    printBoth("Display config saved");
}

void saveDeviceConfig() {
    if (!LittleFS.begin()) {
        printBoth("Failed to mount file system");
        return;
    }

    StaticJsonDocument<200> doc;
    doc["hostname"] = deviceConfig.hostname;

    File configFile = LittleFS.open("/device_config.json", "w");
    if (!configFile) {
        printBoth("Failed to open device config file for writing");
        return;
    }

    if (serializeJson(doc, configFile) == 0) {
        printBoth("Failed to write device config file");
    }
    configFile.close();
    
    printBothf("Device config saved - hostname: %s", deviceConfig.hostname);
    //restart esp 
    ESP.restart();
}

void handleRoot() {
    // Add this debug message before generating the page
    printBothf("Loading config page - auto_brightness is currently: %s", displayConfig.auto_brightness ? "ON" : "OFF");
    
    String page = R"(
<!DOCTYPE html>
<html>
<head>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; max-width: 600px; margin: 0 auto; }
        h1, h2 { color: #333; }
        form { background: #f5f5f5; padding: 20px; border-radius: 8px; margin: 15px 0; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; }
        input[type='text'], input[type='number'], input[type='password'] { 
            width: 100%; padding: 8px; font-size: 16px; border: 1px solid #ddd; 
            border-radius: 4px; box-sizing: border-box; 
        }
        input[type='submit'], input[type='button'] { 
            background: #007bff; color: white; border: none; padding: 10px 20px; 
            font-size: 16px; border-radius: 4px; cursor: pointer; width: 100%; 
            margin-bottom: 10px;
        }
        input[type='submit']:hover, input[type='button']:hover { background: #0056b3; }
        input[type='button'] { background: #17a2b8; }
        input[type='button']:hover { background: #138496; }
        .status { 
            background: #e8f4fd; padding: 10px; border-radius: 4px; margin-bottom: 20px; 
        }
        .reset-btn { background: #dc3545; }
        .reset-btn:hover { background: #c82333; }
        .footer { 
            margin-top: 30px; padding: 20px; background: #f8f9fa; 
            border-radius: 4px; text-align: center; 
        }
        .footer p { margin: 5px 0; color: #666; }
    </style>
    <script>
        function setCurrentDateTime() {
            const now = new Date();
            document.getElementById('year').value = now.getFullYear();
            document.getElementById('month').value = now.getMonth() + 1;
            document.getElementById('day').value = now.getDate();
            let hours = now.getHours();
            const ampm = hours >= 12 ? 'P' : 'A';
            hours = hours % 12;
            hours = hours ? hours : 12;
            document.getElementById('hour').value = hours;
            document.getElementById('minute').value = now.getMinutes();
            
            // Improved AM/PM selection to work across all browsers
            const ampmSelect = document.getElementById('ampm');
            for(let i = 0; i < ampmSelect.options.length; i++) {
                if(ampmSelect.options[i].value === ampm) {
                    ampmSelect.selectedIndex = i;
                    break;
                }
            }
        }
    </script>
</head>
<body>
    <h1>Device Configuration</h1>
    <div class='status'>IP Address: )" + WiFi.localIP().toString() + R"(</div>
    
    <!-- Device Settings -->
    <form action='/save' method='POST'>
        <h2>Device Settings</h2>
        <div class='form-group'>
            <label for='hostname'>Hostname:</label>
            <input type='text' id='hostname' name='hostname' value=')" + String(deviceConfig.hostname) + R"(' maxlength='31'>
            <small style="display: block; margin-top: 5px; color: #666;">The hostname is used to identify this device on your network (used for MQTT and OTA)</small>
        </div>
        <input type='submit' value='Save Device Settings'>
    </form>
    
    <!-- Display Settings -->
    <form action='/save' method='POST'>
        <h2>Display Settings</h2>
        <div class='form-group'>
            <label>Time Format:</label><br>
            <input type='radio' id='ampm' name='time_format' value='ampm' )" + (!displayConfig.use_24h_format ? "checked" : "") + R"(>
            <label for='ampm'>AM/PM</label>
            <input type='radio' id='24h' name='time_format' value='24h' )" + (displayConfig.use_24h_format ? "checked" : "") + R"(>
            <label for='24h'>24-hour</label>
        </div>
        
        <div class='form-group'>
            <label>Temperature Format:</label><br>
            <input type='radio' id='celsius' name='temp_format' value='C' )" + (displayConfig.use_celsius ? "checked" : "") + R"(>
            <label for='celsius'>Celsius</label>
            <input type='radio' id='fahrenheit' name='temp_format' value='F' )" + (!displayConfig.use_celsius ? "checked" : "") + R"(>
            <label for='fahrenheit'>Fahrenheit</label>
        </div>
        
        <div class='form-group'>
            <label>Display Durations (0-60 seconds, 0 = don't show):</label><br>
            <label for='date_duration'>Date:</label>
            <input type='number' id='date_duration' name='date_duration' min='0' max='60' value=')" + String(displayConfig.date_duration) + R"('> seconds<br>
            <label for='temp_duration'>Temperature:</label>
            <input type='number' id='temp_duration' name='temp_duration' min='0' max='60' value=')" + String(displayConfig.temp_duration) + R"('> seconds<br>
            <label for='humidity_duration'>Humidity:</label>
            <input type='number' id='humidity_duration' name='humidity_duration' min='0' max='60' value=')" + String(displayConfig.humidity_duration) + R"('> seconds
        </div>
        <div class='form-group'>
            <label>Brightness Control:</label><br>
            <input type='checkbox' id='auto_brightness' name='auto_brightness' value='1' )" + (displayConfig.auto_brightness ? "checked" : "") + R"(>
            <label for='auto_brightness'>Enable Auto Brightness</label><br>
            <div id='manual_brightness' style='margin-left: 20px; margin-top: 10px;'>
                <label for='manual_brightness_value'>Manual Brightness (0-15):</label>
                <input type='number' id='manual_brightness_value' name='manual_brightness_value' min='0' max='15' value=')" + String(displayConfig.man_brightness) + R"(' )" + (displayConfig.auto_brightness ? "disabled" : "") + R"(><br>
            </div>
            <div id='auto_brightness_controls' style='margin-left: 20px; margin-top: 10px;'>
                <label for='min_brightness'>Minimum Brightness (0-15):</label>
                <input type='number' id='min_brightness' name='min_brightness' min='0' max='15' value=')" + String(displayConfig.min_brightness) + R"(' )" + (displayConfig.auto_brightness ? "" : "disabled") + R"(><br>
                <label for='max_brightness'>Maximum Brightness (0-15):</label>
                <input type='number' id='max_brightness' name='max_brightness' min='0' max='15' value=')" + String(displayConfig.max_brightness) + R"(' )" + (displayConfig.auto_brightness ? "" : "disabled") + R"(><br>
            </div>
        </div>

        <script>
            document.addEventListener('DOMContentLoaded', function() {
                const autoBrightnessCheckbox = document.getElementById('auto_brightness');
                const minBrightness = document.getElementById('min_brightness');
                const maxBrightness = document.getElementById('max_brightness');
                const manualBrightness = document.getElementById('manual_brightness_value');
                const autoBrightnessControls = document.getElementById('auto_brightness_controls');
                
                // Set initial state
                minBrightness.disabled = !autoBrightnessCheckbox.checked;
                maxBrightness.disabled = !autoBrightnessCheckbox.checked;
                manualBrightness.disabled = autoBrightnessCheckbox.checked;
                
                // Add change event listener
                autoBrightnessCheckbox.addEventListener('change', function() {
                    minBrightness.disabled = !this.checked;
                    maxBrightness.disabled = !this.checked;
                    manualBrightness.disabled = this.checked;
                });
            });
        </script>
        <input type='submit' value='Save Display Settings'>
    </form>

    <!-- Manual Time Setting -->
    <form action='/settime' method='POST'>
        <h2>Manual Time Setting</h2>
        <div class='form-group'>
            <label>Date and Time: [Will reset to auto on restart]</label><br>
            <input type='number' name='year' id='year' min='2000' max='2099' required style='width:70px'> Year 
            <input type='number' name='month' id='month' min='1' max='12' required style='width:50px'> Month 
            <input type='number' name='day' id='day' min='1' max='31' required style='width:50px'> Day<br>
            <input type='number' name='hour' id='hour' min='1' max='12' required style='width:50px'> Hour 
            <input type='number' name='minute' id='minute' min='0' max='59' required style='width:50px'> Minute 
            <select name='ampm' id='ampm' style='width:60px'>
                <option value='A'>AM</option>
                <option value='P'>PM</option>
            </select>
        </div>
        <input type='button' value='Set to Browser Time' onclick='setCurrentDateTime()'>
        <input type='submit' value='Set Time Manually'>
    </form>

    <!-- Timezone Settings -->
    <form action='/save' method='POST'>
        <h2>Timezone Settings</h2>
        <div class='form-group'>
            <label for='timezone'>Timezone:</label>
            <select id='timezone' name='timezone'>
                <option value='19800,IST' )" + (timeConfig.timezone_offset == 19800 ? "selected" : "") + R"(>India (IST)</option>
                <option value='28800,CST' )" + (timeConfig.timezone_offset == 28800 ? "selected" : "") + R"(>China (CST)</option>
                <option value='32400,JST' )" + (timeConfig.timezone_offset == 32400 ? "selected" : "") + R"(>Japan (JST)</option>
                <option value='0,GMT' )" + (timeConfig.timezone_offset == 0 ? "selected" : "") + R"(>GMT</option>
                <option value='-18000,EST' )" + (timeConfig.timezone_offset == -18000 ? "selected" : "") + R"(>US Eastern (EST)</option>
                <option value='-28800,PST' )" + (timeConfig.timezone_offset == -28800 ? "selected" : "") + R"(>US Pacific (PST)</option>
            </select>
        </div>
        <input type='submit' value='Save Settings'>
    </form>

    <!-- MQTT Settings -->
    <form action='/save' method='POST'>
        <h2>MQTT Settings</h2>
        <div class='form-group'>
            <label for='mqtt_server'>Server:</label>
            <input type='text' id='mqtt_server' name='mqtt_server' value=')" + String(mqttConfig.mqtt_server) + R"('>
        </div>
        <div class='form-group'>
            <label for='mqtt_port'>Port:</label>
            <input type='number' id='mqtt_port' name='mqtt_port' value=')" + String(mqttConfig.mqtt_port) + R"('>
        </div>
        <div class='form-group'>
            <label for='mqtt_user'>Username:</label>
            <input type='text' id='mqtt_user' name='mqtt_user' value=')" + String(mqttConfig.mqtt_user) + R"('>
        </div>
        <div class='form-group'>
            <label for='mqtt_password'>Password:</label>
            <input type='password' id='mqtt_password' name='mqtt_password' value=')" + String(mqttConfig.mqtt_password) + R"('>
        </div>
        <input type='submit' value='Save MQTT Settings'>
    </form>

    <!-- Reset Form -->
    <form action='/reset' method='POST'>
        <input type='submit' value='Reset and Restart' class='reset-btn'>
    </form>
    
    <div class='footer'>
        <p>Designed by: Arjun Bhattacharjee (mymail.arjun@gmail.com)</p>
        <p>System Storage Remaining: )" + String((ESP.getFlashChipSize() - ESP.getSketchSize()) / (1024.0 * 1024.0), 2) + R"( MB</p>
    </div>
</body>
</html>)";

    server.send(200, "text/html", page);
}

void handleSave() {
    bool configChanged = false;
    bool displayChanged = false;
    bool mqttChanged = false;
    bool deviceChanged = false;
    
    // Handle device settings
    if (server.hasArg("hostname")) {
        // Make sure the hostname isn't empty and doesn't exceed limit
        String hostname = server.arg("hostname");
        if (hostname.length() > 0) {
            strncpy(deviceConfig.hostname, hostname.c_str(), sizeof(deviceConfig.hostname) - 1);
            deviceChanged = true;
            printBothf("Hostname changed to: %s", deviceConfig.hostname);
        }
    }
    
    if (deviceChanged) {
        saveDeviceConfig();
        configChanged = true;
    }
    
    // Handle display settings
    if (server.hasArg("time_format")) {
        displayConfig.use_24h_format = (server.arg("time_format") == "24h");
        displayChanged = true;
    }
    if (server.hasArg("temp_format")) {
        displayConfig.use_celsius = (server.arg("temp_format") == "C");
        displayChanged = true;
    }
    if (server.hasArg("date_duration")) {
        displayConfig.date_duration = constrain(server.arg("date_duration").toInt(), 0, 60);
        displayChanged = true;
    }
    if (server.hasArg("temp_duration")) {
        displayConfig.temp_duration = constrain(server.arg("temp_duration").toInt(), 0, 60);
        displayChanged = true;
    }
    if (server.hasArg("humidity_duration")) {
        displayConfig.humidity_duration = constrain(server.arg("humidity_duration").toInt(), 0, 60);
        displayChanged = true;
    }
    
    // Explicitly set auto_brightness based on presence of argument
    // For checkboxes, the parameter is only sent when checked
    displayConfig.auto_brightness = server.hasArg("auto_brightness");
    displayChanged = true;
    
    if (displayConfig.auto_brightness) {
        if (server.hasArg("min_brightness") && server.hasArg("max_brightness")) {
            int minBrightness = constrain(server.arg("min_brightness").toInt(), 0, 15);
            int maxBrightness = constrain(server.arg("max_brightness").toInt(), 0, 15);
            if (minBrightness <= maxBrightness) {
                displayConfig.min_brightness = minBrightness;
                displayConfig.max_brightness = maxBrightness;
            } else {
                // If min > max, swap them
                displayConfig.min_brightness = maxBrightness;
                displayConfig.max_brightness = minBrightness;
            }
        }
    } else {
        if (server.hasArg("manual_brightness_value")) {
            displayConfig.man_brightness = constrain(server.arg("manual_brightness_value").toInt(), 0, 15);
        }
    }
    
    if (displayChanged) {
        saveDisplayConfig();
        configChanged = true;
        printBoth("Display settings saved");
        printBothf("Auto brightness: %s, Min: %d, Max: %d, Manual: %d", 
                  displayConfig.auto_brightness ? "ON" : "OFF",
                  displayConfig.min_brightness, 
                  displayConfig.max_brightness,
                  displayConfig.man_brightness);
    }

    // Handle timezone settings
    if (server.hasArg("timezone")) {
        String timezone = server.arg("timezone");
        int commaIndex = timezone.indexOf(',');
        if (commaIndex > 0) {
            timeConfig.timezone_offset = timezone.substring(0, commaIndex).toInt();
            strncpy(timeConfig.timezone_name, 
                   timezone.substring(commaIndex + 1).c_str(), 
                   sizeof(timeConfig.timezone_name) - 1);
            saveTimeConfig();
            configChanged = true;
        }
    }

    // Handle MQTT settings
    if (server.hasArg("mqtt_server")) {
        strncpy(mqttConfig.mqtt_server, server.arg("mqtt_server").c_str(), 40);
        mqttChanged = true;
    }
    if (server.hasArg("mqtt_port")) {
        mqttConfig.mqtt_port = server.arg("mqtt_port").toInt();
        mqttChanged = true;
    }
    if (server.hasArg("mqtt_user")) {
        strncpy(mqttConfig.mqtt_user, server.arg("mqtt_user").c_str(), 32);
        mqttChanged = true;
    }
    if (server.hasArg("mqtt_password")) {
        strncpy(mqttConfig.mqtt_password, server.arg("mqtt_password").c_str(), 32);
        mqttChanged = true;
    }

    if (mqttChanged) {
        saveMQTTConfig();
        configChanged = true;
        printBoth("MQTT settings saved");
        // Force MQTT reconnection with new settings
        if (mqttClient.connected()) {
            mqttClient.disconnect();
        }
        setupMQTT();
    }

    String page = R"(
<!DOCTYPE html>
<html>
<head>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; max-width: 600px; margin: 0 auto; }
        h1 { color: #333; }
        .status { background: #e8fff4; padding: 10px; border-radius: 4px; margin: 20px 0; color: #28a745; }
        .btn { display: inline-block; padding: 10px 20px; background: #007bff; color: white; 
               text-decoration: none; border-radius: 4px; margin-top: 20px; }
        .btn:hover { background: #0056b3; }
    </style>
</head>
<body>
    <h1>Settings Saved</h1>)"; 
    
    page += String("<div class='status'>") + 
            (configChanged ? "All settings have been updated successfully" : "No changes were made") + 
            String("</div>");
            
    page += R"(
    <a href='/' class='btn'>Go Back</a>
</body>
</html>)";

    server.send(200, "text/html", page);
}

void handleReset() {
    String page = R"(
<!DOCTYPE html>
<html>
<head>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; max-width: 600px; margin: 0 auto; }
        h1 { color: #333; }
        .status { background: #e8fff4; padding: 10px; border-radius: 4px; margin: 20px 0; color: #28a745; }
    </style>
</head>
<body>
    <h1>Resetting Device...</h1>
    <div class='status'>Device will restart in a few seconds.</div>
</body>
</html>)";

    server.send(200, "text/html", page);
    delay(1000);
    resetWiFiSettings();
}

void handleManualTimeSet() {
    if (server.hasArg("year") && server.hasArg("month") && server.hasArg("day") && 
        server.hasArg("hour") && server.hasArg("minute") && server.hasArg("ampm")) {
        
        int year = server.arg("year").toInt();
        int month = server.arg("month").toInt();
        int day = server.arg("day").toInt();
        int hour = server.arg("hour").toInt();
        int minute = server.arg("minute").toInt();
        String ampm = server.arg("ampm");
        
        // Convert 12-hour to 24-hour format
        if (ampm == "P" && hour < 12) hour += 12;
        if (ampm == "A" && hour == 12) hour = 0;
        
        setManualTime(year, month, day, hour, minute);
        
        String page = R"(
<!DOCTYPE html>
<html>
<head>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; max-width: 600px; margin: 0 auto; }
        h1 { color: #333; }
        .status { background: #e8fff4; padding: 10px; border-radius: 4px; margin: 20px 0; color: #28a745; }
        .btn { display: inline-block; padding: 10px 20px; background: #007bff; color: white; 
               text-decoration: none; border-radius: 4px; margin-top: 20px; }
        .btn:hover { background: #0056b3; }
    </style>
</head>
<body>
    <h1>Time Set</h1>
    <div class='status'>Time has been updated successfully</div>
    <a href='/' class='btn'>Go Back</a>
</body>
</html>)";
        server.send(200, "text/html", page);
    } else {
        String page = R"(
<!DOCTYPE html>
<html>
<head>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; max-width: 600px; margin: 0 auto; }
        h1 { color: #333; }
        .status { background: #ffe6e6; padding: 10px; border-radius: 4px; margin: 20px 0; color: #dc3545; }
        .btn { display: inline-block; padding: 10px 20px; background: #007bff; color: white; 
               text-decoration: none; border-radius: 4px; margin-top: 20px; }
        .btn:hover { background: #0056b3; }
    </style>
</head>
<body>
    <h1>Error</h1>
    <div class='status'>Missing required time parameters</div>
    <a href='/' class='btn'>Go Back</a>
</body>
</html>)";
        server.send(400, "text/html", page);
    }
}

void setManualTime(int year, int month, int day, int hour, int minute) {
    struct tm tm;
    tm.tm_year = year - 1900;  // Years since 1900
    tm.tm_mon = month - 1;     // Months since January
    tm.tm_mday = day;
    tm.tm_hour = hour;         // Already in 24-hour format
    tm.tm_min = minute;
    tm.tm_sec = 0;
    
    time_t t = mktime(&tm);
    struct timeval tv = { .tv_sec = t };
    settimeofday(&tv, nullptr);
    
    timeConfig.manual_time_set = true;
    timeConfig.last_manual_set = t;
    saveTimeConfig();
    
    // Show time in 12-hour format in the log
    int hour12 = hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char* ampm = hour < 12 ? "AM" : "PM";
    
    printBothf("Time manually set to: %04d-%02d-%02d %02d:%02d %s", 
              year, month, day, hour12, minute, ampm);
}

void setupWebServer() {
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/reset", HTTP_POST, handleReset);
    server.on("/settime", HTTP_POST, handleManualTimeSet);
    server.begin();
    printBoth("Web server started");
}

void setupMQTT() {
    loadMQTTConfig();
    
    if (mqttConfig.isEmpty()) {
        printBoth("No MQTT configuration found - MQTT disabled");
        return;
    }

    mqttClient.setServer(mqttConfig.mqtt_server, mqttConfig.mqtt_port);
    mqttClient.setCallback(mqttCallback);
    
    printBothf("Attempting to connect to MQTT broker as %s...", deviceConfig.hostname);
    if (mqttClient.connect(deviceConfig.hostname, mqttConfig.mqtt_user, mqttConfig.mqtt_password)) {
        printBoth("MQTT Connected Successfully");
        
        // Publish discovery configs for temperature sensor
        String tempConfig = "{\"name\":\"" + String(deviceConfig.hostname) + " Temperature\",\"device_class\":\"temperature\",\"state_topic\":\"homeassistant/sensor/" + String(deviceConfig.hostname) + "/temperature/state\",\"unit_of_measurement\":\"°C\",\"unique_id\":\"" + String(deviceConfig.hostname) + "_temp\"}";
        mqttClient.publish(("homeassistant/sensor/" + String(deviceConfig.hostname) + "/temperature/config").c_str(), tempConfig.c_str(), true);
        
        // Publish discovery configs for humidity sensor
        String humConfig = "{\"name\":\"" + String(deviceConfig.hostname) + " Humidity\",\"device_class\":\"humidity\",\"state_topic\":\"homeassistant/sensor/" + String(deviceConfig.hostname) + "/humidity/state\",\"unit_of_measurement\":\"%\",\"unique_id\":\"" + String(deviceConfig.hostname) + "_humidity\"}";
        mqttClient.publish(("homeassistant/sensor/" + String(deviceConfig.hostname) + "/humidity/config").c_str(), humConfig.c_str(), true);
        
        mqttClient.subscribe(("homeassistant/" + String(deviceConfig.hostname) + "/command").c_str());
    } else {
        int state = mqttClient.state();
        String errorMsg = "Initial MQTT connection failed, state: ";
        switch (state) {
            case -4: errorMsg += "MQTT_CONNECTION_TIMEOUT"; break;
            case -3: errorMsg += "MQTT_CONNECTION_LOST"; break;
            case -2: errorMsg += "MQTT_CONNECT_FAILED"; break;
            case -1: errorMsg += "MQTT_DISCONNECTED"; break;
            case 1: errorMsg += "MQTT_CONNECT_BAD_PROTOCOL"; break;
            case 2: errorMsg += "MQTT_CONNECT_BAD_CLIENT_ID"; break;
            case 3: errorMsg += "MQTT_CONNECT_UNAVAILABLE"; break;
            case 4: errorMsg += "MQTT_CONNECT_BAD_CREDENTIALS"; break;
            case 5: errorMsg += "MQTT_CONNECT_UNAUTHORIZED"; break;
            default: errorMsg += String(state);
        }
        printBoth(errorMsg);
        printBoth("Will retry in main loop");
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message = "Message arrived on topic: ";
    message += topic;
    printBoth(message);
    
    message = "Message: ";
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    printBoth(message);
}
 
void reconnectMQTT() {
    // Skip if MQTT is not configured or if server name is empty
    if (mqttConfig.isEmpty() || mqttConfig.mqtt_server[0] == '\0') {
        return;
    }

    // Check if we're already connected
    if (mqttClient.connected()) {
        return;  // Already connected, nothing to do
    }

    // Try to connect once (non-blocking approach)
    printBothf("Attempting MQTT connection as %s...", deviceConfig.hostname);
    if (mqttClient.connect(deviceConfig.hostname, mqttConfig.mqtt_user, mqttConfig.mqtt_password)) {
        printBoth("Connected to MQTT broker");
        mqttClient.subscribe(("homeassistant/" + String(deviceConfig.hostname) + "/command").c_str());
    } else {
        int state = mqttClient.state();
        String errorMsg = "Connection failed, state: ";
        switch (state) {
            case -4: errorMsg += "MQTT_CONNECTION_TIMEOUT"; break;
            case -3: errorMsg += "MQTT_CONNECTION_LOST"; break;
            case -2: errorMsg += "MQTT_CONNECT_FAILED"; break;
            case -1: errorMsg += "MQTT_DISCONNECTED"; break;
            case 1: errorMsg += "MQTT_CONNECT_BAD_PROTOCOL"; break;
            case 2: errorMsg += "MQTT_CONNECT_BAD_CLIENT_ID"; break;
            case 3: errorMsg += "MQTT_CONNECT_UNAVAILABLE"; break;
            case 4: errorMsg += "MQTT_CONNECT_BAD_CREDENTIALS"; break;
            case 5: errorMsg += "MQTT_CONNECT_UNAUTHORIZED"; break;
            default: errorMsg += String(state);
        }
        printBoth(errorMsg);
        printBoth("Will try again later");
        // No delay here - the function will return and the main loop will continue
    }
}

void publishMQTTData(float temperature, float humidity) {
    if (mqttConfig.isEmpty()) {
        return;  // Skip if MQTT is not configured
    }

    // Only try to reconnect once, don't loop
    if (!mqttClient.connected()) {
        printBoth("MQTT disconnected, attempting to reconnect...");
        if (mqttClient.connect(deviceConfig.hostname, mqttConfig.mqtt_user, mqttConfig.mqtt_password)) {
            printBoth("connected");
        } else {
            printBoth("failed");
            return; // Return if can't connect, don't block
        }
    }

    // Single MQTT loop call
    mqttClient.loop();

    // Publish temperature and humidity to state topics using the custom hostname
    char tempPayload[16];
    snprintf(tempPayload, sizeof(tempPayload), "%.1f", temperature);
    mqttClient.publish(("homeassistant/sensor/" + String(deviceConfig.hostname) + "/temperature/state").c_str(), tempPayload, true);

    char humPayload[16];
    snprintf(humPayload, sizeof(humPayload), "%.1f", humidity);
    mqttClient.publish(("homeassistant/sensor/" + String(deviceConfig.hostname) + "/humidity/state").c_str(), humPayload, true);
}

void setupTelnet() {
    telnetServer.begin();
    telnetServer.setNoDelay(true);
    printBoth("Telnet server started");
}

void handleTelnet() {
    if (telnetServer.hasClient()) {
        if (!telnetClient || !telnetClient.connected()) {
            if (telnetClient) telnetClient.stop();
            telnetClient = telnetServer.accept();
            telnetClient.println("Welcome to DeskClock Telnet Server");
        } else {
            telnetServer.accept().stop(); // Reject new client
        }
    }
}

void printBoth(const char* message) {
    if (telnetClient && telnetClient.connected()) {
        telnetClient.println(message);
    }
    Serial.println(message);
}

void printBoth(const String& message) {
    if (telnetClient && telnetClient.connected()) {
        telnetClient.println(message);
    }
    Serial.println(message);
}

// Add a printf-style printBoth
void printBothf(const char* format, ...) {
    char buf[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    printBoth(buf);
}