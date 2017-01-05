#define LOG_TAG "hwservicemanager"
#include "HidlService.h"

#include <android-base/logging.h>
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

HidlService::HidlService(
    const std::string &package,
    const std::string &interface,
    const std::string &name,
    const hidl_version &version,
    const sp<IBase> &service)
: mPackage(package),
  mInterface(interface),
  mInstanceName(name),
  mVersion(version),
  mService(service)
{}

sp<IBase> HidlService::getService() const {
    return mService;
}
void HidlService::setService(sp<IBase> service) {
    mService = service;

    sendRegistrationNotifications();
}
const std::string &HidlService::getPackage() const {
    return mPackage;
}
const std::string &HidlService::getInterface() const {
    return mInterface;
}
const std::string &HidlService::getName() const {
    return mInstanceName;
}
const hidl_version &HidlService::getVersion() const {
    return mVersion;
}

bool HidlService::supportsVersion(const hidl_version &version) const {
    if (version.get_major() == mVersion.get_major() &&
            version.get_minor() <= mVersion.get_minor()) {
        return true;
    }
    // TODO remove log
    LOG(ERROR) << "Service doesn't support version "
               << version.get_major() << "." << version.get_minor();
    return false;
}

void HidlService::addListener(const sp<IServiceNotification> &listener) {
    mListeners.push_back(listener);

    if (mService != nullptr) {
        listener->onRegistration(fqName(), mInstanceName, true /* preexisting */);
    }
}

std::string HidlService::fqName() const {
    std::stringstream ss;
    ss << mPackage
       << "@" << mVersion.get_major() << "." << mVersion.get_minor()
       << "::" << mInterface;
    return ss.str();
}

std::string HidlService::packageInterface() const {
    std::stringstream ss;
    ss << mPackage << "::" << mInterface;
    return ss.str();
}

std::string HidlService::string() const {
    std::stringstream ss;
    ss << mPackage
       << "@" << mVersion.get_major() << "." << mVersion.get_minor()
       << "::" << mInterface
       << "/" << mInstanceName;
    return ss.str();
}

void HidlService::sendRegistrationNotifications() const {
    if (mListeners.size() == 0 || mService == nullptr) {
        return;
    }

    hidl_string iface = fqName();
    hidl_string name = mInstanceName;

    for (const auto &listener : mListeners) {
        auto ret = listener->onRegistration(iface, name, false /* preexisting */);
        ret.isOk(); // ignore result
    }
}

//static
std::unique_ptr<HidlService> HidlService::make(
        const std::string &fqName,
        const std::string &serviceName,
        const sp<IBase> &service) {

    std::smatch match;

    if (!std::regex_match(fqName, match, kRE_FQNAME)) {
        LOG(ERROR) << "Invalid service fqname " << fqName;
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

}  // namespace implementation
}  // namespace V1_0
}  // namespace manager
}  // namespace hidl
}  // namespace android
