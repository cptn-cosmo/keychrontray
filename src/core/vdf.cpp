#include "vdf.h"

namespace {
struct Parser {
    const QByteArray &s;
    qsizetype i = 0;
    bool error = false;

    void skipWs()
    {
        while (i < s.size()) {
            const char c = s[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++i;
            } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
                while (i < s.size() && s[i] != '\n') {
                    ++i;
                }
            } else {
                break;
            }
        }
    }

    // Quoted or bare token.
    bool token(QString *out)
    {
        skipWs();
        if (i >= s.size()) {
            return false;
        }
        QByteArray buf;
        if (s[i] == '"') {
            ++i;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    ++i;
                    const char e = s[i];
                    buf += (e == 'n') ? '\n' : (e == 't') ? '\t' : e;
                } else {
                    buf += s[i];
                }
                ++i;
            }
            if (i >= s.size()) {
                error = true;
                return false;
            }
            ++i; // closing quote
        } else {
            while (i < s.size() && !QByteArrayView(" \t\r\n{}\"").contains(s[i])) {
                buf += s[i++];
            }
        }
        *out = QString::fromUtf8(buf);
        return true;
    }

    QVariantMap object(bool top)
    {
        QVariantMap m;
        while (true) {
            skipWs();
            if (i >= s.size()) {
                if (!top) {
                    error = true;
                }
                return m;
            }
            if (s[i] == '}') {
                if (top) {
                    error = true;
                }
                ++i;
                return m;
            }
            QString key;
            if (!token(&key)) {
                return m;
            }
            skipWs();
            if (i < s.size() && s[i] == '{') {
                ++i;
                m.insert(key, object(false));
            } else {
                QString value;
                if (!token(&value)) {
                    error = true;
                    return m;
                }
                m.insert(key, value);
            }
            if (error) {
                return m;
            }
            // Skip conditionals like [$WIN32]
            skipWs();
            if (i < s.size() && s[i] == '[') {
                while (i < s.size() && s[i] != ']') {
                    ++i;
                }
                ++i;
            }
        }
    }
};
}

QVariantMap Vdf::parse(const QByteArray &text, bool *ok)
{
    Parser p{text};
    QVariantMap m = p.object(true);
    if (ok) {
        *ok = !p.error;
    }
    return m;
}

QVariant Vdf::child(const QVariantMap &m, const QString &key)
{
    auto it = m.constFind(key);
    if (it != m.constEnd()) {
        return *it;
    }
    for (auto i = m.constBegin(); i != m.constEnd(); ++i) {
        if (i.key().compare(key, Qt::CaseInsensitive) == 0) {
            return i.value();
        }
    }
    return {};
}
