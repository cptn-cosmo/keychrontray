#pragma once

#include <QStringList>
#include <optional>

// Handles subcommands (keychrontray set-profile X, ...). Returns the exit code, or nullopt if the
// arguments mean "start the tray app".
std::optional<int> runCli(const QStringList &args);
