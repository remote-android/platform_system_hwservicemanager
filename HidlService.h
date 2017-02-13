#ifndef ANDROID_HARDWARE_MANAGER_V1_0_HIDLSERVICE_H
#define ANDROID_HARDWARE_MANAGER_V1_0_HIDLSERVICE_H

#include <set>

#include <android/hidl/manager/1.0/IServiceManager.h>
#include <hidl/Status.h>
#include <hidl/MQDescriptor.h>

namespace android {
namespace hidl {
namespace manager {
namespace V1_0 {
namespace implementation {

using ::android::hardware::hidl_vec;
using ::android::hardware::hidl_string;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hidl::base::V1_0::IBase;
using ::android::hidl::manager::V1_0::IServiceManager;
using ::android::sp;

struct HidlService {
    HidlService(const std::string &interfaceName,
                const std::string &instanceName,
                const sp<IBase> &service);

    /**
     * Note, getService() can be nullptr. This is because you can have a HidlService
     * with registered IServiceNotification objects but no service registered yet.
     */
    sp<IBase> getService() const;
    void setService(sp<IBase> service);
    const std::string &getInterfaceName() const;
    const std::string &getInstanceName() const;

    void addListener(const sp<IServiceNotification> &listener);
    void registerPassthroughClient(pid_t pid);

    std::string string() const; // e.x. "android.hidl.manager@1.0::IServiceManager/manager"
    const std::set<pid_t> &getPassthroughClients() const;

private:
    void sendRegistrationNotifications();

    const std::string                     mInterfaceName; // e.x. "android.hardware.manager@1.0::IServiceManager"
    const std::string                     mInstanceName;  // e.x. "manager"
    sp<IBase>                             mService;

    std::vector<sp<IServiceNotification>> mListeners{};
    std::set<pid_t>                       mPassthroughClients{};
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace manager
}  // namespace hidl
}  // namespace android

#endif // ANDROID_HARDWARE_MANAGER_V1_0_HIDLSERVICE_H
