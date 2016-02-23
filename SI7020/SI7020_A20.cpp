#include "SI7020_A20.h"
#include "spark_wiring_usbserial.h"
#include "spark_wiring_i2c.h"
#include "spark_wiring_constants.h"

double SI7020_A20::temperatureC(){
    int raw = getRawTemperatureReading();
    double temperature = ((175.72*raw)/65536)-46.85;
    return temperature;
}

double SI7020_A20::temperatureF(){
    double degreesC = temperatureC();
    double temperatureF = (degreesC*1.8)+32;
    return temperatureF;
}

double SI7020_A20::relHumidity() {
    int raw = getRawHumidityReading();
    double humidity = -6.0 + 125.0 * (raw/65536.0);
    return humidity;
}

//Read temperature from SI7020
int SI7020_A20::getRawTemperatureReading(){
    Wire.begin();
    Wire.beginTransmission(address);
    Wire.write(temperatureRegister);
    byte status = Wire.endTransmission();
    if(status != 0){
        // Serial.println("read temperature failed");
        return 0;
    }else{
        //It worked
        Wire.requestFrom(address, 2);
        byte msb = Wire.read();
        byte lsb = Wire.read();
        int reading = (msb*256)+lsb;
        return reading;
    }
}


int SI7020_A20::getRawHumidityReading() {
    Wire.begin();
    Wire.beginTransmission(address);
    Wire.write(humidityRegister);
    byte status = Wire.endTransmission();

    if(status!=0) {
        // Serial.println("read humidity failed");
        return 0;
    } else {
        Wire.requestFrom(address, 2);
        byte msb = Wire.read();
        byte lsb = Wire.read();
        int reading = (msb*256)+lsb;

        return reading;
    }
}