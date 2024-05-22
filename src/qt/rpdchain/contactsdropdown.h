// Copyright (c) 2019 The RPDCHAIN developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CONTACTSDROPDOWN_H
#define CONTACTSDROPDOWN_H

#include "addresstablemodel.h"
#include "qt/rpdchain/pwidget.h"
#include "qt/rpdchain/contactdropdownrow.h"
#include "qt/rpdchain/furabstractlistitemdelegate.h"
#include "qt/rpdchain/addressfilterproxymodel.h"
#include <QListView>
#include <QObject>
#include <QWidget>


class ContactsViewDelegate;
class ContViewHolder;
class WalletModel;


QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

class ContactsDropdown : public PWidget
{
    Q_OBJECT
public:
    explicit ContactsDropdown(int minWidth, int minHeight, PWidget* parent = nullptr);
    ContactsDropdown(int minWidth, int minHeight, RPDCHAINGUI* _window = nullptr,
                     QWidget* parent = nullptr);

    void resizeList(int minWidth, int mintHeight);
    void setWalletModel(WalletModel* _model, const QStringList& type);
    void setType(const QStringList& type);
    void changeTheme(bool isLightTheme, QString& theme) override;
Q_SIGNALS:
    void contactSelected(QString address, QString label);
private:
    FurAbstractListItemDelegate* delegate = nullptr;
    AddressTableModel* model = nullptr;
    AddressFilterProxyModel *filter = nullptr;
    QListView *list;
    QFrame *frameList;
    void init(int minWidth, int minHeight);
private Q_SLOTS:
    void handleClick(const QModelIndex &index);
};

#endif // CONTACTSDROPDOWN_H
