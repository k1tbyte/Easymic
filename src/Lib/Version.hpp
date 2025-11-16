#ifndef EASYMIC_VERSION_HPP
#define EASYMIC_VERSION_HPP

#include <string>
#include <windows.h>
#include <winver.h>
#include <vector>

class Version {
public:
    Version();
    Version(int major, int minor, int patch, int build = 0);
    Version(const std::string& versionString);

    // Getters
    int GetMajor() const { return major_; }
    int GetMinor() const { return minor_; }
    int GetPatch() const { return patch_; }
    int GetBuild() const { return build_; }

    // Formatted output
    std::string GetFormatted() const;
    std::string GetShortFormat() const; // e.g., "1.0.0"
    std::string GetFullFormat() const;  // e.g., "1.0.0.0"

    // Version info
    bool IsPreRelease() const { return isPreRelease_; }
    std::string GetPreReleaseTag() const { return preReleaseTag_; }

    // Comparison operators
    bool operator>(const Version& other) const;
    bool operator<(const Version& other) const;
    bool operator==(const Version& other) const;
    bool operator!=(const Version& other) const;

    // Static method to get current application version
    static Version GetCurrentVersion();

private:
    int major_ = 1;
    int minor_ = 0;
    int patch_ = 0;
    int build_ = 0;
    bool isPreRelease_ = false;
    std::string preReleaseTag_;

    void ParseVersionString(const std::string& versionString);
    static Version LoadFromVersionResource();
};

// Global version instance
extern Version g_AppVersion;

#endif //EASYMIC_VERSION_HPP
