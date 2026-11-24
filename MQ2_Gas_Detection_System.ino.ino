/*
  MQ-2 Gas Detection System
  Board: Arduino Nano (ATmega328P)
  Sensor analog output: A2
  Buzzer signal: D8

  Educational prototype only. NOT a certified gas detector.
  Do not use this project for life-safety or industrial protection.

  Circuit assumed by the resistance equation:
    VCC (Vc) -> MQ-2 sensing element Rs -> A2 / divider node -> RL (10 kOhm) -> GND
  This arrangement makes ADC/Vout rise as Rs falls in response to many combustible gases.
  Check your module's schematic: some MQ-2 breakout boards have different circuitry,
  and their AO pin may not match this bare-sensor divider model.
*/

#include <Arduino.h>

// ----------------------- Hardware configuration -----------------------
const uint8_t SENSOR_PIN = A2;
const uint8_t BUZZER_PIN = 8;

// ----------------------- Measurement parameters ----------------------
const float ADC_REFERENCE_VOLTAGE = 5.00; // ADC reference voltage in volts (measure Vcc for better accuracy)
const uint16_t ADC_MAX_COUNT = 1023;      // 10-bit ADC: 0..1023
const float CIRCUIT_VOLTAGE = 5.00;      // Vc: voltage supplied to the sensor divider
const float LOAD_RESISTANCE_OHMS = 10000.0; // RL: load resistor in ohms

// ----------------------- Detection parameters ------------------------
// Alarm is based on ADC increase from the measured baseline.
// Adjust after observing the sensor in a stable, known safe environment.
const uint16_t ALARM_DELTA_ADC = 80;      // Trigger at baseline ADC + this amount
const uint16_t HYSTERESIS_ADC = 20;       // Clear alarm below trigger - hysteresis

// ----------------------- Timing parameters ---------------------------
const unsigned long WARMUP_DURATION_MS = 60000UL; // 60 s example; follow sensor datasheet for initial burn-in
const unsigned long SAMPLE_INTERVAL_MS = 250UL;   // Read/print every 250 ms
const uint16_t CALIBRATION_SAMPLE_COUNT = 100;    // 100 readings
const unsigned long CALIBRATION_SAMPLE_INTERVAL_MS = 20UL;

// ----------------------- Runtime state ------------------------------
unsigned long bootTimeMs = 0;
unsigned long lastSampleMs = 0;

uint16_t currentAdc = 0;
float currentVoltage = 0.0;
float currentSensorResistance = 0.0;

uint16_t baselineAdc = 0;
float baselineVoltage = 0.0;
float baselineSensorResistance = 0.0;
bool baselineCalibrated = false;
bool alarmActive = false;
bool warmupComplete = false;

// -------------------------------------------------------------------
// Read one analog sample from the MQ-2 analog output.
uint16_t readSensor() {
  return analogRead(SENSOR_PIN);
}

// -------------------------------------------------------------------
// Convert ADC counts to voltage.
// Vout = ADC * Vref / ADC_MAX_COUNT
float adcToVoltage(uint16_t adcValue) {
  return (float)adcValue * ADC_REFERENCE_VOLTAGE / (float)ADC_MAX_COUNT;
}

// -------------------------------------------------------------------
// Calculate approximate sensor resistance using:
// Rs = RL * (Vc / Vout - 1)
//
// Rs: sensing element resistance (ohms)
// RL: load resistance (ohms)
// Vc: divider supply voltage (volts)
// Vout: measured divider voltage (volts)
//
// This equation is valid only for the divider arrangement described
// in the header. If Vout is near zero, the calculation is undefined.
float calculateSensorResistance(float outputVoltage) {
  if (outputVoltage <= 0.001) {
    return NAN;
  }

  float resistance = LOAD_RESISTANCE_OHMS *
                    ((CIRCUIT_VOLTAGE / outputVoltage) - 1.0);

  if (resistance < 0.0) {
    resistance = 0.0;
  }

  return resistance;
}

// -------------------------------------------------------------------
// Collect an average baseline after warm-up.
// Perform this only in a stable, known safe atmosphere. The baseline
// is a local reference, not a universal clean-air value.
void calibrateSensor() {
  Serial.println();
  Serial.println(F("Starting baseline calibration..."));
  Serial.println(F("Keep the sensor in a stable, known safe environment."));

  unsigned long adcTotal = 0;

  for (uint16_t i = 0; i < CALIBRATION_SAMPLE_COUNT; i++) {
    adcTotal += readSensor();
    delay(CALIBRATION_SAMPLE_INTERVAL_MS);
  }

  baselineAdc = (uint16_t)(adcTotal / CALIBRATION_SAMPLE_COUNT);
  baselineVoltage = adcToVoltage(baselineAdc);
  baselineSensorResistance = calculateSensorResistance(baselineVoltage);
  baselineCalibrated = true;
  alarmActive = false;
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println(F("Baseline calibration complete."));
  Serial.print(F("Baseline ADC: "));
  Serial.println(baselineAdc);
  Serial.print(F("Baseline voltage: "));
  Serial.print(baselineVoltage, 3);
  Serial.println(F(" V"));
  Serial.print(F("Baseline sensor resistance: "));
  if (isnan(baselineSensorResistance)) {
    Serial.println(F("Not available (voltage too low)"));
  } else {
    Serial.print(baselineSensorResistance / 1000.0, 2);
    Serial.println(F(" kOhm"));
  }
  Serial.println(F("Alarm monitoring enabled."));
  Serial.println();
}

// -------------------------------------------------------------------
// Update alarm state using a threshold relative to baseline.
// In this circuit, gas response commonly increases ADC readings.
// Hysteresis prevents rapid switching near the threshold.
void checkAlarm(uint16_t adcValue) {
  if (!baselineCalibrated || !warmupComplete) {
    alarmActive = false;
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }

  uint16_t triggerLevel = baselineAdc + ALARM_DELTA_ADC;
  if (triggerLevel > ADC_MAX_COUNT) {
    triggerLevel = ADC_MAX_COUNT;
  }

  int clearLevel = (int)triggerLevel - (int)HYSTERESIS_ADC;
  if (clearLevel < 0) {
    clearLevel = 0;
  }

  if (!alarmActive && adcValue >= triggerLevel) {
    alarmActive = true;
  } else if (alarmActive && adcValue <= (uint16_t)clearLevel) {
    alarmActive = false;
  }

  digitalWrite(BUZZER_PIN, alarmActive ? HIGH : LOW);
}

// -------------------------------------------------------------------
// Print current sensor data in a readable format.
void printSensorData() {
  Serial.println(F("----------------------------------------"));
  Serial.print(F("ADC Value: "));
  Serial.println(currentAdc);

  Serial.print(F("Output Voltage: "));
  Serial.print(currentVoltage, 3);
  Serial.println(F(" V"));

  Serial.print(F("Sensor Resistance: "));
  if (isnan(currentSensorResistance)) {
    Serial.println(F("Not available"));
  } else {
    Serial.print(currentSensorResistance / 1000.0, 2);
    Serial.println(F(" kOhm"));
  }

  Serial.print(F("Baseline Resistance: "));
  if (!baselineCalibrated || isnan(baselineSensorResistance)) {
    Serial.println(F("Not calibrated"));
  } else {
    Serial.print(baselineSensorResistance / 1000.0, 2);
    Serial.println(F(" kOhm"));
  }

  if (!warmupComplete) {
    Serial.println(F("Sensor Status: WARMING UP"));
  } else if (!baselineCalibrated) {
    Serial.println(F("Sensor Status: NOT CALIBRATED (send C)"));
  } else if (alarmActive) {
    Serial.println(F("WARNING: SENSOR RESPONSE ABOVE THRESHOLD"));
  } else {
    Serial.println(F("Sensor Status: NORMAL"));
  }

  Serial.print(F("Buzzer: "));
  Serial.println(alarmActive ? F("ON") : F("OFF"));
}

// -------------------------------------------------------------------
// Process serial commands.
// Send C or c to calibrate; send ? to print command help.
void handleSerialCommands() {
  while (Serial.available() > 0) {
    char command = Serial.read();

    if (command == 'C' || command == 'c') {
      if (warmupComplete) {
        calibrateSensor();
      } else {
        Serial.println(F("Calibration unavailable: wait for warm-up."));
      }
    } else if (command == '?' ) {
      Serial.println(F("Commands: C = baseline calibration, ? = help"));
    }
  }
}

// -------------------------------------------------------------------
void setup() {
  pinMode(SENSOR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.begin(9600);
  bootTimeMs = millis();

  Serial.println(F("MQ-2 Gas Detection System"));
  Serial.println(F("Educational prototype - not a certified safety detector."));
  Serial.println(F("Wait for warm-up, then send C in Serial Monitor to calibrate."));
  Serial.println(F("Set Serial Monitor to 9600 baud."));
}

// -------------------------------------------------------------------
void loop() {
  handleSerialCommands();

  unsigned long now = millis();

  if (!warmupComplete && (now - bootTimeMs >= WARMUP_DURATION_MS)) {
    warmupComplete = true;
    Serial.println();
    Serial.println(F("Warm-up period complete."));
    Serial.println(F("Place sensor in a stable, known safe environment and send C."));
  }

  if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = now;

    currentAdc = readSensor();
    currentVoltage = adcToVoltage(currentAdc);
    currentSensorResistance = calculateSensorResistance(currentVoltage);

    checkAlarm(currentAdc);
    printSensorData();
  }
}
