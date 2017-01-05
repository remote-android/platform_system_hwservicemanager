#ifndef ANDROID_HIDL_TOKEN_V1_0_TOKENMANAGER_H
#define ANDROID_HIDL_TOKEN_V1_0_TOKENMANAGER_H

#include <android/hidl/token/1.0/ITokenManager.h>
#include <chrono>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>
#include <unordered_map>

namespace android {
namespace hidl {
namespace token {
namespace V1_0 {
namespace implementation {

using ::android::hidl::base::V1_0::IBase;
using ::android::hidl::token::V1_0::ITokenManager;
using ::android::hardware::hidl_array;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;

struct TokenManager : public ITokenManager {
    // Methods from ::android::hidl::token::V1_0::ITokenManager follow.
    Return<uint64_t> createToken(const sp<IBase>& store) override;
    Return<bool> unregister(uint64_t token) override;
    Return<sp<IBase>> get(uint64_t token) override;

private:
    uint64_t generateToken();

    // TODO(b/33843007): periodic pruning of mMap
    std::unordered_map<uint64_t, wp<IBase>> mMap;

    uint64_t mTokenIndex;
    uint64_t mSalt = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::system_clock::now().time_since_epoch()).count();
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace token
}  // namespace hidl
}  // namespace android

#endif  // ANDROID_HIDL_TOKEN_V1_0_TOKENMANAGER_H
