
String device_name = "overwinter1";

// #define ENABLE_SERIAL

#define ENABLE_BLYNK
#define BLYNK_KEY "YOUR_BLYNK_KEY_HERE"
#define ENABLE_PUB
#define PUB_EVENT "statsd"

// Relay
#include "NCD2Relay.h"
NCD2Relay relay;

// Light and proximity
#include "VCNL4010.h"
VCNL4010 vcnl4010;

// Temperature and Humidity
#include "SI7020_A20.h"
SI7020_A20 si7020;

// Blynk
#ifdef ENABLE_BLYNK
#include "BlynkSimpleParticle.h"
WidgetLED led1(V2);
WidgetLED led2(V3);
bool blynk_relayStates[2] = {0, 0};
int blynk_lightsOnSchedule = 0;
int blynk_lightsOffSchedule = 0;
uint8_t blynk_tempThreshold = 0;
int blynk_rssi = 0;
int blynk_light = 0;
#endif



// Value in EEPROM address 0 if the EEPROM has been "initialized"
#define EEPROM_CHECK 137


// Threshold values with general values
struct mySettingsStruct {
    // Temp threshold
    int16_t tempThreshold = 50;

    // Lights On Hour
    uint8_t lightsOnHour = 8;

    // Lights On Minute
    uint8_t lightsOnMinute = 0;

    // Lights Off Hour
    uint8_t lightsOffHour = 20;

    // Lights Off Minute
    uint8_t lightsOffMinute = 0;

    // Temperature toggle keepout
    // This will prevent the relay from clicking on/off too quickly
    uint8_t tempKeepoutSeconds = 900;

    // Time zone
    int timeZone = 0;
};

mySettingsStruct settings;


// Relay states
bool relayStates[2] = {0, 0};
int relay1 = 0;
int relay2 = 0;


// Keep track of time changes
uint8_t last_second = 61;
uint8_t last_minute = 61;


// Keep track of the temperature relay keepout
uint32_t tempKeepoutTS = 0;


// Global variables for metrics
double statTemperatureF = 0;
double statTemperatureC = 0;
double statHumidity = 0;
uint32_t statLight = 0;

bool enableVCNL4010 = false;
bool enableSI7020 = true;

bool eepromHasChanged = false;

bool forcePub = false;

int Wifi_RSSI = 0;


// Setup
void setup() {
#ifdef ENABLE_SERIAL
    Serial.begin(9600);
    delay(3000);
#endif

    // Initialize relays
    relay.setAddress(0, 0, 0);
    relay.turnOffAllRelays();   // Relays do not reset when Photon resets

    // Initialize VCNL4010
    enableVCNL4010 = vcnl4010.initialize();

#ifdef ENABLE_SERIAL
    if(!enableVCNL4010)
        Serial.println("!!! VCNL4010 initialization failed !!!");
#endif

#ifdef ENABLE_BLYNK
    // Start up Blynk
    Blynk.begin(BLYNK_KEY);
#endif

    // Make fnRouter available as cloud function
    Particle.function("function", fnRouter);

    // Expose some variables
    Particle.variable("tempf", &statTemperatureF, DOUBLE);
    Particle.variable("tempC", &statTemperatureC, DOUBLE);
    Particle.variable("humidity", &statHumidity, DOUBLE);
    Particle.variable("relay1", &relay1, INT);
    Particle.variable("relay2", &relay2, INT);
    Particle.variable("rssi", &Wifi_RSSI, INT);
    Particle.variable("light", &statLight, INT);

    // Load threshold values from EEPROM
    eepromLoad();

    // Get the time zone and sync the time
    Time.zone(settings.timeZone);
    Particle.syncTime();

    // Set the last minute to now so we don't publish immediately
    last_second = 61;
    last_minute = 61;
}


// Loop
void loop() {
    // Keep these updated at all times since they're exposed as cloud variables
    Wifi_RSSI = (int)WiFi.RSSI();
    relay1 = (int)relayStates[0];
    relay2 = (int)relayStates[1];

    // 2-second updates
    if(Time.second()%2==0 && Time.second()!=last_second) {
        // Update the latest metrics
        statTemperatureF = si7020.temperatureF();
        statTemperatureC = si7020.temperatureC();
        statHumidity = si7020.relHumidity();

        if(enableVCNL4010)
            statLight = vcnl4010.ambientLight();

        // Make sure we maintain relay states
        for(uint8_t i=0; i<2; i++)
            if(relayStates[i]==0)
                relay.turnOffRelay(i+1);
            else if(relayStates[i]==1)
                relay.turnOnRelay(i+1);

#ifdef ENABLE_BLYNK
        // Do not perform this Blynk push on the minute
        // We will have a "forced" push a few lines down for that
        if(Time.second()!=0)
            doBlynk();
#endif

        last_second = Time.second();
    }


    // Re-sync time at midnight
    if(last_minute!=Time.minute() && Time.hour()==0)
        Particle.syncTime();


    // 1-minute operations
    if(last_minute!=Time.minute()) {
        // Check for any EEPROM changes to be saved
        if(eepromHasChanged) {
            eepromSave();
            eepromHasChanged = false;
        }

#ifdef ENABLE_BLYNK
        // Force a Blynk push once a minute
        doBlynk(true);
#endif

        // Check thresholds to see if we need to toggle any relays
        checkLights();
        checkTemps();

#ifdef ENABLE_PUB
        // Don't pub here if we have a forced pub coming up
        if(!forcePub)
            doPub();
#endif
        last_minute = Time.minute();
    }


#ifdef ENABLE_PUB
    if(forcePub) {
        doPub();
        forcePub = false;
    }
#endif

#ifdef ENABLE_BLYNK
    Blynk.run();
#endif
}


// Check time thresholds and turn relay on/off accordingly
// This does not take spanning days into account (when you go from 23:59 PM to 00:00 AM)
void checkLights() {
    // Turn lights on if the time is right and they are off
    if(timeToInt(Time.hour(), Time.minute()) >= timeToInt(settings.lightsOnHour, settings.lightsOnMinute) &&
       timeToInt(Time.hour(), Time.minute()) < timeToInt(settings.lightsOffHour, settings.lightsOffMinute)) {
        relayStates[0] = 1;
        relay.turnOnRelay(0);

    // If the time isn't right and the lights are still on, turn them off
    } else if((timeToInt(Time.hour(), Time.minute()) < timeToInt(settings.lightsOnHour, settings.lightsOnMinute) ||
              timeToInt(Time.hour(), Time.minute()) >= timeToInt(settings.lightsOffHour, settings.lightsOffMinute))) {
        relayStates[0] = 0;
        relay.turnOffRelay(0);
    }
}


#ifdef ENABLE_BLYNK
void doBlynk() {
    doBlynk(false);
}


void doBlynk(bool force) {
    String tmp;

    // If the relay states have changed
    if(!force) {
        for(uint8_t i=0; i<2; i++) {
            if(blynk_relayStates[i]!=relayStates[i]) {
                blynk_relayStates[i] = relayStates[i];

                if(i==0)
                    led1.setValue(relayStates[i]*255);
                else if(i==1)
                    led2.setValue(relayStates[i]*255);
            }

            delay(100);
        }
    } else {
        for(uint8_t i=0; i<2; i++) {
            blynk_relayStates[i] = relayStates[i];

            if(i==0)
                led1.setValue(relayStates[i]*255);
            else if(i==1)
                led2.setValue(relayStates[i]*255);

            delay(100);
        }
    }


    // If lights on schedule has changed
    if(blynk_lightsOnSchedule!=timeToInt(settings.lightsOnHour, settings.lightsOnMinute)) {
        tmp = String(settings.lightsOnMinute);
        if(tmp.length()==1)
            tmp = "0"+tmp;

        Blynk.virtualWrite(V4, String(settings.lightsOnHour)+":"+tmp);
        blynk_lightsOnSchedule = timeToInt(settings.lightsOnHour, settings.lightsOnMinute);

        delay(100);
    }


    // If lights off schedule has changed
    if(blynk_lightsOffSchedule!=timeToInt(settings.lightsOffHour, settings.lightsOffMinute)) {
        tmp = String(settings.lightsOffMinute);
        if(tmp.length()==1)
            tmp = "0"+tmp;

        Blynk.virtualWrite(V5, String(settings.lightsOffHour)+":"+tmp);
        blynk_lightsOffSchedule = timeToInt(settings.lightsOffHour, settings.lightsOffMinute);

        delay(100);
    }


    // If the temperature threshold has changed
    if(blynk_tempThreshold!=settings.tempThreshold) {
        Blynk.virtualWrite(V6, String(settings.tempThreshold)+"°F");
        blynk_tempThreshold = settings.tempThreshold;

        delay(100);
    }


    // RSSI
    if(blynk_rssi!=Wifi_RSSI) {
        Blynk.virtualWrite(V7, Wifi_RSSI);
        blynk_rssi = Wifi_RSSI;

        delay(100);
    }


    // Light readings
    if(enableVCNL4010 && blynk_light!=statLight) {
        Blynk.virtualWrite(V8, statLight);
        blynk_light = statLight;

        delay(100);
    }


    // Always publish temperature and humidity if we've made it this far
    Blynk.virtualWrite(V0, String(statTemperatureF, 2)); //+"°F");
    Blynk.virtualWrite(V1, String(statHumidity, 2)); //+" %");
    delay(100);


    Blynk.virtualWrite(V9, Time.format(Time.now(), "%H:%M:"));
}
#endif


// Convert an hour and minute to a combined 24-hour integer (e.g. 1:37 PM = 1337)
int timeToInt(uint8_t h, uint8_t m) {
    return h*100+m;
}


// Check temperature thresholds and turn relay on/off accordingly
void checkTemps() {
    // Exit early if we're still in the keepout limit
    if(Time.now()<tempKeepoutTS)
        return;

    // Turn the heat on if the temperature is below a threshold and the heat is off
    if(statTemperatureF<=settings.tempThreshold) {
        relayStates[1] = 1;
        relay.turnOnRelay(1);
    } else if(statTemperatureF>settings.tempThreshold) {
        relayStates[1] = 0;
        relay.turnOffRelay(1);
    }

    // Update our temperature keepout
    tempKeepoutTS = Time.now()+settings.tempKeepoutSeconds;
}


// Function to handle cloud POST requests
int fnRouter(String command) {
    command.trim();
    command.toUpperCase();

#ifdef ENABLE_PUB
    forcePub = true;
#endif

    // Control relays
    if(command.startsWith("RELAY")) {   // e.g. relay1=1, 0 = off / 1 = on
        int8_t r = command.substring(5, command.indexOf("=")).toInt();

        // Individual relays
        if(r>0) {
            if(command.endsWith("=0")) {
                relay.turnOffRelay(r);
                relayStates[r] = 0;
            } else if(command.endsWith("=1")) {
                relay.turnOnRelay(r);
                relayStates[r] = 1;
            }

        // 0 = All relays
        } else if(r==0) {
            if(command.endsWith("=0")) {
                relay.turnOffAllRelays();
                memset(relayStates, 0, sizeof(relayStates));
            } else if(command.endsWith("=1")) {
                relay.turnOnAllRelays();
                memset(relayStates, 1, sizeof(relayStates));
            }
        }

        return r;

    // Get relay status
    } else if(command.startsWith("GETRELAY=")) {
        int8_t r = command.substring(9).toInt()-1;
        return relayStates[r];

    // Get ambient light
    } else if(enableVCNL4010 && command.equals("LIGHT")) {
        return statLight;

    // Get temperature in C
    } else if(command.equals("TEMPC")) {
        return statTemperatureC;

    // Get temperature in F
    } else if(command.equals("TEMPF")) {
        return statTemperatureF;

    // Get humidity in %
    } else if(command.equals("HUMID")) {
        return statHumidity;

    // Set temperature threshold
    } else if(command.startsWith("SETTEMP")) {
        settings.tempThreshold = command.substring(8).toInt();
        eepromHasChanged = true;
        return settings.tempThreshold;

    // Set lights on time
    } else if(command.startsWith("SETLIGHTSON")) {
        uint16_t x = command.substring(12).toInt();

        settings.lightsOnHour = x/100;
        settings.lightsOnMinute = x%100;

        eepromHasChanged = true;

        return timeToInt(settings.lightsOnHour, settings.lightsOnMinute);

    // Set lights off time
    } else if(command.startsWith("SETLIGHTSOFF")) {
        uint16_t x = command.substring(13).toInt();

        settings.lightsOffHour = x/100;
        settings.lightsOffMinute = x%100;

        eepromHasChanged = true;

        return timeToInt(settings.lightsOffHour, settings.lightsOffMinute);

    // Get the temperature threshold
    } else if(command.equals("GETTEMPTHRESHOLD")) {
        return settings.tempThreshold;

    // Get lights on time
    } else if(command.equals("GETLIGHTSON")) {
        return timeToInt(settings.lightsOnHour, settings.lightsOnMinute);

    // Get lights off time
    } else if(command.equals("GETLIGHTSOFF")) {
        return timeToInt(settings.lightsOffHour, settings.lightsOffMinute);

    // Set time zone
    } else if(command.startsWith("SETTIMEZONE")) {
        settings.timeZone = command.substring(12).toInt();
        Time.zone(settings.timeZone);
        eepromHasChanged = true;

        return settings.timeZone;

    // Get time zone
    } else if(command.equals("GETTIMEZONE")) {
        return settings.timeZone;

    // Get the time we think it is
    } else if(command.equals("GETTIME")) {
        return timeToInt(Time.hour(), Time.minute());

    }

    // Don't publish on a failed setting
#ifdef ENABLE_PUB
    forcePub = false;
#endif
    return -1;
}


// Load values from EEPROM
void eepromLoad() {
    // Make sure EEPROM data has been "initialized" first
    if(EEPROM.read(0)!=EEPROM_CHECK)
        eepromInit();

    EEPROM.get(1, settings);
}


// Initialize values in EEPROM
void eepromInit() {
    EEPROM.write(0, EEPROM_CHECK);
    eepromSave();
}


// Save values to EEPROM
void eepromSave() {
    EEPROM.put(1, settings);
}


// Publish stats to pubsub
#ifdef ENABLE_PUB
void doPub() {
    String pub = device_name+";h:"+String((float)statHumidity, 2)+"|g,f:"+String((float)statTemperatureF, 2)+"|g";
    pub += ",r0:"+String(relayStates[0])+"|g,r1:"+String(relayStates[1])+"|g";

    if(enableVCNL4010)
        pub += ",l:"+String(statLight)+"|g";

    pub += ",rssi:"+String(WiFi.RSSI())+"|g";

#ifdef ENABLE_SERIAL
    Serial.println(pub);
#endif

    Particle.publish(PUB_EVENT, pub, 60, PRIVATE);
}
#endif