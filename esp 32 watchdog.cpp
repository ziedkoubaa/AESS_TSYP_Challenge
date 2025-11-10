#define HEARTBEAT_PIN 25    // D25 <- Raspberry Pi heartbeat
#define RESET_PIN 26        // D26 -> Raspberry Pi reset  
#define STATUS_LED 2        // Built-in LED

unsigned long lastHeartbeat = 0;
const unsigned long WATCHDOG_TIMEOUT = 10000; // 10 seconds timeout
bool piRunning = false;

void setup() {
  Serial.begin(115200);
  
  pinMode(HEARTBEAT_PIN, INPUT_PULLUP);
  pinMode(RESET_PIN, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);
  
  digitalWrite(RESET_PIN, HIGH); // Don't reset at startup
  digitalWrite(STATUS_LED, LOW);
  
  // Attach interrupt to heartbeat pin
  attachInterrupt(digitalPinToInterrupt(HEARTBEAT_PIN), heartbeatISR, FALLING);
  
  Serial.println("ESP32 Watchdog Started");
  Serial.println("Waiting for Raspberry Pi heartbeat signals...");
  Serial.print("Watchdog timeout: ");
  Serial.print(WATCHDOG_TIMEOUT / 1000);
  Serial.println(" seconds");
  
  lastHeartbeat = millis();
}

void heartbeatISR() {
  lastHeartbeat = millis();
  piRunning = true;
}

void resetPi() {
  Serial.println("!!! WATCHDOG TIMEOUT - RESETTING RASPBERRY PI !!!");
  
  // Turn on LED to indicate reset
  digitalWrite(STATUS_LED, HIGH);
  
  // Reset sequence (active low)
  digitalWrite(RESET_PIN, LOW);
  delay(1000);  // Hold reset for 1 second
  digitalWrite(RESET_PIN, HIGH);
  
  Serial.println("Reset signal sent to Raspberry Pi");
  
  // Keep LED on for 3 seconds
  delay(2000);
  digitalWrite(STATUS_LED, LOW);
  
  // Reset timer
  lastHeartbeat = millis();
  piRunning = false;
}

void loop() {
  // Check if watchdog timer has expired
  if (millis() - lastHeartbeat > WATCHDOG_TIMEOUT) {
    if (piRunning) {
      resetPi();
    }
  }
  
  // Blink LED when Pi is running normally
  static unsigned long lastBlink = 0;
  if (piRunning && millis() - lastBlink > 1000) {
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
    lastBlink = millis();
  }
  
  // Print status every 10 seconds
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 10000) {
    unsigned long timeSinceHeartbeat = millis() - lastHeartbeat;
    Serial.print("Pi Status: ");
    Serial.print(piRunning ? "RUNNING" : "BOOTING");
    Serial.print(" | Time since heartbeat: ");
    Serial.print(timeSinceHeartbeat / 1000);
    Serial.println(" seconds");
    lastStatus = millis();
  }
  
  delay(100);
}