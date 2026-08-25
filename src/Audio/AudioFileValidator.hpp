//
// Created by GitHub Copilot
//

#ifndef EASYMIC_AUDIOFILEVALIDATOR_HPP
#define EASYMIC_AUDIOFILEVALIDATOR_HPP

#include <cstddef>
#include <cstring>
#include <fstream>
#include <string>
#include <windows.h>

#include "Str.hpp"

struct WavValidationResult {
    bool isValid = false;
    std::string errorMessage;
    float durationSeconds = 0.0f;
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
};

class AudioFileValidator {
public:
    /**
     * Validates a WAV file for maximum duration and format correctness
     * @param filePath Path to the WAV file
     * @param maxDurationSeconds Maximum allowed duration (default: 3.0 seconds)
     * @return WavValidationResult with validation details
     */
    static WavValidationResult ValidateWavFile(const std::string& filePath, float maxDurationSeconds = 3.0f);

    /**
     * Shows a file picker dialog for selecting WAV files
     * @param hWnd Parent window handle
     * @param title Dialog title
     * @return Selected file path or empty string if cancelled
     */
    static std::string ShowWavFileDialog(HWND hWnd, const char* title = "Select WAV File");

    /**
     * Picks a WAV file and validates it, reporting the reason to the user when it is rejected
     * @param result receives the file path on success
     * @return false when cancelled or invalid
     */
    static bool PickValidWavFile(HWND hWnd, const char* title, std::string& result);

private:
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

    static bool ReadWavHeader(std::ifstream& file, WavHeader& header);
    static bool ValidateWavHeader(const WavHeader& header);
    static float CalculateDuration(const WavHeader& header);
};

// Implementation
inline WavValidationResult AudioFileValidator::ValidateWavFile(const std::string& filePath, float maxDurationSeconds) {
    WavValidationResult result;

    // The wide overload is the only one that opens a non-ASCII path: the narrow one is ACP
    std::ifstream file(Str::Utf8ToWide(filePath), std::ios::binary);
    if (!file.is_open()) {
        result.errorMessage = "Cannot open file";
        return result;
    }

    WavHeader header;
    if (!ReadWavHeader(file, header)) {
        result.errorMessage = "Invalid WAV file format";
        return result;
    }

    if (!ValidateWavHeader(header)) {
        result.errorMessage = "Unsupported WAV format";
        return result;
    }

    result.durationSeconds = CalculateDuration(header);
    result.sampleRate = header.sampleRate;
    result.channels = header.channels;
    result.bitsPerSample = header.bitsPerSample;

    if (result.durationSeconds > maxDurationSeconds) {
        result.errorMessage = "File duration exceeds " + std::to_string(maxDurationSeconds) + " seconds";
        return result;
    }

    result.isValid = true;
    return result;
}

inline std::string AudioFileValidator::ShowWavFileDialog(HWND hWnd, const char* title) {
    OPENFILENAMEW ofn;
    wchar_t szFile[MAX_PATH] = { 0 };
    const std::wstring caption = Str::Utf8ToWide(title);

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"WAV Files (*.wav)\0*.wav\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = nullptr;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = nullptr;
    ofn.lpstrTitle = caption.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    return GetOpenFileNameW(&ofn) ? Str::WideToUtf8(szFile) : std::string{};
}

inline bool AudioFileValidator::PickValidWavFile(HWND hWnd, const char* title, std::string& result) {
    const std::string selectedFile = ShowWavFileDialog(hWnd, title);
    if (selectedFile.empty()) {
        return false;
    }

    if (const WavValidationResult validation = ValidateWavFile(selectedFile); !validation.isValid) {
        MessageBoxW(hWnd, Str::Utf8ToWide("Invalid WAV file: " + validation.errorMessage).c_str(),
                    L"File Validation Error", MB_OK | MB_ICONERROR);
        return false;
    }

    result = selectedFile;
    return true;
}

/**
 * @brief Walks the RIFF chunk list for "fmt " and "data".
 *
 * They are not at fixed offsets: ffmpeg and Audacity happily emit LIST or fact chunks in
 * between, and reading a fixed 44-byte header there yields a garbage duration.
 */
inline bool AudioFileValidator::ReadWavHeader(std::ifstream& file, WavHeader& header) {
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

inline bool AudioFileValidator::ValidateWavHeader(const WavHeader& header) {
    // Check audio format (PCM = 1)
    if (header.audioFormat != 1) {
        return false;
    }

    // Basic sanity checks
    if (header.channels == 0 || header.channels > 8) {
        return false;
    }

    if (header.sampleRate < 8000 || header.sampleRate > 192000) {
        return false;
    }

    if (header.bitsPerSample != 8 && header.bitsPerSample != 16 && header.bitsPerSample != 24 && header.bitsPerSample != 32) {
        return false;
    }

    return true;
}

inline float AudioFileValidator::CalculateDuration(const WavHeader& header) {
    if (header.byteRate == 0) {
        return 0.0f;
    }
    return static_cast<float>(header.dataSize) / header.byteRate;
}

#endif //EASYMIC_AUDIOFILEVALIDATOR_HPP
