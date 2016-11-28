#define LOG_TAG "hwservicemanager"

#include <utils/Log.h>

#include <inttypes.h>
#include <unistd.h>

#include <android/hidl/manager/1.0/BnServiceManager.h>
#include <android/hidl/manager/1.0/IServiceManager.h>
#include <cutils/properties.h>
#include <hidl/Status.h>
#include <hwbinder/IPCThreadState.h>
#include <hwbinder/ProcessState.h>
#include <utils/Errors.h>
#include <utils/Looper.h>
#include <utils/StrongPointer.h>

#include "ServiceManager.h"

// libutils:
using android::BAD_TYPE;
using android::Looper;
using android::LooperCallback;
using android::OK;
using android::sp;
using android::status_t;

// libhwbinder:
using android::hardware::IPCThreadState;
using android::hardware::ProcessState;

// libhidl
using android::hardware::hidl_string;
using android::hardware::hidl_vec;

// android.hardware.manager@1.0
using android::hidl::manager::V1_0::BnServiceManager;
using android::hidl::manager::V1_0::IServiceManager;

// android.hardware.manager@1.0-service
using android::hidl::manager::V1_0::implementation::ServiceManager;

static std::string serviceName = "manager";

class BinderCallback : public LooperCallback {
public:
    BinderCallback() {}
    ~BinderCallback() override {}

    int handleEvent(int /* fd */, int /* events */, void* /* data */) override {
        IPCThreadState::self()->handlePolledCommands();
        return 1;  // Continue receiving callbacks.
    }
};

int main() {
    ServiceManager *manager = new ServiceManager();
    sp<BnServiceManager> service = new BnServiceManager(manager);

    hidl_vec<hidl_string> chain;
    service->interfaceChain([&chain](const auto &interfaceChain) {
        chain = interfaceChain;
    });

    // no transport error is possible here because we are calling
    // the function directly, so call 'get' without 'isOk'.
    if (!manager->add(chain, serviceName, service).get()) {
        ALOGE("Failed to register hwservicemanager with itself.");
    }

    sp<Looper> looper(Looper::prepare(0 /* opts */));

    int binder_fd = -1;

    IPCThreadState::self()->setupPolling(&binder_fd);
    if (binder_fd < 0) {
        ALOGE("Failed to aquire binder FD; staying around but doing nothing");
        // hwservicemanager is a critical service; until support for /dev/hwbinder
        // is checked in for all devices, prevent it from exiting; if it were to
        // exit, it would get restarted again and fail again several times,
        // eventually causing the device to boot into recovery mode.
        // TODO: revert
        while (true) {
          sleep(UINT_MAX);
        }
        return -1;
    }

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