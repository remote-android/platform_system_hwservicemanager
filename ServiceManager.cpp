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

#define RE_COMPONENT    "[a-zA-Z_][a-zA-Z_0-9]*"
#define RE_PATH         RE_COMPONENT "(?:[.]" RE_COMPONENT ")*"
#define RE_MAJOR        "[0-9]+"
#define RE_MINOR        "[0-9]+"

static const std::regex kRE_FQNAME (
    "(" RE_PATH ")@(" RE_MAJOR ")[.](" RE_MINOR ")::(" RE_COMPONENT ")"
);

ServiceManager::HidlService::HidlService(
    const std::string &package,
    const std::string &interface,
    const std::string &name,
    const hidl_version &version,
    const sp<IBinder> &service)
: mPackage(package),
  mInterface(interface),
  mName(name),
  mVersion(version),
  mService(service)
{}

sp<IBinder> ServiceManager::HidlService::getService() const {
    return mService;
}
void ServiceManager::HidlService::setService(sp<IBinder> service) {
    mService = service;
}
const std::string &ServiceManager::HidlService::getPackage() const {
    return mPackage;
}
const std::string &ServiceManager::HidlService::getInterface() const {
    return mInterface;
}
const std::string &ServiceManager::HidlService::getName() const {
    return mName;
}
const hidl_version &ServiceManager::HidlService::getVersion() const {
    return mVersion;
}

bool ServiceManager::HidlService::supportsVersion(const hidl_version &version) const {
    if (version.get_major() == mVersion.get_major() &&
            version.get_minor() <= mVersion.get_minor()) {
        return true;
    }
    // TODO remove log
    ALOGE("Service doesn't support version %u.%u", version.get_major(), version.get_minor());
    return false;
}

std::string ServiceManager::HidlService::iface() const {
    std::stringstream ss;
    ss << mPackage << "::" << mInterface;
    return ss.str();
}

std::string ServiceManager::HidlService::string() const {
    std::stringstream ss;
    ss << mPackage
       << "@" << mVersion.get_major() << "." << mVersion.get_minor()
       << "::" << mInterface
       << "/" << mName;
    return ss.str();
}

//static
std::unique_ptr<ServiceManager::HidlService> ServiceManager::HidlService::make(
        const std::string &fqName,
        const std::string &serviceName,
        const sp<IBinder> &service) {

    std::smatch match;

    if (!std::regex_match(fqName, match, kRE_FQNAME)) {
        ALOGE("Invalid service fqname %s", fqName.c_str());
        return nullptr;
    }

    CHECK(match.size() == 5u);

    const std::string &package = match.str(1);
    int major = std::stoi(match.str(2));
    int minor = std::stoi(match.str(3));
    const std::string &interfaceName = match.str(4);

    return std::make_unique<HidlService>(
        package,
        interfaceName,
        serviceName,
        hidl_version(major, minor),
        service
    );
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

    auto ifaceIt = mServiceMap.find(desired->iface());
    if (ifaceIt == mServiceMap.end()) {
        _hidl_cb(nullptr);
        return Void();
    }

    const auto &ifaceMap = ifaceIt->second;
    auto range = ifaceMap.equal_range(desired->getName());

    for (auto it = range.first; it != range.second; ++it) {
        const std::unique_ptr<HidlService> &service = it->second;

        if (service->supportsVersion(desired->getVersion())) {
            _hidl_cb(service->getService());
            return Void();
        }
    }

    _hidl_cb(nullptr);
    return Void();
}

Return<bool> ServiceManager::add(const hidl_vec<hidl_string>& interfaceChain,
                                 const hidl_string& name,
                                 const sp<IBinder>& service) {

    if (interfaceChain.size() == 0) {
        return false;
    }

    for(size_t i = 0; i < interfaceChain.size(); i++) {
        std::string fqName = interfaceChain[i];

        // TODO: keep track of what the actual underlying child is
        std::unique_ptr<HidlService> adding = HidlService::make(fqName, name, service);

        if (adding == nullptr) {
            return false;
        }

        auto &ifaceMap = mServiceMap[adding->iface()];
        auto range = ifaceMap.equal_range(name);

        bool replaced = false;
        for (auto it = range.first; it != range.second; ++it) {
            const std::unique_ptr<HidlService> &hidlService = it->second;

            if (hidlService->getVersion() == adding->getVersion()) {
                it->second->setService(service);
                replaced = true;
            }
        }

        if (!replaced) {
            ifaceMap.insert({adding->getName(), std::move(adding)});
        }
    }

    // TODO link to death so we know when it dies

    return true;
}

Return<void> ServiceManager::list(list_cb _hidl_cb) {
    size_t total = 0;

    for (const auto &interfaceMapping : mServiceMap) {
        const auto &ifaceMap = interfaceMapping.second;

        total += ifaceMap.size();
    }

    hidl_vec<hidl_string> list;
    list.resize(total);

    size_t idx = 0;
    for (const auto &interfaceMapping : mServiceMap) {
        const auto &ifaceMap = interfaceMapping.second;

        for (const auto &serviceMapping : ifaceMap) {
            const std::unique_ptr<HidlService> &service = serviceMapping.second;

            list[idx++] = service->string();
        }
    }

    _hidl_cb(list);
    return Void();
}

Return<void> ServiceManager::listByInterface(const hidl_string& fqName, listByInterface_cb _hidl_cb) {
    std::unique_ptr<HidlService> desired = HidlService::make(fqName, "");

    if (desired == nullptr) {
        _hidl_cb(hidl_vec<hidl_string>());
        return Void();
    }

    auto ifaceIt = mServiceMap.find(desired->iface());
    if (ifaceIt == mServiceMap.end()) {
        _hidl_cb(hidl_vec<hidl_string>());
        return Void();
    }

    size_t total = 0;

    const auto &ifaceMap = ifaceIt->second;
    for (const auto &serviceMapping : ifaceMap) {
        const std::unique_ptr<HidlService> &service = serviceMapping.second;

        if (service->supportsVersion(desired->getVersion())) {
            total += 1;
        }
    }

    hidl_vec<hidl_string> list;
    list.resize(total);

    size_t idx = 0;
    for (const auto &serviceMapping : ifaceMap) {
        const std::unique_ptr<HidlService> &service = serviceMapping.second;

        if (service->supportsVersion(desired->getVersion())) {
            list[idx++] = service->getName();
        }
    }

    _hidl_cb(list);
    return Void();
}

} // namespace implementation
}  // namespace V1_0
}  // namespace manager
}  // namespace hidl
}  // namespace android
