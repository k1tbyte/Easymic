#ifndef EASYMIC_AUDIOFILEVALIDATOR_HPP
#define EASYMIC_AUDIOFILEVALIDATOR_HPP

#include <cstddef>
#include <cstring>
#include <fstream>
#include <string>
#include <windows.h>

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
        inline bool ReadWavHeader(std::ifstream& file, WavHeader& header) {
            char riff[4], wave[4];
            uint32_t riffSize;

            file.read(riff, 4);
            file.read(reinterpret_cast<char*>(&riffSize), 4);
            file.read(wave, 4);

            if (!file || strncmp(riff, "RIFF", 4) != 0 || strncmp(wave, "WAVE", 4) != 0) {
                return false;
            }

            bool hasFormat = false;
            header.dataSize = 0;

            while (file) {
                char id[4];
                uint32_t size;
                file.read(id, 4);
                file.read(reinterpret_cast<char*>(&size), 4);
                if (!file) {
                    break;
                }

                if (strncmp(id, "fmt ", 4) == 0 && size >= 16) {
                    file.read(reinterpret_cast<char*>(&header.audioFormat), 16);
                    hasFormat = file.good();
                    size -= 16;
                } else if (strncmp(id, "data", 4) == 0) {
                    header.dataSize = size;
                    break; // Everything past the samples is metadata
                }

                file.seekg(size + (size & 1), std::ios::cur); // Chunks are word aligned
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
            // The wide overload is the only one that opens a non-ASCII path: the narrow one is ACP
            std::ifstream file(Str::Utf8ToWide(filePath), std::ios::binary);
            if (!file.is_open()) {
                return "Cannot open file";
            }

            WavHeader header;
            if (!ReadWavHeader(file, header)) {
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

#endif //EASYMIC_AUDIOFILEVALIDATOR_HPP
