#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QSpinBox>
#include <QVBoxLayout>

#include "utils/ConfigManager.h"
#include "utils/Startup.h"
#include "utils/QtWin.h"

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("AltTaber Settings"));
    setWindowIcon(QIcon(":/img/icon.ico"));
    setModal(false);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    resize(580, 500);
    setMinimumSize(540, 470);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 20, 22, 20);
    root->setSpacing(14);

    auto* intro = new QLabel(tr("Configure AltTaber. Changes are saved to config.ini and can be applied without restarting."), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* generalGroup = new QGroupBox(tr("General"), this);
    auto* generalLayout = new QVBoxLayout(generalGroup);
    generalLayout->setContentsMargins(14, 14, 14, 14);
    startupCheck = new QCheckBox(tr("Start AltTaber with Windows"), generalGroup);
    adminStartupCheck = new QCheckBox(tr("Run at startup as administrator"), generalGroup);
    adminStartupCheck->setToolTip(tr("Uses a Windows Task Scheduler logon task with highest privileges. Enabling or disabling this mode may show one UAC prompt."));
    hideTrayIconCheck = new QCheckBox(tr("Hide the notification area icon"), generalGroup);
    hideTrayIconCheck->setToolTip(tr("To open settings again after hiding the icon, start AltTaber.exe. The running instance will show this window."));
    generalLayout->addWidget(startupCheck);
    generalLayout->addWidget(adminStartupCheck);
    generalLayout->addWidget(hideTrayIconCheck);
    root->addWidget(generalGroup);

    auto* displayGroup = new QGroupBox(tr("Display"), this);
    auto* displayForm = new QFormLayout(displayGroup);
    displayForm->setContentsMargins(14, 14, 14, 14);
    displayForm->setHorizontalSpacing(18);
    displayForm->setVerticalSpacing(10);
    displayForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    monitorCombo = new QComboBox(displayGroup);
    monitorCombo->addItem(tr("Primary monitor"), PrimaryMonitor);
    monitorCombo->addItem(tr("Monitor under mouse pointer"), MouseMonitor);
    displayForm->addRow(tr("Show switcher on:"), monitorCombo);
    root->addWidget(displayGroup);

    auto* appearanceGroup = new QGroupBox(tr("Appearance"), this);
    auto* appearanceForm = new QFormLayout(appearanceGroup);
    appearanceForm->setContentsMargins(14, 14, 14, 14);
    appearanceForm->setHorizontalSpacing(18);
    appearanceForm->setVerticalSpacing(10);
    appearanceForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    fontCombo = new QFontComboBox(appearanceGroup);
    fontCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    fontCombo->setMinimumContentsLength(22);
    fontSizeSpin = new QSpinBox(appearanceGroup);
    fontSizeSpin->setRange(8, 20);
    fontSizeSpin->setSuffix(tr(" pt"));
    fontSizeSpin->setMinimumWidth(110);

    fontPreview = new QLabel(tr("AltTaber window title preview"), appearanceGroup);
    fontPreview->setFrameShape(QFrame::StyledPanel);
    fontPreview->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    fontPreview->setMinimumHeight(46);
    fontPreview->setContentsMargins(10, 4, 10, 4);

    appearanceForm->addRow(tr("Title font:"), fontCombo);
    appearanceForm->addRow(tr("Title size:"), fontSizeSpin);
    appearanceForm->addRow(tr("Preview:"), fontPreview);
    root->addWidget(appearanceGroup);

    root->addStretch();

    auto* bottom = new QHBoxLayout;
    auto* openConfig = new QPushButton(tr("Open config.ini"), this);
    openConfig->setToolTip(tr("Open the raw configuration file for advanced or experimental options."));
    bottom->addWidget(openConfig);
    bottom->addStretch();

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    applyButton = buttons->button(QDialogButtonBox::Apply);
    bottom->addWidget(buttons);
    root->addLayout(bottom);

    connect(openConfig, &QPushButton::clicked, this, &SettingsDialog::openConfigFile);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (saveSettings())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(applyButton, &QPushButton::clicked, this, [this] { saveSettings(); });

    const auto changed = [this] { updateApplyState(); };
    connect(startupCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        adminStartupCheck->setEnabled(enabled);
        updateApplyState();
    });
    connect(adminStartupCheck, &QCheckBox::toggled, this, changed);
    connect(hideTrayIconCheck, &QCheckBox::toggled, this, changed);
    connect(monitorCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(fontCombo, &QFontComboBox::currentFontChanged, this, [this] {
        updateFontPreview();
        updateApplyState();
    });
    connect(fontSizeSpin, &QSpinBox::valueChanged, this, [this] {
        updateFontPreview();
        updateApplyState();
    });

    loadSettings();
}

void SettingsDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    QSettings personalize(
        R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",
        QSettings::NativeFormat);
    const bool dark = personalize.value("AppsUseLightTheme", 1).toInt() == 0;
    QtWin::applyMicaAlt(this, dark);
    loadSettings();
}

void SettingsDialog::loadSettings() {
    loading = true;
    const auto startupMode = Startup::mode();
    startupCheck->setChecked(startupMode != Startup::Mode::Disabled);
    adminStartupCheck->setChecked(startupMode == Startup::Mode::Elevated);
    adminStartupCheck->setEnabled(startupCheck->isChecked());
    hideTrayIconCheck->setChecked(cfg.get("general/hide_tray_icon", false).toBool());

    const int monitorIndex = monitorCombo->findData(static_cast<int>(cfg.getDisplayMonitor()));
    monitorCombo->setCurrentIndex(monitorIndex >= 0 ? monitorIndex : 0);

    const QString fontFamily = cfg.get("label/font_family", "Microsoft YaHei UI").toString();
    fontCombo->setCurrentFont(QFont(fontFamily));
    fontSizeSpin->setValue(qBound(8, cfg.get("label/font_size", 10).toInt(), 20));
    updateFontPreview();

    loading = false;
    applyButton->setEnabled(false);
}

bool SettingsDialog::saveSettings() {
    const auto wantedStartupMode = !startupCheck->isChecked()
        ? Startup::Mode::Disabled
        : (adminStartupCheck->isChecked() ? Startup::Mode::Elevated : Startup::Mode::Normal);

    bool startupOk = true;
    if (Startup::mode() != wantedStartupMode)
        startupOk = Startup::setMode(wantedStartupMode);

    cfg.setDisplayMonitor(static_cast<DisplayMonitor>(monitorCombo->currentData().toInt()));
    cfg.set("general/hide_tray_icon", hideTrayIconCheck->isChecked());
    cfg.set("label/font_family", fontCombo->currentFont().family());
    cfg.set("label/font_size", fontSizeSpin->value());
    cfg.notifyConfigEdited();

    if (!startupOk) {
        QMessageBox::warning(this, tr("Startup settings"),
                             tr("Windows could not apply the requested startup mode. If administrator startup was selected, the UAC prompt may have been cancelled."));
        loadSettings();
        return false;
    }

    loadSettings();
    return true;
}

void SettingsDialog::updateApplyState() {
    if (!loading)
        applyButton->setEnabled(true);
}

void SettingsDialog::updateFontPreview() {
    QFont previewFont = fontCombo->currentFont();
    previewFont.setPointSize(fontSizeSpin->value());
    fontPreview->setFont(previewFont);
}

void SettingsDialog::openConfigFile() {
    cfg.editConfigFile();
}
