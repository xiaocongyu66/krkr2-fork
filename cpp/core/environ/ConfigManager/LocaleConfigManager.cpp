#include "LocaleConfigManager.h"
#include "GlobalConfigManager.h"
#include "IFileBackend.h"
#include "tjsString.h"
#include "UtilStreams.h"
#include "tinyxml2/tinyxml2.h"

namespace {
    // Bundled-locale lookup order, CWD-relative like the desktop CCFileUtils
    // resolution before it. The raw package-relative name covers the desktop
    // data directory and the APK root; "res/" keeps the historic CCFileUtils
    // search-path entry (AppDelegate.cpp:98) resolvable; "Resources/" matches
    // the runtime-asset layout both app targets sync to. Every candidate is
    // optional: a missing pack only leaves the raw tid as the display text.
    constexpr const char *LocaleCandidates[] = { "", "res/", "Resources/" };
} // namespace

LocaleConfigManager::LocaleConfigManager() = default;

std::string LocaleConfigManager::GetLocaleResourceName() {
    for(const char *prefix : LocaleCandidates) {
        std::string name =
            std::string(prefix) + "locale/" + currentLangCode + ".xml";
        if(TVPGetFileBackend()->Exists(ttstr(name)))
            return name;
    }
    if(currentLangCode == "en_us")
        return {}; // restore to default language config(must exist):
                   // even the default is missing, keep raw tids as text
    currentLangCode = "en_us";
    return GetLocaleResourceName();
}

LocaleConfigManager *LocaleConfigManager::GetInstance() {
    static LocaleConfigManager instance;
    return &instance;
}

const std::string &LocaleConfigManager::GetText(const std::string &tid) {
    auto it = AllConfig.find(tid);
    if(it == AllConfig.end()) {
        AllConfig[tid] = tid;
        return AllConfig[tid];
    }
    return it->second;
}

void LocaleConfigManager::Initialize(const std::string &sysLang) {
    // override by global configured lang
    currentLangCode = GlobalConfigManager::GetInstance()->GetValue<std::string>(
        "user_language", "");
    if(currentLangCode.empty())
        currentLangCode = sysLang;
    AllConfig.clear();
    tinyxml2::XMLDocument doc;
    std::string xmlData;
    if(const std::string name = GetLocaleResourceName(); !name.empty()) {
        if(auto stream = TVPGetFileBackend()->OpenRead(ttstr(name))) {
            const tjs_uint64 size = stream->GetSize();
            if(size > 0) {
                xmlData.resize(static_cast<size_t>(size));
                stream->SetPosition(0);
                stream->Read(xmlData.data(), static_cast<tjs_uint>(size));
            }
        }
    }
    bool _writeBOM = false;
    const char *p = xmlData.c_str();
    p = tinyxml2::XMLUtil::ReadBOM(p, &_writeBOM);
    doc.Parse(p);
    tinyxml2::XMLElement *rootElement = doc.RootElement();
    if(rootElement) {
        for(tinyxml2::XMLElement *item = rootElement->FirstChildElement(); item;
            item = item->NextSiblingElement()) {
            const char *key = item->Attribute("id");
            const char *val = item->Attribute("text");
            if(key && val) {
                AllConfig[key] = val;
            }
        }
    }
}
