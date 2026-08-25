#define TRIG1 5
#define ECHO1 18
#define PIN_PUMP 26

float getDistance(int trig, int echo) {
  digitalWrite(trig, LOW); delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 30000);
  return (dur == 0) ? 999.0 : (dur * 0.0343) / 2.0;
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG1, OUTPUT); pinMode(ECHO1, INPUT);
  pinMode(PIN_PUMP, OUTPUT);
  digitalWrite(PIN_PUMP, HIGH); // OFF
}

void loop() {
  float distance = getDistance(TRIG1, ECHO1);
  Serial.printf("Overhead Tank Distance: %.1f cm\n", distance);

  // If water level is low (distance > 80cm) turn ON pump
  if (distance > 80.0) {
    digitalWrite(PIN_PUMP, LOW);
  } else if (distance < 20.0) {
    digitalWrite(PIN_PUMP, HIGH); // Tank FULL
  }
  delay(2000);
}