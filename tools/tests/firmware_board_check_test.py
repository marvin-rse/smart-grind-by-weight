import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "network/firmware_board_check.h"

static FirmwareBoard scan(const std::string& image, size_t chunk) {
    FirmwareBoardScanner scanner{};
    for (size_t offset = 0; offset < image.size(); offset += chunk) {
        const size_t len = std::min(chunk, image.size() - offset);
        scanner.feed(reinterpret_cast<const uint8_t*>(image.data() + offset), len);
    }
    return scanner.result();
}

static void expect(const std::string& image, FirmwareBoard board) {
    for (size_t chunk : {1u, 3u, 7u, 64u, 4096u}) assert(scan(image, chunk) == board);
}

int main(int argc, char** argv) {
    const std::string filler(5000, '\xe9');
    const std::string v1_path = ".pio/libdeps/waveshare-esp32s3-touch-amoled-164/lvgl/src/core/lv_obj.c";
    const std::string v2_path = ".pio/libdeps/waveshare-esp32s3-touch-amoled-164-v2/lvgl/src/core/lv_obj.c";

    expect(filler, FirmwareBoard::UNKNOWN);
    expect(filler + v1_path + filler, FirmwareBoard::V1);
    expect(filler + v2_path + filler, FirmwareBoard::V2);
    // Explicit marker wins over paths and survives chunk boundaries.
    expect(filler + v1_path + std::string("SMARTGRIND-BOARD:V2") + filler, FirmwareBoard::V2);
    expect(std::string("xxSMARTGRIND-BOARD:V1\0", 22) + filler, FirmwareBoard::V1);
    // A prefix without a board digit (e.g. the search code itself) is ignored.
    expect(std::string("SMARTGRIND-BOARD:V\0", 19) + v2_path, FirmwareBoard::V2);
    // Contradicting evidence is never trusted.
    expect(std::string("SMARTGRIND-BOARD:V1 SMARTGRIND-BOARD:V2"), FirmwareBoard::UNKNOWN);
    expect(v1_path + filler + v2_path, FirmwareBoard::UNKNOWN);
    // Overlapping partial matches (KMP fallback).
    expect("touch-amoled-touch-amoled-164-v2/", FirmwareBoard::V2);
    expect("SMARTGRINSMARTGRIND-BOARD:V1", FirmwareBoard::V1);

    assert(std::string(firmware_board_name(FirmwareBoard::V2)) == "V2");
    assert(std::string(firmware_board_marker()) == "SMARTGRIND-BOARD:V2");
    assert(firmware_board_this_build() == FirmwareBoard::V2);

    // Real images passed on the command line as <expected> <path> pairs.
    for (int i = 1; i + 1 < argc; i += 2) {
        FILE* file = std::fopen(argv[i + 1], "rb");
        assert(file);
        std::string image;
        char buffer[8192];
        size_t read;
        while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) image.append(buffer, read);
        std::fclose(file);
        const FirmwareBoard expected = std::string(argv[i]) == "v1" ? FirmwareBoard::V1 : FirmwareBoard::V2;
        assert(scan(image, 4096) == expected);
    }
    std::puts("firmware board check passed");
}
'''


class FirmwareBoardCheckTest(unittest.TestCase):
    def test_detects_board_generation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "network").mkdir()
            (root / "config").mkdir()
            for name in ("firmware_board_check.h", "firmware_board_check.cpp"):
                (root / "network" / name).write_text((ROOT / "src/network" / name).read_text())
            (root / "config" / "constants.h").write_text("#pragma once\n#define HW_DISPLAY_VARIANT_V2 1\n")
            (root / "main.cpp").write_text(HARNESS)
            binary = root / "check"
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-I", str(root),
                            str(root / "main.cpp"), str(root / "network" / "firmware_board_check.cpp"),
                            "-o", str(binary)], check=True)
            images = []
            for image in sorted((ROOT / "firmware").glob("*/*.bin")):
                images += ["v2" if "v2" in image.parent.name else "v1", str(image)]
            subprocess.run([str(binary), *images], check=True, timeout=60)


if __name__ == "__main__":
    unittest.main()
