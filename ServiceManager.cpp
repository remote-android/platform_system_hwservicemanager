#define LOG_TAG "hwservicemanager"

#include "ServiceManager.h"

#include <android-base/logging.h>
#include <hidl/HidlSupport.h>
#include <regex>
#include <sstream>

namespace android {
namespace hidl {
namespace manager {
namespace V1_0 {
namespace implementation {

ServiceManager::InstanceMap &ServiceManager::PackageInterfaceMap::getInstanceMap() {
    return mInstanceMap;
}

const ServiceManager::InstanceMap &ServiceManager::PackageInterfaceMap::getInstanceMap() const {
    return mInstanceMap;
}

const HidlService *ServiceManager::PackageInterfaceMap::lookupSupporting(
        const std::string &name,
        const hidl_version &version) const {
    auto range = mInstanceMap.equal_range(name);

    for (auto it = range.first; it != range.second; ++it) {
        const std::unique_ptr<HidlService> &service = it->second;

        if (service->supportsVersion(version)) {
            return service.get();
        }
    }

    return nullptr;
}

HidlService *ServiceManager::PackageInterfaceMap::lookupSupporting(
        const std::string &name,
        const hidl_version &version) {

    return const_cast<HidlService*>(
        const_cast<const PackageInterfaceMap*>(this)->lookupSupporting(name, version));
}

HidlService *ServiceManager::PackageInterfaceMap::lookupExact(
        const std::string &name,
        const hidl_version &version) {

    auto range = mInstanceMap.equal_range(name);

    for (auto it = range.first; it != range.second; ++it) {
        std::unique_ptr<HidlService> &service = it->second;

        if (service->getVersion() == version) {
            return service.get();
        }
    }

    return nullptr;
}

void ServiceManager::PackageInterfaceMap::insertService(
        std::unique_ptr<HidlService> &&service) {

    hidl_string iface = service->fqName();
    hidl_string instanceName = service->getName();

    mInstanceMap.insert({service->getName(), std::move(service)});

    sendPackageRegistrationNotification(iface, instanceName);
}

void ServiceManager::PackageInterfaceMap::sendPackageRegistrationNotification(
        const hidl_string &fqName,
        const hidl_string &instanceName) const {

    for (const auto &listener : mPackageListeners) {
        listener->onRegistration(fqName, instanceName, false /* preexisting */);
    }
}
void ServiceManager::PackageInterfaceMap::addPackageListener(sp<IServiceNotification> listener) {
    mPackageListeners.push_back(listener);

    for (const auto &instanceMapping : mInstanceMap) {
        const std::unique_ptr<HidlService> &service = instanceMapping.second;

        if (service->getService() == nullptr) {
            continue;
        }

        listener->onRegistration(service->fqName(), service->getName(), true /* preexisting */);
    }
}

// Methods from ::android::hidl::manager::V1_0::IServiceManager follow.
Return<void> ServiceManager::get(const hidl_string& fqName,
                                 const hidl_string& name,
                                 get_cb _hidl_cb) {

    std::unique_ptr<HidlService> desired = HidlService::make(fqName, name);

    if (desired == nullptr) {
        _hidl_cb(nullptr);
        return Void();
    }

    auto ifaceIt = mServiceMap.find(desired->packageInterface());
    if (ifaceIt == mServiceMap.end()) {
        _hidl_cb(nullptr);
        return Void();
    }

    const PackageInterfaceMap &ifaceMap = ifaceIt->second;

    const HidlService *hidlService
        = ifaceMap.lookupSupporting(desired->getName(), desired->getVersion());

    sp<IBinder> result = nullptr;
    if (hidlService != nullptr) {
        result = hidlService->getService();
    }

    _hidl_cb(result);
    return Void();
}

Return<bool> ServiceManager::add(const hidl_vec<hidl_string>& interfaceChain,
                                 const hidl_string& name,
                                 const sp<IBinder>& service) {

    if (interfaceChain.size() == 0 || service == nullptr) {
        return false;
    }

    for(size_t i = 0; i < interfaceChain.size(); i++) {
        std::string fqName = interfaceChain[i];

        // TODO: keep track of what the actual underlying child is
        std::unique_ptr<HidlService> adding = HidlService::make(fqName, name, service);

        if (adding == nullptr) {
            return false;
        }

        PackageInterfaceMap &ifaceMap = mServiceMap[adding->packageInterface()];

        HidlService *hidlService
            = ifaceMap.lookupExact(adding->getName(), adding->getVersion());

        if (hidlService == nullptr) {
            ifaceMap.insertService(std::move(adding));
        } else {
            hidlService->setService(adding->getService());
        }
    }

    // TODO implement link to death so we know when it dies

    return true;
}

Return<void> ServiceManager::list(list_cb _hidl_cb) {
    size_t total = 0;

    for (const auto &interfaceMapping : mServiceMap) {
        const auto &instanceMap = interfaceMapping.second.getInstanceMap();

        total += instanceMap.size();
    }

    hidl_vec<hidl_string> list;
    list.resize(total);

    size_t idx = 0;
    for (const auto &interfaceMapping : mServiceMap) {
        const auto &instanceMap = interfaceMapping.second.getInstanceMap();

        for (const auto &instanceMapping : instanceMap) {
            const std::unique_ptr<HidlService> &service = instanceMapping.second;

            list[idx++] = service->string();
        }
    }

    _hidl_cb(list);
    return Void();
}

Return<void> ServiceManager::listByInterface(const hidl_string& fqName,
                                             listByInterface_cb _hidl_cb) {
    std::unique_ptr<HidlService> desired = HidlService::make(fqName, "");

    if (desired == nullptr) {
        _hidl_cb(hidl_vec<hidl_string>());
        return Void();
    }

    auto ifaceIt = mServiceMap.find(desired->packageInterface());
    if (ifaceIt == mServiceMap.end()) {
        _hidl_cb(hidl_vec<hidl_string>());
        return Void();
    }

    size_t total = 0;

    const auto &instanceMap = ifaceIt->second.getInstanceMap();
    for (const auto &instanceMapping : instanceMap) {
        const std::unique_ptr<HidlService> &service = instanceMapping.second;

        if (service->supportsVersion(desired->getVersion())) {
            total += 1;
        }
    }

    hidl_vec<hidl_string> list;
    list.resize(total);

    size_t idx = 0;
    for (const auto &serviceMapping : instanceMap) {
        const std::unique_ptr<HidlService> &service = serviceMapping.second;

        if (service->supportsVersion(desired->getVersion())) {
            list[idx++] = service->getName();
        }
    }

    _hidl_cb(list);
    return Void();
}

Return<bool> ServiceManager::registerForNotifications(const hidl_string& fqName,
                                                      const hidl_string& name,
                                                      const sp<IServiceNotification>& callback) {
    if (callback == nullptr) {
        return false;
    }

    std::unique_ptr<HidlService> desired = HidlService::make(fqName, name);

    if (desired == nullptr) {
        return false;
    }

    // TODO(b/31632518) (link to death/automatically deregister)

    PackageInterfaceMap &ifaceMap = mServiceMap[desired->packageInterface()];

    if (name.empty()) {
        ifaceMap.addPackageListener(callback);
        return true;
    }

    HidlService *service =
        ifaceMap.lookupSupporting(desired->getName(), desired->getVersion());

    if (service == nullptr) {
        desired->addListener(callback);
        ifaceMap.insertService(std::move(desired));
    } else {
        service->addListener(callback);
    }

    return true;
}

} // namespace implementation
}  // namespace V1_0
}  // namespace manager
}  // namespace hidl
}  // namespace android
