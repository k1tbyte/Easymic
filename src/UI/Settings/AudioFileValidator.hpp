#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <windows.h>

#include "File.hpp"
#include "Str.hpp"

/// Picks a short PCM WAV file and tells the user why a rejected one will not do.
namespace AudioFileValidator {

    inline constexpr float MaxDurationSeconds = 3.0f;

    namespace Detail {
        struct WavHeader {
            uint16_t audioFormat;
            uint16_t channels;
            uint32_t sampleRate;
            uint32_t byteRate;
            uint16_t blockAlign;
            uint16_t bitsPerSample;
            uint32_t dataSize;
        };

        // The first six fields are read as one 16 byte block straight out of the "fmt " chunk
        static_assert(offsetof(WavHeader, bitsPerSample) == 14);

        /// Walks the chunks: "fmt " and "data" are not at fixed offsets, ffmpeg and Audacity emit LIST or fact chunks between them.
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
                    break;
                }
                at += size + (size & 1);
            }

            return hasFormat && header.dataSize > 0;
        }

        inline bool IsSupportedFormat(const WavHeader& header) {
            return header.audioFormat == 1
                   && header.channels > 0 && header.channels <= 8
                   && header.sampleRate >= 8000 && header.sampleRate <= 192000
                   && (header.bitsPerSample == 8 || header.bitsPerSample == 16
                       || header.bitsPerSample == 24 || header.bitsPerSample == 32);
        }

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

