#ifndef ANDROID_HARDWARE_MANAGER_V1_0_SERVICEMANAGER_H
#define ANDROID_HARDWARE_MANAGER_V1_0_SERVICEMANAGER_H

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

struct ServiceManager : public IServiceManager {
    // Methods from ::android::hidl::manager::V1_0::IServiceManager follow.
    Return<void> get(const hidl_string& fqName,
                     const hidl_string& name,
                     get_cb _hidl_cb)  override;
    Return<bool> add(const hidl_vec<hidl_string>& interfaceChain,
                     const hidl_string& name,
                     const sp<IBinder>& service) override;

    Return<void> list(list_cb _hidl_cb) override;
    Return<void> listByInterface(const hidl_string& fqName,
                                 listByInterface_cb _hidl_cb) override;

    struct HidlService {
        HidlService(const std::string &package,
                    const std::string &interface,
                    const std::string &name,
                    const hidl_version &version,
                    const sp<IBinder> &service);

        sp<IBinder> getService() const;
        void setService(sp<IBinder> service);
        const std::string &getPackage() const;
        const std::string &getInterface() const;
        const std::string &getName() const;
        const hidl_version &getVersion() const;

        bool supportsVersion(const hidl_version &version) const;

        std::string iface() const; // e.x. "android.hidl.manager::IServiceManager"
        std::string string() const; // e.x. "android.hidl.manager@1.0::IServiceManager/manager"

        static std::unique_ptr<HidlService> make(
            const std::string &fqName,
            const std::string &name,
            const sp<IBinder>& service = nullptr);

    private:
        const std::string                     mPackage;  // e.x. "android.hidl.manager"
        const std::string                     mInterface; // e.x. "IServiceManager"
        const std::string                     mName;     // e.x. "manager"
        const hidl_version                    mVersion;  // e.x. { 1, 0 }
        sp<IBinder>                           mService;
    };

private:

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
        std::multimap<
            std::string, // name e.x. "manager"
            std::unique_ptr<HidlService>
        >
    > mServiceMap;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace manager
}  // namespace hidl
}  // namespace android

#endif  // ANDROID_HARDWARE_MANAGER_V1_0_SERVICEMANAGER_H
