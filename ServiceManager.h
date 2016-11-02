#ifndef HIDL_GENERATED_android_hardware_manager_V1_0_ServiceManager_H_
#define HIDL_GENERATED_android_hardware_manager_V1_0_ServiceManager_H_

#include <android/hidl/manager/1.0/IServiceManager.h>
#include <hidl/Status.h>
#include <hidl/MQDescriptor.h>
#include <map>

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

using Version = ::android::hidl::manager::V1_0::IServiceManager::Version;

struct HidlService {
      HidlService(const std::string &name,
                  const sp<IBinder>& service,
                  const Version& version,
                  const std::string &metaVersion)
      : mName(name),
        mVersion(version.major, version.minor),
        mMetaVersion(metaVersion),
        mService(service) {}

      sp<IBinder> getService() const {
          return mService;
      }

      void setService(const sp<IBinder>& service) {
          mService = service;
      }

      const hidl_version& getVersion() const {
          return mVersion;
      }

      bool supportsVersion(hidl_version version) {
          if (version.get_major() == mVersion.get_major() &&
                  version.get_minor() <= mVersion.get_minor()) {
              return true;
          }
          // TODO remove log
          ALOGE("Service doesn't support version %u.%u", version.get_major(), version.get_minor());
          return false;
      }

private:
      const std::string                     mName;
      const hidl_version                    mVersion;
      const std::string                     mMetaVersion;
      sp<IBinder>                           mService;
};

struct ServiceManager : public IServiceManager {
    // Methods from ::android::hidl::manager::V1_0::IServiceManager follow.
    Return<void> get(const hidl_string& name, const Version& version, get_cb _hidl_cb)  override;
    Return<bool> add(const hidl_string& name, const sp<IBinder>& service, const Version& version)  override;

private:

    // Access to this map doesn't need to be locked, since hwservicemanager
    // is single-threaded.
    std::multimap<std::string, std::unique_ptr<HidlService>> mServiceMap;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace manager
}  // namespace hidl
}  // namespace android

#endif  // HIDL_GENERATED_android_hardware_manager_V1_0_ServiceManager_H_
