#
# Copyright (C) 2021 Raphielscape LLC. and Haruka LLC.
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

TARGET_SCREEN_DENSITY := 440
TARGET_RECOVERY_UI_MARGIN_HEIGHT := 90

TARGET_KERNEL_CONFIG := raphael_defconfig

# Assert
TARGET_OTA_ASSERT_DEVICE := raphael

# Releasetools
TARGET_RELEASETOOLS_EXTENSIONS := device/xiaomi/raphael

include device/xiaomi/raphael/BoardConfig-common.mk
