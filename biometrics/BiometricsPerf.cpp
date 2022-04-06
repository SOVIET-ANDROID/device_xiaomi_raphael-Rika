/*
 * Copyright (C) 2021 Raphielscape LLC.
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

#define LOG_TAG "BiometricsPerf"

#include <cinttypes>

#include <utils/Log.h>
#include <utils/Mutex.h>

#include <aidl/android/hardware/power/Boost.h>
#include <aidl/android/hardware/power/IPower.h>
#include <android/binder_manager.h>

#include "BiometricsPerf.h"

// Protect gPowerHal_Aidl_
static std::shared_ptr<aidl::android::hardware::power::IPower> gPowerHal_Aidl_;
static std::mutex gPowerHalMutex;
static constexpr int kDefaultBoostDurationMs = 2500;
static constexpr int kBoostOff = -1;

static const std::string kInstance =
        std::string(aidl::android::hardware::power::IPower::descriptor) + "/default";

enum hal_version {
    NONE,
    AIDL,
};

// Connnect PowerHAL
static hal_version connectPowerHalLocked() {
    static bool gPowerHalAidlExists = true;

    if (!gPowerHalAidlExists) {
        return NONE;
    }

    if (gPowerHalAidlExists) {
        // (re)connect if handle is null
        if (!gPowerHal_Aidl_) {
            ndk::SpAIBinder pwBinder =
                    ndk::SpAIBinder(AServiceManager_getService(kInstance.c_str()));
            gPowerHal_Aidl_ = aidl::android::hardware::power::IPower::fromBinder(pwBinder);
        }
        if (gPowerHal_Aidl_) {
            ALOGV("Successfully connected to Power Hal Aidl service.");
            return AIDL;
        } else {
            // no more try on this handle
            gPowerHalAidlExists = false;
        }
    }

    return NONE;
}

bool fingerprint_perf_lock() {
    std::lock_guard<std::mutex> lock(gPowerHalMutex);
    switch (connectPowerHalLocked()) {
        case NONE:
            return false;
        case AIDL: {
            auto ret = gPowerHal_Aidl_->setBoost(aidl::android::hardware::power::Boost::INTERACTION,
                                                 kDefaultBoostDurationMs);
            if (!ret.isOk()) {
                std::string err = ret.getDescription();
                ALOGE("Failed to set power hint. Error: %s", err.c_str());
                gPowerHal_Aidl_ = nullptr;
                return false;
            }
            return true;
        }
        default:
            ALOGE("Unknown HAL state");
            return false;
    }
}

bool fingerprint_perf_release() {
    std::lock_guard<std::mutex> lock(gPowerHalMutex);
    switch (connectPowerHalLocked()) {
        case NONE:
            return false;
        case AIDL: {
            auto ret = gPowerHal_Aidl_->setBoost(aidl::android::hardware::power::Boost::INTERACTION,
                                                 kBoostOff);
            if (!ret.isOk()) {
                std::string err = ret.getDescription();
                ALOGE("Failed to set power hint. Error: %s", err.c_str());
                gPowerHal_Aidl_ = nullptr;
                return false;
            }
            return true;
        }
        default:
            ALOGE("Unknown HAL state");
            return false;
    }
}
