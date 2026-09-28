#pragma once

#include <string>

/// major.minor.patch.build, read from the module's version resource or parsed from a release tag.
class Version {
public:
    /// The running build.
    Version();
    /// Parses "v1.2.3", "1.2.3.4" or "1.2.3-beta" - a pre-release suffix is dropped, and anything
    /// unparseable simply leaves that component at zero rather than throwing at a caller who is
    /// handing us whatever the release feed said.
    explicit Version(const std::string& versionString);

    std::string GetFullFormat() const; // e.g. "1.0.0.0"

    bool operator>(const Version& other) const;

private:
    Version(int major, int minor, int patch, int build);

    int _major = 0;
    int _minor = 0;
    int _patch = 0;
    int _build = 0;

    static Version LoadFromVersionResource();
};

// Global version instance
extern Version g_AppVersion;

