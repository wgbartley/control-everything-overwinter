class SI7020_A20{
public:
    double temperatureC();
    double temperatureF();
    double relHumidity();
private:
    int address = 0x40;
    int temperatureRegister = 227;
    int humidityRegister = 229;
    int getRawTemperatureReading();
    int getRawHumidityReading();
};