//
// Created by GitHub Copilot
//

#ifndef EASYMIC_AUDIOFILEVALIDATOR_HPP
#define EASYMIC_AUDIOFILEVALIDATOR_HPP

#include <windows.h>
#include <string>
#include <fstream>

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

private:
    struct WavHeader {
        char riff[4];           // "RIFF"
        uint32_t fileSize;      // File size - 8
        char wave[4];           // "WAVE"
        char fmt[4];            // "fmt "
        uint32_t fmtSize;       // Format chunk size
        uint16_t audioFormat;   // Audio format (1 = PCM)
        uint16_t channels;      // Number of channels
        uint32_t sampleRate;    // Sample rate
        uint32_t byteRate;      // Byte rate
        uint16_t blockAlign;    // Block align
        uint16_t bitsPerSample; // Bits per sample
        char data[4];           // "data"
        uint32_t dataSize;      // Data chunk size
    };

    static bool ReadWavHeader(std::ifstream& file, WavHeader& header);
    static bool ValidateWavHeader(const WavHeader& header);
    static float CalculateDuration(const WavHeader& header);
};

// Implementation
inline WavValidationResult AudioFileValidator::ValidateWavFile(const std::string& filePath, float maxDurationSeconds) {
    WavValidationResult result;

    std::ifstream file(filePath, std::ios::binary);
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
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "WAV Files (*.wav)\0*.wav\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameA(&ofn)) {
        return std::string(szFile);
    }
    return "";
}

inline bool AudioFileValidator::ReadWavHeader(std::ifstream& file, WavHeader& header) {
    file.read(reinterpret_cast<char*>(&header), sizeof(WavHeader));
    return file.gcount() == sizeof(WavHeader);
}

inline bool AudioFileValidator::ValidateWavHeader(const WavHeader& header) {
    // Check RIFF signature
    if (strncmp(header.riff, "RIFF", 4) != 0) {
        return false;
    }

    // Check WAVE signature
    if (strncmp(header.wave, "WAVE", 4) != 0) {
        return false;
    }

    // Check fmt signature
    if (strncmp(header.fmt, "fmt ", 4) != 0) {
        return false;
    }

    // Check data signature
    if (strncmp(header.data, "data", 4) != 0) {
        return false;
    }

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
