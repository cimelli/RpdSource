// Copyright (c) 2019 The RPDCHAIN developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SETTINGSWALLETOPTIONSWIDGET_H
#define SETTINGSWALLETOPTIONSWIDGET_H

#include <QWidget>
#include <QDataWidgetMapper>
#include "qt/rpdchain/pwidget.h"
namespace Ui {
class SettingsWalletOptionsWidget;
}

class SettingsWalletOptionsWidget : public PWidget
{
    Q_OBJECT

public:
    explicit SettingsWalletOptionsWidget(RPDCHAINGUI* _window, QWidget *parent = nullptr);
    ~SettingsWalletOptionsWidget();

    void setMapper(QDataWidgetMapper *mapper);
    void setSpinBoxStakeSplitThreshold(double val);

Q_SIGNALS:
    void saveSettings();
    void discardSettings();

public Q_SLOTS:
    void onResetClicked();

private:
    Ui::SettingsWalletOptionsWidget *ui;
};

#endif // SETTINGSWALLETOPTIONSWIDGET_H
