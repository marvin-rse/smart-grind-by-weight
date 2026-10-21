"""Host test for the GitHub release download: resumes after dropped connections.

A fake HTTPS server serves a firmware image and drops the connection at
configured offsets. The production download loop must reconnect with a Range
request, cope with servers that ignore Range, and write exactly the original
bytes in order - or give up cleanly after the retry limit.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def function(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


HARNESS = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>
#include "system/operation_interlock.h"
#include "network/ota_stream.h"

#define LOG_BLE(...) ((void)0)
struct String : std::string {
    String() = default;
    String(const char* s) : std::string(s) {}
    String(const std::string& s) : std::string(s) {}
    explicit String(unsigned long v) : std::string(std::to_string(v)) {}
    bool isEmpty() const { return empty(); }
};
inline String operator+(const String& a, const char* b) { return String(std::string(a) + b); }
inline String operator+(const char* a, const String& b) { return String(std::string(a) + std::string(b)); }
inline String operator+(const String& a, const String& b) { return String(std::string(a) + std::string(b)); }
struct AsyncWebServer { explicit AsyncWebServer(int) {} };
struct AsyncWebServerRequest {};

uint32_t fake_ms = 0;
uint32_t millis() { return fake_ms; }
void delay(uint32_t ms) { fake_ms += ms; }
constexpr int MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2, U_FLASH = 0;
size_t heap_caps_get_free_size(int) { return 100000; }
size_t heap_caps_get_largest_free_block(int) { return 50000; }
struct { int RSSI() { return -67; } } WiFi;
struct esp_partition_t { size_t size = 3 * 1024 * 1024; } partition;
const esp_partition_t* esp_ota_get_next_update_partition(void*) { return &partition; }
const uint8_t x509_crt_imported_bundle_bin_start[1] = {0};
const uint8_t x509_crt_imported_bundle_bin_end[1] = {0};
constexpr const char* GITHUB_RELEASE_MIRROR_BASE = "https://example.test/firmware/";
constexpr const char* HW_RELEASE_FIRMWARE_SUFFIX = "-waveshare-164-v2";
@CONSTANTS@

// ---- fake server -------------------------------------------------------------
std::vector<uint8_t> image;
std::vector<size_t> drop_at;        // absolute offsets where a connection drops
bool ignore_range = false;
int refuse_resumes = 0;             // number of resume requests answered with HTTP 500
std::vector<std::string> requests;  // Range header per request ("" for none)

struct WiFiClient {
    size_t position = 0, stop = 0;  // this connection delivers bytes up to "stop"
    bool open = false;
    int available() {
        if (!open || position >= stop) return 0;
        return int(std::min<size_t>(stop - position, 700));
    }
    bool connected() { return open && position < stop; }
    int readBytes(uint8_t* out, size_t n) {
        const size_t count = std::min<size_t>(n, size_t(available()));
        std::memcpy(out, image.data() + position, count);
        position += count;
        return int(count);
    }
    void start(size_t from) {
        position = from; stop = image.size(); open = true;
        // The next pending drop point ends this connection; each drop happens once.
        auto next = std::find_if(drop_at.begin(), drop_at.end(), [&](size_t d) { return d > from; });
        if (next != drop_at.end()) { stop = *next; drop_at.erase(next); }
    }
};
struct WiFiClientSecure : WiFiClient {
    void setCACertBundle(const uint8_t*, size_t) {}
    void setTimeout(int) {}
    void setHandshakeTimeout(int) {}
    int lastError(char* buffer, size_t) { buffer[0] = 0; return 0; }
};
constexpr int HTTP_CODE_OK = 200, HTTP_CODE_PARTIAL_CONTENT = 206;
constexpr int HTTPC_STRICT_FOLLOW_REDIRECTS = 1;
struct HTTPClient {
    WiFiClientSecure* client = nullptr;
    std::string range;
    int size = -1;
    void useHTTP10(bool) {}
    void setConnectTimeout(int) {}
    void setTimeout(int) {}
    void setFollowRedirects(int) {}
    void setUserAgent(const char*) {}
    void addHeader(const char* name, const String& value) { if (std::string(name) == "Range") range = value; }
    bool begin(WiFiClientSecure& c, const String&) { client = &c; range.clear(); return true; }
    int GET() {
        requests.push_back(range);
        size_t start = 0;
        if (!range.empty()) {
            if (refuse_resumes > 0) { --refuse_resumes; size = -1; client->open = false; return 500; }
            start = ignore_range ? 0 : std::stoul(range.substr(6));
        }
        client->start(start);
        size = int(image.size() - start);
        return (range.empty() || ignore_range) ? HTTP_CODE_OK : HTTP_CODE_PARTIAL_CONTENT;
    }
    int getSize() { return size; }
    WiFiClient* getStreamPtr() { return client; }
    bool connected() { return client && client->connected(); }
    void end() { if (client) client->open = false; }
    static String errorToString(int) { return "error"; }
};

// ---- fake flash writer ---------------------------------------------------------
struct Update {
    std::vector<uint8_t> written;
    bool opened = false, aborted = false;
    size_t expected = 0;
    bool begin(size_t size, int) { opened = true; aborted = false; written.clear(); expected = size; return true; }
    size_t write(uint8_t* data, size_t len) { written.insert(written.end(), data, data + len); return len; }
    bool end(bool) { opened = false; return written.size() == expected && written == image; }
    void abort() { opened = false; aborted = true; }
    const char* errorString() { return "fake"; }
} web_firmware_update;

struct Grinder { void stop() {} } grinder;
struct HardwareManager { Grinder* get_grinder() { return &grinder; } } hardware;

#define private public
@HEADER@
#undef private
DeviceWebServer device_web_server;
int finished = -1;
void DeviceWebServer::finish_ota(bool success) { finished = success ? 1 : 0; }
@METHOD@

static void run(size_t size, std::vector<size_t> drops, bool ignore, int refuse, bool expect_success) {
    image.resize(size);
    for (size_t i = 0; i < size; ++i) image[i] = uint8_t((i * 131 + 7) & 0xFF);
    image[0] = 0xE9;
    drop_at = drops; ignore_range = ignore; refuse_resumes = refuse; requests.clear();
    finished = -1; fake_ms = 0;
    DeviceWebServer web; web.hardware_manager_ = &hardware;
    web.perform_github_ota("v9.9.9");
    assert(finished == (expect_success ? 1 : 0));
    if (expect_success) {
        assert(web_firmware_update.written == image);
        assert(web.ota_received_.load() == size);
    } else {
        assert(web_firmware_update.aborted);
    }
}

int main() {
    run(50000, {}, false, 0, true);
    assert(requests.size() == 1 && requests[0].empty());

    run(50000, {10000, 30001}, false, 0, true);           // two drops, Range honoured
    assert(requests.size() == 3);
    assert(requests[1] == "bytes=10000-" && requests[2] == "bytes=30001-");

    run(50000, {12345}, true, 0, true);                   // server ignores Range
    assert(requests.size() == 2 && requests[1] == "bytes=12345-");

    run(50000, {20000}, false, 2, true);                  // two refused resumes, then success
    assert(requests.size() == 4);

    std::vector<size_t> many;
    for (size_t i = 1; i <= OTA_MAX_DOWNLOAD_RESUMES + 1; ++i) many.push_back(i * 5000);
    run(50000, many, false, 0, false);                    // more drops than allowed resumes
    assert(int(requests.size()) == OTA_MAX_DOWNLOAD_RESUMES + 1);

    run(50000, {1}, false, 0, true);                      // drop right after the first byte
    std::puts("github OTA resume tests passed");
}
'''


class GithubOtaResumeTest(unittest.TestCase):
    def test_download_resumes_after_drops(self):
        source = (ROOT / "src/network/device_web_server.cpp").read_text()
        method = function(source, "void DeviceWebServer::perform_github_ota(")
        constant = re.search(r"constexpr int OTA_MAX_DOWNLOAD_RESUMES = \d+;", source).group()
        header = (ROOT / "src/network/device_web_server.h").read_text()
        header = re.sub(r'^#(?:include|pragma).*$', '', header, flags=re.M)
        harness = (HARNESS.replace("@CONSTANTS@", constant)
                   .replace("@HEADER@", header).replace("@METHOD@", method))
        with tempfile.TemporaryDirectory() as directory:
            cpp, binary = Path(directory) / "ota.cpp", Path(directory) / "ota"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined",
                            "-I", str(ROOT / "src"), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=60)


if __name__ == "__main__":
    unittest.main()
