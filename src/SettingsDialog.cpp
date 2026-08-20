#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QStyleHints>

#include "utils/ConfigManager.h"
#include "utils/Startup.h"

namespace {
bool windowsUsesDarkApps() {
#ifdef Q_OS_WIN
    QSettings personalize(
        R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",
        QSettings::NativeFormat);
    return personalize.value("AppsUseLightTheme", 1).toInt() == 0;
#else
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#endif
}

void applyDialogPalette(QWidget* widget) {
    const bool dark = windowsUsesDarkApps();
    if (dark) {
        widget->setStyleSheet(R"(
            QDialog { background: #202020; color: #f5f5f5; }
            QGroupBox { border: 1px solid #454545; border-radius: 7px; margin-top: 12px; padding-top: 8px; }
            QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
            QComboBox, QFontComboBox, QSpinBox { background: #2b2b2b; border: 1px solid #555; border-radius: 4px; padding: 4px 7px; }
            QPushButton { min-width: 78px; padding: 5px 12px; }
        )");
    } else {
        widget->setStyleSheet(R"(
            QDialog { background: #f7f7f7; color: #202020; }
            QGroupBox { border: 1px solid #d6d6d6; border-radius: 7px; margin-top: 12px; padding-top: 8px; background: #ffffff; }
            QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
            QComboBox, QFontComboBox, QSpinBox { background: #ffffff; border: 1px solid #c8c8c8; border-radius: 4px; padding: 4px 7px; }
            QPushButton { min-width: 78px; padding: 5px 12px; }
        )");
    }
}
}

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("AltTaber Settings"));
    setWindowIcon(QIcon(":/img/icon.ico"));
    setModal(false);
    resize(470, 360);
    setMinimumWidth(440);
    applyDialogPalette(this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(12);

    auto* intro = new QLabel(tr("Configure AltTaber without editing config.ini manually."), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* generalGroup = new QGroupBox(tr("General"), this);
    auto* generalLayout = new QVBoxLayout(generalGroup);
    startupCheck = new QCheckBox(tr("Start AltTaber with Windows"), generalGroup);
    generalLayout->addWidget(startupCheck);
    root->addWidget(generalGroup);

    auto* displayGroup = new QGroupBox(tr("Display"), this);
    auto* displayForm = new QFormLayout(displayGroup);
    displayForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    monitorCombo = new QComboBox(displayGroup);
    monitorCombo->addItem(tr("Primary monitor"), PrimaryMonitor);
    monitorCombo->addItem(tr("Monitor under mouse pointer"), MouseMonitor);
    displayForm->addRow(tr("Show switcher on:"), monitorCombo);
    root->addWidget(displayGroup);

    auto* appearanceGroup = new QGroupBox(tr("Appearance"), this);
    auto* appearanceForm = new QFormLayout(appearanceGroup);
    appearanceForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    fontCombo = new QFontComboBox(appearanceGroup);
    fontSizeSpin = new QSpinBox(appearanceGroup);
    fontSizeSpin->setRange(7, 24);
    fontSizeSpin->setSuffix(tr(" pt"));
    appearanceForm->addRow(tr("Window title font:"), fontCombo);
    appearanceForm->addRow(tr("Font size:"), fontSizeSpin);
    root->addWidget(appearanceGroup);

    auto* bottom = new QHBoxLayout;
    auto* openConfig = new QPushButton(tr("Open config.ini"), this);
    bottom->addWidget(openConfig);
    bottom->addStretch();

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    applyButton = buttons->button(QDialogButtonBox::Apply);
    bottom->addWidget(buttons);
    root->addLayout(bottom);

    connect(openConfig, &QPushButton::clicked, this, &SettingsDialog::openConfigFile);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        saveSettings();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(applyButton, &QPushButton::clicked, this, &SettingsDialog::saveSettings);

    const auto changed = [this] { updateApplyState(); };
    connect(startupCheck, &QCheckBox::toggled, this, changed);
    connect(monitorCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(fontCombo, &QFontComboBox::currentFontChanged, this, changed);
    connect(fontSizeSpin, &QSpinBox::valueChanged, this, changed);

    loadSettings();
}

void SettingsDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    loadSettings();
}

void SettingsDialog::loadSettings() {
    loading = true;
    startupCheck->setChecked(Startup::isOn());

    const int monitorIndex = monitorCombo->findData(static_cast<int>(cfg.getDisplayMonitor()));
    monitorCombo->setCurrentIndex(monitorIndex >= 0 ? monitorIndex : 0);

    const QString fontFamily = cfg.get("label/font_family", "Microsoft YaHei UI").toString();
    fontCombo->setCurrentFont(QFont(fontFamily));
    fontSizeSpin->setValue(cfg.get("label/font_size", 10).toInt());

    loading = false;
    applyButton->setEnabled(false);
}

void SettingsDialog::saveSettings() {
    const bool wantedStartup = startupCheck->isChecked();
    if (Startup::isOn() != wantedStartup)
        Startup::set(wantedStartup);

    cfg.setDisplayMonitor(static_cast<DisplayMonitor>(monitorCombo->currentData().toInt()));
    cfg.set("label/font_family", fontCombo->currentFont().family());
    cfg.set("label/font_size", fontSizeSpin->value());
    cfg.notifyConfigEdited();

    startupCheck->setChecked(Startup::isOn());
    applyButton->setEnabled(false);
}

void SettingsDialog::updateApplyState() {
    if (!loading)
        applyButton->setEnabled(true);
}

void SettingsDialog::openConfigFile() {
    cfg.editConfigFile();
}
