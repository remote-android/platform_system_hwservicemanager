#ifndef ANDROID_HARDWARE_MANAGER_V1_0_SERVICEMANAGER_H
#define ANDROID_HARDWARE_MANAGER_V1_0_SERVICEMANAGER_H

#include <android/hidl/manager/1.0/IServiceManager.h>
#include <hidl/Status.h>
#include <hidl/MQDescriptor.h>
#include <map>
#include <unordered_map>

#include "HidlService.h"

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
using ::android::hidl::manager::V1_0::IServiceNotification;
using ::android::sp;

struct ServiceManager : public IServiceManager {
    // Methods from ::android::hidl::manager::V1_0::IServiceManager follow.
    Return<void> get(const hidl_string& fqName,
                     const hidl_string& name,
                     get_cb _hidl_cb)  override;
    Return<bool> add(const hidl_vec<hidl_string>& interfaceChain,
                     const hidl_string& name,
                     const sp<IBinder>& service) override;

    Return<void> list(list_cb _hidl_cb) override;
    Return<void> listByInterface(const hidl_string& fqInstanceName,
                                 listByInterface_cb _hidl_cb) override;

    Return<bool> registerForNotifications(const hidl_string& fqName,
                                          const hidl_string& name,
                                          const sp<IServiceNotification>& callback) override;

private:

    using InstanceMap = std::multimap<
            std::string, // instance name e.x. "manager"
            std::unique_ptr<HidlService>
        >;

    struct PackageInterfaceMap {
        InstanceMap &getInstanceMap();
        const InstanceMap &getInstanceMap() const;

        /**
         * Finds a HidlService which supports the desired version. If none,
         * returns nullptr. HidlService::getService() might also be nullptr
         * if there are registered IServiceNotification objects for it. Return
         * value should be treated as a temporary reference.
         */
        HidlService *lookupSupporting(
            const std::string &name,
            const hidl_version &version);
        const HidlService *lookupSupporting(
            const std::string &name,
            const hidl_version &version) const;
        /**
         * Finds a HidlService which is exactly the desired version. If none,
         * returns nullptr. HidlService::getService() might also be nullptr
         * if there are registered IServiceNotification objects for it. Return
         * value should be treated as a temporary reference.
         */
        HidlService *lookupExact(
            const std::string &name,
            const hidl_version &version);

        void insertService(std::unique_ptr<HidlService> &&service);

        void addPackageListener(sp<IServiceNotification> listener);

    private:
        void sendPackageRegistrationNotification(
            const hidl_string &fqName,
            const hidl_string &instanceName) const;

        InstanceMap mInstanceMap{};

        std::vector<sp<IServiceNotification>> mPackageListeners{};
    };

    /**
     * Access to this map doesn't need to be locked, since hwservicemanager
     * is single-threaded.
     *
     * e.x.
     * mServiceMap["android.hidl.manager::IServiceManager"]["manager"]
     *     -> HidlService object
     */
    std::unordered_map<
        std::string, // package::interface e.x. "android.hidl.manager::IServiceManager"
        PackageInterfaceMap
    > mServiceMap;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace manager
}  // namespace hidl
}  // namespace android

#endif  // ANDROID_HARDWARE_MANAGER_V1_0_SERVICEMANAGER_H
