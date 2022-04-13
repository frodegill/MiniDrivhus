static const byte triggerPin = D5;
static const byte lowSensorPin = D6;
static const byte highSensorPin = D7;

static const unsigned long LONG_DELAY = 15*60*1000L;
static const unsigned long SHORT_DELAY = 1000L;

volatile unsigned long failsafeDelay;

#undef ENABLE_LOGGING


void setup() {
#ifdef ENABLE_LOGGING
  Serial.begin(9600);
#endif

  pinMode(triggerPin, OUTPUT);
  digitalWrite(triggerPin, LOW);
  failsafeDelay = LONG_DELAY;
  
  pinMode(lowSensorPin, INPUT);
  pinMode(highSensorPin, INPUT);
  
  attachInterrupt(digitalPinToInterrupt(lowSensorPin), updateFlowStatus, CHANGE);
  attachInterrupt(digitalPinToInterrupt(highSensorPin), updateFlowStatus, CHANGE);

#ifdef ENABLE_LOGGING
  Serial.println("Ready.");
#endif

  updateFlowStatus();
}

void loop() {
  delay(failsafeDelay);
  updateFlowStatus();
}

ICACHE_RAM_ATTR void updateFlowStatus()
{
  int low_status = digitalRead(lowSensorPin);
  int high_status = digitalRead(highSensorPin);
  int trigger_status = digitalRead(triggerPin);
#ifdef ENABLE_LOGGING
  Serial.println("Update. low="+String(low_status==LOW?"LOW ":"HIGH")+", high="+String(high_status==LOW?"LOW ":"HIGH")+ ", trigger="+String(trigger_status==LOW?"LOW ":"HIGH"));
#endif
  if (trigger_status==LOW && low_status==LOW && high_status!=HIGH /*fail safe*/)
  {
#ifdef ENABLE_LOGGING
    Serial.println("Starting flow");
#endif
    digitalWrite(triggerPin, HIGH);
    failsafeDelay = SHORT_DELAY;
  }
  
  if (trigger_status==HIGH && high_status==HIGH)
  {
#ifdef ENABLE_LOGGING
    Serial.println("Stopping flow");
#endif
    digitalWrite(triggerPin, LOW);
    failsafeDelay = LONG_DELAY;
  }
}
