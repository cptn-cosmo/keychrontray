#include "steampickerdialog.h"
#include "appsettings.h"
#include "steamwebapi.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

SteamPickerDialog::SteamPickerDialog(SteamLibrary *library, AppSettings *settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_api(new SteamWebApi(this))
{
    setWindowTitle(QStringLiteral("Add game from Steam"));
    resize(640, 520);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(QStringLiteral("Filter by name or app id…"));
    m_filter->setClearButtonEnabled(true);
    m_tree = new QTreeWidget;
    m_tree->setHeaderLabels({QStringLiteral("Game"), QStringLiteral("App id"), QStringLiteral("Hours"), QStringLiteral("")});
    m_tree->setRootIsDecorated(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->setSortingEnabled(true);
    m_status = new QLabel;
    auto *fetch = new QPushButton(QIcon::fromTheme(QStringLiteral("download")), QStringLiteral("Load full library from Steam Web API"));
    fetch->setEnabled(!settings->steamApiKey().isEmpty() && !settings->steamId().isEmpty());
    fetch->setToolTip(fetch->isEnabled() ? QString() : QStringLiteral("Set an API key and Steam account under General first"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Add"));

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(m_filter);
    lay->addWidget(m_tree);
    auto *hl = new QHBoxLayout;
    hl->addWidget(m_status, 1);
    hl->addWidget(fetch);
    lay->addLayout(hl);
    lay->addWidget(buttons);

    connect(m_filter, &QLineEdit::textChanged, this, &SteamPickerDialog::applyFilter);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &QDialog::accept);
    connect(fetch, &QPushButton::clicked, this, [this, fetch] {
        fetch->setEnabled(false);
        m_status->setText(QStringLiteral("Contacting Steam…"));
        m_api->fetchOwnedGames(m_settings->steamApiKey(), m_settings->steamId());
    });
    connect(m_api, &SteamWebApi::ownedGamesReady, this, [this](const QList<SteamApp> &owned) {
        // Merge: keep installed flags from the local list.
        QHash<quint32, SteamApp> all;
        for (const auto &a : owned) {
            all.insert(a.appId, a);
        }
        for (const auto &a : std::as_const(m_apps)) {
            SteamApp &m = all[a.appId];
            if (m.appId == 0) {
                m = a;
            }
            m.installed = a.installed;
            m.playtimeMinutes = qMax(m.playtimeMinutes, a.playtimeMinutes);
        }
        populate(all.values());
    });
    connect(m_api, &SteamWebApi::failed, this, [this, fetch](const QString &msg) {
        m_status->setText(msg);
        fetch->setEnabled(true);
    });

    populate(library->knownApps());
}

void SteamPickerDialog::setMapped(const QSet<quint32> &ids)
{
    m_mapped = ids;
    populate(m_apps);
}

void SteamPickerDialog::populate(const QList<SteamApp> &apps)
{
    m_apps = apps;
    m_tree->setSortingEnabled(false);
    m_tree->clear();
    for (const SteamApp &a : apps) {
        auto *it = new QTreeWidgetItem(m_tree);
        it->setText(0, a.name);
        it->setData(1, Qt::DisplayRole, a.appId);
        it->setData(2, Qt::DisplayRole, qRound(a.playtimeMinutes / 6.0) / 10.0);
        QStringList flags;
        if (a.installed) {
            flags << QStringLiteral("installed");
        }
        if (m_mapped.contains(a.appId)) {
            flags << QStringLiteral("has profile");
        }
        it->setText(3, flags.join(QStringLiteral(", ")));
        it->setData(0, Qt::UserRole, a.appId);
    }
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(2, Qt::DescendingOrder);
    m_status->setText(QStringLiteral("%1 games").arg(apps.size()));
    applyFilter();
}

void SteamPickerDialog::applyFilter()
{
    const QString f = m_filter->text().trimmed();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *it = m_tree->topLevelItem(i);
        it->setHidden(!f.isEmpty() && !it->text(0).contains(f, Qt::CaseInsensitive) && !it->text(1).contains(f));
    }
}

std::optional<SteamApp> SteamPickerDialog::selected() const
{
    const auto *it = m_tree->currentItem();
    if (!it) {
        return std::nullopt;
    }
    const quint32 id = it->data(0, Qt::UserRole).toUInt();
    for (const SteamApp &a : m_apps) {
        if (a.appId == id) {
            return a;
        }
    }
    return std::nullopt;
}
