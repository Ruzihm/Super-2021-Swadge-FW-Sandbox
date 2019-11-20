/*
*   mode_color_movement.c
*
*   Created on: 10 Oct 2019
*               Author: bbkiw
*/

//TODO
//Could have buffers for values bigger than 120 so for
// example estimating bpm via tau could go lower than 60/119
// need to modify plotting on OLED to should most recent 120 values
// Use buttons to select options
//     ways to use accel or built in wave forms
//     estimation of bpm or DFT from cc

#include <osapi.h>
#include <user_interface.h>
#include <stdlib.h>
#include "maxtime.h"
#include "user_main.h"  //swadge mode
#include "mode_color_movement.h"
#include "mode_dance.h"
#include "ccconfig.h"
#include "embeddednf.h"
#include "DFT32.h"
#include "buttons.h"
#include "oled.h"       //display functions
#include "font.h"       //draw text
#include "bresenham.h"  //draw shapes
#include "hpatimer.h"   //for sound
#include "linked_list.h" //custom linked list
#include "custom_commands.h" //saving and loading high scores and last scores
#include "math.h"
#include "embeddedout.h"
#include "mode_roll.h"

/*============================================================================
 * Defines
 *==========================================================================*/

//#define CM_DEBUG_PRINT
#ifdef CM_DEBUG_PRINT
    #include <stdlib.h>
    #define CM_printf(...) os_printf(__VA_ARGS__)
#else
    #define CM_printf(...)
#endif


#ifndef max
    #define max(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
    #define min(a,b) ((a) < (b) ? (a) : (b))
#endif

#define CLAMP(x, low, high) (max((min((x),(high))),(low)))

// controls (title)
#define BTN_TITLE_CHOOSE_SUBMODE LEFT
#define BTN_TITLE_START_SUBMODE RIGHT

// controls (game)
#define BTN_GAME_CYCLE_BRIGHTNESS LEFT
#define BTN_GAME_BACK_TO_TITLE RIGHT

// update task (16 would give 60 fps like ipad, need read accel that fast too?)
//TODO note cant handle 120 fps using 8ms
#define UPDATE_TIME_MS 16

// time info.
#define MS_TO_US_FACTOR 1000
#define S_TO_MS_FACTOR 1000
#define BPM_SAMPLE_TIME 12000 // in ms
#define BPM_BUF_SIZE (BPM_SAMPLE_TIME / UPDATE_TIME_MS)

#define NUM_IN_CIRCULAR_BUFFER 120
#define NUM_DOTS_TO_PLOT 120
#define SOUND_ON true
#define ALPHA_FAST 0.3
#define ALPHA_SLOW 0.02
//#define ALPHA_CROSS 0.1
#define ALPHA_ACTIVE 0.03
// #define CROSS_TOL 0
#define SPECIAL_EFFECT true

// LEDs relation to screen
#define LED_UPPER_LEFT LED_1
#define LED_UPPER_MID LED_2
#define LED_UPPER_RIGHT LED_3
#define LED_LOWER_RIGHT LED_4
#define LED_LOWER_MID LED_5
#define LED_LOWER_LEFT LED_6

#define SCREEN_BORDER 0

// any enums go here.

typedef enum
{
    CM_TITLE,   // title screen
    CM_GAME,    // play the actual game
} cmState_t;

typedef enum
{
    UPPER_LEFT,
    LOWER_LEFT,
    LOWER_RIGHT,
    UPPER_RIGHT
} exitSpot_t;

typedef enum
{
    BEAT_SPIN,
    BEAT_SELECT,
    SHOCK_CHANGE,
    SHOCK_CHAOTIC,
    ROLL_BALL,
    ROLL_3_BALLS,
    TILT_A_COLOR,
    POV_EFFECT,
    DFT_SHAKE,
    POWER_SHAKE
} subMethod_t;

typedef enum
{
    XYZ2RGB,
    BPM2HUE,
} colorMethod_t;

typedef enum
{
    ALL_SAME,
    RAINBOW,
} displayMethod_t;



// Circular buffer used to store last NUM_IN_CIRCULAR_BUFFER of accelerometer
// readings. Need only to insert, read certain values and plot from
// oldest to newest
typedef struct circularBuffers
{
    int16_t* buffer;
    uint16_t insertHeadInd;
    uint16_t removeTailInd;
    uint16_t length;
} circularBuffer_t;


// Title screen info.

// function prototypes go here.
/*============================================================================
 * Prototypes
 *==========================================================================*/

void ICACHE_FLASH_ATTR cmInit(void);
void ICACHE_FLASH_ATTR cmDeInit(void);
void ICACHE_FLASH_ATTR cmButtonCallback(uint8_t state, int button, int down);
void ICACHE_FLASH_ATTR cmAccelerometerCallback(accel_t* accel);
void ICACHE_FLASH_ATTR cmSampleHandler(int32_t samp);

// game loop functions.
void ICACHE_FLASH_ATTR cmUpdate(void* arg);

// handle inputs.
void ICACHE_FLASH_ATTR cmTitleInput(void);
void ICACHE_FLASH_ATTR cmGameInput(void);

// update any input-unrelated logic.
void ICACHE_FLASH_ATTR cmTitleUpdate(void);
void ICACHE_FLASH_ATTR cmGameUpdate(void);

// draw the frame.
void ICACHE_FLASH_ATTR cmTitleDisplay(void);
void ICACHE_FLASH_ATTR cmGameDisplay(void);

// mode state management.
void ICACHE_FLASH_ATTR cmChangeState(cmState_t newState);

// input checking.
bool ICACHE_FLASH_ATTR cmIsButtonPressed(uint8_t button);
bool ICACHE_FLASH_ATTR cmIsButtonReleased(uint8_t button);
bool ICACHE_FLASH_ATTR cmIsButtonDown(uint8_t button);
bool ICACHE_FLASH_ATTR cmIsButtonUp(uint8_t button);

// drawing functions.
static void ICACHE_FLASH_ATTR plotCenteredText(uint8_t x0, uint8_t y, uint8_t x1, char* text, fonts font, color col);
static uint8_t getTextWidth(char* text, fonts font);

// Additional Helper
void ICACHE_FLASH_ATTR setCMLeds(led_t* ledData, uint8_t ledDataLen);
void ICACHE_FLASH_ATTR cmChangeLevel(void);

void ICACHE_FLASH_ATTR cmNewSetup(subMethod_t subMode);

void ICACHE_FLASH_ATTR initCircularBuffer(circularBuffer_t* cirbuff,  int16_t* buffer, uint16_t length);
int16_t ICACHE_FLASH_ATTR getCircularBufferAtIndex(circularBuffer_t cirbuff,  int16_t index);
void ICACHE_FLASH_ATTR circularPush(int16_t value, circularBuffer_t* cirbuff);
int32_t ICACHE_FLASH_ATTR sumOfBuffer(circularBuffer_t cbuf, uint16_t numMostRecent);
int32_t ICACHE_FLASH_ATTR maxOfBuffer(circularBuffer_t cbuf, uint16_t numMostRecent);
int32_t ICACHE_FLASH_ATTR sumAbsOfBuffer(circularBuffer_t cbuf, uint16_t numMostRecent);
int16_t ICACHE_FLASH_ATTR IIRFilter(float alpha, int16_t input, int16_t output);
void ICACHE_FLASH_ATTR AdjustAndPlotDots(circularBuffer_t cbuf1, circularBuffer_t cbuf2);
void ICACHE_FLASH_ATTR AdjustAndPlotDotsSingle(circularBuffer_t cbuf);

/*============================================================================
 * Static Const Variables
 *==========================================================================*/

// When using gamma correcton
static const uint8_t cmBrightnesses[] =
{
    0x01,
    0x02,
    0x04,
    0x08,
};

const char* subModeName[] = {"BEAT_SPIN", "BEAT_SELECT", "SHOCK_CHANGE",
                             "SHOCK_CHAOTIC", "ROLL_BALL", "ROLL_3_BALLS", "TILT_A_COLOR",
                             "POV_EFFECT", "DFT_SHAKE", "POWER_SHAKE"
                            };



/*============================================================================
 * Variables
 *==========================================================================*/


// game logic operations.

swadgeMode colorMoveMode =
{
    .modeName = "ColorShake",
    .fnEnterMode = cmInit,
    .fnExitMode = cmDeInit,
    .fnButtonCallback = cmButtonCallback,
    .wifiMode = NO_WIFI,
    .fnEspNowRecvCb = NULL,
    .fnEspNowSendCb = NULL,
    .fnAccelerometerCallback = cmAccelerometerCallback,
    .menuImageData = mnu_colorshake_0,
    .menuImageLen = sizeof(mnu_colorshake_0)
};

// Parameters controling submodes

bool cmUseHighPassAccel;
bool cmUseSmooth;
bool cmFilterAllWithIIR;
bool cmUsePOVeffect;
bool cmUseShiftingLeds;
bool cmUseShiftingColorWheel;
bool cmUseColorChordDFT;

uint16_t cmNumSum;
uint16_t cmScaleLed;
colorMethod_t cmComputeColor;
displayMethod_t cmLedMethod;
FLOATING cmalphaSlow;
FLOATING cmalphaSmooth;
uint8_t cmShowNumLeds;
uint8_t cmNumSubFrames;
float cmRevsPerBeat;
// TODO could change this order for other display effects
uint8_t ledOrderInd[] = {LED_UPPER_LEFT, LED_LOWER_LEFT, LED_LOWER_MID, LED_LOWER_RIGHT, LED_UPPER_RIGHT, LED_UPPER_MID};

// end of parameters

// If use CC
int gFRAMECOUNT_MOD_SHIFT_INTERVAL = 0;
int gROTATIONSHIFT = 0; //Amount of spinning of pattern around a LED ring

int cmSamplesProcessed = 0;
accel_t cmAccel = {0};
accel_t cmLastAccel = {0};
accel_t cmLastTestAccel = {0};
uint8_t cmButtonState = 0;
uint8_t cmLastButtonState = 0;
uint8_t cmBrightnessIdx = 0;
static led_t leds[NUM_LIN_LEDS] = {{0}};

subMethod_t cmCurrentSubMode;
uint8_t cmNumSubModes = sizeof(subModeName) / sizeof(subModeName[0]);

static os_timer_t timerHandleUpdate = {0};

static uint32_t modeStartTime = 0; // time mode started in microseconds.
static uint32_t stateStartTime = 0; // time the most recent state started in microseconds.
static uint32_t deltaTime = 0;  // time elapsed since last update.
static uint32_t modeTime = 0;   // total time the mode has been running.
static uint32_t stateTime = 0;  // total time the game has been running.

static cmState_t currState = CM_TITLE;
static cmState_t prevState;


int16_t xAccel;
int16_t yAccel;
int16_t zAccel;
int16_t bpmFromCrossing;
int16_t bpmFromTau;
int16_t lastsign;

int16_t maxCheck1 = -32768;
int16_t minCheck1 = 32767;
int16_t maxCheck2 = -32768;
int16_t minCheck2 = 32767;
int16_t maxCheck3 = -32768;
int16_t minCheck3 = 32767;



FLOATING cmxAccelSlowAve;
FLOATING cmyAccelSlowAve;
FLOATING cmzAccelSlowAve;
FLOATING cmxAccelHighPassSmoothed;
FLOATING cmyAccelHighPassSmoothed;
FLOATING cmzAccelHighPassSmoothed;

circularBuffer_t  cirBufNormAccel;
circularBuffer_t  cirBufHighPassNormAccel;
circularBuffer_t  cirBufXaccel;
circularBuffer_t  cirBufLowPassXaccel;
circularBuffer_t  cirBufYaccel;
circularBuffer_t  cirBufLowPassYaccel;
circularBuffer_t  cirBufZaccel;
circularBuffer_t  cirBufLowPassZaccel;
circularBuffer_t  cirBufCrossings;

int16_t bufNormAccel[NUM_IN_CIRCULAR_BUFFER];
int16_t bufHighPassNormAccel[NUM_IN_CIRCULAR_BUFFER];
int16_t bufXaccel[NUM_IN_CIRCULAR_BUFFER];
int16_t bufLowPassXaccel[NUM_IN_CIRCULAR_BUFFER];
int16_t bufYaccel[NUM_IN_CIRCULAR_BUFFER];
int16_t bufLowPassYaccel[NUM_IN_CIRCULAR_BUFFER];
int16_t bufZaccel[NUM_IN_CIRCULAR_BUFFER];
int16_t bufLowPassZaccel[NUM_IN_CIRCULAR_BUFFER];
int16_t bufCrossings[BPM_BUF_SIZE];

int16_t deviations[NUM_IN_CIRCULAR_BUFFER];

uint8_t PLOT_SCALE = 32;
uint8_t PLOT_SHIFT = 32;

int16_t subFrameCount = 0; // which sub frame used for POV effects

int16_t adj = SCREEN_BORDER;
int16_t wid = 128 - 2 * SCREEN_BORDER;

uint8_t ledCycle = 0;
uint8_t cmLedCount = 0;

int16_t lowPassNormAccel = 0;
int16_t lowPassXaccel = 0;
int16_t lowPassYaccel = 0;
int16_t lowPassZaccel = 0;
int16_t smoothNormAccel = 0;
int16_t smoothXaccel = 0;
int16_t smoothYaccel = 0;
int16_t smoothZaccel = 0;
int16_t prevHighPassNormAccel = 0;
int16_t smoothActivity = 0;
uint32_t cmCumulativeActivity = 0;
int8_t ledDirection = 1;

// for zero crossing measure time is number of cycles of update
uint16_t crossIntervalCounter = 0;
uint32_t ledPrevIncTime = 0;
uint16_t avePeriodMs = 5;
//uint16_t crossInterval = 5;
bool showCrossOnLed = false;
bool cmCollectingActivity = true;

// Helpers

void ICACHE_FLASH_ATTR initCircularBuffer(circularBuffer_t* cirbuff,  int16_t* buffer, uint16_t length)
{
    cirbuff->length = length;
    cirbuff->buffer = buffer;
    cirbuff->insertHeadInd = 0;
    cirbuff->removeTailInd = 0;
    //TODO is this correct use of memset?
    memset(buffer, 0, length * sizeof(int16_t));
}

int16_t ICACHE_FLASH_ATTR getCircularBufferAtIndex(circularBuffer_t cirbuff,  int16_t index)
{
    // index can be positive, zero, or negative (index is take modulo cirbuff.length)
    // index = -1 is last value placed
    // index = -2 is second to last value placed etc.
    // index = 0 is oldest value
    int16_t i = cirbuff.insertHeadInd + index;
    while (i < 0)
    {
        i += cirbuff.length;
    }
    return cirbuff.buffer[i % cirbuff.length];
}

void ICACHE_FLASH_ATTR circularPush(int16_t value, circularBuffer_t* cirbuff)
{
    cirbuff->buffer[cirbuff->insertHeadInd] = value;
    cirbuff->insertHeadInd = (cirbuff->insertHeadInd + 1) % cirbuff->length;
}

int16_t ICACHE_FLASH_ATTR IIRFilter(float alpha, int16_t  input, int16_t output)
{
    // return updated output and returns it
    return  (1 - alpha) * output + alpha * input;
}

void ICACHE_FLASH_ATTR AdjustAndPlotDots(circularBuffer_t cbuf1, circularBuffer_t cbuf2)
{
    // Plots a graph with x from 0 to 119 and y from cbuf1.buffer - cbuf2.buffer
    uint16_t i;
    uint16_t i1 = cbuf1.insertHeadInd; // oldest
    uint16_t i2 = cbuf2.insertHeadInd;
    for (i = 0; i < min(NUM_DOTS_TO_PLOT, cbuf1.length); i++, i1++, i2++)
    {
        i1 = (i1 < cbuf1.length) ? i1 : 0;
        i2 = (i2 < cbuf2.length) ? i2 : 0;
        drawPixel(i, (cbuf1.buffer[i1] - cbuf2.buffer[i2]) / 25 + PLOT_SHIFT, WHITE);
    }
}
void ICACHE_FLASH_ATTR AdjustAndPlotDotsSingle(circularBuffer_t cbuf)
{
    // Plots a graph with x from 0 to 119 and y from cbuf.buffer
    uint16_t i;
    uint16_t i1 = cbuf.insertHeadInd; // oldest
    for (i = 0; i < min(NUM_DOTS_TO_PLOT, cbuf.length); i++, i1++)
    {
        i1 = (i1 < cbuf.length) ? i1 : 0;
        drawPixel(i, cbuf.buffer[i1] / 30 + PLOT_SHIFT, WHITE);
    }
}

// Could easily adapt to have unequal weighted sum to implement other filters
int32_t ICACHE_FLASH_ATTR sumOfBuffer(circularBuffer_t cbuf, uint16_t numMostRecent)
{
    // computes sum of numMostRecent values ie use to find running average
    //   (if numMostRecent > cbuf.length values will be used more than once)
    uint16_t i;
    int32_t sum = 0;
    for (i = 0; i < numMostRecent; i++)
    {
        sum += getCircularBufferAtIndex(cbuf, -1 - i);
    }
    return sum;
}

int32_t ICACHE_FLASH_ATTR sumAbsOfBuffer(circularBuffer_t cbuf, uint16_t numMostRecent)
{
    // computes sum of numMostRecent abs(values) used to find average 'energy'
    //   (if numMostRecent > cbuf.length values will be used more than once)
    uint16_t i;
    int32_t sum = 0;
    for (i = 0; i < numMostRecent; i++)
    {
        sum += abs(getCircularBufferAtIndex(cbuf, -1 - i));
    }
    return sum;
}

int32_t ICACHE_FLASH_ATTR maxOfBuffer(circularBuffer_t cbuf, uint16_t numMostRecent)
{
    // computes max of numMostRecent values
    //   (if numMostRecent > cbuf.length get same as using = cbuf.length)
    uint16_t i;
    int32_t maxOfValues = 0x80000000;
    for (i = 0; i < numMostRecent; i++)
    {
        maxOfValues = max(maxOfValues, getCircularBufferAtIndex(cbuf, -1 - i));
    }
    return maxOfValues;
}

void ICACHE_FLASH_ATTR cmInit(void)
{
    // External from mode_dance to set brightness when using dance mode display
    setDanceBrightness(2);

    //TODO only if going to use CC
    // Use ColorChord code to process accelerometer samples
    InitColorChord();
    cmSamplesProcessed = 0;

    // Give us reliable button input.
    enableDebounce(false);

    // Reset mode time tracking.
    modeStartTime = system_get_time();
    modeTime = 0;

    // Reset state stuff.
    cmChangeState(CM_TITLE);

    // Set up all initialization
    cmCurrentSubMode = BEAT_SPIN;
    cmNewSetup(cmCurrentSubMode);
    rollEnterMode();

    // Start the update loop.
    os_timer_disarm(&timerHandleUpdate);
    os_timer_setfn(&timerHandleUpdate, (os_timer_func_t*)cmUpdate, NULL);
    os_timer_arm(&timerHandleUpdate, UPDATE_TIME_MS, 1);
}

void ICACHE_FLASH_ATTR cmDeInit(void)
{
    os_timer_disarm(&timerHandleUpdate);
}

void ICACHE_FLASH_ATTR cmButtonCallback(uint8_t state, int button __attribute__((unused)),
                                        int down __attribute__((unused)))
{
    cmButtonState = state;  // Set the state of all buttons
}

void ICACHE_FLASH_ATTR cmAccelerometerCallback(accel_t* accel)
{
    // Set the accelerometer values
    // x coor relates to left right on OLED
    // y coor relates to up down on OLED
    cmAccel.x = accel->y;
    cmAccel.y = accel->x;
    cmAccel.z = accel->z;

    if (cmUseHighPassAccel)
    {
        cmxAccelSlowAve = (1.0 - cmalphaSlow) * cmxAccelSlowAve + cmalphaSlow * (float)cmAccel.x;
        cmyAccelSlowAve = (1.0 - cmalphaSlow) * cmyAccelSlowAve + cmalphaSlow * (float)cmAccel.y;
        cmzAccelSlowAve = (1.0 - cmalphaSlow) * cmzAccelSlowAve + cmalphaSlow * (float)cmAccel.z;

        cmAccel.x = cmAccel.x - cmxAccelSlowAve;
        cmAccel.y = cmAccel.y - cmyAccelSlowAve;
        cmAccel.z = cmAccel.z - cmzAccelSlowAve;
    }
    if (cmUseSmooth)
    {
        cmxAccelHighPassSmoothed = (1.0 - cmalphaSmooth) * cmxAccelHighPassSmoothed + cmalphaSmooth *
                                   (float)cmAccel.x;
        cmyAccelHighPassSmoothed = (1.0 - cmalphaSmooth) * cmyAccelHighPassSmoothed + cmalphaSmooth *
                                   (float)cmAccel.y;
        cmzAccelHighPassSmoothed = (1.0 - cmalphaSmooth) * cmzAccelHighPassSmoothed + cmalphaSmooth *
                                   (float)cmAccel.z;

        cmAccel.x = cmxAccelHighPassSmoothed;
        cmAccel.y = cmyAccelHighPassSmoothed;
        cmAccel.z = cmzAccelHighPassSmoothed;
    }
}

/**
 * Intermediate function which adjusts brightness and sets the LEDs
 *
 * @param ledData    The LEDs to be scaled, then gamma corrected, then set
 * @param ledDataLen The length of the LEDs to set
 */
void ICACHE_FLASH_ATTR setCMLeds(led_t* ledData, uint8_t ledDataLen)
{
    uint8_t i;
    led_t ledsAdjusted[NUM_LIN_LEDS];
    for(i = 0; i < ledDataLen / sizeof(led_t); i++)
    {
        ledsAdjusted[i].r = GAMMA_CORRECT(ledData[i].r / cmBrightnesses[cmBrightnessIdx]);
        ledsAdjusted[i].g = GAMMA_CORRECT(ledData[i].g / cmBrightnesses[cmBrightnessIdx]);
        ledsAdjusted[i].b = GAMMA_CORRECT(ledData[i].b / cmBrightnesses[cmBrightnessIdx]);
    }
    setLeds(ledsAdjusted, ledDataLen);
}

int32_t mzMaxSamp = -30000;
int32_t mzMinSamp = 30000;
/**
 * Just run colorchord but will be given accelerometer samples
 *
 * @param samp A 32 bit sample
 */
void ICACHE_FLASH_ATTR cmSampleHandler(int32_t samp)
{
    //os_printf("%d\n", samp);
    if (abs(samp) > 0)
    {
        PushSample32( samp );
    }
    else
    {
        PushSample32(0);
        //os_printf("%d ", samp);
    }
    // Here  -2700 < samp < 2780 from audio in demo mode
    // if (samp < mzMinSamp)
    // {
    //     mzMinSamp = samp;
    //     os_printf("range %d to %d\n", mzMinSamp, mzMaxSamp);
    // }
    // if (samp > mzMaxSamp)
    // {
    //     mzMaxSamp = samp;
    //     os_printf("range %d to %d\n", mzMinSamp, mzMaxSamp);
    // }

    cmSamplesProcessed++;

    // If at least NUM_SAMPLES_PER_FRAME samples have been processed
    if( cmSamplesProcessed >= NUM_SAMPLES_PER_FRAME )
    {
        //os_printf("COLORCHORD_ACTIVE %d DFTIIR %d\n", COLORCHORD_ACTIVE, DFTIIR);
        // Don't bother if colorchord is inactive
        if( !COLORCHORD_ACTIVE )
        {
            return;
        }


        // Colorchord magic
        HandleFrameInfo();

        // Set LEDs


        gFRAMECOUNT_MOD_SHIFT_INTERVAL++;
        if ( gFRAMECOUNT_MOD_SHIFT_INTERVAL >= COLORCHORD_SHIFT_INTERVAL )
        {
            gFRAMECOUNT_MOD_SHIFT_INTERVAL = 0;
        }
        //printf("MOD FRAME %d ******\n", gFRAMECOUNT_MOD_SHIFT_INTERVAL);

        switch( COLORCHORD_OUTPUT_DRIVER )
        {
            case 254:
                PureRotatingLEDs();
                break;
            case 255:
                DFTInLights();
                break;
            default:
                UpdateLinearLEDs(); // have variety of display options and uses COLORCHORD_OUTPUT_DRIVER to select them
        };

        // could use this if want overall brightness control
        //setCMLeds( (led_t*)ledOut, NUM_LIN_LEDS * 3 );
        setLeds( (led_t*)ledOut, NUM_LIN_LEDS * 3 );

        // Reset the sample count
        cmSamplesProcessed = 0;
    }
}


void ICACHE_FLASH_ATTR cmUpdate(void* arg __attribute__((unused)))
{
    // Update time tracking.
    // NOTE: delta time is in microseconds.
    // UPDATE time is in milliseconds.

    uint32_t newModeTime = system_get_time() - modeStartTime;
    uint32_t newStateTime = system_get_time() - stateStartTime;
    deltaTime = newModeTime - modeTime;
    modeTime = newModeTime;
    stateTime = newStateTime;

    // Handle Input (based on the state)
    switch( currState )
    {
        case CM_TITLE:
        {
            cmTitleInput();
            break;
        }
        case CM_GAME:
        {
            cmGameInput();
            break;
        }
        default:
            break;
    };

    // Mark what our inputs were the last time we acted on them.
    cmLastButtonState = cmButtonState;
    cmLastAccel = cmAccel;

    // Handle Game Logic (based on the state)
    switch( currState )
    {
        case CM_TITLE:
        {
            cmTitleUpdate();
            break;
        }
        case CM_GAME:
        {
            cmGameUpdate();
            break;
        }
        default:
            break;
    };

    // Handle Drawing Frame (based on the state)
    switch( currState )
    {
        case CM_TITLE:
        {
            cmTitleDisplay();
            break;
        }
        case CM_GAME:
        {
            cmGameDisplay();
            break;
        }
        default:
            break;
    };
}

void ICACHE_FLASH_ATTR cmTitleInput(void)
{
    //button a = start sub mode
    if(cmIsButtonPressed(BTN_TITLE_START_SUBMODE))
    {
        //os_printf("%s should start game?\n", __func__);
        cmChangeState(CM_GAME);
    }
    //button b = choose a sub mode
    else if(cmIsButtonPressed(BTN_TITLE_CHOOSE_SUBMODE))
        // Cycle movement methods
    {
        cmCurrentSubMode = (cmCurrentSubMode + 1) % cmNumSubModes;
        os_printf("currentSubMode = %d\n", cmCurrentSubMode);
        //reset init conditions for new method
        cmNewSetup(cmCurrentSubMode);
        cmChangeState(CM_TITLE);
    }
}

void ICACHE_FLASH_ATTR cmGameInput(void)
{
    //button b = back to title so can choose submode again
    if(cmIsButtonPressed(BTN_GAME_BACK_TO_TITLE))
    {
        cmChangeState(CM_TITLE);
    }
    //button a = cycle brightness
    else if(cmIsButtonPressed(BTN_GAME_CYCLE_BRIGHTNESS))
    {
        // Cycle brightnesses
        cmBrightnessIdx = (cmBrightnessIdx + 1) %
                          (sizeof(cmBrightnesses) / sizeof(cmBrightnesses[0]));
    }
}

void ICACHE_FLASH_ATTR cmTitleUpdate(void)
{
}

void ICACHE_FLASH_ATTR cmGameUpdate(void)
{
    float alphaFast;
    static struct maxtime_t CM_updatedisplay_timer = { .name = "CM_updateDisplay"};
    maxTimeBegin(&CM_updatedisplay_timer);

    //os_printf("modeTime %d\n", modeTime);

    crossIntervalCounter++;

    clearDisplay();


    subFrameCount += 1;
    if (subFrameCount > cmNumSubFrames)
    {
        subFrameCount = 0;
    }
    //#define BUILT_IN_INPUT
#ifdef BUILT_IN_INPUT
#define BPM_GEN 16
    xAccel = 255 * sin(6.2831853 * BPM_GEN / 60  * modeTime / 1000000);
    yAccel = -255; //255 * sin(3 * 6.2831853 * BPM_GEN / 60  * modeTime / 1000000);
    zAccel = -255; //255 * sin(5 * 6.2831853 * BPM_GEN / 60  * modeTime / 1000000);
#else
    // cmAccel return direct, high pass filtered and/or smoothed readings depending on
    // cmUseHighPassAccel and cmUseSmooth
    xAccel = cmAccel.x;
    yAccel = cmAccel.y;
    zAccel = cmAccel.z;
#endif

    // NOTE taking the norm doubles the frequency
    int16_t normAccel = sqrt( (double)xAccel * (double)xAccel + (double)yAccel * (double)yAccel + (double)zAccel *
                              (double)zAccel  );

    //os_printf("%d %d %d %d\n", xAccel, yAccel, zAccel,   normAccel);
    //TODO make this an option
    if (SPECIAL_EFFECT)
    {
        // intertwine raw signal with smoothed
        // only need these buffers if want special effect
        circularPush(xAccel, &cirBufXaccel);
        circularPush(yAccel, &cirBufYaccel);
        circularPush(zAccel, &cirBufZaccel);
    }

    if (cmFilterAllWithIIR)
    {
        // slightly smoothed signals
        alphaFast = ALPHA_FAST;
    }
    else
    {
        alphaFast = 1.0;
    }
    smoothNormAccel = IIRFilter(alphaFast, normAccel, smoothNormAccel);
    circularPush(smoothNormAccel, &cirBufNormAccel);

    //TODO make this an option
    // Inactive as now filtering accel readings directly
#if 0
    smoothXaccel = IIRFilter(alphaFast, xAccel, smoothXaccel);
    //OUT circularPush(smoothXaccel, &cirBufXaccel);

    smoothYaccel = IIRFilter(alphaFast, yAccel, smoothYaccel);
    //OUT circularPush(smoothYaccel, &cirBufYaccel);

    smoothZaccel = IIRFilter(alphaFast, zAccel, smoothZaccel);
    //OUT circularPush(smoothZaccel, &cirBufZaccel);
#endif
    // Identify stillness
    float alphaActive = ALPHA_ACTIVE; // smoothing of deviaton from mean
    //TODO very similar to lowPassNormAccel, could use that instead
    smoothActivity = IIRFilter(alphaActive, abs(getCircularBufferAtIndex(cirBufHighPassNormAccel, -1)), smoothActivity);


    // high pass by removing highly smoothed low pass (dc bias)
    float alphaSlow = ALPHA_SLOW;
    //TODO make this option to select which one to use
    // Low pass using IIR
    lowPassNormAccel = IIRFilter(alphaSlow, normAccel, lowPassNormAccel);

    // Low pass via running average
    lowPassNormAccel = sumOfBuffer(cirBufNormAccel, cirBufNormAccel.length) / NUM_IN_CIRCULAR_BUFFER;

    // This is scaled HP filter of smoothNormAccel
    int16_t sample = 10 * (smoothNormAccel - lowPassNormAccel);
    // This is plotted on OLED
    circularPush(sample, &cirBufHighPassNormAccel);

    // Use a simple variation of the YIN method for finding the fundamental freq of signal
    // Calculate slow running average via IIR of deviations of current signal
    //      current signal shifted by tau (where tau from 12 to NUM_IN_CIRCULAR_BUFFER-1)
    // Since accelerometer runs at 10 Hz, 5 Hz is highest freq to detect
    // This is 300 bpm. The frame rate producing the samples here is 60 Hz
    // Ignore tau < 12 ( 12 would estimate 60/12 = 5 Hz signal = 300 bpm)
    // First tau > 11 with smallest deviation gives fundamental period
    // so estimate fundamental freq as sample freq / tau
    // sample freq is 1000 / UPDATE_TIME_MS
    // so lowest freq that can be sensed is sample freq / (NUM_IN_CIRCULAR_BUFFER - 1)
    // ex. 60/119 = 30.25


//TODO some of this may not be wanted for CC or ROLL sub modes
    int16_t minDeviation = 0x7FFF;
    uint8_t tauArgMin = 0;
    for (uint8_t tau = 12; tau < NUM_IN_CIRCULAR_BUFFER; tau++)
    {
        deviations[tau] = IIRFilter(alphaSlow, abs(getCircularBufferAtIndex(cirBufHighPassNormAccel,
                                    -1) - getCircularBufferAtIndex(cirBufHighPassNormAccel, -1 - tau)), deviations[tau]);
        //OUT drawPixel(tau, OLED_HEIGHT - deviations[tau] / 30, WHITE);
        if (deviations[tau] < minDeviation)
        {
            tauArgMin = tau;
            minDeviation = deviations[tau];
        }
    }
    //OUT plotLine(tauArgMin, 0, tauArgMin, OLED_HEIGHT, WHITE);

    // low pass for the three axes
    lowPassXaccel = IIRFilter(alphaSlow, xAccel, lowPassXaccel);
    circularPush(lowPassXaccel, &cirBufLowPassXaccel);

    lowPassYaccel = IIRFilter(alphaSlow, yAccel, lowPassYaccel);
    circularPush(lowPassYaccel, &cirBufLowPassYaccel);

    lowPassZaccel = IIRFilter(alphaSlow, zAccel, lowPassZaccel);
    circularPush(lowPassZaccel, &cirBufLowPassZaccel);

    // Plot slightly smoothed less dc bias by adjusting the dots
    AdjustAndPlotDotsSingle(cirBufHighPassNormAccel);
#if 0
    AdjustAndPlotDots(cirBufXaccel, cirBufLowPassXaccel);
    AdjustAndPlotDots(cirBufYaccel, cirBufLowPassYaccel);
    AdjustAndPlotDots(cirBufZaccel, cirBufLowPassZaccel);
#endif

// Sub mode sections
    if (cmCurrentSubMode == DFT_SHAKE)
    {
        // send this to be dealt with by color chord
        // computes light pattern and OLED display
        cmSampleHandler(sample);
        for (uint8_t  i = 0; i < FIXBINS; i++)
        {
            drawPixel(i, OLED_HEIGHT - fuzzed_bins[i] / 2000, WHITE);
            //os_printf("%d ", fuzzed_bins[i]);
        }
        //os_printf("fuzzed_bins[0] = %d\n", fuzzed_bins[0]);
    }
    else if ((cmCurrentSubMode == ROLL_BALL) || (cmCurrentSubMode == ROLL_3_BALLS))
    {
        // send this to be dealt with by roll mode routines
        // computes light pattern and OLED display
        led_t* outLeds = roll_updateDisplayComputations(xAccel, yAccel, zAccel);
        //os_printf("Size of outLeds %d\n",sizeof(outLeds) );
        setCMLeds(outLeds, NUM_LIN_LEDS * 3);
    }
    else
    {
        // rest of sub modes

        CM_printf("smoothActivity = %d\n", smoothActivity);

        // Estimate bpm when movement activity

        //float alphaCross = ALPHA_CROSS; // for smoothing gaps between zero crossing

        CM_printf("%d %d  smooth: %d\n", getCircularBufferAtIndex(cirBufHighPassNormAccel, -2),
                  getCircularBufferAtIndex(cirBufHighPassNormAccel, -1), smoothActivity);

        // Ignore low level input so only do if enough activity
#define ACTIVITY_BOUND 15
        if (smoothActivity > ACTIVITY_BOUND )
        {
            circularPush(0x0000, &cirBufCrossings);
            //Compute upward crossings and note it
            if (lastsign >= 0 && sample < 0)
            {
                lastsign = -1;
            }
            else if (lastsign <= 0 && sample > 0)
            {
                lastsign = 1;
                showCrossOnLed = true;
                circularPush(0x0001, &cirBufCrossings);
                crossIntervalCounter = 0;
            }
        }

        // BPM Estimation from upward crossings (can highly overestimate)
        bpmFromCrossing = 60 * S_TO_MS_FACTOR * sumOfBuffer(cirBufCrossings, cirBufCrossings.length) / BPM_SAMPLE_TIME;
        //TODO compute and average period

        // BPM Estimation from min shifted deviation
        bpmFromTau = 60 * S_TO_MS_FACTOR / UPDATE_TIME_MS / tauArgMin;


        if (bpmFromCrossing > 0)
        {
            avePeriodMs = 60 * S_TO_MS_FACTOR  / bpmFromCrossing;
            //TODO IIR average here
        }
        else
        {
            //avePeriodMs = 60 * S_TO_MS_FACTOR;
        }
        //TODO compute avePeriodMs from smoothActivity

        //TODO should be in cmGameDisplay()
        // graphical view of bpm could be here

        // graphical view of amp could be here


        if (SOUND_ON)
        {
            // TODO? map bpm to pitch, play on shocks?
        }

        //Clear leds
        memset(leds, 0, sizeof(leds));

        os_printf("bpmFromCrossing %d, bpmFromTau %d, tauArgMin %d, activity %d\n", bpmFromCrossing, bpmFromTau, tauArgMin,
                  smoothActivity);
        uint8_t val;
        uint8_t hue;
        uint32_t colorToShow;
        uint8_t ledr;
        uint8_t ledg;
        uint8_t ledb;
        int16_t xHP;
        int16_t yHP;
        int16_t zHP;;

        // Various led options
        switch (cmComputeColor)
        {
            case XYZ2RGB:
                xHP = sumAbsOfBuffer(cirBufXaccel, cmNumSum) / cmNumSum;
                yHP = sumAbsOfBuffer(cirBufYaccel, cmNumSum) / cmNumSum;
                zHP = sumAbsOfBuffer(cirBufZaccel, cmNumSum) / cmNumSum;

                maxCheck1 = max(maxCheck1, xHP);
                maxCheck2 = max(maxCheck2, yHP);
                maxCheck3 = max(maxCheck3, zHP);
                minCheck1 = min(minCheck1, xHP);
                minCheck2 = min(minCheck2, yHP);
                minCheck3 = min(minCheck3, zHP);
                //os_printf("minxHP:%d  minyHP:%d  minzHP:%d maxxHP:%d  maxyHP:%d  maxzHP:%d \n", minCheck1, minCheck2, minCheck3, maxCheck1, maxCheck2, maxCheck3);

                // Violent shaking can give xHP just over 600 but then hard to just
                // shake only in one direction x,y,z

                ledr = CLAMP(xHP * 255 * cmScaleLed / 600, 0, 255);
                ledg = CLAMP(yHP * 255 * cmScaleLed / 600, 0, 255);
                ledb = CLAMP(zHP * 255 * cmScaleLed / 600, 0, 255);
                break;
            case BPM2HUE:
            default:
                //Color and intensity related to bpm and amount of shaking using color wheel
                //hue = CLAMP(((CLAMP(bpmFromTau, 30, 312) - 30) * 255 / (312 - 30)),0,255);
                //hue = CLAMP(((CLAMP(smoothActivity, 0, 500) - 0) * 255 / (500 - 0)), 0, 255);
                // This seems best
                hue = CLAMP(((CLAMP(bpmFromCrossing, 30, 250) - 30) * 255 / (250 - 30)), 0, 255);
                val = CLAMP(((CLAMP( cmScaleLed * smoothActivity, 15, 1500 ) - 15 ) * 255 / (1500 - 15)), 0, 255);

                maxCheck1 = max(maxCheck1, bpmFromTau);
                maxCheck2 = max(maxCheck2, bpmFromCrossing);
                maxCheck3 = max(maxCheck3, smoothActivity);
                minCheck1 = min(minCheck1, bpmFromTau);
                minCheck2 = min(minCheck2, bpmFromCrossing);
                minCheck3 = min(minCheck3, smoothActivity);
                //os_printf("bpmFromTau: min %d  max %d, bpmFromCrossing min %d max:%d, smoothAct min %d max %d \n", minCheck1, maxCheck1,
                //          minCheck2, maxCheck2, minCheck3, maxCheck3);

                // Don't apply gamma as is done in setCMLeds
                colorToShow = EHSVtoHEXhelper(hue, 0xFF, val, false);

                ledr = (colorToShow >>  0) & 0xFF;
                ledg = (colorToShow >>  8) & 0xFF;
                ledb = (colorToShow >> 16) & 0xFF;

                break;
        }
        // Gamma correction corrects washed out look
        //   Done in setCMLeds

        //os_printf("r:%d  g:%d  b:%d \n", ledr, ledg, ledb);

#define USE_NUM_LEDS 6
        // Spin the leds syncronized to bpm while there is some shaking activity
        if ((modeTime - ledPrevIncTime > MS_TO_US_FACTOR * avePeriodMs / USE_NUM_LEDS / cmRevsPerBeat)
                && (smoothActivity > ACTIVITY_BOUND ))
        {
            //os_printf("modeTime %d, ledPrevIncTime %d\n", modeTime, ledPrevIncTime);
            ledPrevIncTime = modeTime;
            ledCycle += ledDirection;
            ledCycle = (ledCycle + USE_NUM_LEDS) % USE_NUM_LEDS;
            cmLedCount = cmLedCount + 1; //will wrap back to 0
        }

        if (!cmUseShiftingLeds)
        {
            ledCycle = 0;
        }
        if (!cmUseShiftingColorWheel)
        {
            cmLedCount = 0;
        }


        switch (cmLedMethod)
        {
            case ALL_SAME:

                // allcolored the same
                for (uint8_t i = 0; i < cmShowNumLeds; i++)
                {
                    //use axis colors or hue colors computed above
#if USE_NUM_LEDS == 6
                    leds[ledOrderInd[(i + ledCycle) % USE_NUM_LEDS]].r = ledr;
                    leds[ledOrderInd[(i + ledCycle) % USE_NUM_LEDS]].g = ledg;
                    leds[ledOrderInd[(i + ledCycle) % USE_NUM_LEDS]].b = ledb;
#else
                    leds[(i + ledCycle) % USE_NUM_LEDS].r = ledr;
                    leds[(i + ledCycle) % USE_NUM_LEDS].g = ledg;
                    leds[(i + ledCycle) % USE_NUM_LEDS].b = ledb;
#endif
                }
                /* code */
                break;

            case RAINBOW:
            default:

                // rainbow

                for(uint8_t i = 0; i < cmShowNumLeds; i++)
                {
                    hue = (((i * 256) / cmShowNumLeds)) + cmLedCount % 256;
                    colorToShow = EHSVtoHEXhelper(hue, 0xFF, 0xFF, false);

                    ledr = (colorToShow >>  0) & 0xFF;
                    ledg = (colorToShow >>  8) & 0xFF;
                    ledb = (colorToShow >> 16) & 0xFF;

#if USE_NUM_LEDS == 6
                    leds[ledOrderInd[(i + ledCycle) % USE_NUM_LEDS]].r = ledr;
                    leds[ledOrderInd[(i + ledCycle) % USE_NUM_LEDS]].g = ledg;
                    leds[ledOrderInd[(i + ledCycle) % USE_NUM_LEDS]].b = ledb;
#else
                    leds[(i + ledCycle) % USE_NUM_LEDS].r = ledr;
                    leds[(i + ledCycle) % USE_NUM_LEDS].g = ledg;
                    leds[(i + ledCycle) % USE_NUM_LEDS].b = ledb;
#endif
                }
                break;
        }

        //Flip direction on crossing
        if (showCrossOnLed && crossIntervalCounter == 0)
        {
            //ledDirection *= -1;
        }

        //Brighten on crossing
        if (showCrossOnLed && crossIntervalCounter < 10)
        {
            //Need to temp brighten not change index
            //cmBrightnessIdx = 0;
        }
        else
        {
            showCrossOnLed = false;
            //cmBrightnessIdx = 0;
        }

        if (cmUsePOVeffect)
        {
            for (uint8_t indLed = 0; indLed < NUM_LIN_LEDS ; indLed++)
            {

                // POV effect
                if (subFrameCount / 2 == 0)
                {
                    leds[indLed].g = 0;
                    leds[indLed].b = 0;
                }
                else if (subFrameCount / 2 == 1)
                {
                    leds[indLed].r = 0;
                    leds[indLed].b = 0;
                }
                else
                {
                    leds[indLed].r = 0;
                    leds[indLed].g = 0;
                };
            };
        }


        setCMLeds(leds, sizeof(leds));
    } //non color chord

    maxTimeEnd(&CM_updatedisplay_timer);
}


void ICACHE_FLASH_ATTR cmTitleDisplay(void)
{
    // Clear the display.
    clearDisplay();

    // Shake It
    plotCenteredText(0, 5, 127, "SHAKE-COLOR", RADIOSTARS, WHITE);

    plotCenteredText(0, OLED_HEIGHT / 2, 127, (char*)subModeName[cmCurrentSubMode], IBM_VGA_8, WHITE);

    // SCORES   START
    plotText(0, OLED_HEIGHT - (1 * (FONT_HEIGHT_IBMVGA8 + 1)), "CHOOSE", IBM_VGA_8, WHITE);
    plotText(OLED_WIDTH - getTextWidth("START", IBM_VGA_8), OLED_HEIGHT - (1 * (FONT_HEIGHT_IBMVGA8 + 1)), "START",
             IBM_VGA_8, WHITE);



}

void ICACHE_FLASH_ATTR cmGameDisplay(void)
{
    char uiStr[32] = {0};
    ets_snprintf(uiStr, sizeof(uiStr), "%d", bpmFromCrossing);
    plotCenteredText(0, 1, OLED_WIDTH / 3, uiStr, IBM_VGA_8, WHITE);

    ets_snprintf(uiStr, sizeof(uiStr), "%d", bpmFromTau);
    plotCenteredText(OLED_WIDTH / 3, 1, 2 * OLED_WIDTH / 3, uiStr, IBM_VGA_8, WHITE);

    ets_snprintf(uiStr, sizeof(uiStr), "%d", smoothActivity);
    plotCenteredText(2 * OLED_WIDTH / 3, 1, OLED_WIDTH, uiStr, IBM_VGA_8, WHITE);

    // ets_snprintf(uiStr, sizeof(uiStr), "%d", avePeriodMs);
    // plotCenteredText(0, OLED_HEIGHT - 1 - FONT_HEIGHT_IBMVGA8, OLED_WIDTH, uiStr, IBM_VGA_8, WHITE);
}

// helper functions.

/**
 * @brief Initializer for cm, allocates memory for work arrays
 *
 * @param
 * @return pointers to the work array???
 */
void ICACHE_FLASH_ATTR cmNewSetup(subMethod_t subMode)
{
    //NO NEED FOR dynamic as all buffers always same size

    initCircularBuffer(&cirBufNormAccel, bufNormAccel, NUM_IN_CIRCULAR_BUFFER);
    initCircularBuffer(&cirBufHighPassNormAccel, bufHighPassNormAccel, NUM_IN_CIRCULAR_BUFFER);
    initCircularBuffer(&cirBufXaccel, bufXaccel, NUM_IN_CIRCULAR_BUFFER);
    initCircularBuffer(&cirBufLowPassXaccel, bufLowPassXaccel, NUM_IN_CIRCULAR_BUFFER);
    initCircularBuffer(&cirBufYaccel, bufYaccel, NUM_IN_CIRCULAR_BUFFER);
    initCircularBuffer(&cirBufLowPassYaccel, bufLowPassYaccel, NUM_IN_CIRCULAR_BUFFER);
    initCircularBuffer(&cirBufZaccel, bufZaccel, NUM_IN_CIRCULAR_BUFFER);
    initCircularBuffer(&cirBufLowPassZaccel, bufLowPassZaccel, NUM_IN_CIRCULAR_BUFFER);
    initCircularBuffer(&cirBufCrossings, bufCrossings, BPM_BUF_SIZE);

    // Use for debugging to find extreems of readings being used by a SubMode
    maxCheck1 = -32768;
    minCheck1 = 32767;
    maxCheck2 = -32768;
    minCheck2 = 32767;
    maxCheck3 = -32768;
    minCheck3 = 32767;

    CM_printf("%d leds\n", NUM_LIN_LEDS);
    //os_printf("%d %d sum: %d\n", getCircularBufferAtIndex(cirBufCrossings, 0), getCircularBufferAtIndex(cirBufCrossings,
    //         -1), sumOfBuffer(cirBufCrossings));
    memset(leds, 0, sizeof(leds));
    memset(deviations, 0, sizeof(deviations));

    // Defaults of parameters go here used for BEAT_SPIN
    // A Colorwheel rotates in proportion to the BPM
    // Other submodes override in switch

    // Control of cmAccelerometerCallback
    // for finding long term moving average and remove from signal to get High Pass
    // if not used measure wrt absolute orientation, if used measure relative to average pos
    cmalphaSlow = 0.02;
    cmUseHighPassAccel = true;
    // for slight smoothing of original accelerometer readings or High Pass Accel filtered ones
    cmalphaSmooth = 0.3;
    cmUseSmooth = true;

    // extra filtering with ALPHA_FAST
    cmFilterAllWithIIR = true;

    // Number of past samples used to calculate running average of x,y,z readings
    // from 1 to NUM_IN_CIRCULAR_BUFFER
    cmNumSum = 1;
    cmScaleLed = 2; // Multiplier of Leds color which gets clamped from 0 to 255
    cmComputeColor = BPM2HUE; //using bpmFromCrossing
    cmLedMethod = RAINBOW;
    cmUseShiftingColorWheel = true;
    //TODO can extend to give subset to use eg 1,3,5 or 1,4
    //    rather than just how many consecutive
    cmShowNumLeds = USE_NUM_LEDS; // must be <= USE_NUM_LEDS
    cmUseShiftingLeds = false; // possible to shift leds showing around ring
    cmUsePOVeffect = false; // possible to shift hue angle
    cmUseColorChordDFT = false;
    //cmRevsPerBeat = 1.0 / USE_NUM_LEDS;
    cmRevsPerBeat = 1.0;
    //TODO need fix POV effects
    cmNumSubFrames = 6; // used for POV effects


    switch (subMode)
    {
        case BEAT_SELECT:
            // BPM mapped to hue for all leds.
            // Leds display near max selected brightness while movement
            cmShowNumLeds = USE_NUM_LEDS;
            cmUseShiftingColorWheel = false;
            cmLedMethod = ALL_SAME;
            cmScaleLed = 10; // scaling for intensity and want to keep alive until very quiet
            break;
        case SHOCK_CHANGE:
            cmUseSmooth = false;
            cmUseHighPassAccel = false;
            cmFilterAllWithIIR = false; //false will use running average
            cmNumSum = 1;
            cmLedMethod = ALL_SAME;
            break;
        case SHOCK_CHAOTIC:
            /* code */
            break;
        case ROLL_BALL:
            /* code */
            break;
        case ROLL_3_BALLS:
            /* code */
            break;
        case TILT_A_COLOR:
            cmUseSmooth = false;
            cmUseHighPassAccel = false;
            cmFilterAllWithIIR = false; //false will use running average
            cmNumSum = 1;
            cmComputeColor = XYZ2RGB;
            cmLedMethod = ALL_SAME;
            break;
        case POV_EFFECT:
            cmUsePOVeffect = true;
            break;
        case DFT_SHAKE:
            cmUseColorChordDFT = true;
            break;
        case POWER_SHAKE:
            // Shake and lights will slowly get hotter - accumulated energy mapped to hue.
            //  when stop the stored energy will power a display
            //  at that points will show random or partial rainbow of colors seen
            //  during the build up. Using 1 led rotating in proportion to stored energy left
            // Leds display near max selected brightness while movement
            cmShowNumLeds = 1;
            cmUseShiftingColorWheel = false;
            cmUseShiftingLeds = true;
            cmLedMethod = ALL_SAME;
            cmScaleLed = 1000; // scaling for intensity and want to keep alive until very quiet
            cmRevsPerBeat = 3.0;
            break;
        case BEAT_SPIN:
        default:
            // A Colorwheel rotates in proportion to the BPM
            break;
    }


    PLOT_SCALE = 32;
    PLOT_SHIFT = 32;
    // do via timer now 10 fps cmNumSubFrames = 0 // 0 for 60 fps, 1 for 30 fps, 2 for 20 fps, k for 60/(k+1) fps

    subFrameCount = 0; // used for skipping frames
    adj = SCREEN_BORDER;
    wid = 128 - 2 * SCREEN_BORDER;

    ledCycle = 0;
    lastsign = 0;

    lowPassNormAccel = 0;
    lowPassXaccel = 0;
    lowPassYaccel = 0;
    lowPassZaccel = 0;
    smoothNormAccel = 0;
    smoothXaccel = 0;
    smoothYaccel = 0;
    smoothZaccel = 0;
    prevHighPassNormAccel = 0;
    smoothActivity = 0;

    crossIntervalCounter = 0;
    ledPrevIncTime = 0;
    avePeriodMs = 5;
    //crossInterval = 5;
    ledDirection = 1;
    cmCumulativeActivity = 0;
    cmCollectingActivity = true;

}

void ICACHE_FLASH_ATTR cmChangeState(cmState_t newState)
{
    prevState = currState;
    currState = newState;
    stateStartTime = system_get_time();
    stateTime = 0;

    switch( currState )
    {
        case CM_TITLE:
            // Clear leds
            memset(leds, 0, sizeof(leds));
            setCMLeds(leds, sizeof(leds));
            break;
        case CM_GAME:
            // All game restart functions happen here.
            break;
        default:
            break;
    };
}

bool ICACHE_FLASH_ATTR cmIsButtonPressed(uint8_t button)
{
    return (cmButtonState & button) && !(cmLastButtonState & button);
}

bool ICACHE_FLASH_ATTR cmIsButtonReleased(uint8_t button)
{
    return !(cmButtonState & button) && (cmLastButtonState & button);
}

bool ICACHE_FLASH_ATTR cmIsButtonDown(uint8_t button)
{
    return cmButtonState & button;
}

bool ICACHE_FLASH_ATTR cmIsButtonUp(uint8_t button)
{
    return !(cmButtonState & button);
}


// Draw text centered between x0 and x1.
void ICACHE_FLASH_ATTR plotCenteredText(uint8_t x0, uint8_t y, uint8_t x1, char* text, fonts font, color col)
{
    /*// We only get width info once we've drawn.
    // So we draw the text as inverse to get the width.
    uint8_t textWidth = plotText(x0, y, text, font, INVERSE) - x0 - 1; // minus one accounts for the return being where the cursor is.

    // Then we draw the inverse back over it to restore it.
    plotText(x0, y, text, font, INVERSE);*/
    uint8_t textWidth = getTextWidth(text, font);

    // Calculate the correct x to draw from.
    uint8_t fullWidth = x1 - x0 + 1;
    // NOTE: This may result in strange behavior when the width of the drawn text is greater than the distance between x0 and x1.
    uint8_t widthDiff = fullWidth - textWidth;
    uint8_t centeredX = x0 + (widthDiff / 2);

    // Then we draw the correctly centered text.
    plotText(centeredX, y, text, font, col);
}

uint8_t getTextWidth(char* text, fonts font)
{
    // NOTE: The inverse, inverse is cute, but 2 draw calls, could we draw it outside of the display area but still in bounds of a uint8_t?

    // We only get width info once we've drawn.
    // So we draw the text as inverse to get the width.
    uint8_t textWidth = plotText(0, 0, text, font,
                                 INVERSE) - 1; // minus one accounts for the return being where the cursor is.

    // Then we draw the inverse back over it to restore it.
    plotText(0, 0, text, font, INVERSE);
    return textWidth;
}
