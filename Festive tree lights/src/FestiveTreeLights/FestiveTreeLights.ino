#include "Sleep_n0m1.h"

#include "WorkState.h"

// The festive tree light will have 5 LED colors: red, amber, green, blue and white.
// This code uses the following convention to mark their states in code comments:
// - RAGBW - all LEDs are on;
// - ***** - all LEDs are off;
// - RA**W - red, amber and white LEDs are on, green and blue LEDs are off;
// - **GB* -> RAGBW - 'smooth' transition from one state to another.

#define RED_LED_PIN 3
#define AMBER_LED_PIN 5
#define GREEN_LED_PIN 6
#define BLUE_LED_PIN 9
#define WHITE_LED_PIN 10

#define STATUS_LED_PIN 13

#define RED_LED_MIN_BRIGHTNESS 0
#define RED_LED_MAX_BRIGHTNESS 255

#define AMBER_LED_MIN_BRIGHTNESS 0
#define AMBER_LED_MAX_BRIGHTNESS 255

#define GREEN_LED_MIN_BRIGHTNESS 0
#define GREEN_LED_MAX_BRIGHTNESS 150

#define BLUE_LED_MIN_BRIGHTNESS 0
#define BLUE_LED_MAX_BRIGHTNESS 170

#define WHITE_LED_MIN_BRIGHTNESS 0
#define WHITE_LED_MAX_BRIGHTNESS 140

#define PWM_ANIMATION_INTERVAL 15

void setAnalogLedStates(uint8_t red, uint8_t amber, uint8_t green, uint8_t blue, uint8_t white) {
  analogWrite(RED_LED_PIN, red);
  analogWrite(AMBER_LED_PIN, amber);
  analogWrite(GREEN_LED_PIN, green);
  analogWrite(BLUE_LED_PIN, blue);
  analogWrite(WHITE_LED_PIN, white);
}

void setLedStates(bool red, bool amber, bool green, bool blue, bool white) {
  uint8_t analogRed   = red   ?   RED_LED_MAX_BRIGHTNESS :   RED_LED_MIN_BRIGHTNESS;
  uint8_t analogAmber = amber ? AMBER_LED_MAX_BRIGHTNESS : AMBER_LED_MIN_BRIGHTNESS;
  uint8_t analogGreen = green ? GREEN_LED_MAX_BRIGHTNESS : GREEN_LED_MIN_BRIGHTNESS;
  uint8_t analogBlue  = blue  ?  BLUE_LED_MAX_BRIGHTNESS :  BLUE_LED_MIN_BRIGHTNESS;
  uint8_t analogWhite = white ? WHITE_LED_MAX_BRIGHTNESS : WHITE_LED_MIN_BRIGHTNESS;

  setAnalogLedStates(analogRed, analogAmber, analogGreen, analogBlue, analogWhite);
}

uint8_t normalizePwmValue(float rawValue, uint8_t minValue, uint8_t maxValue) {
  float delta = maxValue - minValue;
  return minValue + (uint8_t)(rawValue * delta);
}

void setNormalizedAnalogLedStates(AnalogLedsState state) {
  uint8_t rawRed   = normalizePwmValue(state.red,     RED_LED_MIN_BRIGHTNESS,   RED_LED_MAX_BRIGHTNESS);
  uint8_t rawAmber = normalizePwmValue(state.amber, AMBER_LED_MIN_BRIGHTNESS, AMBER_LED_MAX_BRIGHTNESS);
  uint8_t rawGreen = normalizePwmValue(state.green, GREEN_LED_MIN_BRIGHTNESS, GREEN_LED_MAX_BRIGHTNESS);
  uint8_t rawBlue  = normalizePwmValue(state.blue,   BLUE_LED_MIN_BRIGHTNESS,  BLUE_LED_MAX_BRIGHTNESS);
  uint8_t rawWhite = normalizePwmValue(state.white, WHITE_LED_MIN_BRIGHTNESS, WHITE_LED_MAX_BRIGHTNESS);

  setAnalogLedStates(rawRed, rawAmber, rawGreen, rawBlue, rawWhite);
}

class ILedController {
  public:
    virtual WorkState doControl(uint32_t timeSinceLastCall) = 0;
    virtual void reset() = 0;
};

class LoopingController : public ILedController {
  private:
    ILedController* _controller;
    uint32_t _maxLoopCount;
    uint32_t _currentLoopCount;

  public:
    LoopingController(ILedController* controller, uint32_t maxLoopCount)
      : _controller(controller), _maxLoopCount(maxLoopCount), _currentLoopCount(0) {}

    virtual WorkState doControl(uint32_t timeSinceLastCall) {
      if (_currentLoopCount >= _maxLoopCount) {
        // All loop iterations are done - no more work left to do.        
        return finishedWorkState(timeSinceLastCall);
      }
      
      WorkState innerResult = _controller->doControl(timeSinceLastCall);

      if (!innerResult.finished) {
        // Current iteration is not finished (because inner controller did not finish its work) - return result from inner controller as is.
        return innerResult;
      } 

      // Inner controller finished its work - go to next loop iteration
      _currentLoopCount++;

      if (_currentLoopCount >= _maxLoopCount) {
        // Last iteration just finished - return finished result from inner controller.
        return innerResult;
      }

      _controller->reset();
      
      // There is still some time left - call this method again to process it
      uint32_t remainingTime = innerResult.remainingTime;
      return doControl(remainingTime);
    }

    virtual void reset() {
      _controller->reset();
      _currentLoopCount = 0;
    }
};

class StatefulController : public ILedController {
  private:
    uint8_t _maxStateIndex;
    uint8_t _currentStateIndex;
    uint32_t _timeSpentOnCurrentState;

    boolean _pwmTransitionInProgress;
    AnalogLedsState _initialState;
    AnalogLedsState _targetState;
    uint32_t _totalTransitionTime;

    float animateLedState(float initialState, float targetState, float relativeTransitionTime) {
      float delta = targetState - initialState;
      return initialState + relativeTransitionTime * delta;
    }

    void animatePwmTransition(uint32_t timeSpentOnCurrentState) {
      if (timeSpentOnCurrentState >= _totalTransitionTime) {
        _pwmTransitionInProgress = false;

        setAnalogLedStates(_targetState);
      } else {
        float relativeElapsedTime = (float)timeSpentOnCurrentState / _totalTransitionTime;

        AnalogLedsState newState = intermediateState(_initialState, _targetState, relativeElapsedTime);
        setAnalogLedStates(newState);
      }
    }

  protected:
    virtual void applyLedStatesForState(uint8_t stateIndex) = 0;
    virtual uint32_t getStateDuration(uint8_t stateIndex) = 0;

    void setDigitalLedStates(boolean red, boolean amber, boolean green, boolean blue, boolean white) {
      setLedStates(red, amber, green, blue, white);
    }

    void setAnalogLedStates(AnalogLedsState state) {
      setNormalizedAnalogLedStates(state);
    }

    void startPwmTransition(uint32_t transitionTime, AnalogLedsState initial, AnalogLedsState target) {
      _pwmTransitionInProgress = true;
      _totalTransitionTime = transitionTime;

      _initialState = initial;
      _targetState = target;

      setAnalogLedStates(initial);
    }

    void startFadeOutFromAllLeds(uint32_t transitionTime, AnalogLedsState targetState) {
      startPwmTransition(transitionTime, allLedsOn(), targetState);
    }

    void startFadeInToAllLeds(uint32_t transitionTime, AnalogLedsState initialState) {
      startPwmTransition(transitionTime, initialState, allLedsOn());
    }

    void startFadeInFromNoneLeds(uint32_t transitionTime, AnalogLedsState targetState) {
      startPwmTransition(transitionTime, allLedsOff(), targetState);
    }

    void startFadeOutToNoneLeds(uint32_t transitionTime, AnalogLedsState initialState) {
      startPwmTransition(transitionTime, initialState, allLedsOff());
    }

  public:
    StatefulController(uint8_t maxStateIndex)
      : _maxStateIndex(maxStateIndex), _currentStateIndex(0), _timeSpentOnCurrentState(0), _pwmTransitionInProgress(false) {}

    virtual WorkState doControl(uint32_t timeSinceLastCall) {
      if (_currentStateIndex >= _maxStateIndex) {
        // All states have passed - there is absolutely nothing to do anymore...
        return finishedWorkState(timeSinceLastCall);
      }

      _timeSpentOnCurrentState += timeSinceLastCall;

      uint8_t lastObservedStateIndex = _currentStateIndex;
      uint32_t currentStateDuration = getStateDuration(_currentStateIndex);
      
      while (_timeSpentOnCurrentState >= currentStateDuration && _currentStateIndex < _maxStateIndex) {
        _timeSpentOnCurrentState -= currentStateDuration;
        _currentStateIndex++;

        currentStateDuration = _currentStateIndex < _maxStateIndex 
          ? getStateDuration(_currentStateIndex)
          : 0;
      }

      if (_currentStateIndex >= _maxStateIndex) {
        // Last state just passed - remaining time should be handled by another controller
        return finishedWorkState(_timeSpentOnCurrentState);
      }

      if (lastObservedStateIndex != _currentStateIndex) {
        // There was a state transition - re-initialize LED states for a new state
        applyLedStatesForState(_currentStateIndex);
      }

      if (_pwmTransitionInProgress) {
        animatePwmTransition(_timeSpentOnCurrentState);
        return unfinishedWorkState(PWM_ANIMATION_INTERVAL);
      } else {
        uint32_t remainingTime = currentStateDuration - _timeSpentOnCurrentState;
        return unfinishedWorkState(remainingTime);
      }
    }

    virtual void reset() {
      _currentStateIndex = 0;
      _timeSpentOnCurrentState = 0;
      
      applyLedStatesForState(_currentStateIndex);
    }
};

class CombinationController : public StatefulController {
  private:
    uint32_t _iterationDuration;
    uint32_t _transitionDuration;

    AnalogLedsState getLedStatesForState(uint8_t stateIndex) {
      if (stateIndex >= 10) {
        stateIndex -= 10;
      }

      switch (stateIndex) {
        case 0:
          return analogLedsState(1.0, 1.0, 0.0, 0.0, 0.0); // RA***

        case 1:
          return analogLedsState(1.0, 1.0, 1.0, 0.0, 0.0); // RAG**

        case 2:
          return analogLedsState(0.0, 1.0, 1.0, 0.0, 0.0); // *AG**

        case 3:
          return analogLedsState(0.0, 1.0, 1.0, 1.0, 0.0); // *AGB*

        case 4:
          return analogLedsState(0.0, 0.0, 1.0, 1.0, 0.0); // **GB*

        case 5:
          return analogLedsState(0.0, 0.0, 1.0, 1.0, 1.0); // **GBW

        case 6:
          return analogLedsState(0.0, 0.0, 0.0, 1.0, 1.0); // ***BW

        case 7:
          return analogLedsState(1.0, 0.0, 0.0, 1.0, 1.0); // R**BW

        case 8:
          return analogLedsState(1.0, 0.0, 0.0, 0.0, 1.0); // R***W

        case 9:
          return analogLedsState(1.0, 1.0, 0.0, 0.0, 1.0); // RA**W
      }
    }

  protected:
    virtual void applyLedStatesForState(uint8_t stateIndex) {
      AnalogLedsState currentState = getLedStatesForState(stateIndex);
      AnalogLedsState nextState = getLedStatesForState(stateIndex + 1);

      startPwmTransition(_transitionDuration, currentState, nextState);
    }

    virtual uint32_t getStateDuration(uint8_t stateIndex) {
      return _iterationDuration;
    }

  public:
    CombinationController(uint32_t iterationDuration, uint32_t transitionDuration)
      : StatefulController(10), _iterationDuration(iterationDuration), _transitionDuration(transitionDuration) {}
};

class FlowingTwoLedsController : public StatefulController {
  private:
    uint32_t _stateDuration;
  
  protected:
    virtual void applyLedStatesForState(uint8_t stateIndex) {
      switch (stateIndex) {
        case 0:
          setDigitalLedStates(true, true, false, false, false); // RA***
          break;

        case 1:
          setDigitalLedStates(false, true, true, false, false); // *AG**
          break;

        case 2:
          setDigitalLedStates(false, false, true, true, false); // **GB*
          break;

        case 3:
          setDigitalLedStates(false, false, false, true, true); // ***BW
          break;

        case 4:
          setDigitalLedStates(true, false, false, false, true); // R***W
          break;
      }
    }

    virtual uint32_t getStateDuration(uint8_t stateIndex) {
      return _stateDuration;
    }

  public:
    FlowingTwoLedsController(uint32_t timePerState) 
      : StatefulController(5), _stateDuration(timePerState) {}
};

class SequentialFadeController : public StatefulController {
  private:
    uint32_t _fadeInDuration;
    uint32_t _sustainAfterFadeInDuration;
    uint32_t _delayAfterFadingInAllLeds;
    uint32_t _fadeOutDuration;
    uint32_t _sustainAfterFadeOutDuration;
    uint32_t _delayAfterFadingOutAllLeds;

    void startFadeInTransition(AnalogLedsState initial, AnalogLedsState target) {
      startPwmTransition(_fadeInDuration, initial, target);
    }

    void startFadeOutDuration(AnalogLedsState initial, AnalogLedsState target) {
      startPwmTransition(_fadeOutDuration, initial, target);
    }

  protected:
    virtual void applyLedStatesForState(uint8_t stateIndex) {
      switch (stateIndex) {
        case 0:
          startFadeInTransition(allLedsOff(), analogLedsState(1.0, 0.0, 0.0, 0.0, 0.0)); // ***** -> R****
          break;

        case 1:
          startFadeInTransition(analogLedsState(1.0, 0.0, 0.0, 0.0, 0.0), analogLedsState(1.0, 1.0, 0.0, 0.0, 0.0)); // R**** -> RA***
          break;

        case 2:
          startFadeInTransition(analogLedsState(1.0, 1.0, 0.0, 0.0, 0.0), analogLedsState(1.0, 1.0, 1.0, 0.0, 0.0)); // RA*** -> RAG**
          break;

        case 3:
          startFadeInTransition(analogLedsState(1.0, 1.0, 1.0, 0.0, 0.0), analogLedsState(1.0, 1.0, 1.0, 1.0, 0.0)); // RAG** -> RAGB*
          break;

        case 4:
          startFadeInTransition(analogLedsState(1.0, 1.0, 1.0, 1.0, 0.0), analogLedsState(1.0, 1.0, 1.0, 1.0, 1.0)); // RAGB* -> RAGBW
          break;

        case 5:
          setDigitalLedStates(true, true, true, true, true); // RAGBW
          break;

        case 6:
          startFadeOutDuration(analogLedsState(1.0, 1.0, 1.0, 1.0, 1.0), analogLedsState(0.0, 1.0, 1.0, 1.0, 1.0)); // RAGBW -> *AGBW
          break;

        case 7:
          startFadeOutDuration(analogLedsState(0.0, 1.0, 1.0, 1.0, 1.0), analogLedsState(0.0, 0.0, 1.0, 1.0, 1.0)); // *AGBW -> **GBW
          break;

        case 8:
          startFadeOutDuration(analogLedsState(0.0, 0.0, 1.0, 1.0, 1.0), analogLedsState(0.0, 0.0, 0.0, 1.0, 1.0)); // **GBW -> ***BW
          break;

        case 9:
          startFadeOutDuration(analogLedsState(0.0, 0.0, 0.0, 1.0, 1.0), analogLedsState(0.0, 0.0, 0.0, 0.0, 1.0)); // ***BW -> ****W
          break;

        case 10:
          startFadeOutDuration(analogLedsState(0.0, 0.0, 0.0, 0.0, 1.0), allLedsOff()); // ****W -> *****
          break;

        case 11:
          setDigitalLedStates(false, false, false, false, false); // *****
          break;
      }
    }

    virtual uint32_t getStateDuration(uint8_t stateIndex) {
      if (stateIndex == 5) { // all leds are lit
        return _delayAfterFadingInAllLeds;
      } else if (stateIndex == 11) { // all leds are unlit
        return _delayAfterFadingOutAllLeds;
      } else if (stateIndex < 6) { // fading in leds one-by-one
        return _fadeInDuration + _sustainAfterFadeInDuration;
      } else { // fading out leds one-by-one
        return _fadeOutDuration + _sustainAfterFadeOutDuration;
      }
    }

  public:
    SequentialFadeController(
      uint32_t fadeInDuration, uint32_t sustainAfterFadeInDuration, uint32_t delayAfterFadingInAllLeds, 
      uint32_t fadeOutDuration, uint32_t sustainAfterFadeOutDuration, uint32_t delayAfterFadingOutAllLeds)
      : StatefulController(12), 
      _fadeInDuration(fadeInDuration), _sustainAfterFadeInDuration(sustainAfterFadeInDuration), _delayAfterFadingInAllLeds(delayAfterFadingInAllLeds),
      _fadeOutDuration(fadeOutDuration), _sustainAfterFadeOutDuration(sustainAfterFadeOutDuration), _delayAfterFadingOutAllLeds(delayAfterFadingOutAllLeds) {}
};

class SloGloController : public StatefulController {
  private:
    uint32_t _transitionDuration;
    uint32_t _sustainDuration;

    AnalogLedsState getLedStatesForState(uint8_t stateIndex) {
      if (stateIndex >= 5) {
        stateIndex -= 5;
      }
      
      switch (stateIndex) {
        case 0:
          return analogLedsState(1.0, 0.0, 0.0, 0.0, 0.0); // R****

        case 1:
          return analogLedsState(0.0, 1.0, 0.0, 0.0, 0.0); // *A***

        case 2:
          return analogLedsState(0.0, 0.0, 1.0, 0.0, 0.0); // **G**

        case 3:
          return analogLedsState(0.0, 0.0, 0.0, 1.0, 0.0); // ***B*

        case 4:
          return analogLedsState(0.0, 0.0, 0.0, 0.0, 1.0); // ****W
      }

      return allLedsOff();
    }

  protected:
    virtual void applyLedStatesForState(uint8_t stateIndex) {
      AnalogLedsState currentState = getLedStatesForState(stateIndex);
      AnalogLedsState nextState = getLedStatesForState(stateIndex + 1);

      startPwmTransition(_transitionDuration, currentState, nextState);
    }

    virtual uint32_t getStateDuration(uint8_t stateIndex) {
      return _transitionDuration + _sustainDuration;
    }

  public:
    SloGloController(uint32_t transitionDuration, uint32_t sustainDuration)
      : StatefulController(5), _transitionDuration(transitionDuration), _sustainDuration(sustainDuration) {}
};

class ChasingFlashController : public StatefulController {
  private:
    uint32_t _flashDuration;
    uint32_t _flashLoopsCount;
    uint32_t _chaseDuration;
    uint32_t _chaseTransitionDuration;
    uint32_t _chaseLoopsCount;

    void setLedStatesForFlashingState(uint8_t stateIndex) {
      uint32_t flashingStateIndex = stateIndex / _flashLoopsCount;
      uint32_t flashingStatePhaseIndex = (stateIndex % _flashLoopsCount) % 2;

      if (flashingStateIndex == 0) {
        if (flashingStatePhaseIndex == 0) {
          setDigitalLedStates(false, true, false, false, false); // *A***
        } else {
          setDigitalLedStates(true, false, false, false, false); // R****
        }
      } else if (flashingStateIndex == 1) {
        if (flashingStatePhaseIndex == 0) {
          setDigitalLedStates(false, false, true, false, false); // **G**
        } else {
          setDigitalLedStates(false, true, false, false, false); // *A***
        }
      } else if (flashingStateIndex == 2) {
        if (flashingStatePhaseIndex == 0) {
          setDigitalLedStates(false, false, false, true, false); // ***B*
        } else {
          setDigitalLedStates(false, false, true, false, false); // **G**
        }
      } else if (flashingStateIndex == 3) {
        if (flashingStatePhaseIndex == 0) {
          setDigitalLedStates(false, false, false, false, true); // ****W
        } else {
          setDigitalLedStates(false, false, false, true, false); // ***B*
        }
      } else if (flashingStateIndex == 4) {
        if (flashingStatePhaseIndex == 0) {
          setDigitalLedStates(true, false, false, false, false); // R****
        } else {
          setDigitalLedStates(false, false, false, false, true); // ****W
        }
      }
    }

    AnalogLedsState getLedsStateForChasingState(uint8_t stateIndex) {
      uint8_t chasePhaseIndex = stateIndex % 5;

      switch (chasePhaseIndex) {
        case 0:
          return analogLedsState(1.0, 1.0, 0.0, 0.0, 0.0); // RA***

        case 1:
          return analogLedsState(0.0, 1.0, 1.0, 0.0, 0.0); // *AG**

        case 2:
          return analogLedsState(0.0, 0.0, 1.0, 1.0, 0.0); // **GB*

        case 3:
          return analogLedsState(0.0, 0.0, 0.0, 1.0, 1.0); // ***BW

        case 4:
          return analogLedsState(1.0, 0.0, 0.0, 1.0, 1.0); // R***W
      }
    }

    void setLedStatesForChasingState(uint8_t stateIndex) {
      AnalogLedsState currentState = getLedsStateForChasingState(stateIndex);
      AnalogLedsState nextState = getLedsStateForChasingState(stateIndex + 1);

      startPwmTransition(_chaseTransitionDuration, currentState, nextState);
    }

  protected:
    virtual void applyLedStatesForState(uint8_t stateIndex) {
      uint32_t flashingStatesCount = _flashLoopsCount * 5;

      if (stateIndex < flashingStatesCount) {
        setLedStatesForFlashingState(stateIndex);
      } else {
        uint8_t chasingStateIndex = stateIndex - flashingStatesCount;
        setLedStatesForChasingState(chasingStateIndex);
      }
    }

    virtual uint32_t getStateDuration(uint8_t stateIndex) {
      if (stateIndex < _flashLoopsCount * 5) {
        return _flashDuration;
      } else {
        return _chaseDuration;
      }
    }

  public:
    ChasingFlashController(uint32_t flashDuration, uint32_t flashLoopsCount, uint32_t chaseDuration, uint32_t chaseTransitionDuration, uint32_t chaseLoopsCount)
      : StatefulController(flashLoopsCount * 5 + chaseLoopsCount * 5), _flashDuration(flashDuration), _flashLoopsCount(flashLoopsCount), _chaseDuration(chaseDuration), _chaseTransitionDuration(chaseTransitionDuration), _chaseLoopsCount(chaseLoopsCount) {}
};

class LedBrightnessTestingController : public StatefulController {
  private:
    uint32_t _singleLedDuration;
    uint32_t _allLedsDuration;

  protected:
    virtual void applyLedStatesForState(uint8_t stateIndex) {
      switch (stateIndex) {
        case 0:
          setDigitalLedStates(true, false, false, false, false); // R****
          break;

        case 1:
          setDigitalLedStates(false, true, false, false, false); // *A***
          break;

        case 2:
          setDigitalLedStates(false, false, true, false, false); // **G**
          break;

        case 3:
          setDigitalLedStates(false, false, false, true, false); // ***B*
          break;

        case 4:
          setDigitalLedStates(false, false, false, false, true); // ****W
          break;

        case 5:
          setDigitalLedStates(true, true, true, true, true); // RAGBW
          break;
      }
    }

    virtual uint32_t getStateDuration(uint8_t stateIndex) {
      if (stateIndex == 5) {
        return _allLedsDuration;
      } else {
        return _singleLedDuration;
      }
    }

  public:
    LedBrightnessTestingController(uint32_t singleLedDuration, uint32_t allLedsDuration)
      : StatefulController(6), _singleLedDuration(singleLedDuration), _allLedsDuration(allLedsDuration) {}
};

class AllInOneFadeController : public StatefulController {
  private:
    uint32_t _fadeInDuration;
    uint32_t _sustainDuration;
    uint32_t _fadeOutDuration;
    uint32_t _offDuration;

  protected:
    virtual void applyLedStatesForState(uint8_t stateIndex) {
      AnalogLedsState onState = analogLedsState(1.0, 1.0, 1.0, 1.0, 1.0);  // RAGBW
      AnalogLedsState offState = analogLedsState(0.0, 0.0, 0.0, 0.0, 0.0); // *****
      
      if (stateIndex == 0) {
        startPwmTransition(_fadeInDuration, offState, onState);
      } else if (stateIndex == 1) {
        startPwmTransition(_fadeOutDuration, onState, offState);
      }
    }

    virtual uint32_t getStateDuration(uint8_t stateIndex) {
      if (stateIndex == 0) {
        return _fadeInDuration + _sustainDuration;
      } else {
        return _fadeOutDuration + _offDuration;
      }
    }

  public:
    AllInOneFadeController(uint32_t fadeInDuration, uint32_t sustainDuration, uint32_t fadeOutDuration, uint32_t offDuration) 
      : StatefulController(2), _fadeInDuration(fadeInDuration), _sustainDuration(sustainDuration), _fadeOutDuration(fadeOutDuration), _offDuration(offDuration) {}
};

CombinationController combinationController(60, 60); // final
LoopingController combinationControllerLoop(&combinationController, 25);

CombinationController slowCombinationController(90, 90);
LoopingController slowCombinationControllerLoop(&slowCombinationController, 16);

FlowingTwoLedsController slowFlowingTwoLedsController(500);
LoopingController slowFlowingTwoLedsControllerLoop(&slowFlowingTwoLedsController, 6);

FlowingTwoLedsController mediumFlowingTwoLedsController(250);
LoopingController mediumFlowingTwoLedsControllerLoop(&mediumFlowingTwoLedsController, 12);

FlowingTwoLedsController fastFlowingTwoLedsController(120);
LoopingController fastFlowingTwoLedsControllerLoop(&fastFlowingTwoLedsController, 24);

SloGloController sloGloController(3000, 0);
LoopingController sloGloControllerLoop(&sloGloController, 1);

SloGloController fastGloController(1500, 0);
LoopingController fastGloControllerLoop(&fastGloController, 2);

ChasingFlashController chasingFlashController(50, 4, 100, 30, 4);
LoopingController chasingFlashControllerLoop(&chasingFlashController, 4);

AllInOneFadeController slowAllInOneFadeController(6000, 0, 6000, 0);
LoopingController slowAllInOneFadeControllerLoop(&slowAllInOneFadeController, 1);

AllInOneFadeController fastAllInOneFadeController(3000, 0, 3000, 0);
LoopingController fastAllInOneFadeControllerLoop(&fastAllInOneFadeController, 2);

// LedBrightnessTestingController ledBrightnessTestingController(500, 2500);

class SwitchingController : public ILedController {
  private:
    uint8_t _currentControllerIndex;

    ILedController* getControllerByIndex(uint8_t index) {
      switch (index) {
        case 0:
          return &combinationControllerLoop;

        case 1:
          return &slowCombinationControllerLoop;

        case 2:
          return &slowFlowingTwoLedsControllerLoop;

        case 3:
          return &mediumFlowingTwoLedsControllerLoop;

        case 4:
          return &fastFlowingTwoLedsControllerLoop;

        case 5:
          return &sloGloControllerLoop;

        case 6:
          return &fastGloControllerLoop;

        case 7:
          return &chasingFlashControllerLoop;

        case 8:
          return &slowAllInOneFadeControllerLoop;

        case 9:
          return &fastAllInOneFadeControllerLoop;
          
        default:
          return NULL;
      }
    }

    uint8_t getControllerCount() {
      return 10;
    }

    void switchToController(uint8_t controllerIndex) {
      _currentControllerIndex = controllerIndex;

      ILedController* currentController = getControllerByIndex(controllerIndex);
      if (currentController != NULL) {
        currentController->reset();
      }
    }

  public:
    SwitchingController() {
      _currentControllerIndex = 0;
    }

    virtual WorkState doControl(uint32_t timeSinceLastCall) {
      uint8_t controllerCount = getControllerCount();
      
      boolean thereIsMoreTimeToSpend = false;
      uint32_t remainingTime = timeSinceLastCall;
      uint32_t suggestedSleepTime = 0;

      do {
        ILedController* currentController = getControllerByIndex(_currentControllerIndex);
        WorkState controllerState = currentController->doControl(remainingTime);

        if (controllerState.finished) {
          _currentControllerIndex = _currentControllerIndex < controllerCount - 1 
            ? _currentControllerIndex + 1
            : 0;
            
          switchToController(_currentControllerIndex);
          remainingTime = controllerState.remainingTime;

          thereIsMoreTimeToSpend = true;
        } else {
          thereIsMoreTimeToSpend = false;
          suggestedSleepTime = controllerState.suggestedSleepTime;
        }
      } while (thereIsMoreTimeToSpend);

      return unfinishedWorkState(suggestedSleepTime);
    }

    virtual void reset() {
      switchToController(0);
    }
};

SwitchingController mainController;

Sleep sleep;

void setup() {
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(AMBER_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);
  pinMode(WHITE_LED_PIN, OUTPUT);

  pinMode(STATUS_LED_PIN, OUTPUT);

  sleep.idleMode();
}

void loop() {
  mainController.reset();

  uint32_t sleepTime = 0;

  do {
    WorkState result = mainController.doControl(sleepTime);
    sleepTime = result.suggestedSleepTime;
    
    //sleep.sleepDelay(sleepTime);
    delay(sleepTime);
  } while(true);
}
