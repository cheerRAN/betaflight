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
 Created by Marcin Baliniak
 some functions based on MinimOSD

 OSD-CMS separation by jflyper
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include "platform.h"

#include "build/version.h"

#ifdef OSD

#include "common/utils.h"

#include "drivers/system.h"

#include "io/cms.h"
#include "io/cms_types.h"

#include "io/flashfs.h"
#include "io/osd.h"

#include "fc/config.h"
#include "fc/rc_controls.h"
#include "fc/runtime_config.h"

//#include "flight/pid.h"

#include "config/config_profile.h"
#include "config/config_master.h"
#include "config/feature.h"

#ifdef USE_HARDWARE_REVISION_DETECTION
#include "hardware_revision.h"
#endif

#include "drivers/max7456.h"
#include "drivers/max7456_symbols.h"

#include "common/printf.h"

#include "build/debug.h"

// Things in both OSD and CMS

#define IS_HI(X)  (rcData[X] > 1750)
#define IS_LO(X)  (rcData[X] < 1250)
#define IS_MID(X) (rcData[X] > 1250 && rcData[X] < 1750)

bool blinkState = true;

//extern uint8_t RSSI; // TODO: not used?

static uint16_t flyTime = 0;
uint8_t statRssi;

statistic_t stats;

uint16_t refreshTimeout = 0;
#define REFRESH_1S    12

uint8_t armState;

// OSD forwards

void osdUpdate(uint32_t currentTime);
char osdGetAltitudeSymbol();
int32_t osdGetAltitude(int32_t alt);
void osdEditElement(void *ptr);
void osdDrawElements(void);
void osdDrawSingleElement(uint8_t item);

bool osdInMenu = false;

#define AH_MAX_PITCH 200 // Specify maximum AHI pitch value displayed. Default 200 = 20.0 degrees
#define AH_MAX_ROLL 400  // Specify maximum AHI roll value displayed. Default 400 = 40.0 degrees
#define AH_SIDEBAR_WIDTH_POS 7
#define AH_SIDEBAR_HEIGHT_POS 3

void osdDrawElements(void)
{
    max7456ClearScreen();

#if 0
    if (currentElement)
        osdDrawElementPositioningHelp();
#else
    if (false)
        ;
#endif
    else if (sensors(SENSOR_ACC) || osdInMenu)
    {
        osdDrawSingleElement(OSD_ARTIFICIAL_HORIZON);
        osdDrawSingleElement(OSD_CROSSHAIRS);
    }

    osdDrawSingleElement(OSD_MAIN_BATT_VOLTAGE);
    osdDrawSingleElement(OSD_RSSI_VALUE);
    osdDrawSingleElement(OSD_FLYTIME);
    osdDrawSingleElement(OSD_ONTIME);
    osdDrawSingleElement(OSD_FLYMODE);
    osdDrawSingleElement(OSD_THROTTLE_POS);
    osdDrawSingleElement(OSD_VTX_CHANNEL);
    osdDrawSingleElement(OSD_CURRENT_DRAW);
    osdDrawSingleElement(OSD_MAH_DRAWN);
    osdDrawSingleElement(OSD_CRAFT_NAME);
    osdDrawSingleElement(OSD_ALTITUDE);

#ifdef GPS
    if (sensors(SENSOR_GPS) || osdInMenu) {
        osdDrawSingleElement(OSD_GPS_SATS);
        osdDrawSingleElement(OSD_GPS_SPEED);
    }
#endif // GPS
}

void osdDrawSingleElement(uint8_t item)
{
    if (!VISIBLE(masterConfig.osdProfile.item_pos[item]) || BLINK(masterConfig.osdProfile.item_pos[item]))
        return;

    uint8_t elemPosX = OSD_X(masterConfig.osdProfile.item_pos[item]);
    uint8_t elemPosY = OSD_Y(masterConfig.osdProfile.item_pos[item]);
    char buff[32];

    switch(item) {
        case OSD_RSSI_VALUE:
        {
            uint16_t osdRssi = rssi * 100 / 1024; // change range
            if (osdRssi >= 100)
                osdRssi = 99;

            buff[0] = SYM_RSSI;
            sprintf(buff + 1, "%d", osdRssi);
            break;
        }

        case OSD_MAIN_BATT_VOLTAGE:
        {
            buff[0] = SYM_BATT_5;
            sprintf(buff + 1, "%d.%1dV", vbat / 10, vbat % 10);
            break;
        }

        case OSD_CURRENT_DRAW:
        {
            buff[0] = SYM_AMP;
            sprintf(buff + 1, "%d.%02d", abs(amperage) / 100, abs(amperage) % 100);
            break;
        }

        case OSD_MAH_DRAWN:
        {
            buff[0] = SYM_MAH;
            sprintf(buff + 1, "%d", mAhDrawn);
            break;
        }

#ifdef GPS
        case OSD_GPS_SATS:
        {
            buff[0] = 0x1f;
            sprintf(buff + 1, "%d", GPS_numSat);
            break;
        }

        case OSD_GPS_SPEED:
        {
            sprintf(buff, "%d", GPS_speed * 36 / 1000);
            break;
        }
#endif // GPS

        case OSD_ALTITUDE:
        {
            int32_t alt = osdGetAltitude(BaroAlt);
            sprintf(buff, "%c%d.%01d%c", alt < 0 ? '-' : ' ', abs(alt / 100), abs((alt % 100) / 10), osdGetAltitudeSymbol());
            break;
        }

        case OSD_ONTIME:
        {
            uint32_t seconds = micros() / 1000000;
            buff[0] = SYM_ON_M;
            sprintf(buff + 1, "%02d:%02d", seconds / 60, seconds % 60);
            break;
        }

        case OSD_FLYTIME:
        {
            buff[0] = SYM_FLY_M;
            sprintf(buff + 1, "%02d:%02d", flyTime / 60, flyTime % 60);
            break;
        }

        case OSD_FLYMODE:
        {
            char *p = "ACRO";

            if (isAirmodeActive())
                p = "AIR";

            if (FLIGHT_MODE(FAILSAFE_MODE))
                p = "!FS";
            else if (FLIGHT_MODE(ANGLE_MODE))
                p = "STAB";
            else if (FLIGHT_MODE(HORIZON_MODE))
                p = "HOR";

            max7456Write(elemPosX, elemPosY, p);
            return;
        }

        case OSD_CRAFT_NAME:
        {
            if (strlen(masterConfig.name) == 0)
                strcpy(buff, "CRAFT_NAME");
            else {
                for (uint8_t i = 0; i < MAX_NAME_LENGTH; i++) {
                    buff[i] = toupper((unsigned char)masterConfig.name[i]);
                    if (masterConfig.name[i] == 0)
                        break;
                }
            }

            break;
        }

        case OSD_THROTTLE_POS:
        {
            buff[0] = SYM_THR;
            buff[1] = SYM_THR1;
            sprintf(buff + 2, "%d", (constrain(rcData[THROTTLE], PWM_RANGE_MIN, PWM_RANGE_MAX) - PWM_RANGE_MIN) * 100 / (PWM_RANGE_MAX - PWM_RANGE_MIN));
            break;
        }

#ifdef VTX
        case OSD_VTX_CHANNEL:
        {
            sprintf(buff, "CH:%d", current_vtx_channel % CHANNELS_PER_BAND + 1);
            break;
        }
#endif // VTX

        case OSD_CROSSHAIRS:
            elemPosX = 14 - 1; // Offset for 1 char to the left
            elemPosY = 6;
            if (maxScreenSize == VIDEO_BUFFER_CHARS_PAL)
                ++elemPosY;
            buff[0] = SYM_AH_CENTER_LINE;
            buff[1] = SYM_AH_CENTER;
            buff[2] = SYM_AH_CENTER_LINE_RIGHT;
            buff[3] = 0;
            break;

        case OSD_ARTIFICIAL_HORIZON:
        {
            elemPosX = 14;
            elemPosY = 6 - 4; // Top center of the AH area

            int rollAngle = attitude.values.roll;
            int pitchAngle = attitude.values.pitch;

            if (maxScreenSize == VIDEO_BUFFER_CHARS_PAL)
                ++elemPosY;

            if (pitchAngle > AH_MAX_PITCH)
                pitchAngle = AH_MAX_PITCH;
            if (pitchAngle < -AH_MAX_PITCH)
                pitchAngle = -AH_MAX_PITCH;
            if (rollAngle > AH_MAX_ROLL)
                rollAngle = AH_MAX_ROLL;
            if (rollAngle < -AH_MAX_ROLL)
                rollAngle = -AH_MAX_ROLL;

            // Convert pitchAngle to y compensation value
            pitchAngle = (pitchAngle / 8) - 41; // 41 = 4 * 9 + 5

            for (int8_t x = -4; x <= 4; x++) {
                int y = (rollAngle * x) / 64;
                y -= pitchAngle;
                // y += 41; // == 4 * 9 + 5
                if (y >= 0 && y <= 81) {
                    max7456WriteChar(elemPosX + x, elemPosY + (y / 9), (SYM_AH_BAR9_0 + (y % 9)));
                }
            }

            osdDrawSingleElement(OSD_HORIZON_SIDEBARS);

            return;
        }

        case OSD_HORIZON_SIDEBARS:
        {
            elemPosX = 14;
            elemPosY = 6;

            if (maxScreenSize == VIDEO_BUFFER_CHARS_PAL)
                ++elemPosY;

            // Draw AH sides
            int8_t hudwidth = AH_SIDEBAR_WIDTH_POS;
            int8_t hudheight = AH_SIDEBAR_HEIGHT_POS;
            for (int8_t y = -hudheight; y <= hudheight; y++) {
                max7456WriteChar(elemPosX - hudwidth, elemPosY + y, SYM_AH_DECORATION);
                max7456WriteChar(elemPosX + hudwidth, elemPosY + y, SYM_AH_DECORATION);
            }

            // AH level indicators
            max7456WriteChar(elemPosX - hudwidth + 1, elemPosY, SYM_AH_LEFT);
            max7456WriteChar(elemPosX + hudwidth - 1, elemPosY, SYM_AH_RIGHT);

            return;
        }

        default:
            return;
    }

    max7456Write(elemPosX, elemPosY, buff);
}

void resetOsdConfig(osd_profile_t *osdProfile)
{
    osdProfile->item_pos[OSD_RSSI_VALUE] = OSD_POS(22, 0) | VISIBLE_FLAG;
    osdProfile->item_pos[OSD_MAIN_BATT_VOLTAGE] = OSD_POS(12, 0) | VISIBLE_FLAG;
    osdProfile->item_pos[OSD_ARTIFICIAL_HORIZON] = OSD_POS(8, 6) | VISIBLE_FLAG;
    osdProfile->item_pos[OSD_HORIZON_SIDEBARS] = OSD_POS(8, 6) | VISIBLE_FLAG;
    osdProfile->item_pos[OSD_ONTIME] = OSD_POS(22, 11) | VISIBLE_FLAG;
    osdProfile->item_pos[OSD_FLYTIME] = OSD_POS(22, 12) | VISIBLE_FLAG;
    osdProfile->item_pos[OSD_FLYMODE] = OSD_POS(12, 11) | VISIBLE_FLAG;
    osdProfile->item_pos[OSD_CRAFT_NAME] = OSD_POS(12, 12);
    osdProfile->item_pos[OSD_THROTTLE_POS] = OSD_POS(1, 4);
    osdProfile->item_pos[OSD_VTX_CHANNEL] = OSD_POS(8, 6);
    osdProfile->item_pos[OSD_CURRENT_DRAW] = OSD_POS(1, 3);
    osdProfile->item_pos[OSD_MAH_DRAWN] = OSD_POS(15, 3);
    osdProfile->item_pos[OSD_GPS_SPEED] = OSD_POS(2, 2);
    osdProfile->item_pos[OSD_GPS_SATS] = OSD_POS(2, 12);
    osdProfile->item_pos[OSD_ALTITUDE] = OSD_POS(1, 5);

    osdProfile->rssi_alarm = 20;
    osdProfile->cap_alarm = 2200;
    osdProfile->time_alarm = 10; // in minutes
    osdProfile->alt_alarm = 100; // meters or feet depend on configuration

    osdProfile->video_system = 0;
}

void osdInit(void)
{
    char x, string_buffer[30];

    armState = ARMING_FLAG(ARMED);

    max7456Init(masterConfig.osdProfile.video_system);

    max7456ClearScreen();

    // display logo and help
    x = 160;
    for (int i = 1; i < 5; i++) {
        for (int j = 3; j < 27; j++) {
            if (x != 255)
                max7456WriteChar(j, i, x++);
        }
    }

    sprintf(string_buffer, "BF VERSION: %s", FC_VERSION_STRING);
    max7456Write(5, 6, string_buffer);
#ifdef CMS
    max7456Write(7, 7,  CMS_STARTUP_HELP_TEXT1);
    max7456Write(11, 8, CMS_STARTUP_HELP_TEXT2);
    max7456Write(11, 9, CMS_STARTUP_HELP_TEXT3);
#endif

    max7456RefreshAll();

    refreshTimeout = 4 * REFRESH_1S;

#ifdef CMS
    cmsDeviceRegister(osdCmsInit);
#endif
}

/**
 * Gets the correct altitude symbol for the current unit system
 */
char osdGetAltitudeSymbol()
{
    switch (masterConfig.osdProfile.units) {
        case OSD_UNIT_IMPERIAL:
            return 0xF;
        default:
            return 0xC;
    }
}

/**
 * Converts altitude based on the current unit system.
 * @param alt Raw altitude (i.e. as taken from BaroAlt)
 */
int32_t osdGetAltitude(int32_t alt)
{
    switch (masterConfig.osdProfile.units) {
        case OSD_UNIT_IMPERIAL:
            return (alt * 328) / 100; // Convert to feet / 100
        default:
            return alt;               // Already in metre / 100
    }
}

void osdUpdateAlarms(void)
{
    int32_t alt = osdGetAltitude(BaroAlt) / 100;
    statRssi = rssi * 100 / 1024;

    if (statRssi < masterConfig.osdProfile.rssi_alarm)
        masterConfig.osdProfile.item_pos[OSD_RSSI_VALUE] |= BLINK_FLAG;
    else
        masterConfig.osdProfile.item_pos[OSD_RSSI_VALUE] &= ~BLINK_FLAG;

    if (vbat <= (batteryWarningVoltage - 1))
        masterConfig.osdProfile.item_pos[OSD_MAIN_BATT_VOLTAGE] |= BLINK_FLAG;
    else
        masterConfig.osdProfile.item_pos[OSD_MAIN_BATT_VOLTAGE] &= ~BLINK_FLAG;

    if (STATE(GPS_FIX) == 0)
        masterConfig.osdProfile.item_pos[OSD_GPS_SATS] |= BLINK_FLAG;
    else
        masterConfig.osdProfile.item_pos[OSD_GPS_SATS] &= ~BLINK_FLAG;

    if (flyTime / 60 >= masterConfig.osdProfile.time_alarm && ARMING_FLAG(ARMED))
        masterConfig.osdProfile.item_pos[OSD_FLYTIME] |= BLINK_FLAG;
    else
        masterConfig.osdProfile.item_pos[OSD_FLYTIME] &= ~BLINK_FLAG;

    if (mAhDrawn >= masterConfig.osdProfile.cap_alarm)
        masterConfig.osdProfile.item_pos[OSD_MAH_DRAWN] |= BLINK_FLAG;
    else
        masterConfig.osdProfile.item_pos[OSD_MAH_DRAWN] &= ~BLINK_FLAG;

    if (alt >= masterConfig.osdProfile.alt_alarm)
        masterConfig.osdProfile.item_pos[OSD_ALTITUDE] |= BLINK_FLAG;
    else
        masterConfig.osdProfile.item_pos[OSD_ALTITUDE] &= ~BLINK_FLAG;
}

void osdResetAlarms(void)
{
    masterConfig.osdProfile.item_pos[OSD_RSSI_VALUE] &= ~BLINK_FLAG;
    masterConfig.osdProfile.item_pos[OSD_MAIN_BATT_VOLTAGE] &= ~BLINK_FLAG;
    masterConfig.osdProfile.item_pos[OSD_GPS_SATS] &= ~BLINK_FLAG;
    masterConfig.osdProfile.item_pos[OSD_FLYTIME] &= ~BLINK_FLAG;
    masterConfig.osdProfile.item_pos[OSD_MAH_DRAWN] &= ~BLINK_FLAG;
}

void osdResetStats(void)
{
    stats.max_current = 0;
    stats.max_speed = 0;
    stats.min_voltage = 500;
    stats.max_current = 0;
    stats.min_rssi = 99;
    stats.max_altitude = 0;
}

void osdUpdateStats(void)
{
    int16_t value;

    value = GPS_speed * 36 / 1000;
    if (stats.max_speed < value)
        stats.max_speed = value;

    if (stats.min_voltage > vbat)
        stats.min_voltage = vbat;

    value = amperage / 100;
    if (stats.max_current < value)
        stats.max_current = value;

    if (stats.min_rssi > statRssi)
        stats.min_rssi = statRssi;

    if (stats.max_altitude < BaroAlt)
        stats.max_altitude = BaroAlt;
}

void osdShowStats(void)
{
    uint8_t top = 2;
    char buff[10];

    max7456ClearScreen();
    max7456Write(2, top++, "  --- STATS ---");

    if (STATE(GPS_FIX)) {
        max7456Write(2, top, "MAX SPEED        :");
        itoa(stats.max_speed, buff, 10);
        max7456Write(22, top++, buff);
    }

    max7456Write(2, top, "MIN BATTERY      :");
    sprintf(buff, "%d.%1dV", stats.min_voltage / 10, stats.min_voltage % 10);
    max7456Write(22, top++, buff);

    max7456Write(2, top, "MIN RSSI         :");
    itoa(stats.min_rssi, buff, 10);
    strcat(buff, "%");
    max7456Write(22, top++, buff);

    if (feature(FEATURE_CURRENT_METER)) {
        max7456Write(2, top, "MAX CURRENT      :");
        itoa(stats.max_current, buff, 10);
        strcat(buff, "A");
        max7456Write(22, top++, buff);

        max7456Write(2, top, "USED MAH         :");
        itoa(mAhDrawn, buff, 10);
        strcat(buff, "\x07");
        max7456Write(22, top++, buff);
    }

    max7456Write(2, top, "MAX ALTITUDE     :");
    int32_t alt = osdGetAltitude(stats.max_altitude);
    sprintf(buff, "%c%d.%01d%c", alt < 0 ? '-' : ' ', abs(alt / 100), abs((alt % 100) / 10), osdGetAltitudeSymbol());
    max7456Write(22, top++, buff);

    refreshTimeout = 60 * REFRESH_1S;
}

// called when motors armed
void osdArmMotors(void)
{
    max7456ClearScreen();
    max7456Write(12, 7, "ARMED");
    refreshTimeout = REFRESH_1S / 2;
    osdResetStats();
}

void updateOsd(uint32_t currentTime)
{
    static uint32_t counter = 0;
#ifdef MAX7456_DMA_CHANNEL_TX
    // don't touch buffers if DMA transaction is in progress
    if (max7456DmaInProgres())
        return;
#endif // MAX7456_DMA_CHANNEL_TX

    // redraw values in buffer
    if (counter++ % 5 == 0)
        osdUpdate(currentTime);
    else // rest of time redraw screen 10 chars per idle to don't lock the main idle
        max7456DrawScreen();

    // do not allow ARM if we are in menu
    if (osdInMenu)
        DISABLE_ARMING_FLAG(OK_TO_ARM);
}

void osdUpdate(uint32_t currentTime)
{
    static uint8_t lastSec = 0;
    uint8_t sec;

#ifdef OSD_CALLS_CMS
    // detect enter to menu
    if (IS_MID(THROTTLE) && IS_HI(YAW) && IS_HI(PITCH) && !ARMING_FLAG(ARMED)) {
        cmsOpenMenu();
    }
#endif

    // detect arm/disarm
    if (armState != ARMING_FLAG(ARMED)) {
        if (ARMING_FLAG(ARMED))
            osdArmMotors(); // reset statistic etc
        else
            osdShowStats(); // show statistic

        armState = ARMING_FLAG(ARMED);
    }

    osdUpdateStats();

    sec = currentTime / 1000000;

    if (ARMING_FLAG(ARMED) && sec != lastSec) {
        flyTime++;
        lastSec = sec;
    }

    if (refreshTimeout) {
        if (IS_HI(THROTTLE) || IS_HI(PITCH)) // hide statistics
            refreshTimeout = 1;
        refreshTimeout--;
        if (!refreshTimeout)
            max7456ClearScreen();
        return;
    }

    blinkState = (millis() / 200) % 2;

    if (!osdInMenu) {
        osdUpdateAlarms();
        osdDrawElements();
#ifdef OSD_CALLS_CMS
    } else {
        cmsUpdate(currentTime);
#endif
    }
}

//
// OSD specific CMS functions
//

uint8_t shiftdown;

int osdMenuBegin(void)
{
    osdResetAlarms();
    osdInMenu = true;
    refreshTimeout = 0;

    return 0;
}

int osdMenuEnd(void)
{
    osdInMenu = false;

    return 0;
}

int osdClearScreen(void)
{
    max7456ClearScreen();

    return 0;
}

int osdWrite(uint8_t x, uint8_t y, char *s)
{
    max7456Write(x, y + shiftdown, s);

    return 0;
}

void osdResync(displayPort_t *pPort)
{
    max7456RefreshAll();
    pPort->rows = max7456GetRowsCount() - masterConfig.osdProfile.row_shiftdown;
    pPort->cols = 30;
}

int osdHeartbeat(void)
{
    return 0;
}

uint32_t osdTxBytesFree(void)
{
    return UINT32_MAX;
}

#ifdef EDIT_ELEMENT_SUPPORT
void osdEditElement(void *ptr)
{
    uint32_t address = (uint32_t)ptr;

    // zsave position on menu stack
    menuStack[menuStackIdx] = currentMenu;
    menuStackHistory[menuStackIdx] = currentMenuPos;
    menuStackIdx++;

    currentElement = (uint16_t *)address;

    *currentElement |= BLINK_FLAG;
    max7456ClearScreen();
}

void osdDrawElementPositioningHelp(void)
{
    max7456Write(OSD_X(masterConfig.osdProfile.item_pos[OSD_ARTIFICIAL_HORIZON]), OSD_Y(masterConfig.osdProfile.item_pos[OSD_ARTIFICIAL_HORIZON]), "---  HELP --- ");
    max7456Write(OSD_X(masterConfig.osdProfile.item_pos[OSD_ARTIFICIAL_HORIZON]), OSD_Y(masterConfig.osdProfile.item_pos[OSD_ARTIFICIAL_HORIZON]) + 1, "USE ROLL/PITCH");
    max7456Write(OSD_X(masterConfig.osdProfile.item_pos[OSD_ARTIFICIAL_HORIZON]), OSD_Y(masterConfig.osdProfile.item_pos[OSD_ARTIFICIAL_HORIZON]) + 2, "TO MOVE ELEM. ");
    max7456Write(OSD_X(masterConfig.osdProfile.item_pos[OSD_ARTIFICIAL_HORIZON]), OSD_Y(masterConfig.osdProfile.item_pos[OSD_ARTIFICIAL_HORIZON]) + 3, "              ");
    max7456Write(OSD_X(masterConfig.osdProfile.item_pos[OSD_ARTIFICIAL_HORIZON]), OSD_Y(masterConfig.osdProfile.item_pos[OSD_ARTIFICIAL_HORIZON]) + 4, "YAW - EXIT    ");
}
#endif

displayPortVTable_t osdVTable = {
    osdMenuBegin,
    osdMenuEnd,
    osdClearScreen,
    osdWrite,
    osdHeartbeat,
    osdResync,
    osdTxBytesFree,
};

void osdCmsInit(displayPort_t *pPort)
{
    osdResync(pPort);
    pPort->vTable = &osdVTable;
}
#endif // OSD
