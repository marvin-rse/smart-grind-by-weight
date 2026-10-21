#pragma once

#include <cstddef>
#include <cstdint>

// Identifies which Waveshare 1.64" board generation a firmware image targets,
// so an upload for the other generation can be refused before it is installed.
//
// Images built by this project carry an explicit marker
// ("SMARTGRIND-BOARD:V1" / "...:V2"). Older releases are recognised by the
// PlatformIO environment path the toolchain embeds in every image
// (".../touch-amoled-164/" for V1, ".../touch-amoled-164-v2/" for V2).
// The search patterns are assembled at runtime so this code never matches
// itself inside an image.
enum class FirmwareBoard : uint8_t {
    UNKNOWN = 0,
    V1 = 1,
    V2 = 2,
};

// Streaming scanner: feed the image in chunks of any size. Plain data with no
// constructor so it can live in calloc'd request state; zero-initialised
// memory is a valid starting state.
struct FirmwareBoardScanner {
    void feed(const uint8_t* data, size_t len);
    FirmwareBoard result() const;

    uint8_t marker_matched;
    uint8_t v1_path_matched;
    uint8_t v2_path_matched;
    bool awaiting_marker_digit;
    FirmwareBoard marker_board;
    bool marker_conflict;
    bool saw_v1_path;
    bool saw_v2_path;
};

// Board generation this firmware was built for.
FirmwareBoard firmware_board_this_build();
const char* firmware_board_name(FirmwareBoard board);
// The marker embedded in this image; logged at startup so the linker keeps it.
const char* firmware_board_marker();
