#!/bin/bash
#
# Copyright (C) 2016 The CyanogenMod Project
# Copyright (C) 2017-2021 The LineageOS Project
# Copyright (C) 2021 The Raphielscape LLC. and Haruka LLC. Project
#
# SPDX-License-Identifier: Apache-2.0
#

set -e

DEVICE_COMMON=msmnile-common
VENDOR=xiaomi

# Load extract_utils and do some sanity checks
MY_DIR="${BASH_SOURCE%/*}"
if [[ ! -d "${MY_DIR}" ]]; then MY_DIR="${PWD}"; fi

ANDROID_ROOT="${MY_DIR}/../../.."

HELPER="${ANDROID_ROOT}/vendor/hentai/build/tools/extract_utils.sh"
if [ ! -f "${HELPER}" ]; then
    echo "Unable to find helper script at ${HELPER}"
    exit 1
fi
source "${HELPER}"

# Initialize the helper
setup_vendor "${DEVICE_COMMON}" "${VENDOR}" "${ANDROID_ROOT}" true

# Warning headers and guards
write_headers "raphael"

write_makefiles "${MY_DIR}/proprietary-files-coral.txt" true
write_makefiles "${MY_DIR}/proprietary-files.txt" true

# Finish
write_footers

if [ ! -z "${DEVICE}" ] && [ -s "${MY_DIR}/${DEVICE}/proprietary-files.txt" ]; then
    # Reinitialize the helper for device
    setup_vendor "${DEVICE}" "${VENDOR}" "${ANDROID_ROOT}" false

    # Copyright headers and guards
    write_headers

    # The standard device blobs
    write_makefiles "${MY_DIR}/${DEVICE}/proprietary-files.txt" true

    # Finish
    write_footers
fi
