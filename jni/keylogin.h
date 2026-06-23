#include <json/json.hpp>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <random>
#include <curl/curl.h>
#include <sys/system_properties.h>
#include <pthread.h>

#include "include/obfuscate.h"
#include "version.h"

#ifndef LIB_VERSION
#define LIB_VERSION "dev"
#endif

bool bValid = true;

// ── Auto-update state ────────────────────────────────────────────────
bool        g_UpdateDownloading = false;
bool        g_UpdateReady       = false;
std::string g_UpdateSavePath    = "";

static size_t curlWriteFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
    FILE* fp = (FILE*)userdata;
    return fwrite(ptr, size, nmemb, fp);
}

static void* AutoUpdateThread(void* arg) {
    std::string* urlPtr = (std::string*)arg;
    std::string url = *urlPtr;
    delete urlPtr;

    std::string savePath = std::string("/data/data/") + PACKAGE_NAME + "/files/lyn4xp_update.so";

    CURL* curl = curl_easy_init();
    if (!curl) { g_UpdateDownloading = false; return nullptr; }

    FILE* fp = fopen(savePath.c_str(), "wb");
    if (!fp) { curl_easy_cleanup(curl); g_UpdateDownloading = false; return nullptr; }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK) {
        g_UpdateSavePath = savePath;
        g_UpdateReady    = true;
        LOGI("[AutoUpdate] Download complete → %s", savePath.c_str());
    } else {
        LOGI("[AutoUpdate] Download failed: %s", curl_easy_strerror(res));
        remove(savePath.c_str());
    }
    g_UpdateDownloading = false;
    return nullptr;
}

INLINE void StartAutoUpdate(const std::string& url) {
    if (url.empty() || g_UpdateDownloading || g_UpdateReady) return;
    g_UpdateDownloading = true;
    LOGI("[AutoUpdate] Starting download from: %s", url.c_str());
    pthread_t t;
    std::string* urlCopy = new std::string(url);
    pthread_create(&t, nullptr, AutoUpdateThread, urlCopy);
    pthread_detach(t);
}

std::string xor_encrypt(const std::string& data, const std::string& key) {
    std::string result;
    result.reserve(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        result += data[i] ^ key[i % key.length()];
    }
    return result;
}

std::string xor_decrypt(const std::string& data, const std::string& key) {
    return xor_encrypt(data, key);
}

std::string base64_encode(const std::string& data) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    size_t i = 0;
    unsigned char a3[3];
    unsigned char a4[4];

    while (data.length() - i >= 3) {
        a3[0] = static_cast<unsigned char>(data[i]);
        a3[1] = static_cast<unsigned char>(data[i + 1]);
        a3[2] = static_cast<unsigned char>(data[i + 2]);

        a4[0] = (a3[0] & 0xfc) >> 2;
        a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
        a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
        a4[3] = a3[2] & 0x3f;

        for (int j = 0; j < 4; j++)
            result += chars[a4[j]];

        i += 3;
    }

    if (data.length() - i > 0) {
        int remaining = data.length() - i;
        for (int j = 0; j < 3; j++)
            a3[j] = (j < remaining) ? static_cast<unsigned char>(data[i + j]) : 0;

        a4[0] = (a3[0] & 0xfc) >> 2;
        a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
        a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
        a4[3] = a3[2] & 0x3f;

        for (int j = 0; j < remaining + 1; j++)
            result += chars[a4[j]];
        while (result.length() % 4)
            result += '=';
    }

    return result;
}

std::string base64_decode(const std::string& input) {
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++)
        T[static_cast<unsigned char>(chars[i])] = i;

    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    return result;
}

INLINE std::string getDt(int offsetSeconds = 0) {
    using namespace std::chrono;
    system_clock::time_point now = system_clock::now();
    system_clock::time_point ist = now + hours(3) + seconds(offsetSeconds);
    std::time_t t = system_clock::to_time_t(ist);

    std::tm tm{};
    gmtime_r(&t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

INLINE std::string gToken(const std::string& data, const std::string& key) {
    std::string encrypted = xor_encrypt(data, key);
    std::string encoded = base64_encode(encrypted);
    return encoded;
}

INLINE std::string decryptData(const std::string& encryptedData, const std::string& key) {
    try {
        auto jsonObj = nlohmann::json::parse(encryptedData);

        if (!jsonObj.contains("data") || !jsonObj["data"].is_string())
            return "";

        std::string encoded = jsonObj["data"].get<std::string>();
        std::string decoded = base64_decode(encoded);
        return xor_decrypt(decoded, key);
    } catch (...) {
        return "";
    }
}

static size_t curlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string* response = static_cast<std::string*>(userdata);
    response->append(ptr, size * nmemb);
    return size * nmemb;
}

inline std::string httpPost(const std::string& url, const std::string& jsonBody, int timeoutSec = 15) {
    std::string response;
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)jsonBody.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeoutSec);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)timeoutSec);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        return std::string("__CURL_ERR__:") + curl_easy_strerror(res);
    }
    curl_easy_cleanup(curl);
    return response;
}

std::string ERROR_MESSAGE = "";

static bool logged_in = false;
static bool is_logging_in = false;
std::string g_Token          = "";
std::string g_Auth           = "";
std::string g_ExpTime        = "N/A";
std::string g_Username       = "";
std::string g_HWID           = "";
int         g_DaysLeft       = 0;
bool        g_UpdateAvailable = false;
std::string g_LatestVersion  = "";
std::string g_DownloadUrl    = "";
std::string g_GameVersion    = "1.0";

// ── Detect 8BP version from /proc/self/maps ─────────────────────────
static std::string DetectGameVersion() {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return "1.0";
    char line[512];
    std::string ver = "1.0";
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, "eightballpool")) continue;
        // Path: /data/app/~~xxx/com.miniclip.eightballpool-VERSION/base.apk
        char* pool = strstr(line, "eightballpool");
        if (!pool) continue;
        char* dash = strchr(pool, '-');
        if (!dash) continue;
        dash++;
        char vbuf[32] = {};
        int vi = 0;
        while (*dash && *dash != '/' && *dash != '\n' && *dash != ' ' && vi < 30)
            vbuf[vi++] = *dash++;
        if (vi > 0 && vi < 30) { ver = vbuf; break; }
    }
    fclose(f);
    return ver;
}

INLINE bool Login(std::string androidID, std::string key) {

    if (androidID.empty()) {
        ERROR_MESSAGE = O("Could not get Android ID");
        return false;
    }

    if (key.empty()) {
        ERROR_MESSAGE = O("Key Is Empty or Failed to get Key");
        return false;
    }

    is_logging_in = true;
    ERROR_MESSAGE = "";

    const std::string validateUrl = OO("https://panel-8-bp.vercel.app/api/public/validate").str();

    // Read device info from Android system properties
    char propModel[PROP_VALUE_MAX]   = {};
    char propAndroid[PROP_VALUE_MAX] = {};
    __system_property_get("ro.product.model",          propModel);
    __system_property_get("ro.build.version.release",  propAndroid);
    std::string deviceModel   = propModel[0]   ? propModel   : "Unknown";
    std::string androidVer    = propAndroid[0] ? propAndroid : "Unknown";
    g_GameVersion             = DetectGameVersion();
    std::string gameVer       = g_GameVersion;

    try {
        nlohmann::json payload = {
            {OO("key").str(),             key},
            {OO("hwid").str(),            androidID},
            {OO("device_model").str(),    deviceModel},
            {OO("android_version").str(), androidVer},
            {OO("game_version").str(),    gameVer},
            {OO("lib_version").str(),     std::string(LIB_VERSION)}
        };

        std::string rawResponse = httpPost(validateUrl, payload.dump(), 15);

        if (rawResponse.empty() || rawResponse.substr(0, 12) == "__CURL_ERR__") {
            if (rawResponse.size() > 13)
                ERROR_MESSAGE = OO("Network error: ").str() + rawResponse.substr(13);
            else
                ERROR_MESSAGE = OO("Connection failed. Check your internet.").str();
            is_logging_in = false;
            return false;
        }

        nlohmann::json jsonResponse = nlohmann::json::parse(rawResponse);

        if (!jsonResponse.contains("valid") || jsonResponse["valid"] != true) {
            std::string reason = "";
            try { reason = jsonResponse.value("reason", ""); } catch (...) {}

            if (reason == "invalid")       ERROR_MESSAGE = OO("Invalid license key.").str();
            else if (reason == "banned")   ERROR_MESSAGE = OO("License key has been banned.").str();
            else if (reason == "expired")  ERROR_MESSAGE = OO("License key has expired.").str();
            else if (reason == "hwid_mismatch") ERROR_MESSAGE = OO("Device not authorized. HWID mismatch.").str();
            else if (reason == "missing_params") ERROR_MESSAGE = OO("Missing required parameters.").str();
            else if (reason == "server_error")   ERROR_MESSAGE = OO("Server error. Try again later.").str();
            else ERROR_MESSAGE = OO("License validation failed.").str();

            is_logging_in = false;
            return false;
        }

        std::string expiryDate  = "";
        int         daysLeft    = 0;
        std::string username    = "";
        bool        updAvail    = false;
        std::string latestVer   = "";
        std::string dlUrl       = "";
        try { expiryDate = jsonResponse.value("expires_at",      "");    } catch (...) {}
        try { daysLeft   = jsonResponse.value("days_left",        0);    } catch (...) {}
        try { username   = jsonResponse.value("username",         "");   } catch (...) {}
        try { updAvail   = jsonResponse.value("update_available", false); } catch (...) {}
        try { latestVer  = jsonResponse.value("latest_version",  "");   } catch (...) {}
        try { dlUrl      = jsonResponse.value("download_url",    "");   } catch (...) {}

        logged_in          = true;
        is_logging_in      = false;
        g_Token            = OO("0wQRlDkgoQlf").str();
        g_Auth             = OO("0wQRlDkgoQlf").str();
        g_ExpTime          = expiryDate.empty() ? "N/A" : expiryDate;
        g_DaysLeft         = daysLeft;
        g_Username         = username.empty() ? "User" : username;
        g_UpdateAvailable  = updAvail;
        g_LatestVersion    = latestVer;
        g_DownloadUrl      = dlUrl;
        bValid             = g_Token == g_Auth;
        // Reset stale update-ready flag when server says no update needed
        // (new lib version already applied after restart)
        if (!updAvail) {
            g_UpdateReady    = false;
            g_UpdateSavePath = "";
        }
        if (updAvail && !dlUrl.empty()) StartAutoUpdate(dlUrl);
        g_HWID                        = androidID;
        persistent_string["key"]      = key;
        persistent_string["username"] = g_Username;
        save_persistence();

        return true;

    } catch (const std::exception& e) {
        ERROR_MESSAGE = OO("Error: ").str() + std::string(e.what());
        is_logging_in = false;
        return false;
    }
}
