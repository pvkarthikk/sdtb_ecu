/*
 * Engine ECU Prototype - Final Phase 2.2
 * Improvements: Non-blocking priming telemetry, fixed float-to-int mapping, 
 * RPM-based ignition, and speed-density fueling.
 */

// Pin Definitions
const int CKP_PIN = 2;       
const int TPS_PIN = A0;      
const int ECT_PIN = A1;      
const int MAP_PIN = A2;      
const int INJECTOR_PIN = 3;  
const int IGNITION_PIN = 5;  

// Constants
const unsigned long SERIAL_INTERVAL = 100; 
const int PULSES_PER_REV = 36;
const int TEMP_CUTOFF_THRESHOLD = 850;
const int TEMP_RECOVERY_THRESHOLD = 750;
const unsigned long PRIMING_DELAY = 1500;

// Variables
volatile unsigned long pulseCount = 0;
unsigned long lastRPMCalcMillis = 0;
float currentRPM = 0;
bool engineSafetyCutoff = false;
bool isPrimed = false;
unsigned long powerOnTime;

void setup() {
  Serial.begin(115200);
  pinMode(CKP_PIN, INPUT_PULLUP);
  pinMode(INJECTOR_PIN, OUTPUT);
  pinMode(IGNITION_PIN, OUTPUT);

  powerOnTime = millis();
  attachInterrupt(digitalPinToInterrupt(CKP_PIN), countPulse, RISING);
  Serial.println("{\"status\": \"ECU_PRO_FINAL_READY\"}");
}

void loop() {
  unsigned long currentMillis = millis();
  
  // 1. Sensor Reading & Calibration
  int tpsValue = analogRead(TPS_PIN); 
  int ectValue = analogRead(ECT_PIN); 
  int mapRaw = analogRead(MAP_PIN);
  float pressureKPa = map(mapRaw, 0, 1023, 10, 100);

  // 2. Non-blocking Priming Logic (Correction: Line 55)
  if (!isPrimed && (currentMillis - powerOnTime < PRIMING_DELAY)) {
    digitalWrite(INJECTOR_PIN, HIGH);
    
    static unsigned long lastPrimeTelemetry = 0;
    if (currentMillis - lastPrimeTelemetry >= 50) {
      sendTelemetry(0, tpsValue, ectValue, pressureKPa);
      lastPrimeTelemetry = currentMillis;
    }
    return; // Stay in priming phase until time expires
  } else if (!isPrimed) {
    digitalWrite(INJECTOR_PIN, LOW);
    isPrimed = true;
  }

  // 3. Atomic RPM Calculation
  if (currentMillis - lastRPMCalcMillis >= 1000) {
    unsigned long countCopy;
    noInterrupts();
    countCopy = pulseCount;
    pulseCount = 0;
    interrupts();

    currentRPM = (float)(countCopy * 60) / PULSES_PER_REV;
    lastRPMCalcMillis = currentMillis;
  }

  // 4. Safety Logic (Hysteresis)
  if (!engineSafetyCutoff && ectValue > TEMP_CUTOFF_THRESHOLD) {
    engineSafetyCutoff = true;
  } else if (engineSafetyCutoff && ectValue < TEMP_RECOVERY_THRESHOLD) {
    engineSafetyCutoff = false;
  }

  // 5. Actuator Control
  if (!engineSafetyCutoff) {
    // A. Injection (Speed-Density) - Fixed mapping (Line 85)
    int baseFuel = map(tpsValue, 0, 1023, 10, 180);
    int loadModifier = map((int)pressureKPa, 10, 100, 0, 75);
    int finalDuty = constrain(baseFuel + loadModifier, 0, 255);
    analogWrite(INJECTOR_PIN, finalDuty);

    // B. Ignition Advance (RPM-based)
    int ignitionAdvance = map(constrain((int)currentRPM, 800, 6000), 800, 6000, 50, 200);
    analogWrite(IGNITION_PIN, ignitionAdvance); 
  } else {
    analogWrite(INJECTOR_PIN, 0);
    analogWrite(IGNITION_PIN, 0);
  }

  // 6. Regular Telemetry
  static unsigned long lastTelemetry = 0;
  if (currentMillis - lastTelemetry >= SERIAL_INTERVAL) {
    sendTelemetry(currentRPM, tpsValue, ectValue, pressureKPa);
    lastTelemetry = currentMillis;
  }
}

void countPulse() {
  pulseCount++;
}

void sendTelemetry(float rpm, int tps, int ect, float pressure) {
  Serial.print("{");
  Serial.print("\"rpm\":"); Serial.print(rpm);
  Serial.print(", \"tps\":"); Serial.print(tps);
  Serial.print(", \"kPa\":"); Serial.print(pressure);
  Serial.print(", \"ect\":"); Serial.print(ect);
  Serial.print(", \"cutoff\":"); Serial.print(engineSafetyCutoff ? "true" : "false");
  Serial.println("}");
}