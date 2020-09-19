/*
 * mode_tunernome.c
 *
 *  Created on: September 17th, 2020
 *      Author: bryce
 */
#include "user_config.h"

/*============================================================================
 * Includes
 *==========================================================================*/

#include <osapi.h>
#include <mem.h>
#include <stdint.h>
#include <limits.h>

#include "buttons.h"
#include "user_main.h"
#include "embeddednf.h"
#include "oled.h"
#include "bresenham.h"
#include "assets.h"
#include "buttons.h"
#include "linked_list.h"
#include "font.h"
#include "mode_colorchord.h"

#include "embeddednf.h"
#include "embeddedout.h"

/*============================================================================
 * Defines, Structs, Enums
 *==========================================================================*/

#define TUNERNOME_UPDATE_MS 20
#define TUNERNOME_UPDATE_S (TUNERNOME_UPDATE_MS / 1000.0f)

#define NUM_STRINGS            6
#define GUITAR_OFFSET          0
#define CHROMATIC_OFFSET       6 // adjust start point by quartertones   
#define SENSITIVITY            5

/// Helper macro to return an integer clamped within a range (MIN to MAX)
#define CLAMP(X, MIN, MAX) ( ((X) > (MAX)) ? (MAX) : ( ((X) < (MIN)) ? (MIN) : (X)) )
/// Helper macro to return the absolute value of an integer
#define ABS(X) (((X) < 0) ? -(X) : (X))

typedef enum
{
    TN_TUNER,
    TN_METRONOME
} tnMode;

typedef enum
{
    GUITAR_TUNER = 0,
    SEMITONE_0,
    SEMITONE_1,
    SEMITONE_2,
    SEMITONE_3,
    SEMITONE_4,
    SEMITONE_5,
    SEMITONE_6,
    SEMITONE_7,
    SEMITONE_8,
    SEMITONE_9,
    SEMITONE_10,
    SEMITONE_11,
    MAX_GUITAR_MODES
} tuner_mode_t;

typedef struct
{
    tnMode mode;
    tuner_mode_t currentMode;

    timer_t updateTimer;

    int audioSamplesProcessed;
    uint32_t intensities_filt[NUM_STRINGS];
    int32_t diffs_filt[NUM_STRINGS];

    uint32_t semitone_intensitiy_filt;
    int32_t semitone_diff_filt;
} tunernome_t;

/*============================================================================
 * Prototypes
 *==========================================================================*/

void ICACHE_FLASH_ATTR tunernomeEnterMode(void);
void ICACHE_FLASH_ATTR tunernomeExitMode(void);
void ICACHE_FLASH_ATTR switchToSubmode(tnMode);
void ICACHE_FLASH_ATTR tunernomeButtonCallback(uint8_t state __attribute__((unused)),
        int button, int down);
void ICACHE_FLASH_ATTR tunernomeSampleHandler(int32_t samp);

static void ICACHE_FLASH_ATTR tunernomeUpdate(void* arg __attribute__((unused)));

static inline int16_t getMagnitude(uint16_t idx);
static inline int16_t getDiffAround(uint16_t idx);
static inline int16_t getSemiMagnitude(int16_t idx);
static inline int16_t getSemiDiffAround(uint16_t idx);

/*============================================================================
 * Variables
 *==========================================================================*/

swadgeMode tunernomeMode =
{
    .modeName = "tunernome",
    .fnEnterMode = tunernomeEnterMode,
    .fnExitMode = tunernomeExitMode,
    .fnButtonCallback = tunernomeButtonCallback,
    .wifiMode = NO_WIFI,
    .fnEspNowRecvCb = NULL,
    .fnEspNowSendCb = NULL,
    .fnAccelerometerCallback = NULL,
    .fnAudioCallback = tunernomeSampleHandler,
    .menuImg = "tn-menu.gif"
};

tunernome_t* tunernome;

/*============================================================================
 * Const Variables
 *==========================================================================*/

/**
 * Indicies into fuzzed_bins[], a realtime DFT of sorts
 * fuzzed_bins[0] = A ... 1/2 steps are every 2.
 */
const uint16_t freqBinIdxs[NUM_STRINGS] =
{
    38, // E string needs to skip an octave... Can't read sounds this low.
    62, // e
    52, // B
    44, // G
    34, // D = A + 5 half steps = 34
    24  // A string is exactly at note #24
};

static const char tn_title[]  = "Tunernome";

/*============================================================================
 * Functions
 *==========================================================================*/

/**
 * Initializer for tunernome
 */
void ICACHE_FLASH_ATTR tunernomeEnterMode(void)
{
    // Alloc and clear everything
    tunernome = os_malloc(sizeof(tunernome_t));
    ets_memset(tunernome, 0, sizeof(tunernome_t));

    switchToSubmode(TN_TUNER);

    timerDisarm(&(tunernome->updateTimer));
    timerSetFn(&(tunernome->updateTimer), tunernomeUpdate, NULL);
    timerArm(&(tunernome->updateTimer), TUNERNOME_UPDATE_MS, true);

    enableDebounce(true);

    InitColorChord();
}

/**
 * Switch internal mode
 */
void ICACHE_FLASH_ATTR switchToSubmode(tnMode newMode)
{
    switch(newMode)
    {
        case TN_TUNER:
        {
            tunernome->mode = newMode;

            led_t leds[NUM_LIN_LEDS] = {{0}};
            setLeds(leds, sizeof(leds));

            clearDisplay();
            
            break;
        }
        case TN_METRONOME:
        {
            tunernome-> mode = newMode;

            led_t leds[NUM_LIN_LEDS] = {{0}};
            setLeds(leds, sizeof(leds));

            clearDisplay();
            break;
        }
    }
}

/**
 * Called when tunernome is exited
 */
void ICACHE_FLASH_ATTR tunernomeExitMode(void)
{
    timerDisarm(&(tunernome->updateTimer));
    timerFlush();
    os_free(tunernome);
}

/**
 * Inline helper function to get the magnitude of a frequency bin from fuzzed_bins[]
 *
 * @param idx The index to get a magnitude from
 * @return A signed magnitude, even though fuzzed_bins[] is unsigned
 */
static inline int16_t getMagnitude(uint16_t idx)
{
    return fuzzed_bins[idx];
}

/**
 * Inline helper function to get the difference in magnitudes around a given
 * frequency bin from fuzzed_bins[]
 *
 * @param idx The index to get a difference in magnitudes around
 * @return The difference in magnitudes of the bins before and after the given index
 */
static inline int16_t getDiffAround(uint16_t idx)
{
    return getMagnitude(idx + 1) - getMagnitude(idx - 1);
}

/**
 * Inline helper function to get the magnitude of a frequency bin from folded_bins[]
 *
 * @param idx The index to get a magnitude from
 * @return A signed magnitude, even though folded_bins[] is unsigned
 */
static inline int16_t getSemiMagnitude(int16_t idx)
{
    if(idx < 0)
    {
        idx += FIXBPERO;
    }
    if(idx > FIXBPERO - 1)
    {
        idx -= FIXBPERO;
    }
    return folded_bins[idx];
}

/**
 * Inline helper function to get the difference in magnitudes around a given
 * frequency bin from folded_bins[]
 *
 * @param idx The index to get a difference in magnitudes around
 * @return The difference in magnitudes of the bins before and after the given index
 */
static inline int16_t getSemiDiffAround(uint16_t idx)
{
    return getSemiMagnitude(idx + 1) - getSemiMagnitude(idx - 1);
}

/**
 * @brief called on a timer, updates the game state
 *
 * @param arg
 */
static void ICACHE_FLASH_ATTR tunernomeUpdate(void* arg __attribute__((unused)))
{
    switch(tunernome->mode)
    {
        default:
        case TN_TUNER:
        {
            plotText(0, OLED_HEIGHT - FONT_HEIGHT_TOMTHUMB, "< Exit", TOM_THUMB, WHITE);
            plotText(OLED_WIDTH - 43, OLED_HEIGHT - FONT_HEIGHT_TOMTHUMB, "Metronome >", TOM_THUMB, WHITE);
            break;
        }
        case TN_METRONOME:
        {
            plotText(0, OLED_HEIGHT - FONT_HEIGHT_TOMTHUMB, "< Exit", TOM_THUMB, WHITE);
            plotText(OLED_WIDTH - 27, OLED_HEIGHT - FONT_HEIGHT_TOMTHUMB, "Tuner >", TOM_THUMB, WHITE);
            break;
        }
    }
}

/**
 * TODO
 *
 * @param state  A bitmask of all button states, unused
 * @param button The button which triggered this event
 * @param down   true if the button was pressed, false if it was released
 */
void ICACHE_FLASH_ATTR tunernomeButtonCallback( uint8_t state,
        int button, int down)
{
    switch (tunernome->mode)
    {
        default:
        case TN_TUNER:
        {
            if(down)
            {
                switch(button)
                {
                    case UP:
                    {
                        tunernome->currentMode = (tunernome->currentMode + 1) % MAX_GUITAR_MODES;
                        break;
                    }
                    case DOWN:
                    {
                        if(0 == tunernome->currentMode)
                        {
                            tunernome->currentMode = MAX_GUITAR_MODES - 1;
                        }
                        else
                        {
                            tunernome->currentMode--;
                        }
                        break;
                    }
                    case ACTION:
                    {
                        cycleColorchordSensitivity();
                        break;
                    }
                    case RIGHT:
                    {
                        switchToSubmode(TN_METRONOME);
                        break;
                    }
                    case LEFT:
                    {
                        switchToSwadgeMode(0);
                        clearDisplay();
                        break;
                    }
                } //switch(button)

                if(button == UP || button == DOWN)
                {
                    led_t leds[NUM_STRINGS] = {{0}};
                    if (tunernome->currentMode == 0)
                    {
                        // for guitar mode we flash all LEDs
                        uint8_t i;
                        for(i = 0; i < NUM_STRINGS; i++)
                        {
                            // yellow
                            leds[i].r = 255;
                            leds[i].g = 255;
                            leds[i].b = 0;
                        }
                    }
                    else
                    {
                        int32_t red, grn, blu, loc;
                        if (tunernome->currentMode < 7)
                        {
                            // cyan
                            red = 0;
                            grn = 255;
                            blu = 255;
                        }
                        else
                        {
                            // magenta
                            red = 255;
                            grn = 0;
                            blu = 255;
                        }
                        loc = (NUM_STRINGS + 1 - (tunernome->currentMode % 6)) % 6;
                        leds[loc].r = red;
                        leds[loc].g = grn;
                        leds[loc].b = blu;
                    }
                    setLeds(leds, sizeof(leds));
                    break;
                }
            }
            break;
        }
        case TN_METRONOME:
        {
            if(down)
            {
                switch(button)
                {
                    case RIGHT:
                    {
                        switchToSubmode(TN_TUNER);
                        break;
                    }
                    case LEFT:
                    {
                        switchToSwadgeMode(0);
                        break;
                    }
                }
            }
            break;
        }
    }
}

/**
 * This is called every time an audio sample is read from the ADC
 * This processes the sample and will display update the LEDs every
 * 128 samples
 *
 * @param samp A 32 bit audio sample read from the ADC (microphone)
 */
void ICACHE_FLASH_ATTR tunernomeSampleHandler(int32_t samp)
{
    if(tunernome->mode == TN_TUNER)
    {
        //os_printf("%s :: %d\r\n", __func__, __LINE__);
        PushSample32( samp );
        tunernome->audioSamplesProcessed++;

        // If at least 128 samples have been processed
        if( tunernome->audioSamplesProcessed >= 128 )
        {
            // Don't bother if colorchord is inactive
            if( !COLORCHORD_ACTIVE )
            {
                return;
            }

            // Colorchord magic
            HandleFrameInfo();

            led_t colors[NUM_STRINGS] = {{0}};

            switch(tunernome->currentMode)
            {
                case GUITAR_TUNER:
                {
                    // Guitar tuner magic
                    uint32_t i;
                    for( i = 0; i < NUM_STRINGS; i++ )
                    {
                        // Pick out the current magnitude and filter it
                        tunernome->intensities_filt[i] = (getMagnitude(freqBinIdxs[i] + GUITAR_OFFSET)  + tunernome->intensities_filt[i]) -
                                            (tunernome->intensities_filt[i] >> 5);

                        // Pick out the difference around current magnitude and filter it too
                        tunernome->diffs_filt[i] =       (getDiffAround(freqBinIdxs[i] + GUITAR_OFFSET) + tunernome->diffs_filt[i]) -
                                            (tunernome->diffs_filt[i] >> 5);

                        // This is the magnitude of the target frequency bin, cleaned up
                        int16_t intensity = (tunernome->intensities_filt[i] >> SENSITIVITY) - 40; // drop a baseline.
                        intensity = CLAMP(intensity, 0, 255);

                        //This is the tonal difference.  You "calibrate" out the intensity.
                        int16_t tonalDiff = (tunernome->diffs_filt[i] >> SENSITIVITY) * 200 / (intensity + 1);

                        int32_t red, grn, blu;
                        // Is the note in tune, i.e. is the magnitude difference in surrounding bins small?
                        if( (ABS(tonalDiff) < 10) )
                        {
                            // Note is in tune, make it white
                            red = 255;
                            grn = 255;
                            blu = 255;
                        }
                        else
                        {
                            // Check if the note is sharp or flat
                            if( tonalDiff > 0 )
                            {
                                // Note too sharp, make it red
                                red = 255;
                                grn = blu = 255 - (tonalDiff) * 15;
                            }
                            else
                            {
                                // Note too flat, make it blue
                                blu = 255;
                                grn = red = 255 - (-tonalDiff) * 15;
                            }

                            // Make sure LED output isn't more than 255
                            red = CLAMP(red, INT_MIN, 255);
                            grn = CLAMP(grn, INT_MIN, 255);
                            blu = CLAMP(blu, INT_MIN, 255);
                        }

                        // Scale each LED's brightness by the filtered intensity for that bin
                        red = (red >> 3 ) * ( intensity >> 3);
                        grn = (grn >> 3 ) * ( intensity >> 3);
                        blu = (blu >> 3 ) * ( intensity >> 3);

                        // Set the LED, ensure each channel is between 0 and 255
                        colors[i].r = CLAMP(red, 0, 255);
                        colors[i].g = CLAMP(grn, 0, 255);
                        colors[i].b = CLAMP(blu, 0, 255);
                    }
                    break;
                }
                default:
                {
                    uint8_t semitoneIdx = (tunernome->currentMode - SEMITONE_0) * 2;
                    // Pick out the current magnitude and filter it
                    tunernome->semitone_intensitiy_filt = (getSemiMagnitude(semitoneIdx + CHROMATIC_OFFSET)  + tunernome->semitone_intensitiy_filt) -
                                            (tunernome->semitone_intensitiy_filt >> 5);

                    // Pick out the difference around current magnitude and filter it too
                    tunernome->semitone_diff_filt =       (getSemiDiffAround(semitoneIdx + CHROMATIC_OFFSET) + tunernome->semitone_diff_filt)       -
                                            (tunernome->semitone_diff_filt >> 5);

                    // This is the magnitude of the target frequency bin, cleaned up
                    int16_t intensity = (tunernome->semitone_intensitiy_filt >> SENSITIVITY) - 40; // drop a baseline.
                    intensity = CLAMP(intensity, 0, 255);

                    //This is the tonal difference.  You "calibrate" out the intensity.
                    int16_t tonalDiff = (tunernome->semitone_diff_filt >> SENSITIVITY) * 200 / (intensity + 1);

                    // tonal diff is -32768 to 32767. if its within -10 to 10, it's in tune. positive means too sharp, negative means too flat
                    // intensity is how 'loud' that frequency is, 0 to 255. you'll have to play around with values
                    //os_printf("thaeli, semitone %2d, tonal diff %6d, intensity %3d\r\n", currentMode, tonalDiff, intensity);
                    int32_t red, grn, blu;
                    // Is the note in tune, i.e. is the magnitude difference in surrounding bins small?
                    if( (ABS(tonalDiff) < 10) )
                    {
                        // Note is in tune, make it white
                        red = 255;
                        grn = 255;
                        blu = 255;
                    }
                    else
                    {
                        // Check if the note is sharp or flat
                        if( tonalDiff > 0 )
                        {
                            // Note too sharp, make it red
                            red = 255;
                            grn = blu = 255 - (tonalDiff) * 15;
                        }
                        else
                        {
                            // Note too flat, make it blue
                            blu = 255;
                            grn = red = 255 - (-tonalDiff) * 15;
                        }

                        // Make sure LED output isn't more than 255
                        red = CLAMP(red, INT_MIN, 255);
                        grn = CLAMP(grn, INT_MIN, 255);
                        blu = CLAMP(blu, INT_MIN, 255);
                    }

                    // Scale each LED's brightness by the filtered intensity for that bin
                    red = (red >> 3 ) * ( intensity >> 3);
                    grn = (grn >> 3 ) * ( intensity >> 3);
                    blu = (blu >> 3 ) * ( intensity >> 3);

                    // Set the LED, ensure each channel is between 0 and 255
                    uint32_t i;
                    for (i = 0; i < NUM_STRINGS; i++)
                    {
                        colors[i].r = CLAMP(red, 0, 255);
                        colors[i].g = CLAMP(grn, 0, 255);
                        colors[i].b = CLAMP(blu, 0, 255);
                    }

                    break;
                }
            }

            // Draw the LEDs
            setLeds( colors, sizeof(colors) );

            // Reset the sample count
            tunernome->audioSamplesProcessed = 0;
        }
    } //if(tunernome-> mode == TN_TUNER)
}
