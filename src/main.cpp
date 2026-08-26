/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 mil-cha
 *
 * TouchFocus - first standalone GUI prototype.
 *
 * The verified BSP/LVGL glue owns display, rotation and touch handling. This
 * file intentionally contains only application UI so communication can later
 * be added as a separate module without disturbing hardware bring-up.
 */

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <BLEDevice.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <time.h>
#include "lvgl.h"

#include "board_p4.h"
#include "lvgl_glue.h"
#include "comm/focuser_controller.h"
#include "comm/network_manager.h"
#include "comm/udp_focuser_transport.h"
#include "comm/ble_focuser_transport.h"
#include "comm/auto_focuser_transport.h"
#include "config/network_config.h"
#include "config/ota_config.h"
#include "mount/onstep_mount.h"

namespace {

lv_obj_t *status_label = nullptr;
lv_obj_t *position_label = nullptr;
lv_obj_t *focuser_gauge_extension = nullptr;
lv_obj_t *focuser_gauge_marker = nullptr;
lv_obj_t *main_screen = nullptr;
lv_obj_t *settings_screen = nullptr;
lv_obj_t *edit_presets_screen = nullptr;
lv_obj_t *focuser_setup_screen = nullptr;
lv_obj_t *screen_settings_screen = nullptr;
lv_obj_t *mount_screen = nullptr;
lv_obj_t *onstep_setup_screen = nullptr;
lv_obj_t *wifi_dropdown = nullptr;
lv_obj_t *wifi_password = nullptr;
lv_obj_t *focuser_ip_field = nullptr;
lv_obj_t *onstep_ip_field = nullptr;
lv_obj_t *wifi_keyboard = nullptr;
lv_obj_t *wifi_status = nullptr;
lv_obj_t *ble_dropdown = nullptr;
lv_obj_t *ble_status = nullptr;
lv_obj_t *mount_status = nullptr;
lv_obj_t *mount_rate_label = nullptr;
lv_obj_t *mount_alt_label = nullptr;
lv_obj_t *mount_az_label = nullptr;
lv_obj_t *mount_local_time_label = nullptr;
lv_obj_t *mount_ntp_time_label = nullptr;
lv_obj_t *mount_sector_canvas = nullptr;
lv_obj_t *onstep_latitude_field = nullptr;
lv_obj_t *onstep_longitude_field = nullptr;
lv_obj_t *onstep_setup_status = nullptr;
lv_obj_t *onstep_setup_keyboard = nullptr;
void *mount_sector_canvas_buffer = nullptr;
touchfocus::MountDirection mount_pad_direction = touchfocus::MountDirection::North;
bool mount_pad_active = false;
uint32_t mount_coordinates_polled_ms = 0;
uint32_t mount_action_status_until_ms = 0;
bool mount_action_status_success = true;
char mount_action_status_text[32] = {};
lv_obj_t *preset_name_editor = nullptr;
lv_obj_t *preset_name_keyboard = nullptr;
lv_obj_t *main_preset_labels[9] = {};
lv_obj_t *preset_edit_labels[9] = {};
char preset_names[9][17] = {};
uint8_t preset_being_edited = 0;
lv_obj_t *setup_fields[7] = {};
lv_obj_t *temperature_comp_switch = nullptr;
lv_obj_t *temperature_readout = nullptr;
lv_obj_t *setup_keyboard = nullptr;
lv_obj_t *setup_calculated = nullptr;
lv_obj_t *setup_result = nullptr;
bool setup_values_shown = false;
uint32_t setup_config_revision_seen = 0;
uint32_t setup_temp_config_revision_seen = 0;
uint32_t last_user_activity_ms = 0;
bool display_awake = true;
uint8_t display_brightness_percent = 100;
lv_obj_t *brightness_value_label = nullptr;
lv_obj_t *wake_high_indicators[7] = {};
size_t wake_high_indicator_count = 0;
bool wake_touch_held = false;
bool idle_sleep_announced = false;
constexpr size_t STATUS_HEADER_COUNT = 7;
lv_obj_t *battery_labels[STATUS_HEADER_COUNT] = {};
size_t battery_label_count = 0;
lv_obj_t *ntp_time_labels[STATUS_HEADER_COUNT] = {};
size_t ntp_time_label_count = 0;
lv_obj_t *ota_button_label = nullptr;
lv_obj_t *ota_status_label = nullptr;
bool ota_enabled = false;
volatile bool ota_update_running = false;
volatile uint8_t ota_progress_percent = 0;
volatile uint8_t ota_error_code = 0;

uint32_t display_off_after_ms = 30000;
constexpr uint32_t IDLE_SLEEP_AFTER_MS = 60000;
constexpr uint8_t WAKE_BUTTON_PIN = 33;
constexpr uint8_t WAKE_ACTIVE_LEVEL = HIGH;
bool wake_button_pressed = false;
bool wake_lock_enabled = true;
bool swap_mount_north_south = true;
bool swap_mount_east_west = true;
int maximum_jog_speed = touchfocus::config::DEFAULT_JOG_SPEED;
touchfocus::OnStepMount onstep_mount;
void stop_motion_on_wake_release();

void restore_display_brightness()
{
    const uint8_t duty = static_cast<uint8_t>(
        (static_cast<uint16_t>(display_brightness_percent) * 255U + 50U) / 100U);
    board_p4_set_brightness(duty);
}

void create_wake_high_indicators()
{
    lv_obj_t *screens[] = {main_screen, settings_screen, edit_presets_screen,
                           focuser_setup_screen, screen_settings_screen,
                           mount_screen, onstep_setup_screen};
    const bool active = digitalRead(WAKE_BUTTON_PIN) == WAKE_ACTIVE_LEVEL;
    for (lv_obj_t *screen : screens) {
        if (!screen || wake_high_indicator_count >= 7) continue;
        lv_obj_t *indicator = lv_obj_create(screen);
        lv_obj_set_size(indicator, 14, 14);
        lv_obj_set_pos(indicator, 3, 3);
        lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(indicator, lv_color_hex(0xFF2020), 0);
        lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(indicator, 0, 0);
        lv_obj_set_style_pad_all(indicator, 0, 0);
        lv_obj_clear_flag(indicator, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(indicator, LV_OBJ_FLAG_SCROLLABLE);
        if (!active) lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);
        wake_high_indicators[wake_high_indicator_count++] = indicator;
    }
}

void refresh_wake_high_indicators(bool active)
{
    for (size_t index = 0; index < wake_high_indicator_count; ++index) {
        if (active)
            lv_obj_clear_flag(wake_high_indicators[index], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(wake_high_indicators[index], LV_OBJ_FLAG_HIDDEN);
    }
}

extern "C" bool touchfocus_touch_activity(bool pressed)
{
    // Do not allow commands or screen changes while the application partition
    // is being replaced. ArduinoOTA will reboot after a successful update.
    if (ota_update_running) return true;
    /* Touch is a deliberate two-hand action: WAKE must remain held. */
    if (wake_lock_enabled &&
        digitalRead(WAKE_BUTTON_PIN) != WAKE_ACTIVE_LEVEL) {
        wake_touch_held = false;
        return true;
    }

    if (!pressed) {
        wake_touch_held = false;
        return false;
    }

    last_user_activity_ms = millis();
    idle_sleep_announced = false;
    if (!display_awake || wake_touch_held) {
        if (!display_awake) {
            restore_display_brightness();
            display_awake = true;
            Serial.println("[Power] Display wake by touch");
        }
        wake_touch_held = true;
        return true;
    }
    return false;
}

void power_idle_poll(lv_timer_t *)
{
    const bool wake_input_high =
        digitalRead(WAKE_BUTTON_PIN) == WAKE_ACTIVE_LEVEL;
    refresh_wake_high_indicators(wake_input_high);
    const bool button_pressed = wake_lock_enabled && wake_input_high;
    if (button_pressed) {
        last_user_activity_ms = millis();
        idle_sleep_announced = false;
        if (!display_awake) {
            restore_display_brightness();
            display_awake = true;
            Serial.println("[Power] Display wake by GPIO33 button");
        }
    }

    if (wake_button_pressed && !button_pressed) stop_motion_on_wake_release();
    wake_button_pressed = button_pressed;

    const uint32_t idle_ms = millis() - last_user_activity_ms;
    if (display_awake && display_off_after_ms > 0 &&
        idle_ms >= display_off_after_ms) {
        onstep_mount.stopAxes();
        board_p4_backlight(false);
        display_awake = false;
        Serial.println("[Power] Display off after 30 seconds");
    }
    if (!idle_sleep_announced && idle_ms >= IDLE_SLEEP_AFTER_MS) {
        /* GT911 INT is not connected in the verified BSP, so the P4 must keep
         * polling touch. This is a safe UI-idle state, not hardware sleep. */
        idle_sleep_announced = true;
        Serial.println("[Power] UI idle; touch polling retained for wake");
    }

    // Leave configuration/editing pages after one minute, but keep Mount
    // Control selected: it is an intentional operating mode, not a temporary
    // settings page. OnStep Setup returns to Mount Control rather than focuser.
    lv_obj_t *active_screen = lv_scr_act();
    if (!ota_update_running && idle_ms >= IDLE_SLEEP_AFTER_MS &&
        main_screen != nullptr && active_screen != main_screen &&
        active_screen != mount_screen) {
        onstep_mount.stopAxes();
        if (wifi_keyboard) lv_obj_add_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
        if (preset_name_keyboard) {
            lv_obj_add_flag(preset_name_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
        if (setup_keyboard) lv_obj_add_flag(setup_keyboard, LV_OBJ_FLAG_HIDDEN);
        if (onstep_setup_keyboard)
            lv_obj_add_flag(onstep_setup_keyboard, LV_OBJ_FLAG_HIDDEN);
        if (active_screen == onstep_setup_screen) {
            lv_scr_load_anim(mount_screen, LV_SCR_LOAD_ANIM_FADE_ON,
                             250, 0, false);
            Serial.println("[Power] Idle timeout; returning to Mount Control");
        } else {
            lv_scr_load_anim(main_screen, LV_SCR_LOAD_ANIM_FADE_ON,
                             250, 0, false);
            Serial.println("[Power] Idle timeout; returning to presets");
        }
    }
}

bool wifi_scan_running = false;
bool wifi_scan_retry_pending = false;
uint8_t wifi_scan_attempt = 0;
uint32_t wifi_scan_retry_at_ms = 0;
bool wifi_connecting = false;
uint32_t wifi_connect_started_ms = 0;
String saved_wifi_ssid;
String saved_wifi_password;
String pending_wifi_ssid;
String pending_wifi_password;
String saved_focuser_ip = "192.168.88.240";
String saved_onstep_ip = "192.168.88.60";
uint32_t discovery_revision_seen = 0;

void load_wifi_credentials()
{
    Preferences preferences;
    if (!preferences.begin("touchfocus", true)) return;
    saved_wifi_ssid = preferences.getString("ssid", "");
    saved_wifi_password = preferences.getString("pass", "");
    saved_focuser_ip = preferences.getString("focuser_ip", "192.168.88.240");
    saved_onstep_ip = preferences.getString("onstep_ip", "192.168.88.60");
    preferences.end();
    if (!saved_wifi_ssid.isEmpty()) {
        Serial.printf("[TouchFocus] Saved Wi-Fi network: %s\n",
                      saved_wifi_ssid.c_str());
    }
}

bool save_wifi_credentials()
{
    if (pending_wifi_ssid.isEmpty() ||
        (pending_wifi_ssid == saved_wifi_ssid &&
         pending_wifi_password == saved_wifi_password)) return true;

    /* NVS writes temporarily disable the flash cache on this P4. Blank the
     * backlight to avoid exposing a possible DPI-underrun flash to the user. */
    board_p4_backlight(false);
    bool stored = false;
    Preferences preferences;
    if (preferences.begin("touchfocus", false)) {
        preferences.putString("ssid", pending_wifi_ssid);
        preferences.putString("pass", pending_wifi_password);
        preferences.end();
    }

    // Verify the NVS write before treating the credentials as saved. This
    // also catches a full or damaged NVS partition without exposing the pass.
    if (preferences.begin("touchfocus", true)) {
        const String verified_ssid = preferences.getString("ssid", "");
        const String verified_password = preferences.getString("pass", "");
        stored = verified_ssid == pending_wifi_ssid &&
                 verified_password == pending_wifi_password;
        preferences.end();
    }
    if (stored) {
        saved_wifi_ssid = pending_wifi_ssid;
        saved_wifi_password = pending_wifi_password;
        Serial.printf("[TouchFocus] Credentials saved for: %s\n",
                      saved_wifi_ssid.c_str());
    } else {
        Serial.println("[TouchFocus] ERROR: Wi-Fi credentials were not saved");
    }
    restore_display_brightness();
    return stored;
}

void load_preset_names()
{
    Preferences preferences;
    if (!preferences.begin("touchfocus", true)) return;
    for (uint8_t index = 0; index < 9; ++index) {
        char key[8];
        snprintf(key, sizeof(key), "pname%u", index + 1);
        const String name = preferences.getString(key, "");
        snprintf(preset_names[index], sizeof(preset_names[index]), "%s", name.c_str());
    }
    preferences.end();
}

void save_preset_name(uint8_t preset)
{
    if (preset < 1 || preset > 9) return;
    board_p4_backlight(false);
    Preferences preferences;
    if (preferences.begin("touchfocus", false)) {
        char key[8];
        snprintf(key, sizeof(key), "pname%u", preset);
        preferences.putString(key, preset_names[preset - 1]);
        preferences.end();
    }
    restore_display_brightness();
}

struct BleDeviceEntry {
    char name[40];
    char address[18];
    uint8_t address_type;
};

BleDeviceEntry ble_devices[20] = {};
volatile int ble_device_count = 0;
volatile bool ble_scan_running = false;
volatile bool ble_scan_ready = false;
volatile bool ble_connecting = false;
volatile bool ble_connect_ready = false;
volatile bool ble_connect_result = false;
char ble_connect_error[48] = {};
bool ble_initialized = false;
BLEClient *ble_client = nullptr;
lv_obj_t *ble_icons[STATUS_HEADER_COUNT] = {};
size_t ble_icon_count = 0;
touchfocus::NetworkManager network;
touchfocus::UdpFocuserTransport udp_focuser_transport(network);
touchfocus::BleFocuserTransport ble_focuser_transport;
touchfocus::AutoFocuserTransport focuser_transport(udp_focuser_transport,
                                                    ble_focuser_transport);
touchfocus::FocuserController focuser(focuser_transport);

void enable_ota(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || ota_enabled) return;
    if (strlen(touchfocus::config::OTA_PASSWORD) < 8) {
        lv_label_set_text(ota_status_label, "Configure a private OTA password first");
        lv_obj_set_style_text_color(ota_status_label, lv_color_hex(0xFF7B72), 0);
        return;
    }
    if (!network.isConnected()) {
        lv_label_set_text(ota_status_label, "Connect Wi-Fi before enabling OTA");
        lv_obj_set_style_text_color(ota_status_label, lv_color_hex(0xFF7B72), 0);
        return;
    }

    focuser.stop();
    ArduinoOTA.setHostname(touchfocus::config::OTA_HOSTNAME);
    ArduinoOTA.setPassword(touchfocus::config::OTA_PASSWORD);
    ArduinoOTA.setRebootOnSuccess(true);
    ArduinoOTA.onStart([]() {
        focuser.stop();
        ota_update_running = true;
        ota_progress_percent = 0;
        ota_error_code = 0;
        restore_display_brightness();
        display_awake = true;
        Serial.println("[OTA] Update started; controls locked");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        ota_progress_percent = total ? static_cast<uint8_t>((progress * 100U) / total) : 0;
    });
    ArduinoOTA.onEnd([]() {
        ota_progress_percent = 100;
        Serial.println("[OTA] Update complete; restarting");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        ota_error_code = static_cast<uint8_t>(error) + 1;
        ota_update_running = false;
        Serial.printf("[OTA] Error %u\n", static_cast<unsigned>(error));
    });
    ArduinoOTA.begin();
    ota_enabled = true;
    lv_label_set_text(ota_button_label, "OTA READY");
    lv_label_set_text_fmt(ota_status_label, "%s.local  |  IP %s",
                          touchfocus::config::OTA_HOSTNAME,
                          network.localIp().c_str());
    lv_obj_set_style_text_color(ota_status_label, lv_color_hex(0x56D364), 0);
    Serial.printf("[OTA] Ready at %s.local (%s)\n",
                  touchfocus::config::OTA_HOSTNAME, network.localIp().c_str());
}

void ota_ui_poll(lv_timer_t *)
{
    if (!ota_status_label) return;
    if (ota_update_running) {
        lv_label_set_text_fmt(ota_status_label, "Updating firmware: %u%%",
                              static_cast<unsigned>(ota_progress_percent));
        lv_obj_set_style_text_color(ota_status_label, lv_color_hex(0xFFB454), 0);
    } else if (ota_error_code != 0) {
        lv_label_set_text_fmt(ota_status_label, "OTA failed (error %u)",
                              static_cast<unsigned>(ota_error_code - 1));
        lv_obj_set_style_text_color(ota_status_label, lv_color_hex(0xFF7B72), 0);
    }
}

void stop_motion_on_wake_release()
{
    if (focuser.motion() == touchfocus::LocalMotion::In ||
        focuser.motion() == touchfocus::LocalMotion::Out) {
        focuser.stop();
        Serial.println("[Safety] WAKE released, motor STOP");
    }
    onstep_mount.stopAxes();
}
char transient_status[32] = {};
uint32_t transient_status_until_ms = 0;
uint8_t long_pressed_preset = 0;

void show_transient_status(const char *text, uint32_t duration_ms = 1000)
{
    snprintf(transient_status, sizeof(transient_status), "%s", text);
    transient_status_until_ms = millis() + duration_ms;
}

void preset_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    const uint8_t preset = static_cast<uint8_t>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));

    if (code == LV_EVENT_PRESSED) {
        long_pressed_preset = 0;
        return;
    }

    char message[24];
    if (code == LV_EVENT_LONG_PRESSED) {
        long_pressed_preset = preset;
        if (focuser.savePreset(preset)) {
            snprintf(message, sizeof(message), "P%u saved", preset);
        } else {
            snprintf(message, sizeof(message), "P%u save failed", preset);
        }
        show_transient_status(message, 1500);
        return;
    }

    if (code != LV_EVENT_CLICKED) return;
    if (long_pressed_preset == preset) {
        long_pressed_preset = 0;
        return;
    }

    if (focuser.goPreset(preset)) {
        snprintf(message, sizeof(message), "Preset P%u", preset);
    } else {
        snprintf(message, sizeof(message), "P%u unsupported", preset);
    }
    show_transient_status(message);
}

void home_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    focuser.home();
    show_transient_status("Homing", 1200);
}

void emergency_stop_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_PRESSED) return;
    focuser.stop();
    show_transient_status("STOPPED", 1500);
    Serial.println("[Safety] STOP button pressed");
}

void motion_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    const bool move_in = reinterpret_cast<uintptr_t>(
                             lv_event_get_user_data(event)) == 0;
    if (code == LV_EVENT_PRESSED) {
        if (move_in) {
            focuser.moveIn(touchfocus::config::DEFAULT_MOVE_STEPS);
        } else {
            focuser.moveOut(touchfocus::config::DEFAULT_MOVE_STEPS);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        focuser.stop();
    }
}

void update_focuser_gauge(const touchfocus::FocuserStatus &state)
{
    if (!focuser_gauge_extension || !focuser_gauge_marker) return;

    constexpr lv_coord_t extension_max_width = 294;
    float ratio = 0.0F;
    if (state.has_position && state.max_travel_mm > 0.0F) {
        ratio = state.position_mm / state.max_travel_mm;
        if (ratio < 0.0F) ratio = 0.0F;
        if (ratio > 1.0F) ratio = 1.0F;
    }

    const lv_coord_t width = static_cast<lv_coord_t>(ratio * extension_max_width);
    lv_obj_set_width(focuser_gauge_extension, width > 0 ? width : 1);
}

void focuser_ui_poll(lv_timer_t *)
{
    focuser.poll();
    const touchfocus::FocuserStatus &state = focuser.status();
    update_focuser_gauge(state);

    if (state.has_config && state.config_revision != setup_config_revision_seen &&
        setup_fields[0] != nullptr) {
        char value[48];
        snprintf(value, sizeof(value), "%d", state.motor_steps);
        lv_textarea_set_text(setup_fields[0], value);
        snprintf(value, sizeof(value), "%d", state.microsteps);
        lv_textarea_set_text(setup_fields[1], value);
        snprintf(value, sizeof(value), "%.6f", static_cast<double>(state.travel_per_rev_mm));
        lv_textarea_set_text(setup_fields[2], value);
        snprintf(value, sizeof(value), "%.2f", static_cast<double>(state.max_travel_mm));
        lv_textarea_set_text(setup_fields[3], value);
        snprintf(value, sizeof(value), "Calculated: %.3f steps/mm",
                 static_cast<double>(state.steps_per_mm));
        lv_label_set_text(setup_calculated, value);
        lv_label_set_text(setup_result, "Configuration loaded");
        setup_values_shown = true;
        setup_config_revision_seen = state.config_revision;
    }

    if (state.has_temp_config &&
        state.temp_config_revision != setup_temp_config_revision_seen &&
        setup_fields[5] != nullptr && temperature_comp_switch != nullptr) {
        char value[32];
        snprintf(value, sizeof(value), "%.3f",
                 static_cast<double>(state.temp_coefficient));
        lv_textarea_set_text(setup_fields[5], value);
        snprintf(value, sizeof(value), "%.2f",
                 static_cast<double>(state.temp_hysteresis_c));
        lv_textarea_set_text(setup_fields[6], value);
        if (state.temp_comp_enabled)
            lv_obj_add_state(temperature_comp_switch, LV_STATE_CHECKED);
        else
            lv_obj_clear_state(temperature_comp_switch, LV_STATE_CHECKED);
        setup_temp_config_revision_seen = state.temp_config_revision;
    }

    if (temperature_readout != nullptr) {
        static int shown_tenths = INT32_MIN;
        static bool shown_temp_valid = false;
        static bool shown_temp_active = false;
        const int tenths = static_cast<int>(state.temperature_c * 10.0F);
        if (tenths != shown_tenths || state.has_temperature != shown_temp_valid ||
            state.temp_comp_active != shown_temp_active) {
            char text[64];
            if (state.has_temperature) {
                snprintf(text, sizeof(text), "Sensor: %.1f C   Compensation: %s",
                         static_cast<double>(state.temperature_c),
                         state.temp_comp_active ? "ACTIVE" : "IDLE");
            } else {
                snprintf(text, sizeof(text), "Sensor: not detected");
            }
            lv_label_set_text(temperature_readout, text);
            shown_tenths = tenths;
            shown_temp_valid = state.has_temperature;
            shown_temp_active = state.temp_comp_active;
        }
    }

    static int32_t shown_steps = INT32_MIN;
    static bool shown_connected = false;
    if (state.has_position &&
        (state.position_steps != shown_steps || state.connected != shown_connected)) {
        char position_text[32];
        snprintf(position_text, sizeof(position_text), "Position: %.2f mm",
                 static_cast<double>(state.position_mm));
        lv_label_set_text(position_label, position_text);
        shown_steps = state.position_steps;
    }
    shown_connected = state.connected;

    if (!state.connected) {
        lv_label_set_text(status_label, "DISCONNECTED");
    } else if (focuser.motion() == touchfocus::LocalMotion::In) {
        lv_label_set_text(status_label,
                          focuser.isFastMotion() ? "MOVING IN FAST" : "MOVING IN");
    } else if (focuser.motion() == touchfocus::LocalMotion::Out) {
        lv_label_set_text(status_label,
                          focuser.isFastMotion() ? "MOVING OUT FAST" : "MOVING OUT");
    } else if (focuser.motion() == touchfocus::LocalMotion::Homing) {
        lv_label_set_text(status_label, "HOMING");
    } else if (static_cast<int32_t>(transient_status_until_ms - millis()) > 0) {
        lv_label_set_text(status_label, transient_status);
    } else {
        lv_label_set_text(status_label, "CONNECTED");
    }
}
bool night_mode = false;
lv_color_filter_dsc_t night_color_filter;

void load_screen_settings()
{
    Preferences preferences;
    if (!preferences.begin("touchfocus", true)) return;
    display_off_after_ms = preferences.getUInt("disp_timeout", 30000);
    wake_lock_enabled = preferences.getBool("wake_lock", true);
    night_mode = preferences.getBool("night", false);
    swap_mount_north_south = preferences.getBool("swap_ns", true);
    swap_mount_east_west = preferences.getBool("swap_ew", true);
    maximum_jog_speed = preferences.getInt(
        "jog_speed", touchfocus::config::DEFAULT_JOG_SPEED);
    display_brightness_percent = constrain(
        preferences.getUChar("brightness", 100), 10, 100);
    maximum_jog_speed = constrain(maximum_jog_speed,
                                  touchfocus::config::MIN_JOG_SPEED,
                                  touchfocus::config::MAX_JOG_SPEED);
    preferences.end();
}

void save_screen_settings()
{
    board_p4_backlight(false);
    Preferences preferences;
    if (preferences.begin("touchfocus", false)) {
        preferences.putUInt("disp_timeout", display_off_after_ms);
        preferences.putBool("wake_lock", wake_lock_enabled);
        preferences.putBool("night", night_mode);
        preferences.putBool("swap_ns", swap_mount_north_south);
        preferences.putBool("swap_ew", swap_mount_east_west);
        preferences.putInt("jog_speed", maximum_jog_speed);
        preferences.putUChar("brightness", display_brightness_percent);
        preferences.end();
    }
    restore_display_brightness();
    display_awake = true;
    last_user_activity_ms = millis();
}

lv_color_t night_filter_cb(const lv_color_filter_dsc_t *, lv_color_t color,
                           lv_opa_t)
{
    const uint8_t brightness = lv_color_brightness(color);
    return lv_color_make(brightness, 0, 0);
}

void apply_display_mode()
{
    const lv_opa_t opacity = night_mode ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_obj_t *roots[] = {main_screen, settings_screen, edit_presets_screen,
                         focuser_setup_screen, screen_settings_screen, mount_screen,
                         onstep_setup_screen,
                         lv_layer_top(), lv_layer_sys()};
    for (lv_obj_t *root : roots) {
        if (root == nullptr) continue;
        lv_obj_set_style_color_filter_dsc(root, &night_color_filter, 0);
        lv_obj_set_style_color_filter_opa(root, opacity, 0);
        lv_obj_invalidate(root);
    }
}

void refresh_preset_name_labels(uint8_t preset)
{
    if (preset < 1 || preset > 9) return;
    const uint8_t index = preset - 1;
    char text[48];

    if (main_preset_labels[index] != nullptr) {
        if (preset_names[index][0] == '\0') {
            snprintf(text, sizeof(text), "P%u", preset);
        } else {
            snprintf(text, sizeof(text), "P%u\n%s", preset, preset_names[index]);
        }
        lv_label_set_text(main_preset_labels[index], text);
        lv_obj_set_style_text_align(main_preset_labels[index], LV_TEXT_ALIGN_CENTER, 0);
    }

    if (preset_edit_labels[index] != nullptr) {
        snprintf(text, sizeof(text), "P%u    %s", preset,
                 preset_names[index][0] == '\0' ? "(not named)" : preset_names[index]);
        lv_label_set_text(preset_edit_labels[index], text);
    }
}

void edit_preset_name(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    preset_being_edited = static_cast<uint8_t>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    if (preset_being_edited < 1 || preset_being_edited > 9) return;

    lv_textarea_set_text(preset_name_editor, preset_names[preset_being_edited - 1]);
    char placeholder[24];
    snprintf(placeholder, sizeof(placeholder), "Name for P%u", preset_being_edited);
    lv_textarea_set_placeholder_text(preset_name_editor, placeholder);
    lv_obj_clear_flag(preset_name_editor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(preset_name_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(preset_name_keyboard, preset_name_editor);
    lv_obj_add_state(preset_name_editor, LV_STATE_FOCUSED);
    lv_obj_move_foreground(preset_name_editor);
    lv_obj_move_foreground(preset_name_keyboard);
}

void finish_preset_name_edit(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL) return;

    if (code == LV_EVENT_READY && preset_being_edited >= 1 && preset_being_edited <= 9) {
        snprintf(preset_names[preset_being_edited - 1],
                 sizeof(preset_names[preset_being_edited - 1]), "%s",
                 lv_textarea_get_text(preset_name_editor));
        save_preset_name(preset_being_edited);
        refresh_preset_name_labels(preset_being_edited);
    }

    lv_keyboard_set_textarea(preset_name_keyboard, nullptr);
    lv_obj_add_flag(preset_name_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(preset_name_editor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_state(preset_name_editor, LV_STATE_FOCUSED);
    preset_being_edited = 0;
}

void show_setup_keyboard(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_FOCUSED) return;
    lv_keyboard_set_textarea(setup_keyboard, lv_event_get_target(event));
    lv_keyboard_set_mode(setup_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_clear_flag(setup_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(setup_keyboard);
}

void hide_setup_keyboard(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL) return;
    lv_keyboard_set_textarea(setup_keyboard, nullptr);
    lv_obj_add_flag(setup_keyboard, LV_OBJ_FLAG_HIDDEN);
}

void save_focuser_setup(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const int motor = atoi(lv_textarea_get_text(setup_fields[0]));
    const int microsteps = atoi(lv_textarea_get_text(setup_fields[1]));
    const float travel = atof(lv_textarea_get_text(setup_fields[2]));
    const float maximum = atof(lv_textarea_get_text(setup_fields[3]));
    const int jog_speed = atoi(lv_textarea_get_text(setup_fields[4]));
    const float temp_coefficient = atof(lv_textarea_get_text(setup_fields[5]));
    const float temp_hysteresis = atof(lv_textarea_get_text(setup_fields[6]));
    const bool temp_enabled = lv_obj_has_state(temperature_comp_switch,
                                               LV_STATE_CHECKED);
    if (motor < 1 || microsteps < 1 || travel <= 0.0F || maximum <= 0.0F ||
        jog_speed < touchfocus::config::MIN_JOG_SPEED ||
        jog_speed > touchfocus::config::MAX_JOG_SPEED ||
        temp_coefficient < -100000.0F || temp_coefficient > 100000.0F ||
        temp_hysteresis < 0.05F || temp_hysteresis > 10.0F) {
        lv_label_set_text(setup_result, "Invalid values - not saved");
        return;
    }
    maximum_jog_speed = jog_speed;
    focuser.setJogSpeed(maximum_jog_speed);
    save_screen_settings();
    char calculated[48];
    snprintf(calculated, sizeof(calculated), "Calculated: %.3f steps/mm",
             static_cast<double>(motor * microsteps / travel));
    lv_label_set_text(setup_calculated, calculated);
    const bool mechanical_sent = focuser.saveConfig(motor, microsteps, travel, maximum);
    const bool temperature_sent = focuser.saveTemperatureConfig(
        temp_enabled, temp_coefficient, temp_hysteresis);
    if (mechanical_sent && temperature_sent) {
        lv_label_set_text(setup_result, "Saving - waiting for daemon");
    } else {
        lv_label_set_text(setup_result, "DISCONNECTED - not saved");
    }
}

void select_display_mode(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    night_mode = reinterpret_cast<intptr_t>(lv_event_get_user_data(event)) != 0;
    apply_display_mode();
    save_screen_settings();
    Serial.printf("[TouchFocus] Display mode: %s\n",
                  night_mode ? "NIGHT" : "COLOR");
}

void display_timeout_changed(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    static const uint32_t values[] = {15000, 30000, 60000, 120000, 300000, 0};
    const uint16_t selected = lv_dropdown_get_selected(lv_event_get_target(event));
    if (selected < sizeof(values) / sizeof(values[0])) {
        display_off_after_ms = values[selected];
        last_user_activity_ms = millis();
        save_screen_settings();
    }
}

void wake_lock_changed(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    wake_lock_enabled = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    wake_touch_held = false;
    wake_button_pressed = false;
    save_screen_settings();
}

void brightness_changed(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED) return;
    display_brightness_percent = static_cast<uint8_t>(
        lv_slider_get_value(lv_event_get_target(event)));
    if (brightness_value_label)
        lv_label_set_text_fmt(brightness_value_label, "%u%%",
                              display_brightness_percent);
    restore_display_brightness();
    last_user_activity_ms = millis();
    if (code == LV_EVENT_RELEASED) save_screen_settings();
}

void mount_direction_swap_changed(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    const bool north_south = reinterpret_cast<uintptr_t>(
                                 lv_event_get_user_data(event)) == 0;
    const bool enabled = lv_obj_has_state(lv_event_get_target(event),
                                          LV_STATE_CHECKED);
    if (north_south) swap_mount_north_south = enabled;
    else swap_mount_east_west = enabled;
    onstep_mount.setDirectionSwap(swap_mount_north_south,
                                  swap_mount_east_west);
    save_screen_settings();
}

struct WifiHeader {
    lv_obj_t *icon = nullptr;
    lv_obj_t *ssid = nullptr;
    lv_obj_t *ip = nullptr;
    lv_obj_t *bars[3] = {nullptr, nullptr, nullptr};
};

WifiHeader wifi_headers[STATUS_HEADER_COUNT];
size_t wifi_header_count = 0;

void create_wifi_header(lv_obj_t *parent)
{
    if (wifi_header_count >= STATUS_HEADER_COUNT) return;
    WifiHeader &header = wifi_headers[wifi_header_count++];

    header.icon = lv_label_create(parent);
    lv_label_set_text(header.icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(header.icon, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(header.icon, 408, 18);

    header.ssid = lv_label_create(parent);
    lv_label_set_text(header.ssid, "Offline");
    lv_obj_set_width(header.ssid, 160);
    lv_label_set_long_mode(header.ssid, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(header.ssid, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(header.ssid, &lv_font_montserrat_14, 0);
    lv_obj_align(header.ssid, LV_ALIGN_TOP_MID, 0, 8);

    header.ip = lv_label_create(parent);
    lv_label_set_text(header.ip, "No IP");
    lv_obj_set_width(header.ip, 160);
    lv_label_set_long_mode(header.ip, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(header.ip, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(header.ip, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(header.ip, lv_color_hex(0x8EA5B8), 0);
    lv_obj_align(header.ip, LV_ALIGN_TOP_MID, 0, 34);

    static const lv_coord_t heights[3] = {8, 14, 20};
    for (int index = 0; index < 3; ++index) {
        header.bars[index] = lv_obj_create(parent);
        lv_obj_set_size(header.bars[index], 8, heights[index]);
        lv_obj_set_pos(header.bars[index], 365 + index * 12,
                       40 - heights[index]);
        lv_obj_set_style_radius(header.bars[index], 2, 0);
        lv_obj_set_style_border_width(header.bars[index], 0, 0);
        lv_obj_set_style_pad_all(header.bars[index], 0, 0);
    }
}

void create_ble_header_icon(lv_obj_t *parent)
{
    if (ble_icon_count >= STATUS_HEADER_COUNT) return;
    lv_obj_t *icon = lv_label_create(parent);
    lv_label_set_text(icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xFF7B72), 0);
    lv_obj_set_pos(icon, 438, 18);
    ble_icons[ble_icon_count++] = icon;
}

void create_battery_header(lv_obj_t *parent)
{
    if (battery_label_count >= STATUS_HEADER_COUNT) return;
    lv_obj_t *label = lv_label_create(parent);
    /* This board exposes no verified VBAT ADC/IP5306 telemetry to the P4. */
    lv_label_set_text(label, LV_SYMBOL_BATTERY_EMPTY " --");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x8EA5B8), 0);
    lv_obj_set_pos(label, 312, 20);
    battery_labels[battery_label_count++] = label;
}

void create_ntp_time_header(lv_obj_t *parent)
{
    if (ntp_time_label_count >= STATUS_HEADER_COUNT) return;
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "--:--");
    lv_obj_set_width(label, 58);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x8EA5B8), 0);
    lv_obj_set_pos(label, 305, 41);
    ntp_time_labels[ntp_time_label_count++] = label;
}

void refresh_ntp_time_headers()
{
    struct tm local_time {};
    const bool valid = network.isConnected() &&
                       getLocalTime(&local_time, 0) &&
                       local_time.tm_year >= (2024 - 1900);
    char text[6] = "--:--";
    if (valid) strftime(text, sizeof(text), "%H:%M", &local_time);
    for (size_t index = 0; index < ntp_time_label_count; ++index)
        lv_label_set_text(ntp_time_labels[index], text);
    if (mount_ntp_time_label) lv_label_set_text(mount_ntp_time_label, text);
}

void refresh_ble_headers()
{
    const bool connected = ble_client != nullptr && ble_client->isConnected();
    const lv_color_t color = connected ? lv_color_hex(0x56D364)
                                       : (ble_connecting || ble_scan_running
                                              ? lv_color_hex(0xFFB454)
                                              : lv_color_hex(0xFF7B72));
    for (size_t index = 0; index < ble_icon_count; ++index) {
        lv_obj_set_style_text_color(ble_icons[index], color, 0);
    }
}

void ble_scan_task(void *)
{
    if (!ble_initialized) {
        BLEDevice::init("TouchFocus");
        ble_initialized = true;
    }

    BLEScan *scan = BLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(80);
    BLEScanResults *results = scan->start(5, false);

    int count = results == nullptr ? 0 : results->getCount();
    if (count > 20) count = 20;
    for (int index = 0; index < count; ++index) {
        BLEAdvertisedDevice device = results->getDevice(index);
        String address = device.getAddress().toString();
        String name = device.haveName() ? device.getName() : address;
        snprintf(ble_devices[index].name, sizeof(ble_devices[index].name),
                 "%s", name.c_str());
        snprintf(ble_devices[index].address, sizeof(ble_devices[index].address),
                 "%s", address.c_str());
        ble_devices[index].address_type = device.getAddressType();
    }

    scan->clearResults();
    ble_device_count = count;
    ble_scan_running = false;
    ble_scan_ready = true;
    vTaskDelete(nullptr);
}

void start_ble_scan(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || ble_scan_running ||
        ble_connecting) return;
    ble_scan_running = true;
    ble_scan_ready = false;
    lv_dropdown_set_options(ble_dropdown, "Scanning...");
    lv_label_set_text(ble_status, "Scanning BLE devices for 5 seconds...");
    lv_obj_set_style_text_color(ble_status, lv_color_hex(0xFFB454), 0);
    xTaskCreate(ble_scan_task, "ble_scan", 8192, nullptr, 1, nullptr);
}

void ble_connect_task(void *parameter)
{
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(parameter));
    if (!ble_initialized) {
        BLEDevice::init("TouchFocus");
        ble_initialized = true;
    }
    if (ble_client == nullptr) ble_client = BLEDevice::createClient();
    if (ble_client->isConnected()) ble_client->disconnect();

    BLEAddress address(String(ble_devices[index].address),
                       ble_devices[index].address_type);
    ble_connect_result = ble_client->connect(address,
                                              ble_devices[index].address_type,
                                              10000);
    if (!ble_connect_result) {
        snprintf(ble_connect_error, sizeof(ble_connect_error), "BLE link failed");
    }
    if (ble_connect_result) {
        ble_connect_result = ble_focuser_transport.attach(ble_client);
        if (!ble_connect_result) {
            snprintf(ble_connect_error, sizeof(ble_connect_error), "%s",
                     ble_focuser_transport.lastError());
            Serial.printf("[BLE] %s\n", ble_connect_error);
            ble_client->disconnect();
        }
    }
    ble_connecting = false;
    ble_connect_ready = true;
    vTaskDelete(nullptr);
}

void connect_ble(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || ble_connecting ||
        ble_scan_running) return;
    const int count = ble_device_count;
    if (count <= 0) {
        lv_label_set_text(ble_status, "Scan and select a BLE device first");
        lv_obj_set_style_text_color(ble_status, lv_color_hex(0xFFB454), 0);
        return;
    }

    const int selected = lv_dropdown_get_selected(ble_dropdown);
    if (selected < 0 || selected >= count) return;
    ble_connecting = true;
    ble_connect_ready = false;
    ble_connect_error[0] = '\0';
    lv_label_set_text(ble_status, "Connecting...");
    lv_obj_set_style_text_color(ble_status, lv_color_hex(0xFFB454), 0);
    xTaskCreate(ble_connect_task, "ble_connect", 8192,
                reinterpret_cast<void *>(static_cast<intptr_t>(selected)), 1, nullptr);
}

void refresh_wifi_headers()
{
    const bool connected = network.isConnected();
    const char *name = "Offline";
    const char *ip_address = "No IP";
    lv_color_t icon_color = lv_color_hex(0xFF7B72);
    int active_bars = 0;

    String connected_ssid;
    String connected_ip;
    if (connected) {
        connected_ssid = network.ssid();
        connected_ip = network.localIp();
        name = connected_ssid.c_str();
        ip_address = connected_ip.c_str();
        icon_color = lv_color_hex(0x56D364);
        const int32_t rssi = network.rssi();
        active_bars = rssi >= -60 ? 3 : (rssi >= -75 ? 2 : 1);
    } else if (wifi_connecting) {
        name = "Connecting";
        icon_color = lv_color_hex(0xFFB454);
    } else if (wifi_scan_running) {
        name = "Scanning";
        icon_color = lv_color_hex(0xFFB454);
    }

    for (size_t widget = 0; widget < wifi_header_count; ++widget) {
        WifiHeader &header = wifi_headers[widget];
        lv_label_set_text(header.ssid, name);
        lv_label_set_text(header.ip, ip_address);
        lv_obj_set_style_text_color(header.icon, icon_color, 0);
        for (int bar = 0; bar < 3; ++bar) {
            const lv_color_t color = bar < active_bars
                                         ? lv_color_hex(0x56D364)
                                         : lv_color_hex(0x3B4A56);
            lv_obj_set_style_bg_color(header.bars[bar], color, 0);
        }
    }
}

void set_wifi_status(const char *text, lv_color_t color = lv_color_hex(0x8EA5B8))
{
    if (wifi_status == nullptr) return;
    lv_label_set_text(wifi_status, text);
    lv_obj_set_style_text_color(wifi_status, color, 0);
}

bool apply_and_save_focuser_ip()
{
    if (!focuser_ip_field) return false;
    IPAddress address;
    if (!address.fromString(lv_textarea_get_text(focuser_ip_field))) {
        set_wifi_status("Invalid Raspberry Pi IP address", lv_color_hex(0xFF7B72));
        return false;
    }

    udp_focuser_transport.setHost(address);
    saved_focuser_ip = address.toString();
    Preferences preferences;
    if (preferences.begin("touchfocus", false)) {
        preferences.putString("focuser_ip", saved_focuser_ip);
        preferences.end();
    }
    lv_textarea_set_text(focuser_ip_field, saved_focuser_ip.c_str());
    Serial.printf("[TouchFocus] Raspberry Pi IP saved: %s\n",
                  saved_focuser_ip.c_str());
    return true;
}

bool apply_and_save_onstep_ip()
{
    if (!onstep_ip_field) return false;
    IPAddress address;
    if (!address.fromString(lv_textarea_get_text(onstep_ip_field))) {
        set_wifi_status("Invalid OnStep IP address", lv_color_hex(0xFF7B72));
        return false;
    }

    onstep_mount.stopAxes();
    onstep_mount.setHost(address);
    saved_onstep_ip = address.toString();
    Preferences preferences;
    if (preferences.begin("touchfocus", false)) {
        preferences.putString("onstep_ip", saved_onstep_ip);
        preferences.end();
    }
    lv_textarea_set_text(onstep_ip_field, saved_onstep_ip.c_str());
    Serial.printf("[TouchFocus] OnStep IP saved: %s\n", saved_onstep_ip.c_str());
    return true;
}

void save_onstep_ip(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) apply_and_save_onstep_ip();
}

void start_wifi_scan(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || wifi_scan_running) return;

    lv_dropdown_set_options(wifi_dropdown, "Scanning...");
    set_wifi_status("Scanning Wi-Fi networks...");

    wifi_scan_attempt = 1;
    const int16_t result = network.startScan(true);
    wifi_scan_running = true;
    wifi_scan_retry_pending =
        result != touchfocus::NetworkManager::SCAN_RUNNING;
    if (wifi_scan_retry_pending) {
        wifi_scan_retry_at_ms = millis() + 500;
        Serial.printf("[Network] Wi-Fi scan start attempt 1 returned %d; retrying\n",
                      result);
    }
}

void find_focuser(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (!network.isConnected()) {
        set_wifi_status("Connect Wi-Fi before discovery", lv_color_hex(0xFFB454));
        return;
    }
    if (focuser.discoveryRunning()) return;
    discovery_revision_seen = focuser.discoveryRevision();
    if (focuser.findFocuser()) {
        set_wifi_status("Searching for focuserd...", lv_color_hex(0xFFB454));
    } else {
        set_wifi_status("Could not start discovery", lv_color_hex(0xFF7B72));
    }
}

void connect_wifi(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || wifi_connecting) return;
    if (!apply_and_save_focuser_ip()) return;

    char ssid[33] = {};
    lv_dropdown_get_selected_str(wifi_dropdown, ssid, sizeof(ssid));
    if (ssid[0] == '\0' || strcmp(ssid, "Scanning...") == 0 ||
        strcmp(ssid, "No networks found") == 0) {
        set_wifi_status("Select a Wi-Fi network first", lv_color_hex(0xFFB454));
        return;
    }

    const char *password = lv_textarea_get_text(wifi_password);
    pending_wifi_ssid = ssid;
    pending_wifi_password = password;
    network.connect(ssid, password);
    wifi_connecting = true;
    wifi_connect_started_ms = millis();
    set_wifi_status("Connecting...");
    Serial.printf("[TouchFocus] Connecting to Wi-Fi: %s\n", ssid);
}

void wifi_network_selected(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    char ssid[33] = {};
    lv_dropdown_get_selected_str(wifi_dropdown, ssid, sizeof(ssid));
    if (saved_wifi_ssid == ssid) {
        lv_textarea_set_text(wifi_password, saved_wifi_password.c_str());
    } else {
        lv_textarea_set_text(wifi_password, "");
    }
}

void show_wifi_keyboard(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_FOCUSED) return;
    lv_keyboard_set_textarea(wifi_keyboard, lv_event_get_target(event));
    lv_obj_clear_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(wifi_keyboard);
}

void hide_wifi_keyboard(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL) return;
    lv_obj_t *edited = lv_keyboard_get_textarea(wifi_keyboard);
    if (code == LV_EVENT_READY && edited == focuser_ip_field) {
        apply_and_save_focuser_ip();
    } else if (code == LV_EVENT_READY && edited == onstep_ip_field) {
        apply_and_save_onstep_ip();
    }
    lv_keyboard_set_textarea(wifi_keyboard, nullptr);
    lv_obj_add_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_state(wifi_password, LV_STATE_FOCUSED);
    if (focuser_ip_field) lv_obj_clear_state(focuser_ip_field, LV_STATE_FOCUSED);
    if (onstep_ip_field) lv_obj_clear_state(onstep_ip_field, LV_STATE_FOCUSED);
}

void wifi_poll(lv_timer_t *)
{
    const uint32_t discovery_revision = focuser.discoveryRevision();
    if (discovery_revision != discovery_revision_seen) {
        discovery_revision_seen = discovery_revision;
        if (focuser.discoveryFound()) {
            const String found_ip = focuser.discoveredHost().toString();
            lv_textarea_set_text(focuser_ip_field, found_ip.c_str());
            apply_and_save_focuser_ip();
            String message = "Focuser found: " + found_ip;
            set_wifi_status(message.c_str(), lv_color_hex(0x56D364));
        } else {
            set_wifi_status("No focuserd found", lv_color_hex(0xFF7B72));
        }
    }

    if (wifi_scan_running) {
        int16_t count = touchfocus::NetworkManager::SCAN_RUNNING;
        if (wifi_scan_retry_pending) {
            if (static_cast<int32_t>(wifi_scan_retry_at_ms - millis()) > 0) {
                count = touchfocus::NetworkManager::SCAN_RUNNING;
            } else {
                ++wifi_scan_attempt;
                const int16_t result = network.startScan(true);
                Serial.printf("[Network] Wi-Fi scan start attempt %u returned %d\n",
                              wifi_scan_attempt, result);
                if (result == touchfocus::NetworkManager::SCAN_RUNNING) {
                    wifi_scan_retry_pending = false;
                    count = touchfocus::NetworkManager::SCAN_RUNNING;
                } else if (wifi_scan_attempt < 3) {
                    wifi_scan_retry_at_ms = millis() + 750;
                    count = touchfocus::NetworkManager::SCAN_RUNNING;
                } else {
                    count = touchfocus::NetworkManager::SCAN_FAILED;
                }
            }
        } else {
            count = network.scanComplete();
        }
        if (count >= 0) {
            String options;
            int saved_index = -1;
            for (int16_t index = 0; index < count; ++index) {
                String ssid = network.scanSsid(index);
                ssid.replace("\n", " ");
                ssid.replace("\r", " ");
                if (ssid.isEmpty()) ssid = "<hidden network>";
                if (!options.isEmpty()) options += '\n';
                options += ssid;
                if (ssid == saved_wifi_ssid && saved_index < 0) saved_index = index;
            }

            lv_dropdown_set_options(wifi_dropdown,
                                    count > 0 ? options.c_str() : "No networks found");
            if (saved_index >= 0) {
                lv_dropdown_set_selected(wifi_dropdown, saved_index);
                lv_textarea_set_text(wifi_password, saved_wifi_password.c_str());
            }
            char message[48];
            snprintf(message, sizeof(message), "%d network%s found", count,
                     count == 1 ? "" : "s");
            set_wifi_status(message, lv_color_hex(0x69B7FF));
            Serial.printf("[TouchFocus] Wi-Fi scan: %d networks\n", count);
            network.clearScanResults();
            wifi_scan_running = false;
            wifi_scan_retry_pending = false;
        } else if (count == touchfocus::NetworkManager::SCAN_FAILED) {
            if (wifi_scan_attempt < 3) {
                wifi_scan_retry_pending = true;
                wifi_scan_retry_at_ms = millis() + 750;
                set_wifi_status("Wi-Fi busy, retrying scan...",
                                lv_color_hex(0xFFB454));
            } else {
                lv_dropdown_set_options(
                    wifi_dropdown, saved_wifi_ssid.isEmpty()
                                       ? "Press Scan Wi-Fi"
                                       : saved_wifi_ssid.c_str());
                set_wifi_status("Wi-Fi scan failed after 3 attempts",
                                lv_color_hex(0xFF7B72));
                Serial.println("[Network] Wi-Fi scan failed after 3 attempts");
                wifi_scan_running = false;
                wifi_scan_retry_pending = false;
            }
        }
    }

    if (wifi_connecting) {
        // WiFi.begin() may briefly leave the previous connection visible.
        // Only accept success when the radio reports the requested SSID.
        if (network.isConnected() && network.ssid() == pending_wifi_ssid) {
            String message = "Connected: " + network.ssid();
            Serial.printf("[TouchFocus] Wi-Fi connected, IP: %s\n",
                          network.localIp().c_str());
            if (save_wifi_credentials()) {
                set_wifi_status(message.c_str(), lv_color_hex(0x56D364));
            } else {
                set_wifi_status("Connected, but credentials not saved",
                                lv_color_hex(0xFF7B72));
            }
            wifi_connecting = false;
        } else if (millis() - wifi_connect_started_ms > 20000) {
            set_wifi_status("Connection failed or timed out", lv_color_hex(0xFF7B72));
            network.disconnect();
            wifi_connecting = false;
        }
    }

    refresh_wifi_headers();
    refresh_ntp_time_headers();

    if (ble_scan_ready) {
        ble_scan_ready = false;
        String options;
        const int count = ble_device_count;
        for (int index = 0; index < count; ++index) {
            if (!options.isEmpty()) options += '\n';
            options += ble_devices[index].name;
        }
        lv_dropdown_set_options(ble_dropdown,
                                count > 0 ? options.c_str() : "No BLE devices found");
        char message[48];
        snprintf(message, sizeof(message), "%d BLE device%s found", count,
                 count == 1 ? "" : "s");
        lv_label_set_text(ble_status, message);
        lv_obj_set_style_text_color(ble_status, lv_color_hex(0x69B7FF), 0);
    }

    if (ble_connect_ready) {
        ble_connect_ready = false;
        if (ble_connect_result) {
            const int selected = lv_dropdown_get_selected(ble_dropdown);
            String message = "Connected: " + String(ble_devices[selected].name);
            lv_label_set_text(ble_status, message.c_str());
            lv_obj_set_style_text_color(ble_status, lv_color_hex(0x56D364), 0);
        } else {
            lv_label_set_text(ble_status, ble_connect_error[0] != '\0'
                                           ? ble_connect_error
                                           : "BLE connection failed");
            lv_obj_set_style_text_color(ble_status, lv_color_hex(0xFF7B72), 0);
        }
    }
    refresh_ble_headers();
}

void refresh_onstep_setup_location()
{
    if (!onstep_latitude_field || !onstep_longitude_field) return;
    double latitude = 0.0;
    double longitude = 0.0;
    if (onstep_mount.getLocation(latitude, longitude)) {
        char value[20];
        snprintf(value, sizeof(value), "%.6f", latitude);
        lv_textarea_set_text(onstep_latitude_field, value);
        snprintf(value, sizeof(value), "%.6f", longitude);
        lv_textarea_set_text(onstep_longitude_field, value);
        if (onstep_setup_status) {
            lv_label_set_text(onstep_setup_status, "Location loaded from OnStep");
            lv_obj_set_style_text_color(onstep_setup_status,
                                        lv_color_hex(0x56D364), 0);
        }
    } else if (onstep_setup_status) {
        lv_label_set_text(onstep_setup_status, "Could not read OnStep location");
        lv_obj_set_style_text_color(onstep_setup_status,
                                    lv_color_hex(0xFF7B72), 0);
    }
}

void navigate_on_gesture(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_GESTURE) return;

    const lv_dir_t direction = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (direction != LV_DIR_LEFT && direction != LV_DIR_RIGHT) return;

    lv_obj_t *source = lv_event_get_current_target(event);

    // Left swipe advances through the ring. Focuser and Mount Control are
    // deliberately adjacent; swiping past either end wraps around.
    lv_obj_t *screens[] = {main_screen, mount_screen, onstep_setup_screen,
                           settings_screen, screen_settings_screen,
                           edit_presets_screen, focuser_setup_screen};
    const char *names[] = {"Focuser", "Mount control", "OnStep setup",
                           "Connection", "Screen settings", "Edit presets",
                           "Focuser setup"};
    constexpr size_t count = sizeof(screens) / sizeof(screens[0]);

    size_t current = count;
    for (size_t index = 0; index < count; ++index) {
        if (screens[index] == source) {
            current = index;
            break;
        }
    }
    if (current == count) return;

    const size_t target_index = direction == LV_DIR_LEFT
                                    ? (current + 1) % count
                                    : (current + count - 1) % count;
    lv_obj_t *target = screens[target_index];

    if (source == mount_screen) onstep_mount.stopAxes();
    if (source == onstep_setup_screen && onstep_setup_keyboard)
        lv_obj_add_flag(onstep_setup_keyboard, LV_OBJ_FLAG_HIDDEN);
    if (target == onstep_setup_screen) refresh_onstep_setup_location();
    if (target == focuser_setup_screen) {
        focuser.requestConfig();
        focuser.requestTemperatureConfig();
    }

    const lv_scr_load_anim_t animation = direction == LV_DIR_LEFT
                                             ? LV_SCR_LOAD_ANIM_MOVE_LEFT
                                             : LV_SCR_LOAD_ANIM_MOVE_RIGHT;
    lv_scr_load_anim(target, animation, 250, 0, false);
    Serial.printf("[TouchFocus] %s\n", names[target_index]);
}

void set_status(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || status_label == nullptr) {
        return;
    }

    const char *message = static_cast<const char *>(lv_event_get_user_data(event));
    lv_label_set_text(status_label, message);
    Serial.printf("[TouchFocus] %s\n", message);
}

lv_obj_t *create_button(lv_obj_t *parent, const char *caption,
                        const char *status, lv_color_t background,
                        lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 14, 0);
    lv_obj_set_style_bg_color(button, background, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x4E86C8), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    if (status != nullptr) {
        lv_obj_add_event_cb(button, set_status, LV_EVENT_CLICKED,
                            const_cast<char *>(status));
    }

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, caption);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_center(label);
    return button;
}

void style_screen(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xF2F5F7), 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

void create_status_header(lv_obj_t *screen, const char *screen_name)
{
    lv_obj_t *header = lv_obj_create(screen);
    lv_obj_set_size(header, 480, 64);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, screen_name);
    lv_obj_set_width(title, 140);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x69B7FF), 0);
    lv_obj_set_pos(title, 18, 23);

    create_wifi_header(header);
    create_battery_header(header);
    create_ble_header_icon(header);
    create_ntp_time_header(header);

    lv_obj_t *divider = lv_obj_create(header);
    lv_obj_set_size(divider, 480, 2);
    lv_obj_align(divider, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x31404D), 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

void create_all_status_headers()
{
    create_status_header(main_screen, "FOCUSER");
    create_status_header(settings_screen, "CONNECTION");
    create_status_header(edit_presets_screen, "EDIT PRESETS");
    create_status_header(focuser_setup_screen, "FOCUSER SETUP");
    create_status_header(screen_settings_screen, "SCREEN SETTINGS");
    create_status_header(mount_screen, "MOUNT CONTROL");
    create_status_header(onstep_setup_screen, "ONSTEP SETUP");
}

const char *mount_rate_text(uint8_t rate)
{
    static const char *labels[] = {
        "", "", "", "2x", "4x", "8x", "20x", "48x", "HALF MAX", "MAX"
    };
    return rate <= 9 ? labels[rate] : "?";
}

void refresh_mount_rate()
{
    if (mount_rate_label) lv_label_set_text(mount_rate_label,
                                             mount_rate_text(onstep_mount.rate()));
}

int mount_direction_index(touchfocus::MountDirection direction)
{
    switch (direction) {
        case touchfocus::MountDirection::North: return 0;
        case touchfocus::MountDirection::East:  return 1;
        case touchfocus::MountDirection::South: return 2;
        case touchfocus::MountDirection::West:  return 3;
    }
    return -1;
}

void draw_mount_sectors(int active_sector = -1)
{
    if (!mount_sector_canvas) return;
    lv_canvas_fill_bg(mount_sector_canvas, lv_color_black(), LV_OPA_TRANSP);

    constexpr int centre = 200;
    constexpr int outer_radius = 190;
    constexpr int inner_radius = 66;
    constexpr int arc_segments = 12;
    // Outer arc plus two inner corners makes a convex truncated sector.
    // LVGL 8.3's canvas polygon rasterizer can hang on a concave annular
    // polygon, so do not approximate an inner arc here.
    constexpr int point_count = arc_segments + 3;
    const float centres[] = {-90.0F, 0.0F, 90.0F, 180.0F};

    for (int sector = 0; sector < 4; ++sector) {
        lv_point_t points[point_count] {};
        const float start = centres[sector] - 38.0F;
        const float end = centres[sector] + 38.0F;
        for (int index = 0; index <= arc_segments; ++index) {
            const float angle = (start + (end - start) * index / arc_segments) *
                                PI / 180.0F;
            points[index] = {
                static_cast<lv_coord_t>(lroundf(centre + outer_radius * cosf(angle))),
                static_cast<lv_coord_t>(lroundf(centre + outer_radius * sinf(angle)))};
        }
        const float inner_end = end * PI / 180.0F;
        const float inner_start = start * PI / 180.0F;
        points[arc_segments + 1] = {
            static_cast<lv_coord_t>(lroundf(centre + inner_radius * cosf(inner_end))),
            static_cast<lv_coord_t>(lroundf(centre + inner_radius * sinf(inner_end)))};
        points[arc_segments + 2] = {
            static_cast<lv_coord_t>(lroundf(centre + inner_radius * cosf(inner_start))),
            static_cast<lv_coord_t>(lroundf(centre + inner_radius * sinf(inner_start)))};

        lv_draw_rect_dsc_t fill {};
        lv_draw_rect_dsc_init(&fill);
        fill.bg_color = sector == active_sector ? lv_color_hex(0x2F81F7)
                                                : lv_color_hex(0x18242F);
        fill.bg_opa = sector == active_sector ? LV_OPA_70 : LV_OPA_COVER;
        fill.border_width = 0;
        lv_canvas_draw_polygon(mount_sector_canvas, points, point_count, &fill);

    }
}

void clear_mount_sector_highlight() { draw_mount_sectors(); }

void highlight_mount_sector(touchfocus::MountDirection direction)
{
    draw_mount_sectors(mount_direction_index(direction));
}

void mount_pad_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        lv_point_t point {};
        lv_indev_get_point(lv_indev_get_act(), &point);
        lv_area_t area {};
        lv_obj_get_coords(lv_event_get_target(event), &area);
        const int32_t dx = point.x - (area.x1 + area.x2) / 2;
        const int32_t dy = point.y - (area.y1 + area.y2) / 2;
        const int32_t radius_squared = dx * dx + dy * dy;
        if (radius_squared < 66 * 66 || radius_squared > 194 * 194) return;
        if (abs(dx) > abs(dy)) {
            mount_pad_direction = dx < 0 ? touchfocus::MountDirection::West
                                         : touchfocus::MountDirection::East;
        } else {
            mount_pad_direction = dy < 0 ? touchfocus::MountDirection::North
                                         : touchfocus::MountDirection::South;
        }
        mount_pad_active = true;
        highlight_mount_sector(mount_pad_direction);
        if (!onstep_mount.move(mount_pad_direction) && mount_status)
            lv_label_set_text(mount_status, "ONSTEP DISCONNECTED");
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (mount_pad_active) onstep_mount.stop(mount_pad_direction);
        mount_pad_active = false;
        clear_mount_sector_highlight();
    }
}

void mount_rate_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const int delta = static_cast<int>(reinterpret_cast<intptr_t>(
        lv_event_get_user_data(event)));
    const uint8_t new_rate = static_cast<uint8_t>(constrain(
        static_cast<int>(onstep_mount.rate()) + delta, 3, 9));
    onstep_mount.setRate(new_rate);
    refresh_mount_rate();
}

void mount_auxiliary_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const bool spiral = reinterpret_cast<uintptr_t>(
                            lv_event_get_user_data(event)) != 0;
    const bool success = spiral ? onstep_mount.toggleSpiralSearch()
                                : onstep_mount.backToLastTarget();
    snprintf(mount_action_status_text, sizeof(mount_action_status_text), "%s",
             success ? (spiral ? "SPIRAL SEARCH" : "GOTO LAST TARGET")
                     : (spiral ? "SPIRAL FAILED" : "BACK REJECTED"));
    mount_action_status_success = success;
    mount_action_status_until_ms = millis() + 2500;
}

void mount_ui_poll(lv_timer_t *)
{
    if (lv_scr_act() == mount_screen) onstep_mount.poll();
    if (!mount_status || lv_scr_act() != mount_screen) return;
    if (static_cast<int32_t>(mount_action_status_until_ms - millis()) > 0) {
        lv_label_set_text(mount_status, mount_action_status_text);
        lv_obj_set_style_text_color(mount_status,
            mount_action_status_success ? lv_color_hex(0x56D364)
                                        : lv_color_hex(0xFF7B72), 0);
    } else {
        lv_label_set_text(mount_status,
                          onstep_mount.isConnected() ? "ONSTEP CONNECTED"
                                                     : "ONSTEP DISCONNECTED");
        lv_obj_set_style_text_color(mount_status,
            onstep_mount.isConnected() ? lv_color_hex(0x56D364)
                                       : lv_color_hex(0xFF7B72), 0);
    }

    const uint32_t now = millis();
    if (!mount_pad_active && onstep_mount.isConnected() &&
        now - mount_coordinates_polled_ms >= 1000) {
        mount_coordinates_polled_ms = now;
        String altitude;
        String azimuth;
        String local_time;
        const bool coordinates_ok = onstep_mount.getAltAz(altitude, azimuth);
        const bool time_ok = onstep_mount.getLocalTime(local_time);
        if (coordinates_ok) {
            lv_label_set_text_fmt(mount_alt_label, "ALT  %s", altitude.c_str());
            lv_label_set_text_fmt(mount_az_label, "AZ  %s", azimuth.c_str());
        }
        if (time_ok)
            lv_label_set_text_fmt(mount_local_time_label, "LOCAL TIME  %s",
                                  local_time.c_str());
    } else if (!onstep_mount.isConnected()) {
        lv_label_set_text(mount_alt_label, "ALT  --");
        lv_label_set_text(mount_az_label, "AZ  --");
        lv_label_set_text(mount_local_time_label, "LOCAL TIME  --");
    }
}

void build_mount_screen()
{
    mount_screen = lv_obj_create(nullptr);
    style_screen(mount_screen);
    lv_obj_add_event_cb(mount_screen, navigate_on_gesture, LV_EVENT_GESTURE, nullptr);

    lv_obj_t *title = lv_label_create(mount_screen);
    lv_label_set_text(title, "MOUNT CONTROL");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x69B7FF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    mount_ntp_time_label = lv_label_create(mount_screen);
    lv_label_set_text(mount_ntp_time_label, "--:--");
    lv_obj_set_width(mount_ntp_time_label, 64);
    lv_obj_set_style_text_align(mount_ntp_time_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(mount_ntp_time_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mount_ntp_time_label, lv_color_hex(0x8EA5B8), 0);
    lv_obj_align(mount_ntp_time_label, LV_ALIGN_TOP_RIGHT, -18, 23);

    lv_obj_t *divider = lv_obj_create(mount_screen);
    lv_obj_set_size(divider, 480, 2);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x31404D), 0);
    lv_obj_set_style_border_width(divider, 0, 0);

    mount_status = lv_label_create(mount_screen);
    lv_label_set_text(mount_status, "ONSTEP DISCONNECTED");
    lv_obj_set_width(mount_status, 440);
    lv_obj_set_style_text_align(mount_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(mount_status, lv_color_hex(0xFF7B72), 0);
    lv_obj_align(mount_status, LV_ALIGN_TOP_MID, 0, 82);

    // Four separated annular sectors reproduce the requested hand-controller
    // layout. The transparent centre prevents accidental axis commands.
    lv_obj_t *pad = lv_obj_create(mount_screen);
    lv_obj_set_size(pad, 400, 400);
    lv_obj_align(pad, LV_ALIGN_TOP_MID, 0, 105);
    lv_obj_set_style_radius(pad, 0, 0);
    lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pad, 0, 0);
    lv_obj_set_style_pad_all(pad, 0, 0);
    lv_obj_clear_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pad, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_event_cb(pad, mount_pad_event, LV_EVENT_ALL, nullptr);

    constexpr size_t canvas_bytes =
        LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(400, 400);
    mount_sector_canvas_buffer = heap_caps_malloc(
        canvas_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mount_sector_canvas_buffer) {
        mount_sector_canvas = lv_canvas_create(pad);
        lv_canvas_set_buffer(mount_sector_canvas, mount_sector_canvas_buffer,
                             400, 400, LV_IMG_CF_TRUE_COLOR_ALPHA);
        lv_obj_center(mount_sector_canvas);
        lv_obj_clear_flag(mount_sector_canvas, LV_OBJ_FLAG_CLICKABLE);
        Serial.println("[OnStep] Drawing convex mount sectors");
        clear_mount_sector_highlight();
        Serial.println("[OnStep] Mount sectors ready");
    } else {
        Serial.println("[OnStep] Cannot allocate direction highlight canvas");
    }

    struct PadLabel { const char *text; lv_align_t align; lv_coord_t x; lv_coord_t y; };
    const PadLabel pad_labels[] = {
        {"N", LV_ALIGN_TOP_MID, 0, 42}, {"S", LV_ALIGN_BOTTOM_MID, 0, -42},
        {"W", LV_ALIGN_LEFT_MID, 42, 0}, {"E", LV_ALIGN_RIGHT_MID, -42, 0},
    };
    for (const auto &item : pad_labels) {
        lv_obj_t *label = lv_label_create(pad);
        lv_label_set_text(label, item.text);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        lv_obj_align(label, item.align, item.x, item.y);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_t *rate_title = lv_label_create(mount_screen);
    lv_label_set_text(rate_title, "SLEW RATE");
    lv_obj_set_style_text_color(rate_title, lv_color_hex(0x8EA5B8), 0);
    lv_obj_align(rate_title, LV_ALIGN_TOP_MID, -48, 515);

    mount_alt_label = lv_label_create(mount_screen);
    lv_label_set_text(mount_alt_label, "ALT  --");
    lv_obj_set_width(mount_alt_label, 205);
    lv_obj_set_style_text_align(mount_alt_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(mount_alt_label, &lv_font_montserrat_16, 0);
    lv_obj_align(mount_alt_label, LV_ALIGN_TOP_LEFT, 22, 545);

    mount_az_label = lv_label_create(mount_screen);
    lv_label_set_text(mount_az_label, "AZ  --");
    lv_obj_set_width(mount_az_label, 205);
    lv_obj_set_style_text_align(mount_az_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(mount_az_label, &lv_font_montserrat_16, 0);
    lv_obj_align(mount_az_label, LV_ALIGN_TOP_RIGHT, -22, 545);

    mount_local_time_label = lv_label_create(mount_screen);
    lv_label_set_text(mount_local_time_label, "LOCAL TIME  --");
    lv_obj_set_width(mount_local_time_label, 440);
    lv_obj_set_style_text_align(mount_local_time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(mount_local_time_label, &lv_font_montserrat_16, 0);
    lv_obj_align(mount_local_time_label, LV_ALIGN_TOP_MID, 0, 575);

    lv_obj_t *slower = create_button(mount_screen, "SLOWER", nullptr,
                                      lv_color_hex(0x263D52), 180, 64);
    lv_obj_align(slower, LV_ALIGN_BOTTOM_LEFT, 28, -30);
    lv_obj_add_event_cb(slower, mount_rate_event, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(-1)));

    mount_rate_label = lv_label_create(mount_screen);
    lv_obj_set_width(mount_rate_label, 110);
    lv_obj_set_style_text_align(mount_rate_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(mount_rate_label, &lv_font_montserrat_20, 0);
    lv_obj_align(mount_rate_label, LV_ALIGN_TOP_MID, 72, 511);
    refresh_mount_rate();

    lv_obj_t *faster = create_button(mount_screen, "FASTER", nullptr,
                                      lv_color_hex(0x263D52), 180, 64);
    lv_obj_align(faster, LV_ALIGN_BOTTOM_RIGHT, -28, -30);
    lv_obj_add_event_cb(faster, mount_rate_event, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(1)));

    lv_obj_t *back = create_button(mount_screen, "BACK", nullptr,
                                    lv_color_hex(0x263D52), 105, 42);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 34, 102);
    lv_obj_add_event_cb(back, mount_auxiliary_event, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(0)));

    lv_obj_t *spiral = create_button(mount_screen, "SPIRAL", nullptr,
                                      lv_color_hex(0x263D52), 105, 42);
    lv_obj_align(spiral, LV_ALIGN_TOP_RIGHT, -34, 102);
    lv_obj_add_event_cb(spiral, mount_auxiliary_event, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(1)));

    lv_timer_create(mount_ui_poll, 250, nullptr);
}

enum class OnStepSetupAction : uint8_t {
    SetLocation, SetTime, StartTracking, SetHome, GoHome, SetPark, Park
};

void set_onstep_setup_status(const char *text, bool success)
{
    if (!onstep_setup_status) return;
    lv_label_set_text(onstep_setup_status, text);
    lv_obj_set_style_text_color(onstep_setup_status,
                                success ? lv_color_hex(0x56D364)
                                        : lv_color_hex(0xFF7B72), 0);
}

void show_onstep_setup_keyboard(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_FOCUSED) return;
    lv_keyboard_set_textarea(onstep_setup_keyboard, lv_event_get_target(event));
    lv_keyboard_set_mode(onstep_setup_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_clear_flag(onstep_setup_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(onstep_setup_keyboard);
}

void hide_onstep_setup_keyboard(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL) return;
    lv_keyboard_set_textarea(onstep_setup_keyboard, nullptr);
    lv_obj_add_flag(onstep_setup_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_state(onstep_latitude_field, LV_STATE_FOCUSED);
    lv_obj_clear_state(onstep_longitude_field, LV_STATE_FOCUSED);
}

void onstep_setup_action(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (!onstep_mount.isConnected()) {
        set_onstep_setup_status("ONSTEP DISCONNECTED", false);
        return;
    }
    const auto action = static_cast<OnStepSetupAction>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    bool success = false;
    const char *message = "Command rejected";
    switch (action) {
        case OnStepSetupAction::SetLocation: {
            char *lat_end = nullptr;
            char *lon_end = nullptr;
            const double latitude = strtod(
                lv_textarea_get_text(onstep_latitude_field), &lat_end);
            const double longitude = strtod(
                lv_textarea_get_text(onstep_longitude_field), &lon_end);
            if (!lat_end || *lat_end != '\0' || !lon_end || *lon_end != '\0' ||
                latitude < -90.0 || latitude > 90.0 ||
                longitude < -180.0 || longitude > 180.0) {
                set_onstep_setup_status("Invalid latitude or longitude", false);
                return;
            }
            success = onstep_mount.setLocation(latitude, longitude);
            message = success ? "Location saved in OnStep" : "Location rejected";
            break;
        }
        case OnStepSetupAction::SetTime: {
            time_t now = time(nullptr);
            struct tm local_time {};
            struct tm utc_time {};
            if (now < 1704067200 || !localtime_r(&now, &local_time) ||
                !gmtime_r(&now, &utc_time)) {
                set_onstep_setup_status("NTP time is not synchronized", false);
                return;
            }
            struct tm local_copy = local_time;
            struct tm utc_copy = utc_time;
            // gmtime_r() sets tm_isdst=0; when interpreting that structure as
            // local civil time, let mktime determine DST for the given date.
            utc_copy.tm_isdst = -1;
            const int utc_offset_minutes = static_cast<int>(
                difftime(mktime(&local_copy), mktime(&utc_copy)) / 60.0);
            success = onstep_mount.setDateTime(
                local_time.tm_year + 1900, local_time.tm_mon + 1,
                local_time.tm_mday, local_time.tm_hour, local_time.tm_min,
                local_time.tm_sec, utc_offset_minutes);
            message = success ? "NTP time saved in OnStep" : "Time rejected";
            break;
        }
        case OnStepSetupAction::StartTracking:
            success = onstep_mount.startTracking();
            message = success ? "Tracking started" : "Tracking rejected";
            break;
        case OnStepSetupAction::SetHome:
            success = onstep_mount.setHomePosition();
            message = success ? "Set Home command sent" : "Set Home failed";
            break;
        case OnStepSetupAction::GoHome:
            success = onstep_mount.goHome();
            message = success ? "Go Home command sent" : "Go Home failed";
            break;
        case OnStepSetupAction::SetPark:
            success = onstep_mount.setParkPosition();
            message = success ? "Park position saved" : "Set Park rejected";
            break;
        case OnStepSetupAction::Park:
            success = onstep_mount.park();
            message = success ? "Parking started" : "Park rejected";
            break;
    }
    set_onstep_setup_status(message, success);
}

void add_onstep_action_button(lv_obj_t *parent, const char *caption,
                              OnStepSetupAction action, lv_align_t align,
                              lv_coord_t x, lv_coord_t y,
                              lv_coord_t width = 200)
{
    lv_obj_t *button = create_button(parent, caption, nullptr,
                                      lv_color_hex(0x263D52), width, 62);
    lv_obj_align(button, align, x, y);
    lv_obj_add_event_cb(button, onstep_setup_action, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(action)));
}

void build_onstep_setup_screen()
{
    onstep_setup_screen = lv_obj_create(nullptr);
    style_screen(onstep_setup_screen);
    lv_obj_add_event_cb(onstep_setup_screen, navigate_on_gesture,
                        LV_EVENT_GESTURE, nullptr);

    lv_obj_t *title = lv_label_create(onstep_setup_screen);
    lv_label_set_text(title, "ONSTEP SETUP");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x69B7FF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);

    onstep_setup_status = lv_label_create(onstep_setup_screen);
    lv_label_set_text(onstep_setup_status, "Ready");
    lv_obj_set_width(onstep_setup_status, 440);
    lv_obj_set_style_text_align(onstep_setup_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(onstep_setup_status, lv_color_hex(0x8EA5B8), 0);
    lv_obj_align(onstep_setup_status, LV_ALIGN_TOP_MID, 0, 66);

    lv_obj_t *location = lv_label_create(onstep_setup_screen);
    lv_label_set_text(location, "LOCATION (decimal degrees)");
    lv_obj_set_style_text_font(location, &lv_font_montserrat_18, 0);
    lv_obj_align(location, LV_ALIGN_TOP_MID, 0, 110);

    onstep_latitude_field = lv_textarea_create(onstep_setup_screen);
    lv_textarea_set_one_line(onstep_latitude_field, true);
    lv_textarea_set_max_length(onstep_latitude_field, 11);
    lv_textarea_set_placeholder_text(onstep_latitude_field, "Latitude +N");
    lv_obj_set_size(onstep_latitude_field, 202, 56);
    lv_obj_align(onstep_latitude_field, LV_ALIGN_TOP_LEFT, 28, 145);
    lv_obj_add_event_cb(onstep_latitude_field, show_onstep_setup_keyboard,
                        LV_EVENT_FOCUSED, nullptr);

    onstep_longitude_field = lv_textarea_create(onstep_setup_screen);
    lv_textarea_set_one_line(onstep_longitude_field, true);
    lv_textarea_set_max_length(onstep_longitude_field, 11);
    lv_textarea_set_placeholder_text(onstep_longitude_field, "Longitude +E");
    lv_obj_set_size(onstep_longitude_field, 202, 56);
    lv_obj_align(onstep_longitude_field, LV_ALIGN_TOP_RIGHT, -28, 145);
    lv_obj_add_event_cb(onstep_longitude_field, show_onstep_setup_keyboard,
                        LV_EVENT_FOCUSED, nullptr);

    add_onstep_action_button(onstep_setup_screen, "SET LOCATION",
                            OnStepSetupAction::SetLocation, LV_ALIGN_TOP_MID,
                            0, 220, 300);
    add_onstep_action_button(onstep_setup_screen, "SET TIME FROM NTP",
                            OnStepSetupAction::SetTime, LV_ALIGN_TOP_MID,
                            0, 300, 300);
    add_onstep_action_button(onstep_setup_screen, "START TRACKING",
                            OnStepSetupAction::StartTracking, LV_ALIGN_TOP_MID,
                            0, 380, 300);
    add_onstep_action_button(onstep_setup_screen, "SET HOME POS.",
                            OnStepSetupAction::SetHome, LV_ALIGN_TOP_LEFT,
                            28, 470);
    add_onstep_action_button(onstep_setup_screen, "GOTO HOME POS.",
                            OnStepSetupAction::GoHome, LV_ALIGN_TOP_RIGHT,
                            -28, 470);
    add_onstep_action_button(onstep_setup_screen, "SET PARK POS.",
                            OnStepSetupAction::SetPark, LV_ALIGN_TOP_LEFT,
                            28, 550);
    add_onstep_action_button(onstep_setup_screen, "PARK",
                            OnStepSetupAction::Park, LV_ALIGN_TOP_RIGHT,
                            -28, 550);

    lv_obj_t *swap_ns_label = lv_label_create(onstep_setup_screen);
    lv_label_set_text(swap_ns_label, "Swap North / South");
    lv_obj_set_style_text_font(swap_ns_label, &lv_font_montserrat_16, 0);
    lv_obj_align(swap_ns_label, LV_ALIGN_TOP_LEFT, 32, 635);
    lv_obj_t *swap_ns = lv_switch_create(onstep_setup_screen);
    lv_obj_set_size(swap_ns, 72, 38);
    lv_obj_align(swap_ns, LV_ALIGN_TOP_RIGHT, -34, 625);
    if (swap_mount_north_south) lv_obj_add_state(swap_ns, LV_STATE_CHECKED);
    lv_obj_add_event_cb(swap_ns, mount_direction_swap_changed,
                        LV_EVENT_VALUE_CHANGED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(0)));

    lv_obj_t *swap_ew_label = lv_label_create(onstep_setup_screen);
    lv_label_set_text(swap_ew_label, "Swap East / West");
    lv_obj_set_style_text_font(swap_ew_label, &lv_font_montserrat_16, 0);
    lv_obj_align(swap_ew_label, LV_ALIGN_TOP_LEFT, 32, 690);
    lv_obj_t *swap_ew = lv_switch_create(onstep_setup_screen);
    lv_obj_set_size(swap_ew, 72, 38);
    lv_obj_align(swap_ew, LV_ALIGN_TOP_RIGHT, -34, 680);
    if (swap_mount_east_west) lv_obj_add_state(swap_ew, LV_STATE_CHECKED);
    lv_obj_add_event_cb(swap_ew, mount_direction_swap_changed,
                        LV_EVENT_VALUE_CHANGED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(1)));

    lv_obj_t *hint = lv_label_create(onstep_setup_screen);
    lv_label_set_text(hint, "Swipe left to return to MOUNT CONTROL");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8EA5B8), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -18);

    onstep_setup_keyboard = lv_keyboard_create(onstep_setup_screen);
    lv_obj_set_size(onstep_setup_keyboard, 480, 300);
    lv_obj_align(onstep_setup_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(onstep_setup_keyboard, hide_onstep_setup_keyboard,
                        LV_EVENT_ALL, nullptr);
    lv_obj_add_flag(onstep_setup_keyboard, LV_OBJ_FLAG_HIDDEN);
}

void build_main_screen()
{
    main_screen = lv_scr_act();
    style_screen(main_screen);
    lv_obj_add_event_cb(main_screen, navigate_on_gesture, LV_EVENT_GESTURE, nullptr);

    position_label = lv_label_create(main_screen);
    lv_label_set_text(position_label, "Position: --");
    lv_obj_set_style_text_font(position_label, &lv_font_montserrat_16, 0);
    lv_obj_align(position_label, LV_ALIGN_TOP_LEFT, 16, 68);

    status_label = lv_label_create(main_screen);
    lv_label_set_text(status_label, "Ready");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x8EA5B8), 0);
    lv_obj_set_width(status_label, 220);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_RIGHT, -16, 68);

    lv_obj_t *divider = lv_obj_create(main_screen);
    lv_obj_set_size(divider, 480, 2);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x31404D), 0);
    lv_obj_set_style_border_width(divider, 0, 0);

    static const char *preset_captions[] = {
        "P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8", "P9"
    };
    lv_obj_t *preset_grid = lv_obj_create(main_screen);
    lv_obj_set_size(preset_grid, 420, 330);
    lv_obj_align(preset_grid, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_set_style_bg_opa(preset_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(preset_grid, 0, 0);
    lv_obj_set_style_pad_all(preset_grid, 0, 0);
    lv_obj_set_style_pad_row(preset_grid, 18, 0);
    lv_obj_set_style_pad_column(preset_grid, 18, 0);
    static lv_coord_t columns[] = {128, 128, 128, LV_GRID_TEMPLATE_LAST};
    static lv_coord_t rows[] = {96, 96, 96, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(preset_grid, columns, rows);

    for (int index = 0; index < 9; ++index) {
        lv_obj_t *button = create_button(preset_grid, preset_captions[index],
                                         nullptr, lv_color_hex(0x263D52),
                                         128, 96);
        lv_obj_add_event_cb(button, preset_event, LV_EVENT_ALL,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(index + 1)));
        main_preset_labels[index] = lv_obj_get_child(button, 0);
        refresh_preset_name_labels(index + 1);
        lv_obj_set_grid_cell(button, LV_GRID_ALIGN_CENTER, index % 3, 1,
                             LV_GRID_ALIGN_CENTER, index / 3, 1);
    }

    lv_obj_t *home_button = create_button(main_screen, "HOME", nullptr,
                                          lv_color_hex(0x8A4B16), 190, 86);
    lv_obj_add_event_cb(home_button, home_event, LV_EVENT_CLICKED, nullptr);
    lv_obj_align(home_button, LV_ALIGN_TOP_LEFT, 28, 438);

    lv_obj_t *stop_button = create_button(main_screen, "STOP!", nullptr,
                                          lv_color_hex(0xB42318), 190, 86);
    lv_obj_set_style_bg_color(stop_button, lv_color_hex(0xE5484D),
                              LV_STATE_PRESSED);
    lv_obj_add_event_cb(stop_button, emergency_stop_event,
                        LV_EVENT_PRESSED, nullptr);
    lv_obj_align(stop_button, LV_ALIGN_TOP_RIGHT, -28, 438);

    // Stylized side view of the focuser. The outlined extension grows from the
    // fixed outlined body according to position_mm / max_travel_mm.
    lv_obj_t *gauge_body = lv_obj_create(main_screen);
    lv_obj_set_pos(gauge_body, 42, 552);
    lv_obj_set_size(gauge_body, 64, 76);
    lv_obj_set_style_bg_opa(gauge_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gauge_body, 3, 0);
    lv_obj_set_style_border_color(gauge_body, lv_color_hex(0x8EA5B8), 0);
    lv_obj_set_style_radius(gauge_body, 0, 0);
    lv_obj_set_style_pad_all(gauge_body, 0, 0);
    lv_obj_clear_flag(gauge_body, LV_OBJ_FLAG_SCROLLABLE);

    focuser_gauge_extension = lv_obj_create(main_screen);
    lv_obj_set_pos(focuser_gauge_extension, 106, 568);
    lv_obj_set_size(focuser_gauge_extension, 1, 44);
    lv_obj_set_style_bg_opa(focuser_gauge_extension, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(focuser_gauge_extension, 3, 0);
    lv_obj_set_style_border_color(focuser_gauge_extension,
                                  lv_color_hex(0x8EA5B8), 0);
    lv_obj_set_style_radius(focuser_gauge_extension, 0, 0);
    lv_obj_set_style_pad_all(focuser_gauge_extension, 0, 0);
    lv_obj_clear_flag(focuser_gauge_extension, LV_OBJ_FLAG_SCROLLABLE);

    focuser_gauge_marker = lv_obj_create(main_screen);
    lv_obj_set_pos(focuser_gauge_marker, 81, 594);
    lv_obj_set_size(focuser_gauge_marker, 50, 50);
    lv_obj_set_style_bg_opa(focuser_gauge_marker, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(focuser_gauge_marker, 3, 0);
    lv_obj_set_style_border_color(focuser_gauge_marker, lv_color_hex(0x8EA5B8), 0);
    lv_obj_set_style_radius(focuser_gauge_marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(focuser_gauge_marker, 0, 0);
    lv_obj_clear_flag(focuser_gauge_marker, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *in_button = create_button(main_screen, "IN", nullptr,
                                        lv_color_hex(0x276749), 190, 82);
    lv_obj_add_event_cb(in_button, motion_event, LV_EVENT_ALL,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(0)));
    lv_obj_align(in_button, LV_ALIGN_BOTTOM_LEFT, 28, -28);

    lv_obj_t *out_button = create_button(main_screen, "OUT", nullptr,
                                         lv_color_hex(0x276749), 190, 82);
    lv_obj_add_event_cb(out_button, motion_event, LV_EVENT_ALL,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(1)));
    lv_obj_align(out_button, LV_ALIGN_BOTTOM_RIGHT, -28, -28);
}

void build_edit_presets_screen()
{
    edit_presets_screen = lv_obj_create(nullptr);
    style_screen(edit_presets_screen);
    lv_obj_add_event_cb(edit_presets_screen, navigate_on_gesture,
                        LV_EVENT_GESTURE, nullptr);

    lv_obj_t *title = lv_label_create(edit_presets_screen);
    lv_label_set_text(title, "EDIT PRESETS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x69B7FF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *divider = lv_obj_create(edit_presets_screen);
    lv_obj_set_size(divider, 480, 2);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x31404D), 0);
    lv_obj_set_style_border_width(divider, 0, 0);

    for (uint8_t index = 0; index < 9; ++index) {
        lv_obj_t *button = lv_btn_create(edit_presets_screen);
        lv_obj_set_size(button, 420, 58);
        lv_obj_align(button, LV_ALIGN_TOP_MID, 0, 78 + index * 70);
        lv_obj_set_style_radius(button, 12, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x263D52), 0);
        lv_obj_add_event_cb(button, edit_preset_name, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(index + 1)));

        preset_edit_labels[index] = lv_label_create(button);
        lv_obj_set_width(preset_edit_labels[index], 370);
        lv_obj_set_style_text_font(preset_edit_labels[index], &lv_font_montserrat_18, 0);
        lv_obj_align(preset_edit_labels[index], LV_ALIGN_LEFT_MID, 4, 0);
        refresh_preset_name_labels(index + 1);
    }

    preset_name_editor = lv_textarea_create(edit_presets_screen);
    lv_textarea_set_one_line(preset_name_editor, true);
    lv_textarea_set_max_length(preset_name_editor, 16);
    lv_obj_set_size(preset_name_editor, 420, 58);
    lv_obj_align(preset_name_editor, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_set_style_text_font(preset_name_editor, &lv_font_montserrat_18, 0);
    lv_obj_add_flag(preset_name_editor, LV_OBJ_FLAG_HIDDEN);

    preset_name_keyboard = lv_keyboard_create(edit_presets_screen);
    lv_obj_set_size(preset_name_keyboard, 480, 300);
    lv_obj_align(preset_name_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(preset_name_keyboard, finish_preset_name_edit,
                        LV_EVENT_ALL, nullptr);
    lv_obj_add_flag(preset_name_keyboard, LV_OBJ_FLAG_HIDDEN);
}

void build_focuser_setup_screen()
{
    focuser_setup_screen = lv_obj_create(nullptr);
    style_screen(focuser_setup_screen);
    lv_obj_add_event_cb(focuser_setup_screen, navigate_on_gesture,
                        LV_EVENT_GESTURE, nullptr);
    lv_obj_t *title = lv_label_create(focuser_setup_screen);
    lv_label_set_text(title, "FOCUSER SETUP");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x69B7FF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    temperature_readout = lv_label_create(focuser_setup_screen);
    lv_label_set_text(temperature_readout, "Sensor: waiting for daemon");
    lv_obj_set_width(temperature_readout, 440);
    lv_obj_set_style_text_align(temperature_readout, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(temperature_readout, lv_color_hex(0x8EA5B8), 0);
    lv_obj_align(temperature_readout, LV_ALIGN_TOP_MID, 0, 50);

    static const char *labels[] = {"Motor steps/rev", "Microsteps",
        "Travel/rev (mm)", "Max travel (mm)", "Max jog speed (1-6)",
        "Coefficient (steps/C)", "Hysteresis (C)"};
    static const lv_coord_t rows[] = {82, 143, 204, 265, 326, 447, 508};
    for (int i = 0; i < 7; ++i) {
        lv_obj_t *label = lv_label_create(focuser_setup_screen);
        lv_label_set_text(label, labels[i]);
        lv_obj_set_width(label, 225);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 20, rows[i] + 12);
        setup_fields[i] = lv_textarea_create(focuser_setup_screen);
        lv_textarea_set_one_line(setup_fields[i], true);
        lv_textarea_set_max_length(setup_fields[i], 12);
        lv_obj_set_size(setup_fields[i], 210, 48);
        lv_obj_align(setup_fields[i], LV_ALIGN_TOP_RIGHT, -20, rows[i]);
        lv_obj_add_event_cb(setup_fields[i], show_setup_keyboard,
                            LV_EVENT_FOCUSED, nullptr);
    }
    char jog_value[8];
    snprintf(jog_value, sizeof(jog_value), "%d", maximum_jog_speed);
    lv_textarea_set_text(setup_fields[4], jog_value);
    lv_textarea_set_text(setup_fields[5], "0.000");
    lv_textarea_set_text(setup_fields[6], "0.30");

    lv_obj_t *temperature_label = lv_label_create(focuser_setup_screen);
    lv_label_set_text(temperature_label, "Temperature compensation");
    lv_obj_set_width(temperature_label, 310);
    lv_obj_align(temperature_label, LV_ALIGN_TOP_LEFT, 20, 397);
    temperature_comp_switch = lv_switch_create(focuser_setup_screen);
    lv_obj_set_size(temperature_comp_switch, 84, 42);
    lv_obj_align(temperature_comp_switch, LV_ALIGN_TOP_RIGHT, -20, 384);

    setup_calculated = lv_label_create(focuser_setup_screen);
    lv_label_set_text(setup_calculated, "Calculated: -- steps/mm");
    lv_obj_align(setup_calculated, LV_ALIGN_TOP_MID, 0, 574);
    setup_result = lv_label_create(focuser_setup_screen);
    lv_label_set_text(setup_result, "Open this screen to load daemon settings");
    lv_obj_set_width(setup_result, 430);
    lv_obj_set_style_text_align(setup_result, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(setup_result, LV_ALIGN_TOP_MID, 0, 610);
    lv_obj_t *save = create_button(focuser_setup_screen, "SAVE", nullptr,
                                   lv_color_hex(0x276749), 210, 64);
    lv_obj_add_event_cb(save, save_focuser_setup, LV_EVENT_CLICKED, nullptr);
    lv_obj_align(save, LV_ALIGN_BOTTOM_MID, 0, -42);
    setup_keyboard = lv_keyboard_create(focuser_setup_screen);
    lv_obj_set_size(setup_keyboard, 480, 300);
    lv_obj_align(setup_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(setup_keyboard, hide_setup_keyboard, LV_EVENT_ALL, nullptr);
    lv_obj_add_flag(setup_keyboard, LV_OBJ_FLAG_HIDDEN);
}

void build_screen_settings_screen()
{
    screen_settings_screen = lv_obj_create(nullptr);
    style_screen(screen_settings_screen);
    lv_obj_add_event_cb(screen_settings_screen, navigate_on_gesture,
                        LV_EVENT_GESTURE, nullptr);

    lv_obj_t *title = lv_label_create(screen_settings_screen);
    lv_label_set_text(title, "SCREEN SETTINGS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x69B7FF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *timeout_label = lv_label_create(screen_settings_screen);
    lv_label_set_text(timeout_label, "Display timeout");
    lv_obj_set_style_text_font(timeout_label, &lv_font_montserrat_18, 0);
    lv_obj_align(timeout_label, LV_ALIGN_TOP_LEFT, 30, 110);

    lv_obj_t *timeout = lv_dropdown_create(screen_settings_screen);
    lv_dropdown_set_options(timeout, "15 sec\n30 sec\n60 sec\n2 min\n5 min\nNever");
    lv_obj_set_size(timeout, 220, 56);
    lv_obj_align(timeout, LV_ALIGN_TOP_RIGHT, -30, 94);
    uint16_t selected = display_off_after_ms == 15000 ? 0 :
                        display_off_after_ms == 30000 ? 1 :
                        display_off_after_ms == 60000 ? 2 :
                        display_off_after_ms == 120000 ? 3 :
                        display_off_after_ms == 300000 ? 4 : 5;
    lv_dropdown_set_selected(timeout, selected);
    lv_obj_add_event_cb(timeout, display_timeout_changed,
                        LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *wake_label = lv_label_create(screen_settings_screen);
    lv_label_set_text(wake_label, "Require WAKE button for touch");
    lv_obj_set_style_text_font(wake_label, &lv_font_montserrat_18, 0);
    lv_obj_align(wake_label, LV_ALIGN_TOP_LEFT, 30, 215);

    lv_obj_t *wake_switch = lv_switch_create(screen_settings_screen);
    lv_obj_set_size(wake_switch, 80, 42);
    lv_obj_align(wake_switch, LV_ALIGN_TOP_RIGHT, -35, 203);
    if (wake_lock_enabled) lv_obj_add_state(wake_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(wake_switch, wake_lock_changed,
                        LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *hint = lv_label_create(screen_settings_screen);
    lv_label_set_text(hint, "When enabled, GPIO33 must be HIGH while touching.");
    lv_obj_set_width(hint, 420);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8EA5B8), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 275);

    lv_obj_t *brightness_label = lv_label_create(screen_settings_screen);
    lv_label_set_text(brightness_label, "Brightness");
    lv_obj_set_style_text_font(brightness_label, &lv_font_montserrat_18, 0);
    lv_obj_align(brightness_label, LV_ALIGN_TOP_LEFT, 30, 330);

    brightness_value_label = lv_label_create(screen_settings_screen);
    lv_label_set_text_fmt(brightness_value_label, "%u%%",
                          display_brightness_percent);
    lv_obj_set_width(brightness_value_label, 65);
    lv_obj_set_style_text_align(brightness_value_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(brightness_value_label, &lv_font_montserrat_18, 0);
    lv_obj_align(brightness_value_label, LV_ALIGN_TOP_RIGHT, -30, 330);

    lv_obj_t *brightness_slider = lv_slider_create(screen_settings_screen);
    lv_obj_set_size(brightness_slider, 420, 24);
    lv_slider_set_range(brightness_slider, 10, 100);
    lv_slider_set_value(brightness_slider, display_brightness_percent,
                        LV_ANIM_OFF);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_MID, 0, 380);
    lv_obj_add_event_cb(brightness_slider, brightness_changed,
                        LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(brightness_slider, brightness_changed,
                        LV_EVENT_RELEASED, nullptr);

    lv_obj_t *ota_button = create_button(screen_settings_screen, "ENABLE OTA", nullptr,
                                         lv_color_hex(0x276749), 210, 58);
    ota_button_label = lv_obj_get_child(ota_button, 0);
    lv_obj_add_event_cb(ota_button, enable_ota, LV_EVENT_CLICKED, nullptr);
    lv_obj_align(ota_button, LV_ALIGN_TOP_MID, 0, 450);

    ota_status_label = lv_label_create(screen_settings_screen);
    lv_label_set_text(ota_status_label, "OTA disabled");
    lv_obj_set_width(ota_status_label, 430);
    lv_obj_set_style_text_align(ota_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ota_status_label, lv_color_hex(0x8EA5B8), 0);
    lv_obj_align(ota_status_label, LV_ALIGN_TOP_MID, 0, 520);
    lv_timer_create(ota_ui_poll, 200, nullptr);

    lv_obj_t *night_button = create_button(screen_settings_screen, "NIGHT", nullptr,
                                           lv_color_hex(0x6E1717), 190, 64);
    lv_obj_add_event_cb(night_button, select_display_mode,
                        LV_EVENT_CLICKED, reinterpret_cast<void *>(1));
    lv_obj_align(night_button, LV_ALIGN_BOTTOM_LEFT, 28, -55);

    lv_obj_t *color_button = create_button(screen_settings_screen, "COLOR", nullptr,
                                           lv_color_hex(0x263D52), 190, 64);
    lv_obj_add_event_cb(color_button, select_display_mode,
                        LV_EVENT_CLICKED, reinterpret_cast<void *>(0));
    lv_obj_align(color_button, LV_ALIGN_BOTTOM_RIGHT, -28, -55);
}

void build_settings_screen()
{
    settings_screen = lv_obj_create(nullptr);
    style_screen(settings_screen);
    lv_obj_add_event_cb(settings_screen, navigate_on_gesture, LV_EVENT_GESTURE, nullptr);

    lv_obj_t *title = lv_label_create(settings_screen);
    lv_label_set_text(title, "CONNECTION");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x69B7FF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 18, 19);

    lv_obj_t *divider = lv_obj_create(settings_screen);
    lv_obj_set_size(divider, 480, 2);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x31404D), 0);
    lv_obj_set_style_border_width(divider, 0, 0);

    lv_obj_t *scan_button = lv_btn_create(settings_screen);
    lv_obj_set_size(scan_button, 150, 58);
    lv_obj_align(scan_button, LV_ALIGN_TOP_RIGHT, -24, 82);
    lv_obj_set_style_radius(scan_button, 12, 0);
    lv_obj_set_style_bg_color(scan_button, lv_color_hex(0x263D52), 0);
    lv_obj_add_event_cb(scan_button, start_wifi_scan, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *scan_label = lv_label_create(scan_button);
    lv_label_set_text(scan_label, "Scan Wi-Fi");
    lv_obj_set_style_text_font(scan_label, &lv_font_montserrat_16, 0);
    lv_obj_center(scan_label);

    lv_obj_t *wifi_title = lv_label_create(settings_screen);
    lv_label_set_text(wifi_title, "Wi-Fi");
    lv_obj_set_style_text_font(wifi_title, &lv_font_montserrat_20, 0);
    lv_obj_align(wifi_title, LV_ALIGN_TOP_LEFT, 24, 100);

    lv_obj_t *ssid_label = lv_label_create(settings_screen);
    lv_label_set_text(ssid_label, "Network");
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0x8EA5B8), 0);
    lv_obj_align(ssid_label, LV_ALIGN_TOP_LEFT, 24, 165);

    wifi_dropdown = lv_dropdown_create(settings_screen);
    lv_dropdown_set_options(wifi_dropdown,
                            saved_wifi_ssid.isEmpty()
                                ? "Press Scan Wi-Fi"
                                : saved_wifi_ssid.c_str());
    lv_obj_set_size(wifi_dropdown, 432, 56);
    lv_obj_align(wifi_dropdown, LV_ALIGN_TOP_MID, 0, 190);
    lv_obj_set_style_text_font(wifi_dropdown, &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(wifi_dropdown, wifi_network_selected,
                        LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *password_label = lv_label_create(settings_screen);
    lv_label_set_text(password_label, "Password");
    lv_obj_set_style_text_color(password_label, lv_color_hex(0x8EA5B8), 0);
    lv_obj_align(password_label, LV_ALIGN_TOP_LEFT, 24, 270);

    wifi_password = lv_textarea_create(settings_screen);
    lv_textarea_set_one_line(wifi_password, true);
    lv_textarea_set_password_mode(wifi_password, true);
    lv_textarea_set_placeholder_text(wifi_password, "Wi-Fi password");
    lv_textarea_set_text(wifi_password, saved_wifi_password.c_str());
    lv_obj_set_size(wifi_password, 432, 58);
    lv_obj_align(wifi_password, LV_ALIGN_TOP_MID, 0, 295);
    lv_obj_set_style_text_font(wifi_password, &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(wifi_password, show_wifi_keyboard, LV_EVENT_FOCUSED, nullptr);

    lv_obj_t *ip_label = lv_label_create(settings_screen);
    lv_label_set_text(ip_label, "Focuser IP");
    lv_obj_set_style_text_color(ip_label, lv_color_hex(0x8EA5B8), 0);
    lv_obj_align(ip_label, LV_ALIGN_TOP_LEFT, 24, 360);

    focuser_ip_field = lv_textarea_create(settings_screen);
    lv_textarea_set_one_line(focuser_ip_field, true);
    lv_textarea_set_max_length(focuser_ip_field, 15);
    lv_textarea_set_text(focuser_ip_field, saved_focuser_ip.c_str());
    lv_obj_set_size(focuser_ip_field, 196, 58);
    lv_obj_align(focuser_ip_field, LV_ALIGN_TOP_LEFT, 24, 382);
    lv_obj_set_style_text_font(focuser_ip_field, &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(focuser_ip_field, show_wifi_keyboard,
                        LV_EVENT_FOCUSED, nullptr);

    lv_obj_t *find_button = lv_btn_create(settings_screen);
    lv_obj_set_size(find_button, 98, 58);
    lv_obj_align(find_button, LV_ALIGN_TOP_LEFT, 228, 382);
    lv_obj_set_style_radius(find_button, 12, 0);
    lv_obj_set_style_bg_color(find_button, lv_color_hex(0x263D52), 0);
    lv_obj_add_event_cb(find_button, find_focuser, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *find_label = lv_label_create(find_button);
    lv_label_set_text(find_label, "FIND");
    lv_obj_set_style_text_font(find_label, &lv_font_montserrat_16, 0);
    lv_obj_center(find_label);

    lv_obj_t *connect_button = lv_btn_create(settings_screen);
    lv_obj_set_size(connect_button, 116, 58);
    lv_obj_align(connect_button, LV_ALIGN_TOP_RIGHT, -24, 382);
    lv_obj_set_style_radius(connect_button, 12, 0);
    lv_obj_set_style_bg_color(connect_button, lv_color_hex(0x276749), 0);
    lv_obj_add_event_cb(connect_button, connect_wifi, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *connect_label = lv_label_create(connect_button);
    lv_label_set_text(connect_label, "CONNECT");
    lv_obj_set_style_text_font(connect_label, &lv_font_montserrat_14, 0);
    lv_obj_center(connect_label);

    wifi_status = lv_label_create(settings_screen);
    lv_label_set_text(wifi_status, "Not connected");
    lv_obj_set_width(wifi_status, 432);
    lv_obj_set_style_text_align(wifi_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(wifi_status, lv_color_hex(0x8EA5B8), 0);
    lv_obj_align(wifi_status, LV_ALIGN_TOP_MID, 0, 475);

    lv_obj_t *ble_title = lv_label_create(settings_screen);
    lv_label_set_text(ble_title, "Bluetooth LE");
    lv_obj_set_style_text_font(ble_title, &lv_font_montserrat_20, 0);
    lv_obj_align(ble_title, LV_ALIGN_TOP_LEFT, 24, 525);

    lv_obj_t *ble_scan_button = lv_btn_create(settings_screen);
    lv_obj_set_size(ble_scan_button, 132, 50);
    lv_obj_align(ble_scan_button, LV_ALIGN_TOP_RIGHT, -24, 510);
    lv_obj_set_style_radius(ble_scan_button, 12, 0);
    lv_obj_set_style_bg_color(ble_scan_button, lv_color_hex(0x263D52), 0);
    lv_obj_add_event_cb(ble_scan_button, start_ble_scan, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *ble_scan_label = lv_label_create(ble_scan_button);
    lv_label_set_text(ble_scan_label, "Scan BLE");
    lv_obj_center(ble_scan_label);

    ble_dropdown = lv_dropdown_create(settings_screen);
    lv_dropdown_set_options(ble_dropdown, "Press Scan BLE");
    lv_obj_set_size(ble_dropdown, 292, 54);
    lv_obj_align(ble_dropdown, LV_ALIGN_TOP_LEFT, 24, 580);

    lv_obj_t *ble_connect_button = lv_btn_create(settings_screen);
    lv_obj_set_size(ble_connect_button, 120, 54);
    lv_obj_align(ble_connect_button, LV_ALIGN_TOP_RIGHT, -24, 580);
    lv_obj_set_style_radius(ble_connect_button, 12, 0);
    lv_obj_set_style_bg_color(ble_connect_button, lv_color_hex(0x276749), 0);
    lv_obj_add_event_cb(ble_connect_button, connect_ble, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *ble_connect_label = lv_label_create(ble_connect_button);
    lv_label_set_text(ble_connect_label, "Connect");
    lv_obj_center(ble_connect_label);

    ble_status = lv_label_create(settings_screen);
    lv_label_set_text(ble_status, "BLE not connected");
    lv_obj_set_width(ble_status, 432);
    lv_obj_set_style_text_align(ble_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ble_status, lv_color_hex(0x8EA5B8), 0);
    lv_obj_align(ble_status, LV_ALIGN_TOP_MID, 0, 655);

    lv_obj_t *onstep_ip_label = lv_label_create(settings_screen);
    lv_label_set_text(onstep_ip_label, "OnStep IP (TCP 9999)");
    lv_obj_set_style_text_color(onstep_ip_label, lv_color_hex(0x8EA5B8), 0);
    lv_obj_align(onstep_ip_label, LV_ALIGN_TOP_LEFT, 24, 690);

    onstep_ip_field = lv_textarea_create(settings_screen);
    lv_textarea_set_one_line(onstep_ip_field, true);
    lv_textarea_set_max_length(onstep_ip_field, 15);
    lv_textarea_set_text(onstep_ip_field, saved_onstep_ip.c_str());
    lv_obj_set_size(onstep_ip_field, 270, 56);
    lv_obj_align(onstep_ip_field, LV_ALIGN_TOP_LEFT, 24, 712);
    lv_obj_set_style_text_font(onstep_ip_field, &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(onstep_ip_field, show_wifi_keyboard,
                        LV_EVENT_FOCUSED, nullptr);

    lv_obj_t *onstep_save = create_button(settings_screen, "SAVE", nullptr,
                                           lv_color_hex(0x276749), 138, 56);
    lv_obj_align(onstep_save, LV_ALIGN_TOP_RIGHT, -24, 712);
    lv_obj_add_event_cb(onstep_save, save_onstep_ip, LV_EVENT_CLICKED, nullptr);

    wifi_keyboard = lv_keyboard_create(settings_screen);
    lv_obj_set_size(wifi_keyboard, 480, 300);
    lv_obj_align(wifi_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(wifi_keyboard, hide_wifi_keyboard, LV_EVENT_ALL, nullptr);
    lv_obj_add_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);

    lv_timer_create(wifi_poll, 500, nullptr);
    refresh_wifi_headers();
    refresh_ble_headers();
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    // Capacitive WAKE module drives approximately 3 V when touched. Keep the
    // input defined LOW while the module is inactive or disconnected.
    pinMode(WAKE_BUTTON_PIN, INPUT_PULLDOWN);
    delay(200);
    Serial.println("\n[TouchFocus] starting GUI prototype");
    load_wifi_credentials();
    load_preset_names();
    load_screen_settings();
    onstep_mount.setDirectionSwap(swap_mount_north_south,
                                  swap_mount_east_west);
    focuser.setJogSpeed(maximum_jog_speed);

    IPAddress configured_focuser_ip;
    if (!configured_focuser_ip.fromString(saved_focuser_ip)) {
        configured_focuser_ip = touchfocus::config::FOCUSER_HOST;
        saved_focuser_ip = configured_focuser_ip.toString();
    }
    udp_focuser_transport.setHost(configured_focuser_ip);

    IPAddress configured_onstep_ip;
    if (!configured_onstep_ip.fromString(saved_onstep_ip)) {
        configured_onstep_ip = IPAddress(192, 168, 88, 60);
        saved_onstep_ip = configured_onstep_ip.toString();
    }
    onstep_mount.setHost(configured_onstep_ip);

    if (board_p4_display_init() != ESP_OK) {
        Serial.println("[TouchFocus] display initialization FAILED");
        return;
    }

    if (!lvgl_glue_start(true)) {
        Serial.println("[TouchFocus] LVGL initialization FAILED");
        return;
    }

    if (lvgl_glue_lock(0)) {
        lv_color_filter_dsc_init(&night_color_filter, night_filter_cb);
        Serial.println("[TouchFocus] Building CONNECTION screen");
        build_settings_screen();
        Serial.println("[TouchFocus] Building FOCUSER SETUP screen");
        build_focuser_setup_screen();
        Serial.println("[TouchFocus] Building SCREEN SETTINGS screen");
        build_screen_settings_screen();
        Serial.println("[TouchFocus] Building EDIT PRESETS screen");
        build_edit_presets_screen();
        Serial.println("[TouchFocus] Building MOUNT CONTROL screen");
        build_mount_screen();
        Serial.println("[TouchFocus] Building ONSTEP SETUP screen");
        build_onstep_setup_screen();
        Serial.println("[TouchFocus] Building main screen");
        build_main_screen();
        create_all_status_headers();
        create_wake_high_indicators();
        apply_display_mode();
        restore_display_brightness();
        lv_timer_create(focuser_ui_poll, 20, nullptr);
        last_user_activity_ms = millis();
        lv_timer_create(power_idle_poll, 25, nullptr);
        lvgl_glue_unlock();
    }

    network.begin(saved_wifi_ssid, saved_wifi_password);
    // Central European time with automatic EU daylight-saving transitions.
    configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3",
                 "pool.ntp.org", "time.nist.gov");
    focuser.begin();
    onstep_mount.begin();

    Serial.println("[TouchFocus] GUI ready");
}

void loop()
{
    if (ota_enabled) ArduinoOTA.handle();
    delay(10);
}
