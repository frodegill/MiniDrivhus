#include "steppermotor.h"

#include "global.h"


void StepperMotor::initialize()
{
  pinMode(O_STEPPERMOTOR_CS_PIN, OUTPUT);
  pinMode(O_STEPPERMOTOR_DIR_PIN, OUTPUT);
  pinMode(O_STEPPERMOTOR_ENABLE_PIN, OUTPUT);
  digitalWrite(O_STEPPERMOTOR_ENABLE_PIN, LOW);
  digitalWrite(O_STEPPERMOTOR_CS_PIN, LOW);
  m_steps = 0;
}

void StepperMotor::addStep(int steps, bool reverse)
{
  m_steps += (reverse ? -steps : steps);
}

void StepperMotor::step()
{
  if (m_steps == 0)
    return;

  digitalWrite(O_STEPPERMOTOR_ENABLE_PIN, HIGH);
  digitalWrite(O_STEPPERMOTOR_DIR_PIN, m_steps < 0 ? LOW : HIGH);
  delayMicroseconds(25);
  digitalWrite(O_STEPPERMOTOR_CS_PIN, HIGH);
  delayMicroseconds(STEPPERMOTOR_SPEED);
  digitalWrite(O_STEPPERMOTOR_CS_PIN, LOW);

  m_steps -= (m_steps < 0 ? -1 : 1);
  if (m_steps == 0)
  {
    digitalWrite(O_STEPPERMOTOR_ENABLE_PIN, LOW);
  }
}
