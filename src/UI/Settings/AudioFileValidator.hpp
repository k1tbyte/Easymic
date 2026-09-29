#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <windows.h>

#include "File.hpp"
#include "Str.hpp"

/**
 * @brief Picks a short PCM WAV file, or tells the user why the one they picked will not do.
 *
 * Nothing here reports format details: an action sound either plays or it does not, and the only
 * thing a caller has ever needed is the path and the reason it was rejected.
 */
namespace AudioFileValidator {

    inline constexpr float MaxDurationSeconds = 3.0f;

    namespace Detail {
        /// "fmt " chunk payload plus the size of the "data" chunk found for it.
        struct WavHeader {
            uint16_t audioFormat;   // Audio format (1 = PCM)
            uint16_t channels;      // Number of channels
            uint32_t sampleRate;    // Sample rate
            uint32_t byteRate;      // Byte rate
            uint16_t blockAlign;    // Block align
            uint16_t bitsPerSample; // Bits per sample
            uint32_t dataSize;      // Data chunk size
        };

        // The first six fields are read as one 16 byte block straight out of the "fmt " chunk
        static_assert(offsetof(WavHeader, bitsPerSample) == 14);

        /**
         * @brief Walks the RIFF chunk list for "fmt " and "data".
         *
         * They are not at fixed offsets: ffmpeg and Audacity happily emit LIST or fact chunks in
         * between, and reading a fixed 44-byte header there yields a garbage duration.
         */
        inline bool ReadWavHeader(const std::string_view bytes, WavHeader& header) {
            if (bytes.size() < 12 || !bytes.starts_with("RIFF") || bytes.substr(8, 4) != "WAVE") {
                return false;
            }

            bool hasFormat = false;
            header.dataSize = 0;
            for (size_t at = 12; at + 8 <= bytes.size();) {
                const std::string_view id = bytes.substr(at, 4);
                uint32_t size;
                memcpy(&size, bytes.data() + at + 4, sizeof size);
                at += 8;

                if (id == "fmt " && size >= 16 && at + 16 <= bytes.size()) {
                    memcpy(&header.audioFormat, bytes.data() + at, 16);
                    hasFormat = true;
                } else if (id == "data") {
                    header.dataSize = size;
                    break; // Everything past the samples is metadata
                }
                at += size + (size & 1); // Chunks are word aligned
            }

            return hasFormat && header.dataSize > 0;
        }

        inline bool IsSupportedFormat(const WavHeader& header) {
            return header.audioFormat == 1 // PCM
                   && header.channels > 0 && header.channels <= 8
                   && header.sampleRate >= 8000 && header.sampleRate <= 192000
                   && (header.bitsPerSample == 8 || header.bitsPerSample == 16
                       || header.bitsPerSample == 24 || header.bitsPerSample == 32);
        }

        /// @return why the file was rejected, empty when it is fine.
        inline std::string Reject(const std::string& filePath) {
            // The chunk headers sit up front; the samples are never read
            const auto bytes = File::Read(Str::Utf8ToWide(filePath).c_str(), 1 << 20);
            if (!bytes) {
                return "Cannot open file";
            }

            WavHeader header;
            if (!ReadWavHeader(*bytes, header)) {
                return "Invalid WAV file format";
            }

            if (!IsSupportedFormat(header)) {
                return "Unsupported WAV format";
            }

            const float duration = header.byteRate == 0
                                       ? 0.0f
                                       : static_cast<float>(header.dataSize) / header.byteRate;

            return duration > MaxDurationSeconds
                       ? "File duration exceeds " + std::to_string(MaxDurationSeconds) + " seconds"
                       : std::string{};
        }

        inline std::string ShowWavFileDialog(HWND hWnd, const char* title) {
            wchar_t selected[MAX_PATH] = {};
            const std::wstring caption = Str::Utf8ToWide(title);

            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFile = selected;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"WAV Files (*.wav)\0*.wav\0All Files (*.*)\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrTitle = caption.c_str();
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

            return GetOpenFileNameW(&ofn) ? Str::WideToUtf8(selected) : std::string{};
        }
    }

    /**
     * @brief Picks a WAV file and validates it, reporting the reason when it is rejected.
     * @param result receives the file path on success
     * @return false when cancelled or invalid
     */
    inline bool PickValidWavFile(HWND hWnd, const char* title, std::string& result) {
        const std::string selectedFile = Detail::ShowWavFileDialog(hWnd, title);
        if (selectedFile.empty()) {
            return false;
        }

        if (const std::string reason = Detail::Reject(selectedFile); !reason.empty()) {
            MessageBoxW(hWnd, Str::Utf8ToWide("Invalid WAV file: " + reason).c_str(),
                        L"File Validation Error", MB_OK | MB_ICONERROR);
            return false;
        }

        result = selectedFile;
        return true;
    }
}

