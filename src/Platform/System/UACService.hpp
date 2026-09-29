#pragma once

namespace UAC {

    bool IsElevated();

    /// Relaunches elevated and terminates this process. False when the user declined or cannot elevate.
    bool RequestElevation();

    bool IsSkipUACEnabled();

    bool EnableSkipUAC();
    bool DisableSkipUAC();

    bool RunWithSkipUAC();

}

