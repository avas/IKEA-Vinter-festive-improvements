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
#define GREEN_LED_MAX_BRIGHTNESS 255

#define BLUE_LED_MIN_BRIGHTNESS 0
#define BLUE_LED_MAX_BRIGHTNESS 255

#define WHITE_LED_MIN_BRIGHTNESS 0
#define WHITE_LED_MAX_BRIGHTNESS 255

#define PWM_ANIMATION_INTERVAL 30

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

class TwoLedCombinationsController : public StatefulController {
  private:
    uint32_t _stateDuration;
  
  protected:
    virtual void applyLedStatesForState(uint8_t stateIndex) {
      switch (stateIndex) {
        case 0:
          setDigitalLedStates(true, true, false, false, false); // RA***
          break;

        case 1:
          setDigitalLedStates(false, false, true, true, false); // **GB*
          break;

        case 2:
          setDigitalLedStates(true, false, false, false, true); // R***W
          break;

        case 3:
          setDigitalLedStates(false, true, true, false, false); // *AG**
          break;

        case 4:
          setDigitalLedStates(false, false, false, true, true); // ***BW
          break;

        case 5:
          setDigitalLedStates(true, false, true, false, false); // R*G**
          break;

        case 6:
          setDigitalLedStates(false, true, false, true, false); // *A*B*
          break;

        case 7:
          setDigitalLedStates(false, false, true, false, true); // **G*W
          break;

        case 8:
          setDigitalLedStates(true, false, false, true, false); // R**B*
          break;

        case 9:
          setDigitalLedStates(false, true, false, false, true); // *A**W
          break;
      }
    }

    virtual uint32_t getStateDuration(uint8_t stateIndex) {
      return _stateDuration;
    }

  public:
    // There are 10 unique combinations of 2 LEDs from 5 leds in total:
    // 0: RA***
    // 1: **GB*
    // 2: R***W
    // 3: *AG**
    // 4: ***BW
    // 5: R*G**
    // 6: *A*B*
    // 7: **G*W
    // 8: R**B*
    // 9: *A**W
    
    TwoLedCombinationsController(uint32_t timePerState)
      : StatefulController(10), _stateDuration(timePerState) {}
};

class FlowingThreeLedsController : public StatefulController {
  private:
    uint32_t _stateDuration;
  
  protected:
    virtual void applyLedStatesForState(uint8_t stateIndex) {
      switch (stateIndex) {
        case 0:
          setDigitalLedStates(true, true, true, false, false); // RAG**
          break;

        case 1:
          setDigitalLedStates(false, true, true, true, false); // *AGB*
          break;

        case 2:
          setDigitalLedStates(false, false, true, true, true); // **GBW
          break;

        case 3:
          setDigitalLedStates(true, false, false, true, true); // R**BW
          break;

        case 4:
          setDigitalLedStates(true, true, false, false, true); // RA**W
          break;
      }
    }

    virtual uint32_t getStateDuration(uint8_t stateIndex) {
      return _stateDuration;
    }

  public:
    FlowingThreeLedsController(uint32_t timePerState) 
      : StatefulController(5), _stateDuration(timePerState) {}
};

class TwoLedCombinationsFadeOutThenInController : public StatefulController {
  private:
    uint32_t _fadeOutDuration;
    uint32_t _sustainAfterFadeOutDuration;
    uint32_t _fadeInDuration;
    uint32_t _sustainAfterFadeInDuration;

    AnalogLedsState getLedsStateForState(uint8_t stateIndex) {
      // States for this controller follow in pairs: RAGBW -> (some state) and (some state) -> RAGBW.
      // This method just defines what this (some state) should be.
      
      switch (stateIndex / 2) {
        case 0:
          return analogLedsState(0.0, 0.0, 1.0, 1.0, 1.0); // **GBW

        case 1:
          return analogLedsState(1.0, 1.0, 0.0, 0.0, 1.0); // RA**W

        case 2:
          return analogLedsState(0.0, 1.0, 1.0, 1.0, 0.0); // *AGB*

        case 3:
          return analogLedsState(1.0, 0.0, 0.0, 1.0, 1.0); // R**BW

        case 4:
          return analogLedsState(1.0, 1.0, 1.0, 0.0, 0.0); // RAG**

        case 5:
          return analogLedsState(0.0, 1.0, 0.0, 1.0, 1.0); // *A*BW

        case 6:
          return analogLedsState(1.0, 0.0, 1.0, 0.0, 1.0); // R*G*W

        case 7:
          return analogLedsState(1.0, 1.0, 0.0, 1.0, 0.0); // RA*B*

        case 8:
          return analogLedsState(0.0, 1.0, 1.0, 0.0, 1.0); // *AG*W

        case 9:
          return analogLedsState(1.0, 0.0, 1.0, 1.0, 0.0); // R*GB*

        default:
          return allLedsOff();
      }
    }

  protected:
    virtual void applyLedStatesForState(uint8_t stateIndex) {
      AnalogLedsState targetLedsState = getLedsStateForState(stateIndex);

      if (stateIndex % 2 == 0) {
        startFadeOutFromAllLeds(_fadeOutDuration, targetLedsState);
      } else {
        startFadeInToAllLeds(_fadeInDuration, targetLedsState);
      }
    }

    virtual uint32_t getStateDuration(uint8_t stateIndex) {
      if (stateIndex % 2 == 0) {
        return _fadeOutDuration + _sustainAfterFadeOutDuration;
      } else {
        return _fadeInDuration + _sustainAfterFadeInDuration;
      }
    }

  public:
    TwoLedCombinationsFadeOutThenInController(uint32_t fadeOutDuration, uint32_t sustainAfterFadeOutDuration, uint32_t fadeInDuration, uint32_t sustainAfterFadeInDuration)
      : StatefulController(20), _fadeOutDuration(fadeOutDuration), _sustainAfterFadeOutDuration(sustainAfterFadeOutDuration), _fadeInDuration(fadeInDuration), _sustainAfterFadeInDuration(sustainAfterFadeInDuration) {}
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

class OneByOneFadeController : public StatefulController {
  private:
    uint32_t _singleLedFadeInDuration;
    uint32_t _singleLedSustainAfterFadeInDuration;
    uint32_t _singleLedFadeOutDuration;
    uint32_t _singleLedSustainAfterFadeOutDuration;
    uint32_t _allLedsFadeInDuration;
    uint32_t _allLedsSustainAfterFadeInDuration;
    uint32_t _allLedsFadeOutDuration;
    uint32_t _allLedsSustainAfterFadeOutDuration;

    void startSingleLedFadeIn(AnalogLedsState target) {
      startFadeInFromNoneLeds(_singleLedFadeInDuration, target);
    }

    void startSingleLedFadeOut(AnalogLedsState initial) {
      startFadeOutToNoneLeds(_singleLedFadeOutDuration, initial);
    }

    void startAllLedsFadeIn() {
      startFadeInFromNoneLeds(_allLedsFadeInDuration, allLedsOn());
    }

    void startAllLedsFadeOut() {
      startFadeOutToNoneLeds(_allLedsFadeOutDuration, allLedsOn());
    }

    AnalogLedsState getLedsStateForSingleLedFadeState(uint8_t stateIndex) {
      // Single LED transitions go in pairs in this controller: ***** -> (some state) and (someState) -> *****.
      // This method calculates (some state) for given state index if it corresponds to single LED transition.

      if (stateIndex < 10) {
        uint8_t transitionIndex = stateIndex / 2;

        switch (transitionIndex) {
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
      } 

      return allLedsOff();
    }

  protected:
    virtual void applyLedStatesForState(uint8_t stateIndex) {
      if (stateIndex < 10) {
        AnalogLedsState targetState = getLedsStateForSingleLedFadeState(stateIndex);

        if (stateIndex % 2 == 0) {
          startSingleLedFadeIn(targetState);
        } else {
          startSingleLedFadeOut(targetState);
        }
      } else if (stateIndex == 10) {
        startAllLedsFadeIn();
      } else if (stateIndex == 11) {
        startAllLedsFadeOut();
      }
    }

    virtual uint32_t getStateDuration(uint8_t stateIndex) {
      if (stateIndex == 10) { // all LEDs fade in
        return _allLedsFadeInDuration + _allLedsSustainAfterFadeInDuration;
      } else if (stateIndex == 11) { // all LEDs fade out
        return _allLedsFadeOutDuration + _allLedsSustainAfterFadeOutDuration;
      } else if (stateIndex % 2 == 0) { // single LED fade in
        return _singleLedFadeInDuration + _singleLedSustainAfterFadeInDuration;
      } else { // single LED fade out
        return _singleLedFadeOutDuration + _singleLedSustainAfterFadeOutDuration;
      }
    }

  public:
    OneByOneFadeController(
      uint32_t singleLedFadeInDuration, uint32_t singleLedSustainAfterFadeInDuration, uint32_t singleLedFadeOutDuration, uint32_t singleLedSustainAfterFadeOutDuration,
      uint32_t allLedsFadeInDuration, uint32_t allLedsSustainAfterFadeInDuration, uint32_t allLedsFadeOutDuration, uint32_t allLedsSustainAfterFadeOutDuration)
      : StatefulController(12),
      _singleLedFadeInDuration(singleLedFadeInDuration), _singleLedSustainAfterFadeInDuration(singleLedSustainAfterFadeInDuration), 
      _singleLedFadeOutDuration(singleLedFadeOutDuration), _singleLedSustainAfterFadeOutDuration(singleLedSustainAfterFadeOutDuration),
      _allLedsFadeInDuration(allLedsFadeInDuration), _allLedsSustainAfterFadeInDuration(allLedsSustainAfterFadeInDuration),
      _allLedsFadeOutDuration(allLedsFadeOutDuration), _allLedsSustainAfterFadeOutDuration(allLedsSustainAfterFadeOutDuration) {}
};

TwoLedCombinationsController slowTwoLedCombinationsController(1000);
LoopingController slowTwoLedCombinationsControllerLoop(&slowTwoLedCombinationsController, 2);

TwoLedCombinationsController fastTwoLedCombinationsController(500);
LoopingController fastTwoLedCombinationsControllerLoop(&fastTwoLedCombinationsController, 4);

FlowingThreeLedsController flowingThreeLedsController(100);
LoopingController flowingThreeLedsControllerLoop(&flowingThreeLedsController, 20);

TwoLedCombinationsFadeOutThenInController twoLedCombinationsFadeOutThenInController(150, 100, 150, 100);
LoopingController twoLedCombinationsFadeOutThenInControllerLoop(&twoLedCombinationsFadeOutThenInController, 4);

SequentialFadeController sequentialFadeController(150, 50, 1000, 300, 100, 1000);
LoopingController sequentialFadeControllerLoop(&sequentialFadeController, 3);

OneByOneFadeController oneByOneFadeController(1000, 2000, 1000, 0, 3000, 5000, 3000, 1000);

class SwitchingController : public ILedController {
  private:
    uint8_t _currentControllerIndex;

    ILedController* getControllerByIndex(uint8_t index) {
      switch (index) {
        // TODO: fix order after debugging
        case 0:
          return &slowTwoLedCombinationsControllerLoop;

        case 1:
          return &fastTwoLedCombinationsControllerLoop;

        case 2:
          return &sequentialFadeControllerLoop;

        case 3:
          return &flowingThreeLedsControllerLoop;

        case 4:
          return &twoLedCombinationsFadeOutThenInControllerLoop;

        case 5:
          return &oneByOneFadeController;

        default:
          return NULL;
      }
    }

    uint8_t getControllerCount() {
      return 6;
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
    
    sleep.sleepDelay(sleepTime);
  } while(true);
}
