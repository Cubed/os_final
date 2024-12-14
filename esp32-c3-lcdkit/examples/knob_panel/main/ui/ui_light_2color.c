/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "lvgl.h"
#include <stdio.h>

#include "lv_example_pub.h"
#include "lv_example_image.h"
#include "bsp/esp-bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "app_audio.h"
#include "freertos/event_groups.h"

static EventGroupHandle_t announcement_event_group;

// Define bit masks for each announcement type
#define ANNOUNCE_PWM_100_BIT (1 << 0)
#define ANNOUNCE_PWM_75_BIT (1 << 1)
#define ANNOUNCE_PWM_50_BIT (1 << 2)
#define ANNOUNCE_PWM_25_BIT (1 << 3)
#define ANNOUNCE_LIGHT_OFF_BIT (1 << 4)
#define ANNOUNCE_TIMER_COMPLETE_BIT (1 << 5)
#define ANNOUNCE_COLOR_WARM_BIT (1 << 6)
#define ANNOUNCE_COLOR_COOL_BIT (1 << 7)

#define ALL_ANNOUNCEMENT_BITS (ANNOUNCE_PWM_100_BIT | ANNOUNCE_PWM_75_BIT |           \
                               ANNOUNCE_PWM_50_BIT | ANNOUNCE_PWM_25_BIT |            \
                               ANNOUNCE_LIGHT_OFF_BIT | ANNOUNCE_TIMER_COMPLETE_BIT | \
                               ANNOUNCE_COLOR_WARM_BIT | ANNOUNCE_COLOR_COOL_BIT)

static bool light_2color_layer_enter_cb(void *layer);
static bool light_2color_layer_exit_cb(void *layer);
static void light_2color_layer_timer_cb(lv_timer_t *tmr);
static lv_obj_t *page_label; // New label for status messages
typedef enum
{
    LIGHT_CCK_WARM,
    LIGHT_CCK_COOL,
    LIGHT_CCK_MAX,
} LIGHT_CCK_TYPE;
typedef struct
{
    uint8_t light_pwm;
    LIGHT_CCK_TYPE light_cck;
} light_set_attribute_t;
typedef struct
{
    const lv_img_dsc_t *img_bg[2];

    const lv_img_dsc_t *img_pwm_25[2];
    const lv_img_dsc_t *img_pwm_50[2];
    const lv_img_dsc_t *img_pwm_75[2];
    const lv_img_dsc_t *img_pwm_100[2];
} ui_light_img_t;

TaskHandle_t xHandle = NULL;

typedef enum
{
    ANNOUNCE_LIGHT_ON,
    ANNOUNCE_LIGHT_OFF,
    ANNOUNCE_COLOR_WARM,
    ANNOUNCE_COLOR_COOL,
    ANNOUNCE_PWM_SET,
    ANNOUNCE_PWM_100,
    ANNOUNCE_PWM_75,
    ANNOUNCE_PWM_50,
    ANNOUNCE_PWM_25,
    ANNOUNCE_TIMER_COMPLETE
} announcement_type_t;

// Timer Variables
static int timer_seconds = 180; // 3 minutes in seconds
static lv_timer_t *countdown_timer_handle = NULL;
static int countdown_counter = 0; // Counts the number of timer callbacks
static bool timer_active = false; // Indicates if the timer is active

typedef enum
{
    MODE_NORMAL,
    SETTING_COLOR, // User is selecting the flash color
    SETTING_TIMER, // User is setting the timer duration
    IDLE,         // Normal operation,
    TIMER_SET // Timer was set.
} setting_state_t;

static setting_state_t current_setting_state = MODE_NORMAL; // Initial state
static LIGHT_CCK_TYPE selected_color = LIGHT_CCK_WARM;      // Default color
static int set_timer_minutes = 0;                           // Timer duration in minutes


typedef struct
{
    announcement_type_t type;
    uint8_t pwm_level; // Relevant for PWM announcements
} announcement_message_t;

static lv_obj_t *page;

static QueueHandle_t announcement_queue = NULL;
static time_out_count time_20ms, time_500ms;

static lv_obj_t *img_light_bg, *label_pwm_set;
static lv_obj_t *img_light_pwm_25, *img_light_pwm_50, *img_light_pwm_75, *img_light_pwm_100, *img_light_pwm_0;

static light_set_attribute_t light_set_conf, light_xor;

static const ui_light_img_t light_image = {
    {&light_warm_bg, &light_cool_bg},
    {&light_warm_25, &light_cool_25},
    {&light_warm_50, &light_cool_50},
    {&light_warm_75, &light_cool_75},
    {&light_warm_100, &light_cool_100},
};

lv_layer_t light_2color_Layer = {
    .lv_obj_name = "light_2color_Layer",
    .lv_obj_parent = NULL,
    .lv_obj_layer = NULL,
    .lv_show_layer = NULL,
    .enter_cb = light_2color_layer_enter_cb,
    .exit_cb = light_2color_layer_exit_cb,
    .timer_cb = light_2color_layer_timer_cb,
};


void LED_FLASH_TASK(void *pvParameters)
{
    (void)pvParameters; // this is supressing unused param.
    while (1)
    {
        bsp_led_rgb_set(0xFF, 0x00, 0x00); 
        vTaskDelay(pdMS_TO_TICKS(100));
        bsp_led_rgb_set(0x00, 0x00, 0xFF);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}




static void audio_announcement_task(void *pvParameters)
{
    EventBits_t uxBits;
    while (1)
    {
        uxBits = xEventGroupWaitBits(
            announcement_event_group,
            ALL_ANNOUNCEMENT_BITS,    
            pdTRUE,                  
            pdFALSE,                  
            portMAX_DELAY             
        );
        if (uxBits & ANNOUNCE_PWM_100_BIT)
        {
            audio_handle_info(SOUND_TYPE_LIGHT_100);
        }
        if (uxBits & ANNOUNCE_PWM_75_BIT)
        {
            audio_handle_info(SOUND_TYPE_LIGHT_75);
        }
        if (uxBits & ANNOUNCE_PWM_50_BIT)
        {
            audio_handle_info(SOUND_TYPE_LIGHT_50);
        }
        if (uxBits & ANNOUNCE_PWM_25_BIT)
        {
            audio_handle_info(SOUND_TYPE_LIGHT_25);
        }
        if (uxBits & ANNOUNCE_LIGHT_OFF_BIT)
        {
            audio_handle_info(SOUND_TYPE_LIGHT_OFF);
        }
        if (uxBits & ANNOUNCE_COLOR_WARM_BIT)
        {
            audio_handle_info(SOUND_TYPE_COLOR_WARM);
        }
        if (uxBits & ANNOUNCE_COLOR_COOL_BIT)
        {
            audio_handle_info(SOUND_TYPE_COLOR_COOL);
        }
        if (uxBits & ANNOUNCE_TIMER_COMPLETE_BIT)
        {
            audio_handle_info(SOUND_TYPE_ALARM);
        }
        // Add a small delay to prevent tight looping, if necessary
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void light_2color_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    announcement_message_t msg;

    if (code == LV_EVENT_FOCUSED) {
        lv_group_set_editing(lv_group_get_default(), true);
    }
    else if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);

        if (is_time_out(&time_500ms)) {
            if (current_setting_state == MODE_NORMAL) {
                // Brightness Control Mode If branches.
                if (key == LV_KEY_RIGHT) {
                    if (light_set_conf.light_pwm < 100) {
                        light_set_conf.light_pwm += 25;
                        // Update the UI to reflect the new brightness level
                        lv_label_set_text_fmt(label_pwm_set, "%d%%", light_set_conf.light_pwm);
                        msg.type = ANNOUNCE_PWM_SET;
                        msg.pwm_level = light_set_conf.light_pwm;
                        xEventGroupSetBits(announcement_event_group, msg.type);
                    }
                }
                else if (key == LV_KEY_LEFT) {
                    if (light_set_conf.light_pwm > 0) {
                        light_set_conf.light_pwm -= 25;
                        lv_label_set_text_fmt(label_pwm_set, "%d%%", light_set_conf.light_pwm);
                        msg.type = ANNOUNCE_PWM_SET;
                        msg.pwm_level = light_set_conf.light_pwm;
                        xEventGroupSetBits(announcement_event_group, msg.type);
                    }
                }
            }
            else if (current_setting_state == SETTING_TIMER) {
                // Timer Setting Mode
                if (key == LV_KEY_RIGHT) {
                    set_timer_minutes += 1;
                    if (set_timer_minutes > 60) { 
                        set_timer_minutes = 60;
                        lv_label_set_text(page_label, "Max Timer Set");
                    }
                    else {
                        lv_label_set_text_fmt(label_pwm_set, "%02d:%02d", set_timer_minutes, 0);
                        lv_label_set_text(page_label, "Timer Set: Rotate Knob");
                    }
                }
                else if (key == LV_KEY_LEFT) {
                    if (set_timer_minutes > 1) { // Prevent timer from going below 1 minute
                        set_timer_minutes -= 1;
                        lv_label_set_text_fmt(label_pwm_set, "%02d:%02d", set_timer_minutes, 0);
                        lv_label_set_text(page_label, "Timer Set: Rotate Knob");
                    }
                }
            }
        }
    }
    else if (code == LV_EVENT_CLICKED) {
        if (current_setting_state == MODE_NORMAL) {
            current_setting_state = SETTING_TIMER;
            set_timer_minutes = 0;
            lv_label_set_text_fmt(label_pwm_set, "%02d:%02d", set_timer_minutes, 0);
            lv_label_set_text(page_label, "Set Timer: Rotate Knob");
        }
        else if (current_setting_state == SETTING_TIMER) {
            if (set_timer_minutes > 0) {
                timer_seconds = set_timer_minutes * 60;
                timer_active = true;
                countdown_counter = 0;
                lv_label_set_text_fmt(label_pwm_set, "%02d:%02d", timer_seconds / 60, timer_seconds % 60);
                lv_label_set_text(page_label, "Timer Started");
                current_setting_state = TIMER_SET;
            }
            else {
                //Invalid input handling.
                lv_label_set_text(page_label, "Set Timer: Min 1 Minute");
            }
        }
        else if (current_setting_state == TIMER_SET) 
        {
            vTaskDelete(xHandle);
            timer_seconds = 0;
            lv_label_set_text_fmt(label_pwm_set, "%02d:%02d", 0, 0);
            lv_label_set_text(page_label, "Timer Ended");
            current_setting_state = MODE_NORMAL;
        }
    }
    else if (code == LV_EVENT_LONG_PRESSED) {
        lv_indev_wait_release(lv_indev_get_next(NULL));
        ui_remove_all_objs_from_encoder_group();
        lv_func_goto_layer(&menu_layer);
        current_setting_state = MODE_NORMAL;
    }
}


void ui_light_2color_init(lv_obj_t *parent)
{
    light_xor.light_pwm = 0xFF;
    light_xor.light_cck = LIGHT_CCK_MAX;

    light_set_conf.light_pwm = 50;
    light_set_conf.light_cck = LIGHT_CCK_WARM;

    page = lv_obj_create(parent);
    lv_obj_set_size(page, LV_HOR_RES, LV_VER_RES);

    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(page);

    img_light_bg = lv_img_create(page);
    lv_img_set_src(img_light_bg, &light_warm_bg);
    lv_obj_align(img_light_bg, LV_ALIGN_CENTER, 0, 0);

    label_pwm_set = lv_label_create(page);
    lv_obj_set_style_text_font(label_pwm_set, &HelveticaNeue_Regular_24, 0);

    if (light_set_conf.light_pwm)
    {
        lv_label_set_text_fmt(label_pwm_set, "%d%%", light_set_conf.light_pwm);
    }
    else
    {
        lv_label_set_text_fmt(label_pwm_set, "%02d:%02d", timer_seconds / 60, timer_seconds % 60);
        // Start the countdown timer
        timer_active = true;
        countdown_counter = 0;
    }
    lv_obj_align(label_pwm_set, LV_ALIGN_CENTER, 0, 65);

    img_light_pwm_0 = lv_img_create(page);
    lv_img_set_src(img_light_pwm_0, &light_close_status);
    lv_obj_add_flag(img_light_pwm_0, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(img_light_pwm_0, LV_ALIGN_TOP_MID, 0, 0);

    img_light_pwm_25 = lv_img_create(page);
    lv_img_set_src(img_light_pwm_25, &light_warm_25);
    lv_obj_align(img_light_pwm_25, LV_ALIGN_TOP_MID, 0, 0);

    img_light_pwm_50 = lv_img_create(page);
    lv_img_set_src(img_light_pwm_50, &light_warm_50);
    lv_obj_align(img_light_pwm_50, LV_ALIGN_TOP_MID, 0, 0);

    img_light_pwm_75 = lv_img_create(page);
    lv_img_set_src(img_light_pwm_75, &light_warm_75);
    lv_obj_add_flag(img_light_pwm_75, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(img_light_pwm_75, LV_ALIGN_TOP_MID, 0, 0);

    img_light_pwm_100 = lv_img_create(page);
    lv_img_set_src(img_light_pwm_100, &light_warm_100);
    lv_obj_add_flag(img_light_pwm_100, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(img_light_pwm_100, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_add_event_cb(page, light_2color_event_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(page, light_2color_event_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(page, light_2color_event_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(page, light_2color_event_cb, LV_EVENT_CLICKED, NULL);
    ui_add_obj_to_encoder_group(page);

    page_label = lv_label_create(page);
    lv_obj_set_style_text_font(page_label, &HelveticaNeue_Regular_24, 0);
    lv_label_set_text(page_label, "Select Color: Press Knob to Confirm");
    lv_obj_align(page_label, LV_ALIGN_TOP_MID, 0, 10);
}

static bool light_2color_layer_enter_cb(void *layer)
{
    bool ret = false;

    LV_LOG_USER("");
    lv_layer_t *create_layer = layer;
    if (NULL == create_layer->lv_obj_layer)
    {
        ret = true;
        create_layer->lv_obj_layer = lv_obj_create(lv_scr_act());
        lv_obj_remove_style_all(create_layer->lv_obj_layer);
        lv_obj_set_size(create_layer->lv_obj_layer, LV_HOR_RES, LV_VER_RES);

        ui_light_2color_init(create_layer->lv_obj_layer);
        set_time_out(&time_20ms, 20);
        set_time_out(&time_500ms, 200);

        announcement_event_group = xEventGroupCreate();
        if (announcement_event_group == NULL)
        {
            LV_LOG_ERROR("Failed to create announcement event group");
        }
        else
        {
            // Create the audio announcement task
            xTaskCreate(audio_announcement_task, "AudioAnnouncement", 2048, NULL, 5, NULL);
        }
    }

    return ret;
}

static bool light_2color_layer_exit_cb(void *layer)
{
    LV_LOG_USER("");
    bsp_led_rgb_set(0x00, 0x00, 0x00);

    // Stop the timer if it's active
    if (timer_active)
    {
        timer_active = false;
        countdown_counter = 0;
        timer_seconds = 180; // Reset for next use

        // Reset the label to display PWM percentage or a default state
        if (light_set_conf.light_pwm)
        {
            lv_label_set_text_fmt(label_pwm_set, "%d%%", light_set_conf.light_pwm);
        }
        else
        {
            lv_label_set_text(label_pwm_set, "--");
        }
    }
    // Delete the event group
    if (announcement_event_group != NULL)
    {
        vEventGroupDelete(announcement_event_group);
        announcement_event_group = NULL;
    }
    return true;
}

// Handles timer callback and light level call back.
static void light_2color_layer_timer_cb(lv_timer_t *tmr)
{
    uint32_t RGB_color = 0xFF;
    feed_clock_time();

    if (is_time_out(&time_20ms))
    {

        // Timer Countdown Logic
        if (timer_active)
        {
            countdown_counter += 1; // This increments the counter each callback

            // Assuming is_time_out(&time_20ms) is called every 20ms
            // To count 1 second: 1000ms / 20ms = 50 callbacks
            if (countdown_counter >= 50)
            {                          // This is because 50 * 20ms = 1000ms = 1 second
                if (timer_seconds > 0)
                {
                    timer_seconds--;

                    int minutes = timer_seconds / 60;
                    int seconds = timer_seconds % 60;
                    lv_label_set_text_fmt(label_pwm_set, "%02d:%02d", minutes, seconds);

                    if (timer_seconds == 0)
                    {
                        timer_active = false;
                        if (selected_color == LIGHT_CCK_WARM)
                        {
                            // Add task call. AKA a seperate thread init.
                            xEventGroupSetBits(announcement_event_group, ANNOUNCE_TIMER_COMPLETE_BIT);
                            xTaskCreate(LED_FLASH_TASK, "LED_FLASH_TASK", 1024, NULL, 5, &xHandle);
                            
                        }
                        else if (selected_color == LIGHT_CCK_COOL)
                        {
                            // Add task call
                            xEventGroupSetBits(announcement_event_group, ANNOUNCE_TIMER_COMPLETE_BIT);
                            xTaskCreate(LED_FLASH_TASK, "LED_FLASH_TASK", 1024, NULL, 5, &xHandle);
                        }
                        
                    }
                }
            }
        }

        if ((light_set_conf.light_pwm ^ light_xor.light_pwm) || (light_set_conf.light_cck ^ light_xor.light_cck))
        {
            light_xor.light_pwm = light_set_conf.light_pwm;
            light_xor.light_cck = light_set_conf.light_cck;

            if (LIGHT_CCK_COOL == light_xor.light_cck)
            {
                RGB_color = (0xFF * light_xor.light_pwm / 100) << 16 |
                            (0xFF * light_xor.light_pwm / 100) << 8 |
                            (0xFF * light_xor.light_pwm / 100) << 0;
            }
            else
            {
                RGB_color = (0xFF * light_xor.light_pwm / 100) << 16 |
                            (0xFF * light_xor.light_pwm / 100) << 8 |
                            (0x33 * light_xor.light_pwm / 100) << 0;
            }
            bsp_led_rgb_set((RGB_color >> 16) & 0xFF,
                            (RGB_color >> 8) & 0xFF,
                            (RGB_color >> 0) & 0xFF);

            lv_obj_add_flag(img_light_pwm_100, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(img_light_pwm_75, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(img_light_pwm_50, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(img_light_pwm_25, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(img_light_pwm_0, LV_OBJ_FLAG_HIDDEN);

            if (light_set_conf.light_pwm)
            {
                lv_label_set_text_fmt(label_pwm_set, "%d%%", light_set_conf.light_pwm);
            }
            else
            {
                lv_label_set_text(label_pwm_set, "--");
            }

            uint8_t cck_set = (uint8_t)light_xor.light_cck;
            announcement_message_t msg;

            switch (light_xor.light_pwm)
            {
            case 100:
                xEventGroupSetBits(announcement_event_group, ANNOUNCE_PWM_100_BIT);
                lv_obj_clear_flag(img_light_pwm_100, LV_OBJ_FLAG_HIDDEN);
                lv_img_set_src(img_light_pwm_100, light_image.img_pwm_100[cck_set]);
                break;
            case 75:
                xEventGroupSetBits(announcement_event_group, ANNOUNCE_PWM_75_BIT);
                lv_obj_clear_flag(img_light_pwm_75, LV_OBJ_FLAG_HIDDEN);
                lv_img_set_src(img_light_pwm_75, light_image.img_pwm_75[cck_set]);
                break;
            case 50:
                xEventGroupSetBits(announcement_event_group, ANNOUNCE_PWM_50_BIT);
                lv_obj_clear_flag(img_light_pwm_50, LV_OBJ_FLAG_HIDDEN);
                lv_img_set_src(img_light_pwm_50, light_image.img_pwm_50[cck_set]);
                break;
            case 25:
                xEventGroupSetBits(announcement_event_group, ANNOUNCE_PWM_25_BIT);
                lv_obj_clear_flag(img_light_pwm_25, LV_OBJ_FLAG_HIDDEN);
                lv_img_set_src(img_light_pwm_25, light_image.img_pwm_25[cck_set]);
                lv_img_set_src(img_light_bg, light_image.img_bg[cck_set]);
                break;
            case 0:
                xEventGroupSetBits(announcement_event_group, ANNOUNCE_LIGHT_OFF_BIT);
                lv_obj_clear_flag(img_light_pwm_0, LV_OBJ_FLAG_HIDDEN);
                lv_img_set_src(img_light_bg, &light_close_bg);
                break;
            default:
                break;
            }
        }
    }
}
