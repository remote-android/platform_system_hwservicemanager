#define LOG_TAG "hwservicemanager"

#include "ServiceManager.h"

#include <android-base/logging.h>
#include <hwbinder/IPCThreadState.h>
#include <hidl/HidlSupport.h>
#include <regex>
#include <sstream>

using android::hardware::IPCThreadState;

namespace android {
namespace hidl {
namespace manager {
namespace V1_0 {
namespace implementation {

size_t ServiceManager::countExistingService() const {
    size_t total = 0;
    forEachExistingService([&] (const HidlService *) {
        ++total;
    });
    return total;
}

void ServiceManager::forEachExistingService(std::function<void(const HidlService *)> f) const {
    forEachServiceEntry([f] (const HidlService *service) {
        if (service->getService() == nullptr) {
            return;
        }
        f(service);
    });
}

void ServiceManager::forEachServiceEntry(std::function<void(const HidlService *)> f) const {
    for (const auto &interfaceMapping : mServiceMap) {
        const auto &instanceMap = interfaceMapping.second.getInstanceMap();

        for (const auto &instanceMapping : instanceMap) {
            f(instanceMapping.second.get());
        }
    }
}

void ServiceManager::serviceDied(uint64_t /*cookie*/, const wp<IBase>& who) {
    // TODO(b/32837397)
    remove(who);
}

ServiceManager::InstanceMap &ServiceManager::PackageInterfaceMap::getInstanceMap() {
    return mInstanceMap;
}

const ServiceManager::InstanceMap &ServiceManager::PackageInterfaceMap::getInstanceMap() const {
    return mInstanceMap;
}

const HidlService *ServiceManager::PackageInterfaceMap::lookup(
        const std::string &name) const {
    auto it = mInstanceMap.find(name);

    if (it == mInstanceMap.end()) {
        return nullptr;
    }

    return it->second.get();
}

HidlService *ServiceManager::PackageInterfaceMap::lookup(
        const std::string &name) {

    return const_cast<HidlService*>(
        const_cast<const PackageInterfaceMap*>(this)->lookup(name));
}

void ServiceManager::PackageInterfaceMap::insertService(
        std::unique_ptr<HidlService> &&service) {
    mInstanceMap.insert({service->getInstanceName(), std::move(service)});
}

void ServiceManager::PackageInterfaceMap::sendPackageRegistrationNotification(
        const hidl_string &fqName,
        const hidl_string &instanceName) const {

    for (const auto &listener : mPackageListeners) {
        auto ret = listener->onRegistration(fqName, instanceName, false /* preexisting */);
        ret.isOk(); // ignore
    }
}
void ServiceManager::PackageInterfaceMap::addPackageListener(sp<IServiceNotification> listener) {
    mPackageListeners.push_back(listener);

    for (const auto &instanceMapping : mInstanceMap) {
        const std::unique_ptr<HidlService> &service = instanceMapping.second;

        if (service->getService() == nullptr) {
            continue;
        }

        auto ret = listener->onRegistration(
            service->getInterfaceName(),
            service->getInstanceName(),
            true /* preexisting */);
        ret.isOk(); // ignore
    }
}

// Methods from ::android::hidl::manager::V1_0::IServiceManager follow.
Return<sp<IBase>> ServiceManager::get(const hidl_string& fqName,
                                      const hidl_string& name) {
    auto ifaceIt = mServiceMap.find(fqName);
    if (ifaceIt == mServiceMap.end()) {
        return nullptr;
    }

    const PackageInterfaceMap &ifaceMap = ifaceIt->second;
    const HidlService *hidlService = ifaceMap.lookup(name);

    if (hidlService == nullptr) {
        return nullptr;
    }

    return hidlService->getService();
}

Return<bool> ServiceManager::add(const hidl_string& name, const sp<IBase>& service) {
    bool isValidService = false;

    if (service == nullptr) {
        return false;
    }

    // TODO(b/34235311): use HIDL way to determine this
    // also, this assumes that the PID that is registering is the pid that is the service
    pid_t pid = IPCThreadState::self()->getCallingPid();

    auto ret = service->interfaceChain([&](const auto &interfaceChain) {
        if (interfaceChain.size() == 0) {
            return;
        }

        for(size_t i = 0; i < interfaceChain.size(); i++) {
            std::string fqName = interfaceChain[i];

            PackageInterfaceMap &ifaceMap = mServiceMap[fqName];
            HidlService *hidlService = ifaceMap.lookup(name);

            if (hidlService == nullptr) {
                ifaceMap.insertService(
                    std::make_unique<HidlService>(fqName, name, service, pid));
            } else {
                if (hidlService->getService() != nullptr) {
                    auto ret = hidlService->getService()->unlinkToDeath(this);
                    ret.isOk(); // ignore
                }
                hidlService->setService(service, pid);
            }

            ifaceMap.sendPackageRegistrationNotification(fqName, name);
        }

        auto linkRet = service->linkToDeath(this, 0 /*cookie*/);
        linkRet.isOk(); // ignore

        isValidService = true;
    });

    if (!ret.isOk()) {
        LOG(ERROR) << "Failed to retrieve interface chain.";
        return false;
    }

    return isValidService;
}

Return<void> ServiceManager::list(list_cb _hidl_cb) {

    hidl_vec<hidl_string> list;
    list.resize(countExistingService());

    size_t idx = 0;
    forEachExistingService([&] (const HidlService *service) {
        list[idx++] = service->string();
    });

    _hidl_cb(list);
    return Void();
}

Return<void> ServiceManager::listByInterface(const hidl_string& fqName,
                                             listByInterface_cb _hidl_cb) {
    auto ifaceIt = mServiceMap.find(fqName);
    if (ifaceIt == mServiceMap.end()) {
        _hidl_cb(hidl_vec<hidl_string>());
        return Void();
    }

    const auto &instanceMap = ifaceIt->second.getInstanceMap();

    hidl_vec<hidl_string> list;

    size_t total = 0;
    for (const auto &serviceMapping : instanceMap) {
        const std::unique_ptr<HidlService> &service = serviceMapping.second;
        if (service->getService() == nullptr) continue;

        ++total;
    }
    list.resize(total);

    size_t idx = 0;
    for (const auto &serviceMapping : instanceMap) {
        const std::unique_ptr<HidlService> &service = serviceMapping.second;
        if (service->getService() == nullptr) continue;

        list[idx++] = service->getInstanceName();
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

    // TODO(b/31632518) (link to death/automatically deregister)

    PackageInterfaceMap &ifaceMap = mServiceMap[fqName];

    if (name.empty()) {
        ifaceMap.addPackageListener(callback);
        return true;
    }

    HidlService *service = ifaceMap.lookup(name);

    if (service == nullptr) {
        auto adding = std::make_unique<HidlService>(fqName, name);
        adding->addListener(callback);
        ifaceMap.insertService(std::move(adding));
    } else {
        service->addListener(callback);
    }

    return true;
}

Return<void> ServiceManager::debugDump(debugDump_cb _cb) {
    std::vector<IServiceManager::InstanceDebugInfo> list;
    forEachServiceEntry([&] (const HidlService *service) {
        hidl_vec<int32_t> clientPids;
        clientPids.resize(service->getPassthroughClients().size());

        size_t i = 0;
        for (pid_t p : service->getPassthroughClients()) {
            clientPids[i++] = p;
        }

        list.push_back({
            .pid = service->getPid(),
            .interfaceName = service->getInterfaceName(),
            .instanceName = service->getInstanceName(),
            .clientPids = clientPids,
            .arch = ::android::hidl::base::V1_0::DebugInfo::Architecture::UNKNOWN
        });
    });

    _cb(list);
    return Void();
}


Return<void> ServiceManager::registerPassthroughClient(const hidl_string &fqName,
        const hidl_string &name, int32_t pid) {

    PackageInterfaceMap &ifaceMap = mServiceMap[fqName];

    if (name.empty()) {
        LOG(WARNING) << "registerPassthroughClient encounters empty instance name for "
                     << fqName.c_str();
        return Void();
    }

    HidlService *service = ifaceMap.lookup(name);

    if (service == nullptr) {
        auto adding = std::make_unique<HidlService>(fqName, name);
        adding->registerPassthroughClient(pid);
        ifaceMap.insertService(std::move(adding));
    } else {
        service->registerPassthroughClient(pid);
    }
    return Void();
}

bool ServiceManager::remove(const wp<IBase>& who) {
    bool found = false;
    for (auto &interfaceMapping : mServiceMap) {
        auto &instanceMap = interfaceMapping.second.getInstanceMap();

        for (auto &servicePair : instanceMap) {
            const std::unique_ptr<HidlService> &service = servicePair.second;
            if (service->getService() == who) {
                service->setService(nullptr, static_cast<pid_t>(IServiceManager::PidConstant::NO_PID));
                found = true;
            }
        }
    }
    return found;
}

} // namespace implementation
}  // namespace V1_0
}  // namespace manager
}  // namespace hidl
}  // namespace android
