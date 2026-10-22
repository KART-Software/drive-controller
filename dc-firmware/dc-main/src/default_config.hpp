#pragma once

#include "proto/drive_controller.pb.h"

inline constexpr dc_Config DEFAULT_CONFIG = {
    .has_sensor_calib = true,
    .sensor_calib =
        {
            .apps1_min = 27070,
            .apps1_max = 34499,
            .apps2_min = 39292,
            .apps2_max = 31798,
            .ittr_min = 27070,
            .ittr_max = 34499,
            .tps1_min = 19278,
            .tps1_max = 30657,
            .tps2_min = 44710,
            .tps2_max = 33505,
            .target_tp_idling = 15,
            .target_tp_normal_max = 100,
            .target_tp_restricted_max = 60,
            .clutch_min = 5000,
            .clutch_max = 60000,
            .has_gps = true,
            .gps =
                {
                    .type = dc_TransmissionType_TRANSMISSION_IST,
                    .ist_raw_values_count = 5,
                    .ist_raw_values = {32768, 32768, 32768, 32768, 32768},
                    .normal_raw_values_count = 7,
                    .normal_raw_values = {32768, 32768, 32768, 32768, 32768, 32768, 32768},
                },
        },
    .has_etc = true,
    .etc =
        {
            .has_plausibility_check_flags = true,
            .plausibility_check_flags =
                {
                    .apps = true,
                    .tps = true,
                    .apps1 = true,
                    .apps2 = true,
                    .tps1 = true,
                    .tps2 = true,
                    .target = true,
                    .bps = true,
                    .bps_tps = true,
                },
            .use_ittr = true,
            .has_pid = true,
            .pid =
                {
                    .k_p = 3.0f,
                    .k_i = 0.4f,
                    .k_d = 0.0f,
                },
            .has_target_curve = true,
            .target_curve =
                {
                    .a4 = 0.0,
                    .a3 = 0.0,
                    .a2 = 0.0087,
                    .a1 = 0.13,
                },
        },
    .has_launch = true,
    .launch =
        {
            .launch_rpm_threshold = 3000.0f,
            .clutch_depress_threshold = 20.0f,
            .approach_speed_pct_per_s = 50.0f,
            .engagement_ramp_rate = 0.3f,
            .has_engagement_pid = true,
            .engagement_pid = {.k_p = 50.0f, .k_i = 0.0f, .k_d = 10.0f},
            .has_inner_pid = true,
            .inner_pid = {.k_p = 2.0f, .k_i = 0.5f, .k_d = 0.0f},
            .bite_point_margin = 5.0f,
            .creep_speed_pct_per_s = 10.0f,
            // pulse 分解能 (100ms 窓で数パルス) で検出可能な現実的な値。1% は分解能以下で不可。
            .slip_detect_threshold = 0.05f,
            .slip_detect_debounce = 3,
            .bite_point_ema_alpha = 0.3f,
            .gear_ratio = 3.0f,
            .final_drive_ratio = 3.5f,
        },
    .has_auto_shift = true,
    .auto_shift =
        {
            // RPM しきい値はエンジン特性依存。要実機調整。
            .upshift_rpm = 11000.0f,
            .downshift_rpm = 6000.0f,
            .min_wheel_kmh = 5.0f,  // 停止/走行 境界 [km/h] (歩く速さ程度)。タイヤ直径 (rotation.wheel_*.tire_diameter_mm) が未設定だと常に停車扱い
            .cooldown_ms = 250,
            .ist_pulse_ms = 15,
            .normal_drive_pulse_ms = 100,
            .normal_neutral_pulse_ms = 25,
            .throttle_on_pct = 50.0f,
        },
    .has_starter = true,
    .starter =
        {
            .engine_running_rpm = 2000.0f,
            .running_confirm_ms = 1000,
            .min_hold_ms = 5000,
        },
    // 回転センサー (docs/loop_nonblocking_spec.md §6.7)。車輪の歯数とタイヤ直径は実車の値に要設定
    .has_rotation = true,
    .rotation =
        {
            .has_engine = true,
            .engine = {.teeth = 12, .window_teeth = 24, .max_window_ms = 20, .stale_ms = 300, .both_edges = true},
            .has_clutch = true,
            .clutch = {.teeth = 100, .window_teeth = 100, .max_window_ms = 10, .stale_ms = 300, .both_edges = true},
            .has_wheel_front = true,
            .wheel_front =
                {
                    .has_sensor = true,
                    .sensor = {.teeth = 1, .window_teeth = 1, .max_window_ms = 20, .stale_ms = 300, .both_edges = true},
                    .tire_diameter_mm = 0.0f,
                },
            .has_wheel_rear = true,
            .wheel_rear =
                {
                    .has_sensor = true,
                    .sensor = {.teeth = 1, .window_teeth = 1, .max_window_ms = 20, .stale_ms = 300, .both_edges = true},
                    .tire_diameter_mm = 0.0f,
                },
        },
};
