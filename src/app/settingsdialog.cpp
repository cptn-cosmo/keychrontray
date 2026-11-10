#include "settingsdialog.h"
#include "autostart.h"
#include "logging.h"
#include "steampickerdialog.h"
#include "traycontroller.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QImage>
#include <QFile>
#include <QFontDatabase>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum GameColumns { ColName, ColAppIds, ColProcs, ColProfile, ColPriority, ColCount };

QList<quint32> parseIds(const QString &text, bool *ok)
{
    QList<quint32> ids;
    *ok = true;
    for (const QString &part : text.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts)) {
        bool k = false;
        const quint32 v = part.toUInt(&k);
        if (!k || v == 0) {
            *ok = false;
            return {};
        }
        ids << v;
    }
    return ids;
}

QString joinIds(const QList<quint32> &ids)
{
    QStringList s;
    for (auto i : ids) {
        s << QString::number(i);
    }
    return s.join(QStringLiteral(", "));
}
}

SettingsDialog::SettingsDialog(TrayController *tray, QWidget *parent)
    : QDialog(parent)
    , m_tray(tray)
{
    setWindowTitle(QStringLiteral("Keychron Tray Settings"));
    resize(900, 620);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildProfilesTab(), QIcon::fromTheme(QStringLiteral("color-management")), QStringLiteral("Profiles"));
    tabs->addTab(buildGamesTab(), QIcon::fromTheme(QStringLiteral("input-gaming")), QStringLiteral("Games"));
    tabs->addTab(buildGeneralTab(), QIcon::fromTheme(QStringLiteral("configure")), QStringLiteral("General"));
    tabs->addTab(buildDiagnosticsTab(), QIcon::fromTheme(QStringLiteral("tools-report-bug")), QStringLiteral("Diagnostics"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(tabs);
    lay->addWidget(buttons);

    connect(m_tray->profiles(), &ProfileManager::profilesChanged, this, [this] {
        if (!m_dirty) {
            reloadProfileList(m_edit.id);
        }
    });
    connect(m_tray->detector(), &GameDetector::configChanged, this, &SettingsDialog::loadGames);
    connect(tabs, &QTabWidget::currentChanged, this, [this, tabs](int i) {
        if (tabs->widget(i) != m_profileList->window() && i != 0) {
            m_tray->lighting()->endPreview();
        }
        if (i == 3) {
            refreshDiagnostics();
        }
    });
    reloadProfileList(m_tray->lighting()->activeProfileId());
    loadGames();
    loadGeneral();
    refreshDiagnostics();
}

SettingsDialog::~SettingsDialog()
{
    m_tray->lighting()->endPreview();
}

void SettingsDialog::done(int r)
{
    if (!confirmDiscard()) {
        return;
    }
    m_tray->lighting()->endPreview();
    QDialog::done(r);
}

// ---------------------------------------------------------------- Profiles

QWidget *SettingsDialog::buildProfilesTab()
{
    auto *w = new QWidget;
    auto *split = new QSplitter(w);

    auto *left = new QWidget;
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    m_profileList = new QListWidget;
    ll->addWidget(m_profileList);
    auto *lb = new QHBoxLayout;
    auto *newBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), QStringLiteral("New"));
    auto *dupBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-copy")), QStringLiteral("Duplicate"));
    auto *delBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), QStringLiteral("Delete"));
    lb->addWidget(newBtn);
    lb->addWidget(dupBtn);
    lb->addWidget(delBtn);
    ll->addLayout(lb);
    m_profileErrors = new QLabel;
    m_profileErrors->setWordWrap(true);
    m_profileErrors->setStyleSheet(QStringLiteral("color: palette(highlight);"));
    ll->addWidget(m_profileErrors);
    split->addWidget(left);

    auto *right = new QWidget;
    auto *form = new QFormLayout(right);
    m_idLabel = new QLabel;
    m_idLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_name = new QLineEdit;
    m_group = new QComboBox;
    m_group->setEditable(true);
    m_description = new QLineEdit;
    m_effect = new QComboBox;
    int n = 0;
    const Effects::Entry *table = Effects::table(&n);
    for (int i = 0; i < n; ++i) {
        m_effect->addItem(QStringLiteral("%1 (%2)").arg(QString::fromLatin1(table[i].name)).arg(table[i].id), table[i].id);
    }
    m_colorButton = new QPushButton;
    m_brightness = new QSlider(Qt::Horizontal);
    m_brightness->setRange(0, 100);
    m_brightnessLabel = new QLabel;
    m_speed = new QSlider(Qt::Horizontal);
    m_speed->setRange(0, 255);
    m_speedLabel = new QLabel;
    auto row = [](QWidget *a, QWidget *b) {
        auto *h = new QWidget;
        auto *l = new QHBoxLayout(h);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(a, 1);
        l->addWidget(b);
        b->setMinimumWidth(48);
        return h;
    };
    m_style = new QComboBox;
    m_style->addItem(QStringLiteral("Gradient (per-key colors)"));
    m_style->addItem(QStringLiteral("Single color effect"));
    m_stopsWidget = new QWidget;
    m_stopsLayout = new QHBoxLayout(m_stopsWidget);
    m_stopsLayout->setContentsMargins(0, 0, 0, 0);
    m_motion = new QComboBox;
    m_motion->addItem(QStringLiteral("Static"));
    m_motion->addItem(QStringLiteral("Scroll slowly left → right"));
    m_cycle = new QDoubleSpinBox;
    m_cycle->setRange(2, 600);
    m_cycle->setDecimals(0);
    m_cycle->setSuffix(QStringLiteral(" s per pass"));
    m_gradientPreview = new QLabel;
    m_gradientPreview->setMinimumHeight(22);
    m_gradientPreview->setScaledContents(true);

    m_form = form;
    form->addRow(QStringLiteral("File id:"), m_idLabel);
    form->addRow(QStringLiteral("Name:"), m_name);
    form->addRow(QStringLiteral("Group:"), m_group);
    form->addRow(QStringLiteral("Description:"), m_description);
    form->addRow(QStringLiteral("Style:"), m_style);
    form->addRow(QStringLiteral("Colors (left → right):"), m_stopsWidget);
    form->addRow(QStringLiteral("Motion:"), row(m_motion, m_cycle));
    form->addRow(QString(), m_gradientPreview);
    form->addRow(QStringLiteral("Effect:"), m_effect);
    form->addRow(QStringLiteral("Color:"), m_colorButton);
    form->addRow(QStringLiteral("Brightness:"), row(m_brightness, m_brightnessLabel));
    form->addRow(QStringLiteral("Speed:"), row(m_speed, m_speedLabel));
    m_styleNote = new QLabel;
    m_styleNote->setWordWrap(true);
    m_styleNote->setEnabled(false);
    form->addRow(m_styleNote);
    m_livePreview = new QCheckBox(QStringLiteral("Live preview on the keyboard (RAM only, nothing is saved to the keyboard)"));
    m_livePreview->setChecked(true);
    form->addRow(m_livePreview);

    auto *rb = new QHBoxLayout;
    auto *useBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")), QStringLiteral("Use now"));
    useBtn->setToolTip(QStringLiteral("Switch to this profile (like choosing it in the tray menu)"));
    m_saveProfile = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save")), QStringLiteral("Save profile"));
    auto *eepromBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("drive-harddisk")), QStringLiteral("Save to keyboard…"));
    eepromBtn->setToolTip(QStringLiteral("Write this lighting to the keyboard's EEPROM so it becomes its own default"));
    rb->addStretch();
    rb->addWidget(useBtn);
    rb->addWidget(eepromBtn);
    rb->addWidget(m_saveProfile);
    form->addRow(rb);
    split->addWidget(right);
    split->setStretchFactor(1, 2);

    auto *lay = new QVBoxLayout(w);
    lay->addWidget(split);

    connect(m_profileList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *cur, QListWidgetItem *prev) {
        if (m_loading || !cur) {
            return;
        }
        if (prev && m_dirty && !confirmDiscard()) {
            m_loading = true;
            m_profileList->setCurrentItem(prev);
            m_loading = false;
            return;
        }
        loadProfileIntoEditor(cur->data(Qt::UserRole).toString());
    });
    connect(m_name, &QLineEdit::textEdited, this, &SettingsDialog::editorChanged);
    connect(m_group, &QComboBox::editTextChanged, this, &SettingsDialog::editorChanged);
    connect(m_description, &QLineEdit::textEdited, this, &SettingsDialog::editorChanged);
    connect(m_effect, &QComboBox::currentIndexChanged, this, &SettingsDialog::editorChanged);
    connect(m_brightness, &QSlider::valueChanged, this, &SettingsDialog::editorChanged);
    connect(m_speed, &QSlider::valueChanged, this, &SettingsDialog::editorChanged);
    connect(m_style, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_loading) {
            return;
        }
        if (i == 0 && !m_edit.gradient) {
            // Start from the single color plus a hue-rotated partner.
            Gradient g;
            g.stops = {m_edit.color, QColor::fromHsv((qMax(0, m_edit.color.hsvHue()) + 60) % 360, qMax(160, m_edit.color.hsvSaturation()), 255)};
            m_edit.gradient = g;
        } else if (i == 1 && m_edit.gradient) {
            m_edit.color = m_edit.gradient->stops.value(0, m_edit.color);
            m_edit.gradient.reset();
            if (m_edit.effect == Effects::PerKeyRgb) {
                m_edit.effect = 1;
            }
            m_effect->setCurrentIndex(m_effect->findData(m_edit.effect));
            updateColorButton();
        }
        rebuildStopButtons();
        updateStyleRows();
        editorChanged();
    });
    connect(m_motion, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_loading || !m_edit.gradient) {
            return;
        }
        m_edit.gradient->motion = i == 1 ? Gradient::Motion::Scroll : Gradient::Motion::Static;
        updateStyleRows();
        editorChanged();
    });
    connect(m_cycle, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_loading || !m_edit.gradient) {
            return;
        }
        m_edit.gradient->cycleSeconds = v;
        editorChanged();
    });
    connect(m_livePreview, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            m_tray->lighting()->preview(LightingScene::fromProfile(m_edit));
        } else {
            m_tray->lighting()->endPreview();
        }
    });
    connect(m_colorButton, &QPushButton::clicked, this, [this] {
        const QColor before = m_edit.color;
        QColorDialog dlg(m_edit.color, this);
        dlg.setWindowTitle(QStringLiteral("Profile color"));
        connect(&dlg, &QColorDialog::currentColorChanged, this, [this](const QColor &c) {
            m_edit.color = c;
            updateColorButton();
            editorChanged();
        });
        if (dlg.exec() != QDialog::Accepted) {
            m_edit.color = before;
            updateColorButton();
            editorChanged();
        }
    });
    connect(m_saveProfile, &QPushButton::clicked, this, &SettingsDialog::saveCurrentProfile);
    connect(useBtn, &QPushButton::clicked, this, [this] {
        if (m_dirty && !saveCurrentProfile()) {
            return;
        }
        m_livePreview->setChecked(false);
        m_tray->lighting()->setManualProfile(m_edit.id);
    });
    connect(eepromBtn, &QPushButton::clicked, this, [this] {
        if (!m_tray->backend()->isConnected()) {
            QMessageBox::information(this, QStringLiteral("Save to keyboard"), QStringLiteral("The keyboard is not reachable. Connect the USB cable."));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("Save to keyboard"),
                                  QStringLiteral("Write \"%1\" into the keyboard's EEPROM?\n\nIt becomes the keyboard's own default lighting. "
                                                 "Flash memory has limited write cycles, so only do this when you mean it.")
                                      .arg(m_edit.displayName()))
            != QMessageBox::Yes) {
            return;
        }
        m_tray->lighting()->preview(LightingScene::fromProfile(m_edit));
        const bool ok = m_tray->lighting()->saveToKeyboard();
        if (!m_livePreview->isChecked()) {
            m_tray->lighting()->endPreview();
        }
        QMessageBox::information(this, QStringLiteral("Save to keyboard"), ok ? QStringLiteral("Saved.") : QStringLiteral("Saving failed."));
    });
    connect(newBtn, &QPushButton::clicked, this, [this] {
        if (!confirmDiscard()) {
            return;
        }
        Profile p;
        p.id = m_tray->profiles()->uniqueId(QStringLiteral("new-profile"));
        p.name = QStringLiteral("New profile");
        p.brightnessPercent = 80;
        m_tray->profiles()->save(p);
        reloadProfileList(p.id);
    });
    connect(dupBtn, &QPushButton::clicked, this, [this] {
        if (m_edit.id.isEmpty() || !confirmDiscard()) {
            return;
        }
        Profile p = m_edit;
        p.id = m_tray->profiles()->uniqueId(m_edit.id);
        p.name = m_edit.displayName() + QStringLiteral(" (copy)");
        m_tray->profiles()->save(p);
        reloadProfileList(p.id);
    });
    connect(delBtn, &QPushButton::clicked, this, [this] {
        if (m_edit.id.isEmpty()) {
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("Delete profile"), QStringLiteral("Delete profile \"%1\"?").arg(m_edit.displayName())) != QMessageBox::Yes) {
            return;
        }
        m_dirty = false;
        m_tray->profiles()->remove(m_edit.id);
        m_edit = {};
        reloadProfileList();
    });
    return w;
}

void SettingsDialog::reloadProfileList(const QString &select)
{
    m_loading = true;
    m_profileList->clear();
    QStringList groups;
    QListWidgetItem *toSelect = nullptr;
    for (const Profile &p : m_tray->profiles()->profiles()) {
        auto *item = new QListWidgetItem(p.group.isEmpty() ? p.displayName() : QStringLiteral("%1 › %2").arg(p.group, p.displayName()));
        item->setData(Qt::UserRole, p.id);
        item->setIcon([&] {
            QImage img(16, 16, QImage::Format_RGB32);
            for (int x = 0; x < 16; ++x) {
                QColor c = p.color;
                if (p.gradient) {
                    const HueSat hs = p.gradient->sample(x / 15.0);
                    c = ColorConv::fromQmk(hs.h, hs.s);
                }
                for (int y = 0; y < 16; ++y) {
                    img.setPixelColor(x, y, c);
                }
            }
            return QIcon(QPixmap::fromImage(img));
        }());
        m_profileList->addItem(item);
        if (p.id == select) {
            toSelect = item;
        }
        if (!p.group.isEmpty() && !groups.contains(p.group)) {
            groups << p.group;
        }
    }
    const QString groupText = m_group->currentText();
    m_group->clear();
    m_group->addItem(QString());
    m_group->addItems(groups);
    m_group->setEditText(groupText);
    const QStringList errors = m_tray->profiles()->loadErrors();
    m_profileErrors->setText(errors.isEmpty() ? QString() : QStringLiteral("Files with errors (not loaded):\n") + errors.join(QLatin1Char('\n')));
    m_loading = false;
    if (!toSelect && m_profileList->count() > 0) {
        toSelect = m_profileList->item(0);
    }
    if (toSelect) {
        m_profileList->setCurrentItem(toSelect);
        loadProfileIntoEditor(toSelect->data(Qt::UserRole).toString());
    }
}

void SettingsDialog::loadProfileIntoEditor(const QString &id)
{
    const auto p = m_tray->profiles()->profile(id);
    if (!p) {
        return;
    }
    m_loading = true;
    m_edit = *p;
    m_idLabel->setText(QStringLiteral("%1.json").arg(p->id));
    m_name->setText(p->name);
    m_group->setEditText(p->group);
    m_description->setText(p->description);
    const int idx = m_effect->findData(p->effect);
    if (idx < 0) {
        m_effect->addItem(Effects::nameFor(p->effect), p->effect);
    }
    m_effect->setCurrentIndex(m_effect->findData(p->effect));
    m_brightness->setValue(p->brightnessPercent);
    m_speed->setValue(p->speed);
    m_brightnessLabel->setText(QStringLiteral("%1%").arg(p->brightnessPercent));
    m_speedLabel->setText(QString::number(p->speed));
    updateColorButton();
    m_style->setCurrentIndex(p->gradient ? 0 : 1);
    if (p->gradient) {
        m_motion->setCurrentIndex(p->gradient->motion == Gradient::Motion::Scroll ? 1 : 0);
        m_cycle->setValue(p->gradient->cycleSeconds);
    }
    rebuildStopButtons();
    updateStyleRows();
    m_loading = false;
    m_dirty = false;
    m_saveProfile->setEnabled(false);
    if (m_livePreview->isChecked()) {
        m_tray->lighting()->preview(LightingScene::fromProfile(m_edit));
    }
}

void SettingsDialog::updateColorButton()
{
    QPixmap px(32, 16);
    px.fill(m_edit.color);
    m_colorButton->setIcon(QIcon(px));
    m_colorButton->setText(m_edit.color.name());
}

void SettingsDialog::editorChanged()
{
    if (m_loading) {
        return;
    }
    m_edit.name = m_name->text();
    m_edit.group = m_group->currentText().trimmed();
    m_edit.description = m_description->text();
    if (!m_edit.gradient) {
        m_edit.effect = uint8_t(m_effect->currentData().toInt());
    }
    updateGradientPreview();
    m_edit.brightnessPercent = m_brightness->value();
    m_edit.speed = uint8_t(m_speed->value());
    m_brightnessLabel->setText(QStringLiteral("%1%").arg(m_edit.brightnessPercent));
    m_speedLabel->setText(QString::number(m_edit.speed));
    m_dirty = true;
    m_saveProfile->setEnabled(true);
    if (m_livePreview->isChecked()) {
        m_tray->lighting()->preview(LightingScene::fromProfile(m_edit));
    }
}

bool SettingsDialog::saveCurrentProfile()
{
    if (m_edit.id.isEmpty()) {
        return false;
    }
    QString err;
    m_dirty = false; // before save: profilesChanged reloads the list
    if (!m_tray->profiles()->save(m_edit, &err)) {
        m_dirty = true;
        QMessageBox::warning(this, QStringLiteral("Save profile"), err);
        return false;
    }
    m_saveProfile->setEnabled(false);
    return true;
}

bool SettingsDialog::confirmDiscard()
{
    if (!m_dirty) {
        return true;
    }
    const auto r = QMessageBox::question(this, QStringLiteral("Unsaved changes"), QStringLiteral("Save changes to \"%1\"?").arg(m_edit.displayName()),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel) {
        return false;
    }
    if (r == QMessageBox::Save) {
        return saveCurrentProfile();
    }
    m_dirty = false;
    return true;
}

// ---------------------------------------------------------------- Games

QWidget *SettingsDialog::buildGamesTab()
{
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    auto *form = new QFormLayout;
    m_defaultProfile = new QComboBox;
    m_fallbackProfile = new QComboBox;
    m_ignored = new QLineEdit;
    m_ignored->setToolTip(QStringLiteral("Steam app ids that never trigger a switch (SteamVR, Proton, Steam Linux Runtime, …)"));
    form->addRow(QStringLiteral("Profile when no game runs:"), m_defaultProfile);
    form->addRow(QStringLiteral("Other Steam games use:"), m_fallbackProfile);
    form->addRow(QStringLiteral("Ignored Steam app ids:"), m_ignored);
    lay->addLayout(form);

    m_games = new QTableWidget(0, ColCount);
    m_games->setHorizontalHeaderLabels({QStringLiteral("Game"), QStringLiteral("Steam app ids"), QStringLiteral("Process names (wildcards ok)"),
                                        QStringLiteral("Profile"), QStringLiteral("Priority")});
    m_games->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_games->horizontalHeader()->setSectionResizeMode(ColProcs, QHeaderView::Stretch);
    m_games->horizontalHeader()->setSectionResizeMode(ColProfile, QHeaderView::ResizeToContents);
    m_games->verticalHeader()->hide();
    m_games->setSelectionBehavior(QAbstractItemView::SelectRows);
    lay->addWidget(m_games);

    auto *hint = new QLabel(QStringLiteral("When several games run, a configured game beats the generic Steam profile, then the higher priority wins, "
                                           "then the most recently started game. FFXIV expansions can't be detected; pick the default expansion "
                                           "profile here and switch the others from the tray menu."));
    hint->setWordWrap(true);
    hint->setEnabled(false);
    lay->addWidget(hint);

    m_gamesStatus = new QLabel;
    lay->addWidget(m_gamesStatus);

    auto *bl = new QHBoxLayout;
    auto *add = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), QStringLiteral("Add row"));
    auto *steam = new QPushButton(QIcon::fromTheme(QStringLiteral("steam")), QStringLiteral("Add from Steam library…"));
    auto *remove = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), QStringLiteral("Remove"));
    auto *save = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save")), QStringLiteral("Save"));
    bl->addWidget(add);
    bl->addWidget(steam);
    bl->addWidget(remove);
    bl->addStretch();
    bl->addWidget(save);
    lay->addLayout(bl);

    connect(add, &QPushButton::clicked, this, [this] {
        addGameRow(QString(), QStringLiteral("New game"), QString(), QStringLiteral("game.exe"), QStringLiteral("steam-generic"), 0);
        m_games->scrollToBottom();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const auto rows = m_games->selectionModel()->selectedRows();
        for (int i = rows.size() - 1; i >= 0; --i) {
            m_games->removeRow(rows[i].row());
        }
    });
    connect(save, &QPushButton::clicked, this, &SettingsDialog::saveGames);
    connect(steam, &QPushButton::clicked, this, &SettingsDialog::addFromSteam);
    return w;
}

void SettingsDialog::fillProfileCombo(QComboBox *c, const QString &selected, bool allowNone, bool allowKeyboard)
{
    c->clear();
    if (allowNone) {
        c->addItem(QStringLiteral("(don't switch)"), QString());
    }
    if (allowKeyboard) {
        c->addItem(QStringLiteral("Keyboard default (lighting at startup)"), LightingController::KeyboardProfileId);
    }
    for (const Profile &p : m_tray->profiles()->profiles()) {
        c->addItem(p.group.isEmpty() ? p.displayName() : QStringLiteral("%1 › %2").arg(p.group, p.displayName()), p.id);
    }
    int idx = c->findData(selected);
    if (idx < 0 && !selected.isEmpty()) {
        c->addItem(QStringLiteral("%1 (missing!)").arg(selected), selected);
        idx = c->count() - 1;
    }
    c->setCurrentIndex(qMax(0, idx));
}

void SettingsDialog::addGameRow(const QString &id, const QString &name, const QString &appIds, const QString &procs, const QString &profile, int priority)
{
    const int r = m_games->rowCount();
    m_games->insertRow(r);
    auto *nameItem = new QTableWidgetItem(name);
    nameItem->setData(Qt::UserRole, id);
    m_games->setItem(r, ColName, nameItem);
    m_games->setItem(r, ColAppIds, new QTableWidgetItem(appIds));
    m_games->setItem(r, ColProcs, new QTableWidgetItem(procs));
    auto *combo = new QComboBox;
    fillProfileCombo(combo, profile, false, true);
    m_games->setCellWidget(r, ColProfile, combo);
    auto *spin = new QSpinBox;
    spin->setRange(-100, 100);
    spin->setValue(priority);
    m_games->setCellWidget(r, ColPriority, spin);
}

void SettingsDialog::loadGames()
{
    const GameConfig c = m_tray->detector()->config();
    m_games->setRowCount(0);
    for (const GameRule &r : c.games) {
        addGameRow(r.id, r.name, joinIds(r.steamAppIds), r.processNames.join(QStringLiteral(", ")), r.profile, r.priority);
    }
    fillProfileCombo(m_defaultProfile, m_tray->settings()->defaultProfile(), false, true);
    fillProfileCombo(m_fallbackProfile, c.fallbackProfile, true, false);
    QList<quint32> ignored(c.ignoredSteamAppIds.begin(), c.ignoredSteamAppIds.end());
    std::sort(ignored.begin(), ignored.end());
    m_ignored->setText(joinIds(ignored));
    const QString err = m_tray->detector()->loadError();
    const auto cur = m_tray->detector()->current();
    m_gamesStatus->setText(!err.isEmpty() ? QStringLiteral("games.json error: %1").arg(err)
                                          : QStringLiteral("Detected now: %1").arg(cur ? cur->label : QStringLiteral("no game")));
}

void SettingsDialog::saveGames()
{
    GameConfig c;
    bool ok = true;
    c.fallbackProfile = m_fallbackProfile->currentData().toString();
    const auto ignored = parseIds(m_ignored->text(), &ok);
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Games"), QStringLiteral("Ignored app ids must be numbers separated by commas."));
        return;
    }
    c.ignoredSteamAppIds = QSet<quint32>(ignored.begin(), ignored.end());
    QSet<QString> used;
    for (int r = 0; r < m_games->rowCount(); ++r) {
        GameRule g;
        g.name = m_games->item(r, ColName)->text().trimmed();
        g.id = m_games->item(r, ColName)->data(Qt::UserRole).toString();
        if (g.id.isEmpty()) {
            g.id = m_tray->profiles()->uniqueId(g.name); // only for a readable, unique slug
        }
        while (used.contains(g.id)) {
            g.id += QStringLiteral("-x");
        }
        used.insert(g.id);
        g.steamAppIds = parseIds(m_games->item(r, ColAppIds)->text(), &ok);
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("Games"), QStringLiteral("Row %1 (%2): Steam app ids must be numbers.").arg(r + 1).arg(g.name));
            return;
        }
        for (const QString &p : m_games->item(r, ColProcs)->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            if (!p.trimmed().isEmpty()) {
                g.processNames << p.trimmed();
            }
        }
        g.profile = qobject_cast<QComboBox *>(m_games->cellWidget(r, ColProfile))->currentData().toString();
        g.priority = qobject_cast<QSpinBox *>(m_games->cellWidget(r, ColPriority))->value();
        c.games << g;
    }
    QString err;
    if (!m_tray->detector()->saveConfig(c, &err)) {
        QMessageBox::warning(this, QStringLiteral("Games"), err);
        return;
    }
    m_tray->settings()->setDefaultProfile(m_defaultProfile->currentData().toString());
    m_tray->settings()->sync();
    m_tray->applySettings();
    m_gamesStatus->setText(QStringLiteral("Saved."));
}

void SettingsDialog::addFromSteam()
{
    SteamPickerDialog dlg(m_tray->steam(), m_tray->settings(), this);
    QSet<quint32> mapped;
    for (const auto &r : m_tray->detector()->config().games) {
        for (auto id : r.steamAppIds) {
            mapped.insert(id);
        }
    }
    dlg.setMapped(mapped);
    if (dlg.exec() != QDialog::Accepted || !dlg.selected()) {
        return;
    }
    const SteamApp app = *dlg.selected();
    auto *pm = m_tray->profiles();
    Profile p = pm->profile(m_fallbackProfile->currentData().toString()).value_or(pm->profile(QStringLiteral("steam-generic")).value_or(Profile{}));
    p.id = pm->uniqueId(app.name);
    p.name = app.name;
    p.group = QStringLiteral("Games");
    p.description.clear();
    pm->save(p);
    addGameRow(QString(), app.name, QString::number(app.appId), QString(), p.id, 0);
    saveGames();
    m_games->scrollToBottom();
    reloadProfileList(m_dirty ? m_edit.id : p.id);
    m_gamesStatus->setText(QStringLiteral("Added %1 with new profile \"%2\" – edit its colors on the Profiles tab.").arg(app.name, p.id));
}

// ---------------------------------------------------------------- General

QWidget *SettingsDialog::buildGeneralTab()
{
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);

    auto *behav = new QGroupBox(QStringLiteral("Behavior"));
    auto *f1 = new QFormLayout(behav);
    m_autoSwitch = new QCheckBox(QStringLiteral("Switch profiles automatically when a game starts"));
    m_scanInterval = new QSpinBox;
    m_scanInterval->setRange(1, 60);
    m_scanInterval->setSuffix(QStringLiteral(" s"));
    m_thresholds = new QLineEdit;
    m_thresholds->setPlaceholderText(QStringLiteral("20, 10"));
    m_autostart = new QCheckBox(QStringLiteral("Start with the Plasma session"));
    f1->addRow(m_autoSwitch);
    f1->addRow(QStringLiteral("Game scan interval:"), m_scanInterval);
    f1->addRow(QStringLiteral("Low battery warnings at (%):"), m_thresholds);
    f1->addRow(m_autostart);
    lay->addWidget(behav);

    auto *steam = new QGroupBox(QStringLiteral("Steam Web API (optional: full owned-games list for the picker)"));
    auto *f2 = new QFormLayout(steam);
    m_apiKey = new QLineEdit;
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(QStringLiteral("from steamcommunity.com/dev/apikey"));
    m_steamId = new QLineEdit;
    m_steamId->setPlaceholderText(QStringLiteral("SteamID64 or vanity name"));
    f2->addRow(QStringLiteral("API key:"), m_apiKey);
    f2->addRow(QStringLiteral("Steam account:"), m_steamId);
    auto *keyNote = new QLabel(QStringLiteral("Stored in ~/.config/keychrontrayrc (readable only by you). Never logged."));
    keyNote->setEnabled(false);
    f2->addRow(keyNote);
    lay->addWidget(steam);
    lay->addStretch();

    auto *save = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save")), QStringLiteral("Save"));
    auto *bl = new QHBoxLayout;
    bl->addStretch();
    bl->addWidget(save);
    lay->addLayout(bl);
    connect(save, &QPushButton::clicked, this, &SettingsDialog::saveGeneral);
    return w;
}

void SettingsDialog::loadGeneral()
{
    AppSettings *s = m_tray->settings();
    m_autoSwitch->setChecked(s->autoSwitch());
    m_scanInterval->setValue(s->scanIntervalSeconds());
    QStringList t;
    for (int v : s->batteryThresholds()) {
        t << QString::number(v);
    }
    m_thresholds->setText(t.join(QStringLiteral(", ")));
    m_autostart->setChecked(Autostart::isEnabled());
    m_apiKey->setText(s->steamApiKey());
    m_steamId->setText(s->steamId());
}

void SettingsDialog::saveGeneral()
{
    QList<int> thresholds;
    for (const QString &p : m_thresholds->text().split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int v = p.toInt(&ok);
        if (!ok || v <= 0 || v >= 100) {
            QMessageBox::warning(this, QStringLiteral("Settings"), QStringLiteral("Battery thresholds must be numbers between 1 and 99."));
            return;
        }
        thresholds << v;
    }
    AppSettings *s = m_tray->settings();
    s->setAutoSwitch(m_autoSwitch->isChecked());
    s->setScanIntervalSeconds(m_scanInterval->value());
    s->setBatteryThresholds(thresholds);
    s->setSteamApiKey(m_apiKey->text().trimmed());
    s->setSteamId(m_steamId->text().trimmed());
    s->sync();
    QFile::setPermissions(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/keychrontrayrc"),
                          QFile::ReadOwner | QFile::WriteOwner);
    QString err;
    if (!Autostart::setEnabled(m_autostart->isChecked(), &err)) {
        QMessageBox::warning(this, QStringLiteral("Autostart"), err);
    }
    m_tray->applySettings();
}

// ---------------------------------------------------------------- Diagnostics

QWidget *SettingsDialog::buildDiagnosticsTab()
{
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    m_diag = new QLabel;
    m_diag->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_diag->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_diag->setWordWrap(true);
    lay->addWidget(m_diag);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setMaximumBlockCount(2000);
    m_log->setPlainText(LogBuffer::instance()->lines().join(QLatin1Char('\n')));
    lay->addWidget(m_log, 1);
    connect(LogBuffer::instance(), &LogBuffer::lineAdded, m_log, &QPlainTextEdit::appendPlainText);

    auto *bl = new QHBoxLayout;
    auto *rescan = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), QStringLiteral("Rescan devices"));
    auto *copy = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-copy")), QStringLiteral("Copy report"));
    bl->addWidget(rescan);
    bl->addStretch();
    bl->addWidget(copy);
    lay->addLayout(bl);
    connect(rescan, &QPushButton::clicked, this, [this] {
        m_tray->devices()->refresh();
        m_tray->backend()->rescan();
        m_tray->battery()->refresh();
        refreshDiagnostics();
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_diag->text() + QStringLiteral("\n\n") + m_log->toPlainText());
    });
    return w;
}

void SettingsDialog::refreshDiagnostics()
{
    const KeyboardInfo i = m_tray->backend()->info();
    const auto orig = m_tray->lighting()->originalState();
    const BatteryStatus b = m_tray->battery()->status();
    QStringList l;
    l << m_tray->statusText();
    l << QString();
    l << QStringLiteral("VIA device: %1").arg(m_tray->backend()->isConnected()
                                                  ? QStringLiteral("%1 %2 [%3:%4] firmware %5, VIA protocol %6")
                                                        .arg(i.product, i.path)
                                                        .arg(i.vendorId, 4, 16, QLatin1Char('0'))
                                                        .arg(i.productId, 4, 16, QLatin1Char('0'))
                                                        .arg(i.firmware)
                                                        .arg(i.viaProtocol)
                                                  : QStringLiteral("none answering"));
    l << QStringLiteral("Receiver present: %1, cable: %2, Bluetooth: %3")
             .arg(m_tray->devices()->receiverPresent() ? QStringLiteral("yes") : QStringLiteral("no"),
                  m_tray->devices()->wiredPresent() ? QStringLiteral("yes") : QStringLiteral("no"),
                  m_tray->devices()->bluetoothConnected() ? QStringLiteral("yes") : QStringLiteral("no"));
    l << QStringLiteral("Battery source: %1").arg(b.available ? b.source : QStringLiteral("none (UPower has no Keychron device)"));
    l << QStringLiteral("Original lighting: %1").arg(orig ? orig->toString() : QStringLiteral("not captured"));
    l << QStringLiteral("Profiles: %1").arg(m_tray->profiles()->profilesDir());
    l << QStringLiteral("Games: %1").arg(m_tray->detector()->gamesFile());
    l << QStringLiteral("Steam: %1").arg(m_tray->steam()->isValid() ? m_tray->steam()->root() : QStringLiteral("not found"));
    m_diag->setText(l.join(QLatin1Char('\n')));
}

void SettingsDialog::rebuildStopButtons()
{
    while (QLayoutItem *it = m_stopsLayout->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    if (!m_edit.gradient) {
        return;
    }
    QList<QColor> &stops = m_edit.gradient->stops;
    for (int i = 0; i < stops.size(); ++i) {
        auto *b = new QPushButton;
        QPixmap px(28, 16);
        px.fill(stops[i]);
        b->setIcon(QIcon(px));
        b->setIconSize(px.size());
        b->setToolTip(QStringLiteral("%1 – click to change").arg(stops[i].name()));
        connect(b, &QPushButton::clicked, this, [this, i] {
            if (!m_edit.gradient || i >= m_edit.gradient->stops.size()) {
                return;
            }
            const QColor before = m_edit.gradient->stops[i];
            QColorDialog dlg(before, this);
            dlg.setWindowTitle(QStringLiteral("Gradient color %1").arg(i + 1));
            connect(&dlg, &QColorDialog::currentColorChanged, this, [this, i](const QColor &c) {
                m_edit.gradient->stops[i] = c;
                editorChanged();
            });
            if (dlg.exec() != QDialog::Accepted) {
                m_edit.gradient->stops[i] = before;
            }
            rebuildStopButtons();
            editorChanged();
        });
        m_stopsLayout->addWidget(b);
    }
    auto *add = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), QString());
    if (add->icon().isNull()) {
        add->setText(QStringLiteral("+"));
    }
    add->setToolTip(QStringLiteral("Add a color"));
    add->setEnabled(stops.size() < 16);
    connect(add, &QPushButton::clicked, this, [this] {
        m_edit.gradient->stops.append(m_edit.gradient->stops.last());
        rebuildStopButtons();
        editorChanged();
    });
    auto *remove = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), QString());
    if (remove->icon().isNull()) {
        remove->setText(QStringLiteral("−"));
    }
    remove->setToolTip(QStringLiteral("Remove the last color"));
    remove->setEnabled(stops.size() > 2);
    connect(remove, &QPushButton::clicked, this, [this] {
        m_edit.gradient->stops.removeLast();
        rebuildStopButtons();
        editorChanged();
    });
    m_stopsLayout->addWidget(add);
    m_stopsLayout->addWidget(remove);
    m_stopsLayout->addStretch();
    updateGradientPreview();
}

void SettingsDialog::updateGradientPreview()
{
    if (!m_edit.gradient) {
        m_gradientPreview->clear();
        return;
    }
    // Same blend as on the keyboard (hue/saturation only, full value).
    QImage img(210, 1, QImage::Format_RGB32);
    for (int x = 0; x < img.width(); ++x) {
        const HueSat hs = m_edit.gradient->sample(double(x) / (img.width() - 1), 0.0);
        img.setPixelColor(x, 0, ColorConv::fromQmk(hs.h, hs.s));
    }
    m_gradientPreview->setPixmap(QPixmap::fromImage(img));
}

void SettingsDialog::updateStyleRows()
{
    const bool grad = m_edit.gradient.has_value();
    m_form->setRowVisible(m_stopsWidget, grad);
    m_form->setRowVisible(m_motion->parentWidget(), grad);
    m_form->setRowVisible(m_gradientPreview, grad);
    m_form->setRowVisible(m_effect, !grad);
    m_form->setRowVisible(m_colorButton, !grad);
    m_form->setRowVisible(m_speed->parentWidget(), !grad);
    m_cycle->setEnabled(grad && m_edit.gradient->motion == Gradient::Motion::Scroll);
    const bool perKey = m_tray->backend()->perKeyLedCount() > 0 || !m_tray->backend()->isConnected();
    m_styleNote->setText(grad ? (perKey ? QStringLiteral("Only hue and saturation are used; brightness is one value for the whole keyboard, so gradients "
                                                          "can fade to white but not to black. Scrolling is rendered by this app (keyboard on USB).")
                                        : QStringLiteral("This keyboard's firmware has no per-key colors: the first color is shown as a solid color."))
                              : QStringLiteral("Firmware effects use one color; rainbow/cycle effects ignore it."));
}
