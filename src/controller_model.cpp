#include "controller_model.h"
#include "injection_coordinator.h"

#include <windows.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace {
constexpr UINT WM_CONTROLLER_STATE = WM_APP + 41;

std::wstring processTitle(DWORD pid) {
    struct Search {
        DWORD pid;
        std::wstring title;
    } search{pid, {}};
    EnumWindows([](HWND window, LPARAM value) -> BOOL {
        auto* search = reinterpret_cast<Search*>(value);
        DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        if (owner != search->pid || !IsWindowVisible(window)) {
            return TRUE;
        }
        wchar_t title[256]{};
        GetWindowTextW(window, title, static_cast<int>(std::size(title)));
        if (title[0] != L'\0') {
            search->title = title;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.title;
}

std::wstring executableDirectory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring result(path);
    const auto separator = result.find_last_of(L"\\/");
    return separator == std::wstring::npos ? L"." : result.substr(0, separator);
}
}

ControllerModel::ControllerModel() {
    const std::wstring setting = cacheDirectory() + L"cache.preference";
    std::wifstream input(setting);
    int enabled = 0;
    if (input >> enabled) {
        cachePreference_ = enabled != 0;
    }
}

ControllerModel::~ControllerModel() {
    cancelAuth_ = true;
    if (authThread_.joinable()) authThread_.join();
}

ControllerPage ControllerModel::page() const {
    std::lock_guard lock(mutex_);
    return page_;
}

void ControllerModel::setPage(ControllerPage value) {
    std::lock_guard lock(mutex_);
    page_ = value;
}

std::wstring ControllerModel::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

void ControllerModel::setStatus(std::wstring value) {
    std::lock_guard lock(mutex_);
    status_ = std::move(value);
}

std::wstring& ControllerModel::username() { return username_; }
std::wstring& ControllerModel::password() { return password_; }

std::vector<MinecraftProcess> ControllerModel::minecraftProcesses() const {
    std::lock_guard lock(mutex_);
    return minecraftProcesses_;
}

void ControllerModel::refreshMinecraftProcesses() {
    std::vector<MinecraftProcess> found;
    std::vector<std::uint32_t> injected;
    {
        std::lock_guard lock(mutex_);
        injected = injectedProcesses_;
    }
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{sizeof(entry)};
        if (Process32FirstW(snapshot, &entry)) {
            do {
                std::wstring name(entry.szExeFile);
                std::transform(name.begin(), name.end(), name.begin(), towlower);
                if (name != L"javaw.exe" && name != L"java.exe") {
                    continue;
                }
                auto title = processTitle(entry.th32ProcessID);
                if (title.empty() || title.find(L"Minecraft") == std::wstring::npos) {
                    continue;
                }
                const bool wasInjected = std::find(injected.begin(), injected.end(),
                    entry.th32ProcessID) != injected.end();
                found.push_back({entry.th32ProcessID, std::move(title), wasInjected});
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    std::lock_guard lock(mutex_);
    minecraftProcesses_ = std::move(found);
    lastMinecraftRefresh_ = std::chrono::steady_clock::now();
}

bool ControllerModel::injectMinecraft(std::uint32_t processId) {
    std::string token;
    {
        std::lock_guard lock(mutex_);
        token = accessToken_;
    }
    if (!service_.start(std::move(token), cachePreference_, true)) {
        setStatus(L"Failed to create local controller service");
        setPage(ControllerPage::Error);
        return false;
    }
    std::wstring error;
    const std::wstring dllPath = executableDirectory() + L"\\vape_v4.dll";
    if (!InjectionCoordinator::injectReflectiveDll(processId, dllPath, service_.port(), error)) {
        setStatus(L"Failed to inject\nUse a supported Minecraft version and client listed on the FAQ of the website");
        setPage(ControllerPage::Error);
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        if (std::find(injectedProcesses_.begin(), injectedProcesses_.end(), processId) ==
            injectedProcesses_.end()) {
            injectedProcesses_.push_back(processId);
        }
        loadingStage_ = 0;
        loadingStarted_ = std::chrono::steady_clock::now();
        stageStarted_ = loadingStarted_;
        page_ = ControllerPage::Loading;
        status_.clear();
    }
    return true;
}

std::wstring ControllerModel::makeHwid() {
    DWORD serial = 0;
    GetVolumeInformationW(L"C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
    wchar_t value[32]{};
    swprintf_s(value, L"%08X", serial);
    return value;
}

std::string ControllerModel::httpPost(const wchar_t* host, const wchar_t* path,
                                      const std::string& body) {
    HINTERNET session = WinHttpOpen(L"Vape4/Launcher",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return {};
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
    HINTERNET connection = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"POST", path,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    std::string response;
    const wchar_t* headers = L"Content-Type: application/x-www-form-urlencoded";
    if (request && WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L),
            const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
            static_cast<DWORD>(body.size()), 0) && WinHttpReceiveResponse(request, nullptr)) {
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
            const auto offset = response.size();
            response.resize(offset + available);
            DWORD read = 0;
            if (!WinHttpReadData(request, response.data() + offset, available, &read)) break;
            response.resize(offset + read);
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return response;
}

std::string ControllerModel::jsonString(const std::string& json, const char* key) {
    const std::string needle = std::string("\"") + key + "\"";
    auto position = json.find(needle);
    if (position == std::string::npos) return {};
    position = json.find(':', position + needle.size());
    position = json.find('"', position == std::string::npos ? position : position + 1);
    if (position == std::string::npos) return {};
    const auto end = json.find('"', position + 1);
    return end == std::string::npos ? std::string{} : json.substr(position + 1, end - position - 1);
}

void ControllerModel::beginBrowserAuthentication(void* windowHandle) {
    cancelAuth_ = true;
    if (authThread_.joinable()) authThread_.join();
    cancelAuth_ = false;
    setPage(ControllerPage::BrowserAuth);
    setStatus(L"");
    const auto window = static_cast<HWND>(windowHandle);
    authThread_ = std::thread([this, window] {
        const auto hwid = makeHwid();
        std::string narrowHwid;
        narrowHwid.reserve(hwid.size());
        for (const wchar_t character : hwid) {
            narrowHwid.push_back(static_cast<char>(character));
        }
        const auto challenge = httpPost(L"www.vape.gg", L"/api/v1/app-auth/generate",
            "edition=v4&hwid=" + narrowHwid);
        if (challenge.size() != 40 || cancelAuth_) {
            if (!cancelAuth_) {
                setStatus(L"Unable to start browser login");
                setPage(ControllerPage::Login);
            }
            PostMessageW(window, WM_CONTROLLER_STATE, 0, 0);
            return;
        }
        std::wstring wideChallenge(challenge.begin(), challenge.end());
        const std::wstring url = L"https://www.vape.gg/app-auth/proceed/" + wideChallenge;
        {
            std::lock_guard lock(mutex_);
            browserUrl_ = url;
        }
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        while (!cancelAuth_) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            const auto response = httpPost(L"www.vape.gg", L"/api/v1/app-auth/status",
                "token=" + challenge);
            const auto status = jsonString(response, "status");
            if (status == "success") {
                {
                    std::lock_guard lock(mutex_);
                    accessToken_ = jsonString(response, "token");
                }
                refreshMinecraftProcesses();
                setPage(ControllerPage::MinecraftSelection);
                break;
            }
            if (status == "timed out") {
                setStatus(L"Browser login timed out");
                setPage(ControllerPage::Login);
                break;
            }
        }
        PostMessageW(window, WM_CONTROLLER_STATE, 0, 0);
    });
}

void ControllerModel::reopenBrowserAuthentication() {
    std::wstring url;
    {
        std::lock_guard lock(mutex_);
        url = browserUrl_;
    }
    if (!url.empty()) ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ControllerModel::cancelBrowserAuthentication() {
    cancelAuth_ = true;
    setPage(ControllerPage::Login);
}

std::wstring ControllerModel::cacheDirectory() const {
    wchar_t profile[MAX_PATH]{};
    DWORD size = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    std::wstring directory = size ? std::wstring(profile, size) : executableDirectory();
    directory += L"\\.vapeclient\\";
    return directory;
}

void ControllerModel::persistCachePreference(bool enabled) {
    cachePreference_ = enabled;
    const auto directory = cacheDirectory();
    CreateDirectoryW(directory.c_str(), nullptr);
    std::wofstream output(directory + L"cache.preference", std::ios::trunc);
    output << (enabled ? 1 : 0);
}

bool ControllerModel::cachePreference() const { return cachePreference_; }

void ControllerModel::tick() {
    const auto now = std::chrono::steady_clock::now();
    const auto currentPage = page();
    if (currentPage == ControllerPage::MinecraftSelection) {
        bool refresh = false;
        {
            std::lock_guard lock(mutex_);
            refresh = lastMinecraftRefresh_.time_since_epoch().count() == 0 ||
                now - lastMinecraftRefresh_ >= std::chrono::seconds(2);
        }
        if (refresh) refreshMinecraftProcesses();
        return;
    }
    if (currentPage != ControllerPage::Loading) return;

    const int serviceStage = service_.stage();
    {
        std::lock_guard lock(mutex_);
        if (serviceStage != loadingStage_) {
            loadingStage_ = serviceStage;
            stageStarted_ = now;
        }
    }
    if (service_.completed()) {
        setPage(cachePreference_ ? ControllerPage::LoadingComplete : ControllerPage::CachePrompt);
    }
}

int ControllerModel::loadingStage() const {
    std::lock_guard lock(mutex_);
    return loadingStage_;
}

double ControllerModel::loadingElapsedSeconds() const {
    std::lock_guard lock(mutex_);
    if (loadingStarted_.time_since_epoch().count() == 0) return 0.0;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - loadingStarted_).count();
}

double ControllerModel::stageElapsedSeconds() const {
    std::lock_guard lock(mutex_);
    if (stageStarted_.time_since_epoch().count() == 0) return 0.0;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - stageStarted_).count();
}

void ControllerModel::submitCredentialAuthentication() {
    // The original dispatches this request into the protected VM at FUN_140985d2d.
    // Keep the UI responsive without inventing a wire protocol not present in the dump.
    setStatus(L"Credential authentication is unavailable in the recovered VM boundary");
}
