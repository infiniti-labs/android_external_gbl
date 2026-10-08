// SPDX-License-Identifier: Apache-2.0

#include <android-base/logging.h>
#include <sys/system_properties.h>

#include <algorithm>
#include <string_view>

namespace {
bool override_property(const char* name, const char* value) {
    const prop_info* property = __system_property_find(name);
    const unsigned int length = std::char_traits<char>::length(value);
    const int result = property
            ? __system_property_update(const_cast<prop_info*>(property), value, length)
            : __system_property_add(name, std::char_traits<char>::length(name), value, length);
    if (result != 0) LOG(ERROR) << "GBL: could not initialize " << name;
    return result == 0;
}
}

void vendor_load_properties() {
#ifdef GBL_VERIFIED_BOOT_HASH
    constexpr std::string_view hash = GBL_VERIFIED_BOOT_HASH;
    const bool valid = hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
    if (!valid) {
        LOG(ERROR) << "GBL: invalid configured verified boot hash";
        return;
    }
    if (override_property("ro.boot.vbmeta.digest", GBL_VERIFIED_BOOT_HASH)) {
        override_property("ro.boot.vbmeta.hash_alg", "sha256");
    }
#endif
}
