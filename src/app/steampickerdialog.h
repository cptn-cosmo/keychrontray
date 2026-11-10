#pragma once

#include "steamlibrary.h"

#include <QDialog>
#include <QSet>
#include <optional>

class AppSettings;
class QLabel;
class QLineEdit;
class QTreeWidget;
class SteamWebApi;

// Lists Steam games (local library + playtime, optionally the Web API's full owned list) for picking one.
class SteamPickerDialog : public QDialog
{
    Q_OBJECT
public:
    SteamPickerDialog(SteamLibrary *library, AppSettings *settings, QWidget *parent = nullptr);

    void setMapped(const QSet<quint32> &ids);
    std::optional<SteamApp> selected() const;

private:
    void populate(const QList<SteamApp> &apps);
    void applyFilter();

    AppSettings *m_settings;
    SteamWebApi *m_api;
    QLineEdit *m_filter;
    QTreeWidget *m_tree;
    QLabel *m_status;
    QSet<quint32> m_mapped;
    QList<SteamApp> m_apps;
};
