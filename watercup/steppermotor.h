#ifndef _STEPPERMOTOR_H_
#define _STEPPERMOTOR_H_

#include "Arduino.h"


class StepperMotor
{
public:
  void initialize();

  void addStep(int steps=1, bool reverse=false);
  void step();

private:
  volatile int m_steps;
};


#endif // _STEPPERMOTOR_H_
