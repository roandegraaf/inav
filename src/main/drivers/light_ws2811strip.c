/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */


/*
 * "Note that the timing on the WS2812/WS2812B LEDs has changed as of batches from WorldSemi
 * manufactured made in October 2013, and timing tolerance for approx 10-30% of parts is very small.
 * Recommendation from WorldSemi is now: 0 = 400ns high/850ns low, and 1 = 850ns high, 400ns low"
 *
 * Currently the timings are 0 = 350ns high/800ns and 1 = 700ns high/650ns low.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <platform.h>

#ifdef USE_LED_STRIP

#include "build/build_config.h"
#include "build/debug.h"

#include "common/color.h"
#include "common/colorconversion.h"

#include "drivers/dma.h"
#include "drivers/io.h"
#include "drivers/timer.h"
#include "drivers/light_ws2811strip.h"

#include "config/parameter_group_ids.h"
#include "fc/settings.h"
#include "fc/runtime_config.h"

#define WS2811_PERIOD (WS2811_TIMER_HZ / WS2811_CARRIER_HZ)
#define WS2811_BIT_COMPARE_1 ((WS2811_PERIOD * 2) / 3)
#define WS2811_BIT_COMPARE_0 (WS2811_PERIOD / 3)

PG_REGISTER_WITH_RESET_TEMPLATE(ledPinConfig_t, ledPinConfig, PG_LEDPIN_CONFIG, 1);

PG_RESET_TEMPLATE(ledPinConfig_t, ledPinConfig,
    .led_pin_pwm_mode = SETTING_LED_PIN_PWM_MODE_DEFAULT
);

static DMA_RAM timerDMASafeType_t ledStripDMABuffer[LED_STRIP_COUNT][WS2811_DMA_BUFFER_SIZE];

static IO_t ws2811IO[LED_STRIP_COUNT] = { IO_NONE };
static TCH_t * ws2811TCH[LED_STRIP_COUNT] = { NULL };
static bool ws2811Initialised[LED_STRIP_COUNT] = { false };
static bool pwmMode[LED_STRIP_COUNT] = { false };

static hsvColor_t ledColorBuffer[LED_STRIP_COUNT][WS2811_LED_STRIP_LENGTH];

void setLedHsv(uint16_t index, const hsvColor_t *color)
{
    ledColorBuffer[0][index] = *color;
}

void getLedHsv(uint16_t index, hsvColor_t *color)
{
    *color = ledColorBuffer[0][index];
}

void setLedValue(uint16_t index, const uint8_t value)
{
    ledColorBuffer[0][index].v = value;
}

void scaleLedValue(uint16_t index, const uint8_t scalePercent)
{
    ledColorBuffer[0][index].v = ((uint16_t)ledColorBuffer[0][index].v * scalePercent / 100);
}

void setStripColor(const hsvColor_t *color)
{
    uint16_t index;
    for (index = 0; index < WS2811_LED_STRIP_LENGTH; index++) {
        setLedHsv(index, color);
    }
}

void setStripColors(const hsvColor_t *colors)
{
    uint16_t index;
    for (index = 0; index < WS2811_LED_STRIP_LENGTH; index++) {
        setLedHsv(index, colors++);
    }
}

#ifdef USE_LED_STRIP_2
void setLedHsvIdx(uint8_t stripIdx, uint16_t index, const hsvColor_t *color)
{
    ledColorBuffer[stripIdx][index] = *color;
}

void getLedHsvIdx(uint8_t stripIdx, uint16_t index, hsvColor_t *color)
{
    *color = ledColorBuffer[stripIdx][index];
}

void setLedValueIdx(uint8_t stripIdx, uint16_t index, const uint8_t value)
{
    ledColorBuffer[stripIdx][index].v = value;
}

void scaleLedValueIdx(uint8_t stripIdx, uint16_t index, const uint8_t scalePercent)
{
    ledColorBuffer[stripIdx][index].v = ((uint16_t)ledColorBuffer[stripIdx][index].v * scalePercent / 100);
}
#endif

static bool ledConfigureDMAIdx(uint8_t stripIdx) {
    uint8_t period = WS2811_TIMER_HZ / WS2811_CARRIER_HZ;

    timerConfigBase(ws2811TCH[stripIdx], period, WS2811_TIMER_HZ);
    timerPWMConfigChannel(ws2811TCH[stripIdx], 0);

    return timerPWMConfigChannelDMA(ws2811TCH[stripIdx], ledStripDMABuffer[stripIdx], sizeof(ledStripDMABuffer[0][0]), WS2811_DMA_BUFFER_SIZE);
}

static void ledConfigurePWMIdx(uint8_t stripIdx) {
    timerConfigBase(ws2811TCH[stripIdx], 100, WS2811_TIMER_HZ);
    timerPWMConfigChannel(ws2811TCH[stripIdx], 0);
    timerPWMStart(ws2811TCH[stripIdx]);
    timerEnable(ws2811TCH[stripIdx]);
    pwmMode[stripIdx] = true;
}

static void ws2811LedStripInitByIndex(uint8_t stripIdx, ioTag_t pinTag)
{
    const timerHardware_t * timHw = timerGetByTag(pinTag, TIM_USE_ANY);

    if (timHw == NULL) {
        return;
    }

    if (stripIdx == 0 && !(timHw->usageFlags & TIM_USE_LED)) {
        timHw = timerGetByUsageFlag(TIM_USE_LED);
        if (timHw == NULL) {
            return;
        }
    }

    ws2811TCH[stripIdx] = timerGetTCH(timHw);
    if (ws2811TCH[stripIdx] == NULL) {
        return;
    }

    ws2811IO[stripIdx] = IOGetByTag(timHw->tag);
    IOInit(ws2811IO[stripIdx], OWNER_LED_STRIP, RESOURCE_OUTPUT, stripIdx);
    IOConfigGPIOAF(ws2811IO[stripIdx], IOCFG_AF_PP_FAST, timHw->alternateFunction);

    if (stripIdx == 0 && ledPinConfig()->led_pin_pwm_mode == LED_PIN_PWM_MODE_LOW) {
        ledConfigurePWMIdx(stripIdx);
        *timerCCR(ws2811TCH[stripIdx]) = 0;
    } else if (stripIdx == 0 && ledPinConfig()->led_pin_pwm_mode == LED_PIN_PWM_MODE_HIGH) {
        ledConfigurePWMIdx(stripIdx);
        *timerCCR(ws2811TCH[stripIdx]) = 100;
    } else {
        if (!ledConfigureDMAIdx(stripIdx)) {
            ws2811Initialised[stripIdx] = false;
            return;
        }

        memset(ledStripDMABuffer[stripIdx], 0, sizeof(ledStripDMABuffer[stripIdx]));
        if (stripIdx == 0 && ledPinConfig()->led_pin_pwm_mode == LED_PIN_PWM_MODE_SHARED_HIGH) {
           ledStripDMABuffer[stripIdx][WS2811_DMA_BUFFER_SIZE-1] = 255;
        }
        ws2811Initialised[stripIdx] = true;
    }
}

void ws2811LedStripInit(void)
{
    ws2811LedStripInitByIndex(0, IO_TAG(WS2811_PIN));
#ifdef USE_LED_STRIP_2
    ws2811LedStripInitByIndex(1, IO_TAG(WS2811_PIN_2));
#endif
}

bool isWS2811LedStripReady(void)
{
    return !timerPWMDMAInProgress(ws2811TCH[0]);
}

#ifdef USE_LED_STRIP_2
bool isWS2811LedStripReadyIdx(uint8_t stripIdx)
{
    return !timerPWMDMAInProgress(ws2811TCH[stripIdx]);
}
#endif

STATIC_UNIT_TESTED uint16_t dmaBufferOffset;
static int16_t ledIndex;

static void fastUpdateLEDDMABufferIdx(uint8_t stripIdx, rgbColor24bpp_t *color)
{
    uint32_t grb = (color->rgb.g << 16) | (color->rgb.r << 8) | (color->rgb.b);

    for (int8_t index = 23; index >= 0; index--) {
        ledStripDMABuffer[stripIdx][WS2811_DELAY_BUFFER_LENGTH + dmaBufferOffset++] = (grb & (1 << index)) ? WS2811_BIT_COMPARE_1 : WS2811_BIT_COMPARE_0;
    }
}

STATIC_UNIT_TESTED void fastUpdateLEDDMABuffer(rgbColor24bpp_t *color)
{
    fastUpdateLEDDMABufferIdx(0, color);
}

static void ws2811UpdateStripInternal(uint8_t stripIdx)
{
    static rgbColor24bpp_t *rgb24;

    if (pwmMode[stripIdx] || timerPWMDMAInProgress(ws2811TCH[stripIdx])) {
        return;
    }

    dmaBufferOffset = 0;
    ledIndex = 0;

    while (ledIndex < WS2811_LED_STRIP_LENGTH)
    {
        rgb24 = hsvToRgb24(&ledColorBuffer[stripIdx][ledIndex]);
        fastUpdateLEDDMABufferIdx(stripIdx, rgb24);
        ledIndex++;
    }

    if (!ws2811Initialised[stripIdx] || !ws2811TCH[stripIdx]) {
        return;
    }

    timerPWMPrepareDMA(ws2811TCH[stripIdx], WS2811_DMA_BUFFER_SIZE);
    timerPWMStartDMA(ws2811TCH[stripIdx]);
}

/*
 * This method is non-blocking unless an existing LED update is in progress.
 * it does not wait until all the LEDs have been updated, that happens in the background.
 */
void ws2811UpdateStrip(void)
{
    ws2811UpdateStripInternal(0);
}

#ifdef USE_LED_STRIP_2
void ws2811UpdateStripIdx(uint8_t stripIdx)
{
    ws2811UpdateStripInternal(stripIdx);
}
#endif

//value
void ledPinStartPWM(uint16_t value) {
    if (ws2811TCH[0] == NULL) {
        return;
    }

	if ( !pwmMode[0] ) {
	    timerPWMStopDMA(ws2811TCH[0]);
        //FIXME: implement method to release DMA
        ws2811TCH[0]->dma->owner = OWNER_FREE;

        ledConfigurePWMIdx(0);
    }
	*timerCCR(ws2811TCH[0]) = value;
}

void ledPinStopPWM(void) {
    if (ws2811TCH[0] == NULL || !pwmMode[0] ) {
        return;
    }

    if ( ledPinConfig()->led_pin_pwm_mode == LED_PIN_PWM_MODE_HIGH ) {
		*timerCCR(ws2811TCH[0]) = 100;
        return;
    } else if ( ledPinConfig()->led_pin_pwm_mode == LED_PIN_PWM_MODE_LOW ) {
		*timerCCR(ws2811TCH[0]) = 0;
        return;
    }
    pwmMode[0] = false;

    if (!ledConfigureDMAIdx(0)) {
        ws2811Initialised[0] = false;
    }
}


#endif
