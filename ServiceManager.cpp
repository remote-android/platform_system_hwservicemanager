#include "ServiceManager.h"

#include <hidl/HidlSupport.h>

namespace android {
namespace hidl {
namespace manager {
namespace V1_0 {
namespace implementation {

// Methods from ::android::hidl::manager::V1_0::IServiceManager follow.
Return<void> ServiceManager::get(const hidl_string& name, const Version& version, get_cb _hidl_cb)  {
    const std::string name_str = name.c_str();
    auto numEntries = mServiceMap.count(name_str);
    auto it = mServiceMap.find(name_str);

    hidl_version hidlVersion (version.major, version.minor);

    while (numEntries > 0) {
        if (it->second->supportsVersion(hidlVersion)) {
            _hidl_cb(it->second->getService());
            return Void();
        }
        --numEntries;
        ++it;
    }

    _hidl_cb(nullptr);
    return Void();
}

Return<bool> ServiceManager::add(const hidl_string& name, const sp<IBinder>& service, const Version& version)  {
    const std::string name_str = name.c_str();
    size_t numEntries = mServiceMap.count(name_str);
    auto service_iter = mServiceMap.find(name_str);
    bool replaced = false;

    const hidl_version hidlVersion (version.major, version.minor);

    while (numEntries > 0) {
        if (service_iter->second->getVersion() == hidlVersion) {
            // Just update service reference
            service_iter->second->setService(service);
            replaced = true;
            break;
        }
        --numEntries;
        ++service_iter;
    }
    if (!replaced) {
        mServiceMap.insert({name_str, std::unique_ptr<HidlService>(
                new HidlService(name_str, service, version, ""))});
    }

    // TODO link to death so we know when it dies

    return true;
}

} // namespace implementation
}  // namespace V1_0
}  // namespace manager
}  // namespace hidl
}  // namespace android
