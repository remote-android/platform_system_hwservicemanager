#include <map>

#include <inttypes.h>
#include <unistd.h>

#include <hwbinder/IInterface.h>
#include <hwbinder/IPCThreadState.h>
#include <hwbinder/IServiceManager.h>
#include <hwbinder/ProcessState.h>
#include <hwbinder/Status.h>
#include <nativehelper/ScopedFd.h>
#include <utils/Errors.h>
#include <utils/Log.h>
#include <utils/Looper.h>
#include <utils/StrongPointer.h>

// libutils:
using android::BAD_TYPE;
using android::Looper;
using android::LooperCallback;
using android::OK;
using android::sp;
using android::status_t;
using android::String8;
using android::String16;

// libbinder:
using android::hidl::BBinder;
using android::hidl::BnInterface;
using android::hidl::defaultServiceManager;
using android::hidl::IBinder;
using android::hidl::IInterface;
using android::hidl::IPCThreadState;
using android::hidl::Parcel;
using android::hidl::ProcessState;
using android::hidl::binder::Status;
using android::hidl::hidl_version;
using android::hidl::get_major_hidl_version;
using android::hidl::get_minor_hidl_version;

// Standard library
using std::map;
using std::string;
using std::unique_ptr;
using std::vector;

// Service manager definition
using android::hidl::IServiceManager;

namespace {

class BinderCallback : public LooperCallback {
 public:
  BinderCallback() {}
  ~BinderCallback() override {}

  int handleEvent(int /* fd */, int /* events */, void* /* data */) override {
    IPCThreadState::self()->handlePolledCommands();
    return 1;  // Continue receiving callbacks.
  }
};

class HidlService {
  public:
      HidlService(const String16 &name, const sp<IBinder>& service, const hidl_version& version,
                  const String16 &metaVersion) : mName(name), mVersion(version),
                                                 mMetaVersion(metaVersion), mService(service) {
      }
      sp<IBinder> getService() {
          return mService;
      }
      void setService(const sp<IBinder>& service) {
          mService = service;
      }

      bool supportsVersion(hidl_version version) {
          if (get_major_hidl_version(version) == get_major_hidl_version(mVersion) &&
                  get_minor_hidl_version(version) <= get_minor_hidl_version(mVersion)) {
              return true;
          }
          ALOGE("Service doesn't support version %d.%d", get_major_hidl_version(version),
                  get_minor_hidl_version(version));
          return false;
      }
  private:
      String16                         mName;        // Service name
      hidl_version                     mVersion;     // Supported interface version
      String16                         mMetaVersion; // Meta-version of the HIDL interface
      sp<IBinder>                      mService;     // Binder handle to service

};

class HwServiceManager : public BnInterface<IServiceManager> {
  public:
    HwServiceManager() {}
    virtual ~HwServiceManager() = default;

    map<String16, unique_ptr<HidlService>> mServiceMap;

    /*
     **************************************************************************
     IServiceManager methods
     **************************************************************************
    */

    /**
     * getService() is an interface not used on the server-side; it's used
     * on the client-side to block on calling checkService().
     */
    virtual sp<IBinder>         getService( const String16& name,
                                            const hidl_version version) const {
        return checkService(name, version);
    }

    virtual sp<IBinder>         checkService( const String16& name,
                                              const hidl_version version) const {
        auto service = mServiceMap.find(name);
        if (service == mServiceMap.end()) {
            return nullptr;
        }
        if (service->second->supportsVersion(version)) {
            return service->second->getService();
        } else {
            return nullptr;
        }
    }

    /**
     * Register a service.
     */
    virtual status_t            addService( const String16& name,
                                            const sp<IBinder>& service,
                                            const hidl_version version,
                                            bool /*allowIsolated = false*/) {
        ALOGE("addService for service %s version %" PRIu32, String8(name).string(), version);
        auto existing_service = mServiceMap.find(name);
        if (existing_service != mServiceMap.end()) {
            // Only update handle to service, don't dynamically update versions
            // TODO needs to deal with different major versions correctly
            existing_service->second->setService(service);
        } else {
            mServiceMap[name] = unique_ptr<HidlService>(
                    new HidlService(name, service, version, String16()));
        }

        // TODO link to death so we know when it dies
        return OK;
    }

    /*
     **************************************************************************
     BnInterface methods
     **************************************************************************
    */

    virtual status_t onTransact(uint32_t code, const Parcel& parcel_in, Parcel* parcel_out,
                                uint32_t flags, TransactCallback callback) {
        status_t ret_status = OK;
        switch (code) {
            case GET_SERVICE_TRANSACTION:
            {
                String16 serviceName;
                hidl_version version;
                if (!(parcel_in.checkInterface(this))) {
                    ret_status = BAD_TYPE;
                    break;
                }
                ret_status = parcel_in.readString16(&serviceName);
                if (ret_status != OK) {
                    break;
                }
                ret_status = parcel_in.readUint32(&version);
                if (ret_status != OK) {
                    break;
                }
                // TODO SELinux access control
                sp<IBinder> service = getService(serviceName, version);
                ret_status = parcel_out->writeStrongBinder(service);
                if (ret_status != OK) {
                    break;
                } else if (callback != nullptr) {
                    callback(*parcel_out);
                }
                break;
            }
            case CHECK_SERVICE_TRANSACTION:
            {
                String16 serviceName;
                hidl_version version;
                if (!(parcel_in.checkInterface(this))) {
                    ret_status = BAD_TYPE;
                    break;
                }
                ret_status = parcel_in.readString16(&serviceName);
                if (ret_status != OK) {
                    break;
                }
                ret_status = parcel_in.readUint32(&version);
                if (ret_status != OK) {
                    break;
                }
                // TODO SELinux access control
                sp<IBinder> service = getService(serviceName, version);
                ret_status = parcel_out->writeStrongBinder(service);
                if (ret_status != OK) {
                    break;
                } else if (callback != nullptr) {
                    callback(*parcel_out);
                }
                break;
            }
            case ADD_SERVICE_TRANSACTION:
            {
                String16 serviceName;
                sp<IBinder> service;
                hidl_version version;
                if (!(parcel_in.checkInterface(this))) {
                    ret_status = BAD_TYPE;
                    break;
                }
                ret_status = parcel_in.readString16(&serviceName);
                if (ret_status != OK) {
                    break;
                }
                ret_status = parcel_in.readStrongBinder(&service);
                if (ret_status != OK) {
                    break;
                }
                ret_status = parcel_in.readUint32(&version);
                if (ret_status != OK) {
                    break;
                }
                // TODO need isolation param?
                // TODO SELinux access control
                ret_status = addService(serviceName, service, version, false);
                parcel_out->writeInt32(ret_status);
                if (ret_status != OK) {
                    break;
                } else if (callback != nullptr) {
                    callback(*parcel_out);
                }
                break;
            }
            default:
            {
                ret_status = BBinder::onTransact(code, parcel_in, parcel_out, flags, callback);
                break;
            }
        }

        return ret_status;
    }
};

int Run() {
  android::sp<HwServiceManager> service = new HwServiceManager;
  sp<Looper> looper(Looper::prepare(0 /* opts */));

  int binder_fd = -1;

  IPCThreadState::self()->setupPolling(&binder_fd);
  if (binder_fd < 0) return -1;

  sp<BinderCallback> cb(new BinderCallback);
  if (looper->addFd(binder_fd, Looper::POLL_CALLBACK, Looper::EVENT_INPUT, cb,
                    nullptr) != 1) {
    ALOGE("Failed to add binder FD to Looper");
    return -1;
  }

  // Tell IPCThreadState we're the service manager
  IPCThreadState::self()->setTheContextObject(service);
  // Then tell binder kernel
  ioctl(binder_fd, BINDER_SET_CONTEXT_MGR, 0);

  while (true) {
    looper->pollAll(-1 /* timeoutMillis */);
  }

  return 0;
}

} // namespace

int main(int /* argc */, char* /* argv */ []) {
    return Run();
}
