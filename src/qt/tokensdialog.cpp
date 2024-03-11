// Copyright (c) 2011-2013 The Bitcoin developers // Distributed under the MIT/X11 software license, see the accompanying // file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tokensdialog.h"
#include "ui_tokensdialog.h"

#include "qt/rpdchain/qtutils.h"

#include "clientmodel.h"
#include "walletmodel.h"
#include "guiutil.h"

#include "tokencore/tokencore.h"
#include "tokencore/sp.h"
#include "tokencore/tally.h"
#include "tokencore/walletutils.h"

#include "amount.h"
#include "sync.h"
#include "wallet/wallet.h"

#include <stdint.h>
#include <map>
#include <sstream>
#include <string>

#include <QAbstractItemView>
#include <QAction>
#include <QDialog>
#include <QHeaderView>
#include <QMenu>
#include <QModelIndex>
#include <QPoint>
#include <QResizeEvent>
#include <QString>
#include <QTableWidgetItem>
#include <QWidget>
#include <QScrollBar>

using std::ostringstream;
using std::string;
using namespace mastercore;

TokensDialog::TokensDialog(QWidget *parent) :
    QDialog(parent), ui(new Ui::tokensDialog), clientModel(0), walletModel(0)
{
    // setup
    ui->setupUi(this);

    // CSS
    this->setStyleSheet(parent->styleSheet());
    setCssProperty(ui->label, "text-subtitle", false);
    setCssProperty(ui->propSelectorWidget, "btn-combo-secundary");
    setCssProperty(ui->balancesTable, "token-table");
    ui->propSelectorWidget->setView(new QListView());

    ui->balancesTable->setShowGrid(false);
    ui->balancesTable->setStyleSheet("QTableWidget {color:#d1d5db; border:none; background-color:transparent;} QHeaderView::section {font-weight:bold;}");

    ui->balancesTable->setColumnCount(5);
    ui->balancesTable->setHorizontalHeaderItem(0, new QTableWidgetItem("ID"));
    ui->balancesTable->setHorizontalHeaderItem(1, new QTableWidgetItem("Token Name"));
    ui->balancesTable->setHorizontalHeaderItem(2, new QTableWidgetItem("Ticker"));
    ui->balancesTable->setHorizontalHeaderItem(3, new QTableWidgetItem("Reserved"));
    ui->balancesTable->setHorizontalHeaderItem(4, new QTableWidgetItem("Available"));
    borrowedColumnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(ui->balancesTable, 100, 100);
    // Align text in header cells
    ui->balancesTable->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignLeft);
    ui->balancesTable->horizontalHeaderItem(1)->setTextAlignment(Qt::AlignLeft);
    ui->balancesTable->horizontalHeaderItem(2)->setTextAlignment(Qt::AlignLeft);
    ui->balancesTable->horizontalHeaderItem(3)->setTextAlignment(Qt::AlignRight);
    ui->balancesTable->horizontalHeaderItem(4)->setTextAlignment(Qt::AlignRight);
    // note neither resizetocontents or stretch allow user to adjust - go interactive then manually set widths
    #if QT_VERSION < 0x050000
       ui->balancesTable->horizontalHeader()->setResizeMode(0, QHeaderView::Interactive);
       ui->balancesTable->horizontalHeader()->setResizeMode(1, QHeaderView::Interactive);
       ui->balancesTable->horizontalHeader()->setResizeMode(2, QHeaderView::Interactive);
       ui->balancesTable->horizontalHeader()->setResizeMode(3, QHeaderView::Interactive);
       ui->balancesTable->horizontalHeader()->setResizeMode(4, QHeaderView::Interactive);
    #else
       ui->balancesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
       ui->balancesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
       ui->balancesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
       ui->balancesTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
       ui->balancesTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Interactive);
    #endif
    ui->balancesTable->setAlternatingRowColors(true);

    // do an initial population
    UpdatePropSelector();
    PopulateBalances(2147483646); // 2147483646 = summary (last possible ID for test eco props)

    // Initial resizing
    ui->balancesTable->resizeColumnToContents(0);
    ui->balancesTable->resizeColumnToContents(1);
    ui->balancesTable->resizeColumnToContents(2);
    ui->balancesTable->resizeColumnToContents(3);
    ui->balancesTable->resizeColumnToContents(4);
    ui->balancesTable->verticalHeader()->setVisible(false);
    ui->balancesTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->balancesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->balancesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->balancesTable->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->balancesTable->setTabKeyNavigation(false);
    ui->balancesTable->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->balancesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Actions
    QAction *balancesCopyIDAction = new QAction(tr("Copy property ID"), this);
    QAction *balancesCopyNameAction = new QAction(tr("Copy property name"), this);
    QAction *balancesCopyTickerAction = new QAction(tr("Copy ticker"), this);
    QAction *balancesCopyAddressAction = new QAction(tr("Copy address"), this);
    QAction *balancesCopyLabelAction = new QAction(tr("Copy label"), this);
    QAction *balancesCopyReservedAmountAction = new QAction(tr("Copy reserved amount"), this);
    QAction *balancesCopyAvailableAmountAction = new QAction(tr("Copy available amount"), this);

    contextMenu = new QMenu();
    contextMenu->addAction(balancesCopyLabelAction);
    contextMenu->addAction(balancesCopyAddressAction);
    contextMenu->addAction(balancesCopyTickerAction);
    contextMenu->addAction(balancesCopyReservedAmountAction);
    contextMenu->addAction(balancesCopyAvailableAmountAction);
    contextMenuSummary = new QMenu();
    contextMenuSummary->addAction(balancesCopyIDAction);
    contextMenuSummary->addAction(balancesCopyNameAction);
    contextMenuSummary->addAction(balancesCopyTickerAction);
    contextMenuSummary->addAction(balancesCopyReservedAmountAction);
    contextMenuSummary->addAction(balancesCopyAvailableAmountAction);

    // Connect actions
    connect(ui->balancesTable, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));
    connect(ui->propSelectorWidget, SIGNAL(activated(int)), this, SLOT(propSelectorChanged()));
    connect(balancesCopyIDAction, SIGNAL(triggered()), this, SLOT(balancesCopyCol0()));
    connect(balancesCopyNameAction, SIGNAL(triggered()), this, SLOT(balancesCopyCol1()));
    connect(balancesCopyTickerAction, SIGNAL(triggered()), this, SLOT(balancesCopyCol2()));
    connect(balancesCopyLabelAction, SIGNAL(triggered()), this, SLOT(balancesCopyCol0()));
    connect(balancesCopyAddressAction, SIGNAL(triggered()), this, SLOT(balancesCopyCol1()));
    connect(balancesCopyReservedAmountAction, SIGNAL(triggered()), this, SLOT(balancesCopyCol3()));
    connect(balancesCopyAvailableAmountAction, SIGNAL(triggered()), this, SLOT(balancesCopyCol4()));
}

TokensDialog::~TokensDialog()
{
    delete ui;
}

void TokensDialog::reinitToken()
{
    ui->propSelectorWidget->clear();
    ui->balancesTable->setRowCount(0);
    UpdatePropSelector();
    PopulateBalances(2147483646); // 2147483646 = summary (last possible ID for test eco props)
}

void TokensDialog::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if (model != NULL) {
        connect(model, SIGNAL(refreshTokenBalance()), this, SLOT(balancesUpdated()));
        connect(model, SIGNAL(reinitTokenState()), this, SLOT(reinitToken()));
    }
}

void TokensDialog::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if (model != NULL) { } // do nothing, signals from walletModel no longer needed
}

void TokensDialog::UpdatePropSelector()
{
    LOCK(cs_tally);

    // don't waste time updating if there are no new properties
    if ((uint32_t)ui->propSelectorWidget->count() > global_wallet_property_list.size()) return;

    // a new property has been added to the wallet, update the property selector
    QString spId = ui->propSelectorWidget->itemData(ui->propSelectorWidget->currentIndex()).toString();
    ui->propSelectorWidget->clear();
    ui->propSelectorWidget->addItem("Select Ticker For Address Info","2147483646"); //use last possible ID for summary for now
    // populate property selector
    for (std::set<uint32_t>::iterator it = global_wallet_property_list.begin(); it != global_wallet_property_list.end(); ++it) {
        uint32_t propertyId = *it;
        
        // Check if available balance is greater than 1, or if reserved balance is greater than 1 or if 0
        if ((global_balance_money[propertyId] > 1 && global_balance_reserved[propertyId] == 0) ||
            (global_balance_money[propertyId] == 0 && global_balance_reserved[propertyId] > 1))
        {
            std::string spId = strprintf("%d", propertyId);
            std::string spTicker = getPropertyTicker(propertyId);
            if(spTicker.size()>20) spTicker=spTicker.substr(0,20)+"...";
            spTicker += " (ID#" + spId + ")";
            ui->propSelectorWidget->addItem(spTicker.c_str(), spId.c_str());
        }
    }
    int propIdx = ui->propSelectorWidget->findData(spId);
    if (propIdx != -1) { ui->propSelectorWidget->setCurrentIndex(propIdx); }
}

void TokensDialog::AddRow(const std::string& label, const std::string& address, const std::string& spTicker, const std::string& reserved, const std::string& available)
{
    int workingRow = ui->balancesTable->rowCount();
    ui->balancesTable->insertRow(workingRow);
    QTableWidgetItem *labelCell = new QTableWidgetItem(QString::fromStdString(label));
    QTableWidgetItem *addressCell = new QTableWidgetItem(QString::fromStdString(address));
    QTableWidgetItem *spTickerCell = new QTableWidgetItem(QString::fromStdString(spTicker));
    QTableWidgetItem *reservedCell = new QTableWidgetItem(QString::fromStdString(reserved));
    QTableWidgetItem *availableCell = new QTableWidgetItem(QString::fromStdString(available));

    if (workingRow % 2 == 0) {
        labelCell->setData(Qt::BackgroundRole, QColor(80,80,79));
        addressCell->setData(Qt::BackgroundRole, QColor(80,80,79));
        spTickerCell->setData(Qt::BackgroundRole, QColor(80,80,79));
        reservedCell->setData(Qt::BackgroundRole, QColor(80,80,79));
        availableCell->setData(Qt::BackgroundRole, QColor(80,80,79));
    } else {
        labelCell->setData(Qt::BackgroundRole, QColor(60,60,59));
        addressCell->setData(Qt::BackgroundRole, QColor(60,60,59));
        spTickerCell->setData(Qt::BackgroundRole, QColor(60,60,59));
        reservedCell->setData(Qt::BackgroundRole, QColor(60,60,59));
        availableCell->setData(Qt::BackgroundRole, QColor(60,60,59));
    }

    labelCell->setTextAlignment(Qt::AlignLeft + Qt::AlignVCenter);
    addressCell->setTextAlignment(Qt::AlignLeft + Qt::AlignVCenter);
    spTickerCell->setTextAlignment(Qt::AlignLeft + Qt::AlignVCenter);
    reservedCell->setTextAlignment(Qt::AlignRight + Qt::AlignVCenter);
    availableCell->setTextAlignment(Qt::AlignRight + Qt::AlignVCenter);
    ui->balancesTable->setItem(workingRow, 0, labelCell);
    ui->balancesTable->setItem(workingRow, 1, addressCell);
    ui->balancesTable->setItem(workingRow, 2, spTickerCell);
    ui->balancesTable->setItem(workingRow, 3, reservedCell);
    ui->balancesTable->setItem(workingRow, 4, availableCell);
}

void TokensDialog::PopulateBalances(unsigned int propertyId) {
    ui->balancesTable->setRowCount(0); // Fresh slate (note this will automatically clean up all existing QWidgetItems in the table)

    LOCK(cs_tally);

    // Are we summarizing?
    if(propertyId == 2147483646) {
        ui->balancesTable->setHorizontalHeaderItem(0, new QTableWidgetItem("ID"));
        ui->balancesTable->setHorizontalHeaderItem(1, new QTableWidgetItem("Token Name"));
        ui->balancesTable->setHorizontalHeaderItem(2, new QTableWidgetItem("Ticker"));
        ui->balancesTable->setHorizontalHeaderItem(3, new QTableWidgetItem("Reserved"));
        ui->balancesTable->setHorizontalHeaderItem(4, new QTableWidgetItem("Available"));

        ui->balancesTable->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignLeft);
        ui->balancesTable->horizontalHeaderItem(1)->setTextAlignment(Qt::AlignLeft);
        ui->balancesTable->horizontalHeaderItem(2)->setTextAlignment(Qt::AlignLeft);
        ui->balancesTable->horizontalHeaderItem(3)->setTextAlignment(Qt::AlignRight);
        ui->balancesTable->horizontalHeaderItem(4)->setTextAlignment(Qt::AlignRight);

        // Loop over the wallet property list and add the wallet totals
        for (std::set<uint32_t>::iterator it = global_wallet_property_list.begin(); it != global_wallet_property_list.end(); ++it) {
            uint32_t propertyId = *it;
            std::string spId = strprintf("%d", propertyId);
            std::string spName = getPropertyName(propertyId).c_str();
            std::string spTicker = getPropertyTicker(propertyId).c_str();
            std::string available = FormatMP(propertyId, global_balance_money[propertyId]);
            std::string reserved = FormatMP(propertyId, global_balance_reserved[propertyId]);

            // Check if available balance is greater than 1, or if reserved balance is greater than 1 or if 0
            if ((global_balance_money[propertyId] > 1 && global_balance_reserved[propertyId] == 0) ||
                (global_balance_money[propertyId] == 0 && global_balance_reserved[propertyId] > 1))
                AddRow(spId, spName, spTicker, reserved, available);
        }
    } else {
        ui->balancesTable->setHorizontalHeaderItem(0, new QTableWidgetItem("Label"));
        ui->balancesTable->setHorizontalHeaderItem(1, new QTableWidgetItem("Address"));
        ui->balancesTable->setHorizontalHeaderItem(2, new QTableWidgetItem("Ticker"));
        ui->balancesTable->setHorizontalHeaderItem(3, new QTableWidgetItem("Reserved"));
        ui->balancesTable->setHorizontalHeaderItem(4, new QTableWidgetItem("Available"));

        ui->balancesTable->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignLeft);
        ui->balancesTable->horizontalHeaderItem(1)->setTextAlignment(Qt::AlignLeft);
        ui->balancesTable->horizontalHeaderItem(2)->setTextAlignment(Qt::AlignLeft);
        ui->balancesTable->horizontalHeaderItem(3)->setTextAlignment(Qt::AlignRight);
        ui->balancesTable->horizontalHeaderItem(4)->setTextAlignment(Qt::AlignRight);

        bool propertyIsDivisible = isPropertyDivisible(propertyId); // Only fetch the SP once, not for every address

        // Iterate mp_tally_map looking for addresses that hold a balance in propertyId
        for(std::unordered_map<string, CMPTally>::iterator my_it = mp_tally_map.begin(); my_it != mp_tally_map.end(); ++my_it) {
            const std::string& address = my_it->first;
            CMPTally& tally = my_it->second;
            tally.init();

            uint32_t id;
            bool watchAddress = false, includeAddress = false;
            while (0 != (id = (tally.next()))) {
                if (id == propertyId) {
                    includeAddress = true;
                    break;
                }
            }
            if (!includeAddress) continue; // Ignore this address, has never transacted in this propertyId

            // Determine if this address is in the wallet
            int addressIsMine = IsMyAddress(address);
            if (!addressIsMine) continue; // Ignore this address, not in the wallet
            if (addressIsMine != ISMINE_SPENDABLE) watchAddress = true;

            // Obtain the balances for the address directly from tally
            int64_t available = tally.getMoney(propertyId, BALANCE);
            available += tally.getMoney(propertyId, PENDING);
            int64_t reserved = tally.getMoney(propertyId, SELLOFFER_RESERVE);
            reserved += tally.getMoney(propertyId, ACCEPT_RESERVE);
            reserved += tally.getMoney(propertyId, METADEX_RESERVE);

            // Format the balances
            string reservedStr, availableStr;
            if (propertyIsDivisible) {
                reservedStr = FormatDivisibleMP(reserved);
                availableStr = FormatDivisibleMP(available);
            } else {
                reservedStr = FormatIndivisibleMP(reserved);
                availableStr = FormatIndivisibleMP(available);
            }

            // Fetch ticker for this propertyId
            std::string spTicker = getPropertyTicker(propertyId);
            
            // add the row
            if (!watchAddress) {
                AddRow(GetAddressLabel(my_it->first), address, spTicker, reservedStr, availableStr);
            } else {
                AddRow(GetAddressLabel(my_it->first), address + " (watch-only)", spTicker, reservedStr, availableStr);
            }
        }
    }
}

void TokensDialog::propSelectorChanged()
{
    QString spId = ui->propSelectorWidget->itemData(ui->propSelectorWidget->currentIndex()).toString();
    unsigned int propertyId = spId.toUInt();
    PopulateBalances(propertyId);
}

void TokensDialog::contextualMenu(const QPoint &point)
{
    QModelIndex index = ui->balancesTable->indexAt(point);
    if(index.isValid())
    {
        QString spId = ui->propSelectorWidget->itemData(ui->propSelectorWidget->currentIndex()).toString();
        unsigned int propertyId = spId.toUInt();
        if (propertyId == 2147483646) {
            contextMenuSummary->exec(QCursor::pos());
        } else {
            contextMenu->exec(QCursor::pos());
        }
    }
}

void TokensDialog::balancesCopyCol0()
{
    GUIUtil::setClipboard(ui->balancesTable->item(ui->balancesTable->currentRow(),0)->text());
}

void TokensDialog::balancesCopyCol1()
{
    GUIUtil::setClipboard(ui->balancesTable->item(ui->balancesTable->currentRow(),1)->text());
}

void TokensDialog::balancesCopyCol2()
{
    GUIUtil::setClipboard(ui->balancesTable->item(ui->balancesTable->currentRow(),2)->text());
}

void TokensDialog::balancesCopyCol3()
{
    GUIUtil::setClipboard(ui->balancesTable->item(ui->balancesTable->currentRow(),3)->text());
}

void TokensDialog::balancesCopyCol4()
{
    GUIUtil::setClipboard(ui->balancesTable->item(ui->balancesTable->currentRow(),4)->text());
}

void TokensDialog::balancesUpdated()
{
    UpdatePropSelector();
    propSelectorChanged(); // refresh the table with the currently selected property ID
}

// We override the virtual resizeEvent of the QWidget to adjust tables column
// sizes as the tables width is proportional to the dialogs width.
void TokensDialog::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    borrowedColumnResizingFixer->stretchColumnWidth(1);
}
