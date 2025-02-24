/*
 * Copyright (C) 2025 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "PAL: SpeakerTfa98xx"
#include "SpeakerTfa98xx.h"
#include <log/log.h>
#include <algorithm>
#include "ResourceManager.h"

SpeakerTfa98xx::SpeakerTfa98xx()
    : isInitialized(false),
      speakerCount(0),
      powerAmpCount(0),
      isF0CalibrationSupported(false),
      isValidCalibration(false) {
    memset(f0Values, 0, sizeof(f0Values));
    int status = 0;
    rm = ResourceManager::getInstance();
    if (!rm) {
        PAL_ERR(LOG_TAG, "Failed to get ResourceManager instance");
        return;
    }

    // Getting mixture controls from Resource Manager
    status = rm->getVirtualAudioMixer(&virtMixer);
    if (status) {
        PAL_ERR(LOG_TAG, "virt mixer error %d", status);
    }

    status = rm->getHwAudioMixer(&hwMixer);
    if (status) {
        PAL_ERR(LOG_TAG, "hw mixer error %d", status);
    }

    calibrationInfoInit();
    updateCalibrationValue();
    PAL_INFO(LOG_TAG, "SpeakerTfa98xx initialized");
    isInitialized = true;
}

SpeakerTfa98xx::~SpeakerTfa98xx() {
    hwMixer = nullptr;
    virtMixer = nullptr;
}

bool SpeakerTfa98xx::isTfaDevicePresent(struct mixer* hwMixer) {
    return mixer_get_ctl_by_name(hwMixer, "TFA Calibration");
}

void SpeakerTfa98xx::calibrationInfoInit() {
    calibratedImpedance = mixer_get_ctl_by_name(hwMixer, "TFA Calibration");
    if (!calibratedImpedance) {
        PAL_ERR(LOG_TAG, "Invalid mixer control: TFA Calibration");
        return;
    }

    speakerCount = mixer_ctl_get_num_values(calibratedImpedance);
    if (speakerCount <= 0) {
        PAL_ERR(LOG_TAG, "Invalid speaker count: %d", speakerCount);
        return;
    }
    PAL_INFO(LOG_TAG, "speakerCount:%d, powerAmpCount:%d", speakerCount, powerAmpCount);

    // 2 speakers -> 1 power amp
    // 4 speakers -> 2 power amps
    powerAmpCount = std::min((speakerCount + 1) >> 1, static_cast<int>(MAX_PA_COUNT));

    defaultImpedance = mixer_get_ctl_by_name(hwMixer, "TFA Default Impedance");
    if (!defaultImpedance) {
        PAL_ERR(LOG_TAG, "Invalid mixer control: TFA Default Impedance");
        return;
    }

    caliInfo.clear();
    for (int i = 0; i < speakerCount; i++) {
        if (FILE* fp = openDeviceFile(DEVICE_ADDRESSES[i], "cali_info")) {
            char buffer[64] = {0};
            if (readDeviceFile(buffer, sizeof(buffer), fp) > 0) {
                CaliInfo info = {};
                uint32_t addr;
                if (sscanf(buffer, "0x%x, %hhx, %d, %d\n", &addr, &info.dev_idx, &info.min_imp,
                           &info.max_imp) == 4) {
                    info.i2c_addr = DEVICE_ADDRESSES[i];
                    if (info.dev_idx < speakerCount) {
                        caliInfo.push_back(info);
                    }
                }
            }
            fclose(fp);
        }
    }

    if (!caliInfo.empty()) {
        std::sort(caliInfo.begin(), caliInfo.end());

        for (auto& info : caliInfo) {
            info.def_imp = mixer_ctl_get_value(defaultImpedance, info.dev_idx);
            PAL_INFO(LOG_TAG, "Speaker %hhx: addr=0x%x, impedance:(min=%d, max=%d, default=%d)",
                     info.dev_idx, info.i2c_addr, info.min_imp, info.max_imp, info.def_imp);
        }
    }
}

void SpeakerTfa98xx::updateCalibrationValue() {
    isValidCalibration = false;
    struct mixer_ctl* calCtl = mixer_get_ctl_by_name(hwMixer, "TFA Calibration");
    if (!calCtl) return;

    for (auto& info : caliInfo) {
        int calValue = mixer_ctl_get_value(calCtl, info.dev_idx);
        info.cal_imp = calValue;

        if (calValue > info.min_imp && calValue < info.max_imp) {
            info.dsp_imp = (calValue << 16) / 1000;
            isValidCalibration = true;
            PAL_INFO(LOG_TAG, "i2c=0x%x dev_idx=%i, impedance:(default=%d, cal=%d, dsp=0x%x)",
                     info.i2c_addr, info.dev_idx, info.def_imp, info.cal_imp, info.dsp_imp);
        } else {
            if (info.def_imp > 0) {
                info.dsp_imp = (info.def_imp << 16) / 1000;
            } else if (info.max_imp > 0 && info.min_imp > 0) {
                info.dsp_imp = ((info.max_imp + info.min_imp) >> 1 << 16) / 1000;
            }

            PAL_ERR(LOG_TAG, "i2c=0x%x, dev_idx=%hhx, abnormal impedance=%d", info.i2c_addr,
                    info.dev_idx, calValue);
        }
    }

    f0CalibrationControl = mixer_get_ctl_by_name(hwMixer, "TFA F0 Calibration");
    if (f0CalibrationControl) {
        int numF0Values = mixer_ctl_get_num_values(f0CalibrationControl);
        if (numF0Values > 0) {
            isF0CalibrationSupported = true;
            for (uint32_t i = 0; i < std::min(2u, (uint32_t)(numF0Values + 1) >> 1); i++) {
                uint32_t f0Value = mixer_ctl_get_value(f0CalibrationControl, i * 2);
                if (f0Value <= 0xfffffed3) {
                    f0Values[i] = ((f0Value * 2 + 300) << 11);
                }
            }
        }
    }
}

// References: OplusSpeakerTfa98xx::payloadSPConfig and tfa98xx_adsp_send_calib_values() from tfa98xx_v6.c
void SpeakerTfa98xx::payloadSPConfig(uint8_t** payload, size_t* size, uint32_t miid) {
    struct apm_module_param_data_t* header = nullptr;
    uint8_t* payloadInfo = nullptr;
    size_t payloadSize = 0, padBytes = 0;

    *payload = nullptr;
    *size = 0;

    // Check if calibration info is valid
    if (caliInfo.empty() || caliInfo.size() != speakerCount) {
        PAL_ERR(LOG_TAG, "Invalid calibration info size: %zu, expected: %d", caliInfo.size(),
                speakerCount);
        return;
    }

    // Calculate total payload size including header and padding
    payloadSize = sizeof(struct apm_module_param_data_t) + 10;  // 10 bytes for calibration data
    padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);

    // Allocate memory for payload including padding
    payloadInfo = (uint8_t*)calloc(1, payloadSize + padBytes);
    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "Failed to allocate payload memory");
        return;
    }

    // Setup header
    header = (struct apm_module_param_data_t*)payloadInfo;
    header->module_instance_id = miid;
    header->param_id = TFADSP_RX_SET_COMMAND;
    header->error_code = 0x0;
    header->param_size = payloadSize - sizeof(struct apm_module_param_data_t);

    // Setup calibration data after header
    uint8_t* calData = payloadInfo + sizeof(struct apm_module_param_data_t);
    calData[0] = 0x00;  // Command byte
    calData[1] = 0x81;  // Module ID
    calData[2] = 0x05;  // Parameter ID

    // Process each speaker's calibration data
    for (const auto& info : caliInfo) {
        if (info.dsp_imp == 0) {
            PAL_ERR(LOG_TAG, "Invalid DSP impedance for speaker %d", info.dev_idx);
            free(payloadInfo);
            return;
        }

        // Calculate array index based on speaker index (4 for left, 7 for right)
        int baseIdx = (info.dev_idx == 0) ? 3 : 6;

        // Store impedance value in big-endian format
        calData[baseIdx + 0] = (info.dsp_imp >> 16) & 0xFF;
        calData[baseIdx + 1] = (info.dsp_imp >> 8) & 0xFF;
        calData[baseIdx + 2] = info.dsp_imp & 0xFF;

        PAL_INFO(LOG_TAG, "Speaker %d: impedance=0x%x", info.dev_idx, info.dsp_imp);
    }

    // For mono case, copy primary channel data to secondary
    if (speakerCount == 1) {
        memcpy(&calData[6], &calData[3], 3);
    }

    // Handle F0 calibration if supported
    if (isF0CalibrationSupported) {
        // Allocate new payload for F0 calibration
        uint8_t* f0PayloadInfo = nullptr;
        size_t f0PayloadSize = sizeof(struct apm_module_param_data_t) + 6;  // 6 bytes for F0 data
        size_t f0PadBytes = PAL_PADDING_8BYTE_ALIGN(f0PayloadSize);

        f0PayloadInfo = (uint8_t*)calloc(1, f0PayloadSize + f0PadBytes);
        if (f0PayloadInfo) {
            struct apm_module_param_data_t* f0Header =
                    (struct apm_module_param_data_t*)f0PayloadInfo;
            f0Header->module_instance_id = miid;
            f0Header->param_id = TFADSP_RX_GET_RESULT;
            f0Header->error_code = 0x0;
            f0Header->param_size = f0PayloadSize - sizeof(struct apm_module_param_data_t);

            uint8_t* f0Data = f0PayloadInfo + sizeof(struct apm_module_param_data_t);
            f0Data[0] = 0x00;
            f0Data[1] = 0x81;
            f0Data[2] = 0x1c;

            for (uint32_t i = 0; i < powerAmpCount && i < MAX_PA_COUNT; i++) {
                if (f0Values[i] != 0) {
                    f0Data[3 + i * 3] = (f0Values[i] >> 16) & 0xFF;
                    f0Data[4 + i * 3] = (f0Values[i] >> 8) & 0xFF;
                    f0Data[5 + i * 3] = f0Values[i] & 0xFF;
                    PAL_INFO(LOG_TAG, "F0 calibration value for PA %d: 0x%x", i, f0Values[i]);
                }
            }

            // Free F0 payload after use - in real implementation, this would be sent to DSP
            free(f0PayloadInfo);
        }
    }

    *size = payloadSize + padBytes;
    *payload = payloadInfo;
}

FILE* SpeakerTfa98xx::openDeviceFile(uint8_t address, const char* type) {
    char path[128] = {0};
    if (!type) {
        PAL_ERR(LOG_TAG, "Invalid type parameter");
        return nullptr;
    }
    
    snprintf(path, sizeof(path), "/proc/tfa98xx-%x/%s", address, type);
    FILE* fp = fopen(path, "r");
    if (!fp) {
        PAL_INFO(LOG_TAG, "Failed to open %s", path);
    }
    return fp;
}

long SpeakerTfa98xx::readDeviceFile(char* buffer, size_t size, FILE* fp) {
    if (!buffer || !fp || size == 0) return 0;
    size_t bytes = fread(buffer, 1, size - 1, fp);
    if (bytes > 0) buffer[bytes] = '\0';
    return bytes;
}
