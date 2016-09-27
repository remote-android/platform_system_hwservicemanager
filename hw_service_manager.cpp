/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "hwservicemanager"
#include <utils/Log.h>

#include <map>

#include <inttypes.h>
#include <unistd.h>

#include <cutils/properties.h>
#include <hidl/IServiceManager.h>
#include <hidl/Status.h>
#include <hwbinder/IInterface.h>
#include <hwbinder/IPCThreadState.h>
#include <hwbinder/ProcessState.h>
#include <utils/Errors.h>
#include <utils/Looper.h>
#include <utils/StrongPointer.h>

// libutils:
using android::BAD_TYPE;
using android::Looper;
using android::LooperCallback;
using android::OK;
using android::sp;
using android::status_t;
using android::String16;

// libbinder:
using android::hardware::BBinder;
using android::hardware::BnInterface;
using android::hardware::defaultServiceManager;
using android::hardware::IBinder;
using android::hardware::IInterface;
using android::hardware::IPCThreadState;
using android::hardware::Parcel;
using android::hardware::ProcessState;
using android::hardware::Status;
using android::hardware::hidl_version;

// Standard library
using std::multimap;
using std::string;
using std::unique_ptr;
using std::vector;

// Service manager definition
using android::hardware::IServiceManager;
using android::hardware::IHwServiceManager;

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
      HidlService(const string &name, const sp<IBinder>& service, const hidl_version& version,
                  const string &metaVersion) : mName(name), mVersion(version),
                                                 mMetaVersion(metaVersion), mService(service) {
      }
      sp<IBinder> getService() {
          return mService;
      }
      void setService(const sp<IBinder>& service) {
          mService = service;
      }

      hidl_version& getVersion() {
          return mVersion;
      }

      bool supportsVersion(hidl_version version) {
          if (version.get_major() == mVersion.get_major() &&
                  version.get_minor() <= mVersion.get_minor()) {
              return true;
          }
          ALOGE("Service doesn't support version %u.%u", version.get_major(), version.get_minor());
          return false;
      }
  private:
      string                           mName;        // Service name
      hidl_version                     mVersion;     // Supported interface version
      string                           mMetaVersion; // Meta-version of the HIDL interface
      sp<IBinder>                      mService;     // Binder handle to service

};

class HwServiceManager : public BnInterface<IServiceManager, IHwServiceManager> {
  public:
    // Access to this map doesn't need to be locked, since hwservicemanager
    // is single-threaded.
    multimap<string, unique_ptr<HidlService>> mServiceMap;

    HwServiceManager() : BnInterface<IServiceManager, IHwServiceManager>(
            sp<IServiceManager>(this)) {

    }

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
                                            const hidl_version& version) const {
        return checkService(name, version);
    }

    virtual sp<IBinder>         checkService( const String16& name,
                                              const hidl_version& version) const {
        const string name_str = String16::std_string(name);
        auto numEntries = mServiceMap.count(name_str);
        auto service_iter = mServiceMap.find(name_str);

        while (numEntries > 0) {
            if (service_iter->second->supportsVersion(version)) {
                return service_iter->second->getService();
            }
            --numEntries;
            ++service_iter;
        }
        return nullptr;
    }

    /**
     * Register a service.
     */
    virtual status_t            addService( const String16& name,
                                            const sp<IBinder>& service,
                                            const hidl_version& version,
                                            bool /*allowIsolated = false*/) {
        const string name_str = String16::std_string(name);
        auto numEntries = mServiceMap.count(name_str);
        auto service_iter = mServiceMap.find(name_str);
        bool replaced = false;
        while (numEntries > 0) {
            if (service_iter->second->getVersion() == version) {
                // Just update service reference
                service_iter->second->setService(service);
                replaced = true;
                break;
            }
            --numEntries;
            ++service_iter;
        }
        if (!replaced) {
            mServiceMap.insert({name_str, unique_ptr<HidlService>(
                    new HidlService(name_str, service, version, ""))});
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
                hidl_version *version;
                if (!(parcel_in.checkInterface(this))) {
                    ret_status = BAD_TYPE;
                    break;
                }
                ret_status = parcel_in.readString16(&serviceName);
                if (ret_status != OK) {
                    break;
                }
                version = hidl_version::readFromParcel(parcel_in);
                if (version == nullptr) {
                    break;
                }
                // TODO SELinux access control
                sp<IBinder> service = getService(serviceName, *version);
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
                hidl_version *version;
                if (!(parcel_in.checkInterface(this))) {
                    ret_status = BAD_TYPE;
                    break;
                }
                ret_status = parcel_in.readString16(&serviceName);
                if (ret_status != OK) {
                    break;
                }
                version = hidl_version::readFromParcel(parcel_in);
                if (version == nullptr) {
                    break;
                }
                // TODO SELinux access control
                sp<IBinder> service = getService(serviceName, *version);
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
                hidl_version *version;
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
                version = hidl_version::readFromParcel(parcel_in);
                if (version == nullptr) {
                    break;
                }
                // TODO need isolation param?
                // TODO SELinux access control
                ret_status = addService(serviceName, service, *version, false);
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
  android::sp<HwServiceManager> service = new HwServiceManager();
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

  int rc = property_set("hwservicemanager.ready", "true");
  if (rc) {
    ALOGE("Failed to set \"hwservicemanager.ready\" (error %d). "\
          "HAL services will not launch!\n", rc);
  }

  while (true) {
    looper->pollAll(-1 /* timeoutMillis */);
  }

  return 0;
}

} // namespace

int main(int /* argc */, char* /* argv */ []) {
    return Run();
}
