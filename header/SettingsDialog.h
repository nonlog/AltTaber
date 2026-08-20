#ifndef ALTTABER_SETTINGSDIALOG_H
#define ALTTABER_SETTINGSDIALOG_H

#include <QDialog>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QSpinBox;
class QPushButton;
class QShowEvent;

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void loadSettings();
    void saveSettings();
    void updateApplyState();
    void openConfigFile();

    QCheckBox* startupCheck{};
    QComboBox* monitorCombo{};
    QFontComboBox* fontCombo{};
    QSpinBox* fontSizeSpin{};
    QPushButton* applyButton{};
    bool loading{false};
};

#endif // ALTTABER_SETTINGSDIALOG_H
