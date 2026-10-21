#include "firmware_board_check.h"

#include "../config/constants.h"

namespace {

#if HW_DISPLAY_VARIANT_V2
constexpr char kBoardMarker[] = "SMARTGRIND-BOARD:V2";
#else
constexpr char kBoardMarker[] = "SMARTGRIND-BOARD:V1";
#endif

// Search patterns, XOR-encoded with kPatternKey. Storing them plainly would
// put them into every image and make each image match the other generation.
//   marker prefix: "SMARTGRIND-BOARD:V"
//   V1 path:       "touch-amoled-164/"
//   V2 path:       "touch-amoled-164-v2/"
constexpr uint8_t kEncodedMarkerPrefix[] = {
    0x09, 0x17, 0x1b, 0x08, 0x0e, 0x1d, 0x08, 0x13, 0x14, 0x1e,
    0x77, 0x18, 0x15, 0x1b, 0x08, 0x1e, 0x60, 0x0c};
constexpr uint8_t kEncodedV1Path[] = {
    0x2e, 0x35, 0x2f, 0x39, 0x32, 0x77, 0x3b, 0x37, 0x35, 0x36,
    0x3f, 0x3e, 0x77, 0x6b, 0x6c, 0x6e, 0x75};
constexpr uint8_t kEncodedV2Path[] = {
    0x2e, 0x35, 0x2f, 0x39, 0x32, 0x77, 0x3b, 0x37, 0x35, 0x36,
    0x3f, 0x3e, 0x77, 0x6b, 0x6c, 0x6e, 0x77, 0x2c, 0x68, 0x75};
// Volatile so the compiler cannot decode the patterns at build time.
volatile uint8_t kPatternKey = 0x5A;

constexpr size_t kMaxPatternLength = 24;

// One pattern plus its Knuth-Morris-Pratt failure table, so matches that
// straddle chunk boundaries are found without buffering the stream.
struct Pattern {
    uint8_t bytes[kMaxPatternLength];
    uint8_t failure[kMaxPatternLength];
    uint8_t length;

    template <size_t N>
    explicit Pattern(const uint8_t (&encoded)[N]) : bytes{}, failure{}, length(N) {
        static_assert(N <= kMaxPatternLength, "pattern too long");
        const uint8_t key = kPatternKey;
        for (size_t i = 0; i < N; ++i) bytes[i] = encoded[i] ^ key;
        uint8_t k = 0;
        for (uint8_t i = 1; i < length; ++i) {
            while (k > 0 && bytes[i] != bytes[k]) k = failure[k - 1];
            if (bytes[i] == bytes[k]) ++k;
            failure[i] = k;
        }
    }

    // Advances the match state by one byte; returns true on a full match.
    bool advance(uint8_t& matched, uint8_t byte) const {
        while (matched > 0 && bytes[matched] != byte) matched = failure[matched - 1];
        if (bytes[matched] == byte) ++matched;
        if (matched < length) return false;
        matched = failure[length - 1];
        return true;
    }
};

struct Patterns {
    Pattern marker_prefix{kEncodedMarkerPrefix};
    Pattern v1_path{kEncodedV1Path};
    Pattern v2_path{kEncodedV2Path};
};

const Patterns& patterns() {
    static const Patterns instance;  // thread-safe one-time initialisation
    return instance;
}

}  // namespace

void FirmwareBoardScanner::feed(const uint8_t* data, size_t len) {
    if (!data) return;
    const Patterns& p = patterns();
    for (size_t i = 0; i < len; ++i) {
        const uint8_t byte = data[i];
        if (awaiting_marker_digit) {
            awaiting_marker_digit = false;
            const FirmwareBoard found = byte == '1'   ? FirmwareBoard::V1
                                        : byte == '2' ? FirmwareBoard::V2
                                                      : FirmwareBoard::UNKNOWN;
            if (found != FirmwareBoard::UNKNOWN) {
                if (marker_board != FirmwareBoard::UNKNOWN && marker_board != found) {
                    marker_conflict = true;
                }
                marker_board = found;
            }
        }
        if (p.marker_prefix.advance(marker_matched, byte)) awaiting_marker_digit = true;
        if (p.v1_path.advance(v1_path_matched, byte)) saw_v1_path = true;
        if (p.v2_path.advance(v2_path_matched, byte)) saw_v2_path = true;
    }
}

FirmwareBoard FirmwareBoardScanner::result() const {
    if (marker_board != FirmwareBoard::UNKNOWN && !marker_conflict) return marker_board;
    if (marker_conflict) return FirmwareBoard::UNKNOWN;
    if (saw_v1_path != saw_v2_path) return saw_v2_path ? FirmwareBoard::V2 : FirmwareBoard::V1;
    return FirmwareBoard::UNKNOWN;
}

FirmwareBoard firmware_board_this_build() {
    return HW_DISPLAY_VARIANT_V2 ? FirmwareBoard::V2 : FirmwareBoard::V1;
}

const char* firmware_board_name(FirmwareBoard board) {
    switch (board) {
        case FirmwareBoard::V1: return "V1";
        case FirmwareBoard::V2: return "V2";
        default: return "unknown";
    }
}

const char* firmware_board_marker() {
    return kBoardMarker;
}
