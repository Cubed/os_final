
/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#pragma once

typedef enum{
    SOUND_TYPE_KNOB,
    SOUND_TYPE_SNORE,
    SOUND_TYPE_ALARM,
    SOUND_TYPE_WASH_END_CN,
    SOUND_TYPE_WASH_END_EN,
    SOUND_TYPE_FACTORY,
    SOUND_TYPE_LIGHT_ON,
    SOUND_TYPE_LIGHT_OFF,
    SOUND_TYPE_COLOR_WARM,
    SOUND_TYPE_COLOR_COOL,
    SOUND_TYPE_LIGHT_100,
    SOUND_TYPE_LIGHT_75,
    SOUND_TYPE_LIGHT_50,
    SOUND_TYPE_LIGHT_25
}PDM_SOUND_TYPE;

esp_err_t audio_force_quite(bool ret);

esp_err_t audio_handle_info(PDM_SOUND_TYPE voice);

esp_err_t audio_play_start();