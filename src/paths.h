#pragma once

#include <QString>

// Runtime sockets and per-user config locations. Directories are created on
// first use with mode 0700 because they hold cookies and control sockets.
namespace Paths {
QString runtimeDir();
QString controlSocket();
QString mpvSocket();
QString cavaConfig();
QString configDir();
QString authFile();
QString cookiesFile();
}
