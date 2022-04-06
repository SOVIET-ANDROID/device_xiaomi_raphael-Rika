#
# Copyright (C) 2020-2021 Raphielscape LLC. and Haruka LLC.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

PRODUCT_HARDWARE := raphael

include device/xiaomi/raphael/device-common.mk

DEVICE_PACKAGE_OVERLAYS += device/xiaomi/raphael/raphael/overlay

# Audio configs
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/raphael/audio/audio_platform_info.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_platform_info.xml \
    $(LOCAL_PATH)/raphael/audio/mixer_paths_overlay_dynamic.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_overlay_dynamic.xml \
    $(LOCAL_PATH)/raphael/audio/mixer_paths_overlay_static.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_overlay_static.xml \
    $(LOCAL_PATH)/raphael/audio/mixer_paths_tavil.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_tavil.xml

# per device
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/raphael/init.hardware.raphael.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.qcom.raphael.rc

# Camxoverride settings
PRODUCT_COPY_FILES += \
     $(LOCAL_PATH)/raphael/camera/camxoverridesettings.txt:$(TARGET_COPY_OUT_VENDOR)/etc/camera/camxoverridesettings.txt

# Settings overlay packages for regulatory_info
PRODUCT_PACKAGES += \
    SettingsOverlayM1903F11G

# Hentai Motor
PRODUCT_PACKAGES += \
    HentaiMotor

$(call inherit-product-if-exists, vendor/xiaomi/raphael/raphael-vendor.mk)
