// Workaround for Arduino IDE - struct definitions are extracted to separate .h file, so that they will be forced to be defined before their usage in code.

#pragma once

struct WorkState {
  bool finished;
  uint32_t suggestedSleepTime;
  uint32_t remainingTime;
};

WorkState finishedWorkState(uint32_t remainingTime) {
  WorkState result;
  result.finished = true;
  result.suggestedSleepTime = 0;
  result.remainingTime = remainingTime;

  return result;
}

WorkState unfinishedWorkState(uint32_t suggestedSleepTime) {
  WorkState result;
  result.finished = false;
  result.suggestedSleepTime = suggestedSleepTime;
  result.remainingTime = 0;

  return result;
}

struct AnalogLedsState {
  float red;
  float amber;
  float green;
  float blue;
  float white;
};

AnalogLedsState analogLedsState(float red, float amber, float green, float blue, float white) {
  AnalogLedsState result;
  result.red = red;
  result.amber = amber;
  result.green = green;
  result.blue = blue;
  result.white = white;

  return result;
}

float intermediateValue(float initial, float target, float relativeTime) {
  float delta = target - initial;
  return initial + relativeTime * delta;
}

AnalogLedsState intermediateState(AnalogLedsState initial, AnalogLedsState target, float relativeTime) {
  relativeTime = constrain(relativeTime, 0.0, 1.0);

  AnalogLedsState result;
  result.red = intermediateValue(initial.red, target.red, relativeTime);
  result.amber = intermediateValue(initial.amber, target.amber, relativeTime);
  result.green = intermediateValue(initial.green, target.green, relativeTime);
  result.blue = intermediateValue(initial.blue, target.blue, relativeTime);
  result.white = intermediateValue(initial.white, target.white, relativeTime);

  return result;
}

AnalogLedsState allLedsOn() {
  return analogLedsState(1.0, 1.0, 1.0, 1.0, 1.0);
}

AnalogLedsState allLedsOff() {
  return analogLedsState(0.0, 0.0, 0.0, 0.0, 0.0);
}
