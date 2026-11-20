// Step 1: ultrasonic distance test (HC-SR04) on Arduino Uno R4 Minima
// Wiring: VCC -> 5V, GND -> GND, Trig -> D7, Echo -> D8
// Open Serial Monitor at 9600 baud.

const int TRIG_PIN = 7;
const int ECHO_PIN = 8;

void setup() {
  Serial.begin(9600);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
}

// Returns distance in cm, or -1 if no echo came back
float readDistanceCm() {
  // Send a clean 10 microsecond pulse on Trig
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Echo stays HIGH for the round-trip time of the sound pulse.
  // 30000 us timeout (~5 m) so the sketch never hangs waiting.
  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) {
    return -1;
  }

  // Sound travels 0.0343 cm per microsecond; divide by 2 for the round trip
  return duration * 0.0343 / 2.0;
}

void loop() {
  float d = readDistanceCm();
  Serial.print("Distance: ");
  Serial.print(d);
  Serial.println(" cm");
  delay(200);
}
