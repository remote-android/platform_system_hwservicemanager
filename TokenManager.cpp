#define LOG_TAG "hwservicemanager"

#include "TokenManager.h"

#include <android-base/logging.h>
#include <functional>

namespace android {
namespace hidl {
namespace token {
namespace V1_0 {
namespace implementation {

// Methods from ::android::hidl::token::V1_0::ITokenManager follow.
Return<uint64_t> TokenManager::createToken(const sp<IBase>& store) {
    uint64_t token;

    do {
        token = generateToken();
    } while (mMap.find(token) != mMap.end()); // unlikely to ever happen

    mMap[token] = wp<IBase>(store);
    return token;
}

Return<bool> TokenManager::unregister(uint64_t token) {
    auto it = mMap.find(token);

    if (it == mMap.end()) {
        return false;
    }

    mMap.erase(it);
    return true;
}

Return<void> TokenManager::get(uint64_t token, get_cb _hidl_cb) {
    const auto it = mMap.find(token);

    if (it == mMap.end()) {
        _hidl_cb(nullptr);
        return Void();
    }

    const wp<IBase> &wp = it->second;
    const sp<IBase> strong = wp.promote();

    if (strong == nullptr) {
        mMap.erase(it);
        _hidl_cb(nullptr);
        return Void();
    }

    _hidl_cb(strong);
    return Void();
}

uint64_t TokenManager::generateToken() {
    // TODO(b/33842662): pending security review
    // TODO(b/33842662): use cryptographic hash function, this is a placeholder
    return std::hash<uint64_t>{}(mSalt + mTokenIndex++);
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace token
}  // namespace hidl
}  // namespace android
