#pragma once

#include <string>

class Version {
public:
    /// "v1.2.3", "1.2.3.4" or "1.2.3-beta": a pre-release suffix is dropped, an unparseable component stays zero.
    explicit Version(const std::string& versionString);

    /// Read on first use, so version.dll stays unloaded until something asks.
    static const Version& App();

    std::string GetFullFormat() const;

    bool operator>(const Version& other) const;

private:
    Version() = default;

    int _major = 0;
    int _minor = 0;
    int _patch = 0;
    int _build = 0;

    static Version LoadFromVersionResource();
};

