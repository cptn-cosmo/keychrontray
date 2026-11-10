#pragma once

#include <QByteArray>
#include <QVariantMap>

// Minimal parser for Valve's text KeyValues format (libraryfolders.vdf, appmanifest_*.acf, localconfig.vdf).
// Returns nested QVariantMaps; values are QStrings. Keys are matched case-sensitively as written.
namespace Vdf {
QVariantMap parse(const QByteArray &text, bool *ok = nullptr);
// Case-insensitive child lookup (Steam is inconsistent: "Apps" vs "apps").
QVariant child(const QVariantMap &m, const QString &key);
}
