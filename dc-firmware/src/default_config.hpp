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
};
