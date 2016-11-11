#ifndef ANDROID_HARDWARE_MANAGER_V1_0_HIDLSERVICE_H
#define ANDROID_HARDWARE_MANAGER_V1_0_HIDLSERVICE_H

#include <android/hidl/manager/1.0/IServiceManager.h>
#include <hidl/Status.h>
#include <hidl/MQDescriptor.h>
#include <map>
#include <unordered_map>

namespace android {
namespace hidl {
namespace manager {
namespace V1_0 {
namespace implementation {

using ::android::hardware::hidl_vec;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_version;
using ::android::hardware::IBinder;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hidl::manager::V1_0::IServiceManager;
using ::android::sp;

struct HidlService {
    HidlService(const std::string &package,
                const std::string &interface,
                const std::string &name,
                const hidl_version &version,
                const sp<IBinder> &service);

    /**
     * Note, getService() can be nullptr. This is because you can have a HidlService
     * with registered IServiceNotification objects but no service registered yet.
     */
    sp<IBinder> getService() const;
    void setService(sp<IBinder> service);
    const std::string &getPackage() const;
    const std::string &getInterface() const;
    const std::string &getName() const;
    const hidl_version &getVersion() const;

    bool supportsVersion(const hidl_version &version) const;

    void addListener(const sp<IServiceNotification> &listener);

    std::string fqName() const; // e.x. "android.hardware.manager@1.0::IServiceManager"
    std::string packageInterface() const; // e.x. "android.hidl.manager::IServiceManager"
    std::string string() const; // e.x. "android.hidl.manager@1.0::IServiceManager/manager"

    static std::unique_ptr<HidlService> make(
        const std::string &fqName,
        const std::string &name,
        const sp<IBinder>& service = nullptr);

private:
    void sendRegistrationNotifications() const;

    const std::string                     mPackage;      // e.x. "android.hidl.manager"
    const std::string                     mInterface;    // e.x. "IServiceManager"
    const std::string                     mInstanceName; // e.x. "manager"
    const hidl_version                    mVersion;      // e.x. { 1, 0 }
    sp<IBinder>                           mService;

    std::vector<sp<IServiceNotification>> mListeners{};
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace manager
}  // namespace hidl
}  // namespace android

#endif // ANDROID_HARDWARE_MANAGER_V1_0_HIDLSERVICE_H