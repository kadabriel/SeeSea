// JSN-SR20-Y1 / HC-SR04 sweep test for Arduino Uno
// Cycles five timing profiles every ~2 seconds and prints result to Serial (115200 bps)
// TRIG -> D9, ECHO -> D8

struct Profile {
  const char* name;
  unsigned int trig_us;
  unsigned int startup_us;
  unsigned long wait_fall_us;
};

Profile profiles[] = {
  {"10us", 10, 200, 40000},
  {"20us", 20, 200, 40000},
  {"30us", 30, 200, 45000},
  {"40us", 40, 400, 50000},
  {"60us", 60, 500, 60000},
};
const uint8_t trigPin = 9;
const uint8_t echoPin = 8;

// Uncomment to enable a weak pull-up on ECHO when running at 3.3V logic
//#define ENABLE_ECHO_PULLUP
uint8_t idx = 0;

void setup() {
  Serial.begin(115200);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
#ifdef ENABLE_ECHO_PULLUP
  digitalWrite(echoPin, HIGH); // weak pull-up if using 3.3V logic
#endif
  digitalWrite(trigPin, LOW);
  Serial.println(F("JSN sweep start (profiles 10-60us)"));
}

void loop() {
  Profile &p = profiles[idx];
  // Trigger pulse
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(p.trig_us);
  digitalWrite(trigPin, LOW);
  delayMicroseconds(p.startup_us);

  // Echo
  unsigned long duration = pulseIn(echoPin, HIGH, p.wait_fall_us);
  if (duration == 0) {
    Serial.print(F("Profile "));
    Serial.print(p.name);
    Serial.println(F(": no echo"));
  } else {
    float cm = duration / 58.0;
    Serial.print(F("Profile "));
    Serial.print(p.name);
    Serial.print(F(": "));
    Serial.print(cm, 1);
    Serial.println(F(" cm"));
  }

  idx = (idx + 1) % (sizeof(profiles)/sizeof(profiles[0]));
  delay(2000); // 2 seconds between profiles
}
