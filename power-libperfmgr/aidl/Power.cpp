/*
 * Copyright (C) 2020 The Android Open Source Project
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

#define ATRACE_TAG (ATRACE_TAG_POWER | ATRACE_TAG_HAL)
#define LOG_TAG "android.hardware.power-service.pixel-libperfmgr"

#include "Power.h"

#include <mutex>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include <utils/Log.h>
#include <utils/Trace.h>

#include <linux/input.h>

#include "disp-power/DisplayLowPower.h"

namespace {
int open_ts_input() {
    int fd = -1;
    DIR *dir = opendir("/dev/input");

    if (dir != NULL) {
        struct dirent *ent;

        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_type == DT_CHR) {
                char absolute_path[PATH_MAX] = {0};
                char name[80] = {0};

                strcpy(absolute_path, "/dev/input/");
                strcat(absolute_path, ent->d_name);

                fd = open(absolute_path, O_RDWR);
                if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), &name) > 0) {
                    if (strcmp(name, "fts") == 0 || strcmp(name, "fts_521") == 0 ||
                        strcmp(name, "fts_ts") == 0 || strcmp(name, "ft5x46") == 0 ||
                        strcmp(name, "goodix_ts") == 0)
                        break;
                }

                close(fd);
                fd = -1;
            }
        }

        closedir(dir);
    }

    return fd;
}
}  // anonymous namespace

namespace aidl {
namespace google {
namespace hardware {
namespace power {
namespace impl {
namespace pixel {

static constexpr int kInputEventWakeupModeOff = 4;
static constexpr int kInputEventWakeupModeOn = 5;

using ::aidl::android::hardware::power::Mode;

bool isDeviceSpecificModeSupported(Mode type, bool *_aidl_return) {
    switch (type) {
        case Mode::DOUBLE_TAP_TO_WAKE:
            *_aidl_return = true;
            return true;
        default:
            return false;
    }
}

bool setDeviceSpecificMode(Mode type, bool enabled) {
    switch (type) {
        case Mode::DOUBLE_TAP_TO_WAKE: {
            int fd = open_ts_input();
            if (fd == -1) {
                LOG(WARNING)
                    << "DT2W won't work because no supported touchscreen input devices were found";
                return false;
            }
            struct input_event ev;
            ev.type = EV_SYN;
            ev.code = SYN_CONFIG;
            ev.value = enabled ? kInputEventWakeupModeOn : kInputEventWakeupModeOff;
            write(fd, &ev, sizeof(ev));
            close(fd);
        }
            return true;
        default:
            return false;
    }
}

constexpr char kPowerHalStateProp[] = "vendor.powerhal.state";
constexpr char kPowerHalAudioProp[] = "vendor.powerhal.audio";
constexpr char kPowerHalRenderingProp[] = "vendor.powerhal.rendering";

Power::Power(std::shared_ptr<HintManager> hm, std::shared_ptr<DisplayLowPower> dlpw)
    : mHintManager(hm),
      mDisplayLowPower(dlpw),
      mInteractionHandler(nullptr),
      mVRModeOn(false),
      mSustainedPerfModeOn(false) {
    mInteractionHandler = std::make_unique<InteractionHandler>(mHintManager);
    mInteractionHandler->Init();

    std::string state = ::android::base::GetProperty(kPowerHalStateProp, "");
    if (state == "SUSTAINED_PERFORMANCE") {
        ALOGI("Initialize with SUSTAINED_PERFORMANCE on");
        mHintManager->DoHint("SUSTAINED_PERFORMANCE");
        mSustainedPerfModeOn = true;
    } else if (state == "VR") {
        ALOGI("Initialize with VR on");
        mHintManager->DoHint(state);
        mVRModeOn = true;
    } else if (state == "VR_SUSTAINED_PERFORMANCE") {
        ALOGI("Initialize with SUSTAINED_PERFORMANCE and VR on");
        mHintManager->DoHint("VR_SUSTAINED_PERFORMANCE");
        mSustainedPerfModeOn = true;
        mVRModeOn = true;
    } else {
        ALOGI("Initialize PowerHAL");
    }

    state = ::android::base::GetProperty(kPowerHalAudioProp, "");
    if (state == "AUDIO_STREAMING_LOW_LATENCY") {
        ALOGI("Initialize with AUDIO_LOW_LATENCY on");
        mHintManager->DoHint(state);
    }

    state = ::android::base::GetProperty(kPowerHalRenderingProp, "");
    if (state == "EXPENSIVE_RENDERING") {
        ALOGI("Initialize with EXPENSIVE_RENDERING on");
        mHintManager->DoHint("EXPENSIVE_RENDERING");
    }

    // Now start to take powerhint
    ALOGI("PowerHAL ready to process hints");
}

ndk::ScopedAStatus Power::setMode(Mode type, bool enabled) {
    LOG(DEBUG) << "Power setMode: " << toString(type) << " to: " << enabled;
    ATRACE_INT(toString(type).c_str(), enabled);
    if (setDeviceSpecificMode(type, enabled)) {
        return ndk::ScopedAStatus::ok();
    }
    switch (type) {
        case Mode::LOW_POWER:
            mDisplayLowPower->SetDisplayLowPower(enabled);
            if (enabled) {
                mHintManager->DoHint(toString(type));
            } else {
                mHintManager->EndHint(toString(type));
            }
            break;
        case Mode::SUSTAINED_PERFORMANCE:
            if (enabled && !mSustainedPerfModeOn) {
                if (!mVRModeOn) {  // Sustained mode only.
                    mHintManager->DoHint("SUSTAINED_PERFORMANCE");
                } else {  // Sustained + VR mode.
                    mHintManager->EndHint("VR");
                    mHintManager->DoHint("VR_SUSTAINED_PERFORMANCE");
                }
                mSustainedPerfModeOn = true;
            } else if (!enabled && mSustainedPerfModeOn) {
                mHintManager->EndHint("VR_SUSTAINED_PERFORMANCE");
                mHintManager->EndHint("SUSTAINED_PERFORMANCE");
                if (mVRModeOn) {  // Switch back to VR Mode.
                    mHintManager->DoHint("VR");
                }
                mSustainedPerfModeOn = false;
            }
            break;
        case Mode::VR:
            if (enabled && !mVRModeOn) {
                if (!mSustainedPerfModeOn) {  // VR mode only.
                    mHintManager->DoHint("VR");
                } else {  // Sustained + VR mode.
                    mHintManager->EndHint("SUSTAINED_PERFORMANCE");
                    mHintManager->DoHint("VR_SUSTAINED_PERFORMANCE");
                }
                mVRModeOn = true;
            } else if (!enabled && mVRModeOn) {
                mHintManager->EndHint("VR_SUSTAINED_PERFORMANCE");
                mHintManager->EndHint("VR");
                if (mSustainedPerfModeOn) {  // Switch back to sustained Mode.
                    mHintManager->DoHint("SUSTAINED_PERFORMANCE");
                }
                mVRModeOn = false;
            }
            break;
        case Mode::LAUNCH:
            if (mVRModeOn || mSustainedPerfModeOn) {
                break;
            }
            [[fallthrough]];
        case Mode::DOUBLE_TAP_TO_WAKE:
            [[fallthrough]];
        case Mode::FIXED_PERFORMANCE:
            [[fallthrough]];
        case Mode::EXPENSIVE_RENDERING:
            [[fallthrough]];
        case Mode::INTERACTIVE:
            [[fallthrough]];
        case Mode::DEVICE_IDLE:
            [[fallthrough]];
        case Mode::DISPLAY_INACTIVE:
            [[fallthrough]];
        case Mode::AUDIO_STREAMING_LOW_LATENCY:
            [[fallthrough]];
        case Mode::CAMERA_STREAMING_SECURE:
            [[fallthrough]];
        case Mode::CAMERA_STREAMING_LOW:
            [[fallthrough]];
        case Mode::CAMERA_STREAMING_MID:
            [[fallthrough]];
        case Mode::CAMERA_STREAMING_HIGH:
            [[fallthrough]];
        default:
            if (enabled) {
                mHintManager->DoHint(toString(type));
            } else {
                mHintManager->EndHint(toString(type));
            }
            break;
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::isModeSupported(Mode type, bool *_aidl_return) {
    bool supported = mHintManager->IsHintSupported(toString(type));
    // LOW_POWER handled insides PowerHAL specifically
    if (type == Mode::LOW_POWER) {
        supported = true;
    }
    LOG(INFO) << "Power mode " << toString(type) << " isModeSupported: " << supported;
    if (isDeviceSpecificModeSupported(type, _aidl_return)) {
        return ndk::ScopedAStatus::ok();
    }
    *_aidl_return = supported;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::setBoost(Boost type, int32_t durationMs) {
    LOG(DEBUG) << "Power setBoost: " << toString(type) << " duration: " << durationMs;
    ATRACE_INT(toString(type).c_str(), durationMs);
    switch (type) {
        case Boost::INTERACTION:
            if (mVRModeOn || mSustainedPerfModeOn) {
                break;
            }
            mInteractionHandler->Acquire(durationMs);
            break;
        case Boost::DISPLAY_UPDATE_IMMINENT:
            [[fallthrough]];
        case Boost::ML_ACC:
            [[fallthrough]];
        case Boost::AUDIO_LAUNCH:
            [[fallthrough]];
        case Boost::CAMERA_LAUNCH:
            [[fallthrough]];
        case Boost::CAMERA_SHOT:
            [[fallthrough]];
        default:
            if (mVRModeOn || mSustainedPerfModeOn) {
                break;
            }
            if (durationMs > 0) {
                mHintManager->DoHint(toString(type), std::chrono::milliseconds(durationMs));
            } else if (durationMs == 0) {
                mHintManager->DoHint(toString(type));
            } else {
                mHintManager->EndHint(toString(type));
            }
            break;
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::isBoostSupported(Boost type, bool *_aidl_return) {
    bool supported = mHintManager->IsHintSupported(toString(type));
    LOG(INFO) << "Power boost " << toString(type) << " isBoostSupported: " << supported;
    *_aidl_return = supported;
    return ndk::ScopedAStatus::ok();
}

constexpr const char *boolToString(bool b) {
    return b ? "true" : "false";
}

binder_status_t Power::dump(int fd, const char **, uint32_t) {
    std::string buf(::android::base::StringPrintf(
            "HintManager Running: %s\n"
            "VRMode: %s\n"
            "SustainedPerformanceMode: %s\n",
            boolToString(mHintManager->IsRunning()), boolToString(mVRModeOn),
            boolToString(mSustainedPerfModeOn)));
    // Dump nodes through libperfmgr
    mHintManager->DumpToFd(fd);
    if (!::android::base::WriteStringToFd(buf, fd)) {
        PLOG(ERROR) << "Failed to dump state to fd";
    }
    fsync(fd);
    return STATUS_OK;
}

}  // namespace pixel
}  // namespace impl
}  // namespace power
}  // namespace hardware
}  // namespace google
}  // namespace aidl
