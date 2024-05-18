// Copyright (c) 2021 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "qt/rpdchain/governancemodel.h"

#include "masternodeman.h"
#include "masternode-budget.h"
#include "masternode-vote.h"
#include "guiconstants.h"
#include "qt/transactiontablemodel.h"
#include "qt/transactionrecord.h"
#include "qt/rpdchain/mnmodel.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "walletmodel.h"

#include <algorithm>
#include <QTimer>

std::string ProposalInfo::statusToStr() const
{
    switch(status) {
        case WAITING_FOR_APPROVAL:
            return _("Waiting");
        case PASSING:
            return _("Passing");
        case PASSING_NOT_FUNDED:
            return _("Passing not funded");
        case NOT_PASSING:
            return _("Not Passing");
        case FINISHED:
            return _("Finished");
    }
    return "";
}

GovernanceModel::GovernanceModel(ClientModel* _clientModel, MNModel* _mnModel) : clientModel(_clientModel), mnModel(_mnModel) {}
GovernanceModel::~GovernanceModel() {}

void GovernanceModel::setWalletModel(WalletModel* _walletModel)
{
    walletModel = _walletModel;
    connect(walletModel->getTransactionTableModel(), &TransactionTableModel::txLoaded, this, &GovernanceModel::txLoaded);
}

ProposalInfo GovernanceModel::buildProposalInfo(const CBudgetProposal* prop, bool isPassing, bool isPending)
{
    CTxDestination recipient;
    ExtractDestination(prop->GetPayee(), recipient);

    // Calculate status
    int votesYes = prop->GetYeas();
    int votesNo = prop->GetNays();
    int mnCount = mnodeman.CountEnabled();
    int remainingPayments = prop->GetRemainingPaymentCount(clientModel->getLastBlockProcessedHeight());
    ProposalInfo::Status status;

    if (isPending) {
        // Proposal waiting for confirmation to be broadcasted.
        status = ProposalInfo::WAITING_FOR_APPROVAL;
    } else {
        if (remainingPayments <= 0) {
            status = ProposalInfo::FINISHED;
        } else if (isPassing) {
            status = ProposalInfo::PASSING;
        } else if (allocatedAmount + prop->GetAmount() > getMaxAvailableBudgetAmount() && votesYes - votesNo > mnCount / 10) {
            status = ProposalInfo::PASSING_NOT_FUNDED;
        } else {
            status = ProposalInfo::NOT_PASSING;
        }
    }

    return ProposalInfo(prop->GetHash(),
            prop->GetName(),
            prop->GetURL(),
            votesYes,
            votesNo,
            EncodeDestination(recipient),
            prop->GetAmount(),
            prop->GetTotalPaymentCount(),
            remainingPayments,
            status,
            prop->GetBlockStart(),
            prop->GetBlockEnd());
}

std::list<ProposalInfo> GovernanceModel::getProposals(const ProposalInfo::Status* filterByStatus, bool filterFinished)
{
    if (!clientModel) return {};
    std::list<ProposalInfo> ret;
    std::vector<CBudgetProposal*> budgets = budget.GetBudget();
    allocatedAmount = 0;
    for (const auto& prop : budget.GetAllProposals()) {
        bool isPassing = std::find(budgets.begin(), budgets.end(), prop) != budgets.end();
        ProposalInfo propInfo = buildProposalInfo(prop, isPassing, false);

        if (filterFinished && propInfo.isFinished()) continue;
        if (!filterByStatus || propInfo.status == *filterByStatus) {
            ret.emplace_back(propInfo);
        }
        if (isPassing) allocatedAmount += prop->GetAmount();
    }

    // Add pending proposals
    for (const auto& prop : waitingPropsForConfirmations) {
        ProposalInfo propInfo = buildProposalInfo(&prop, false, true);
        if (!filterByStatus || propInfo.status == *filterByStatus) {
            ret.emplace_back(propInfo);
        }
    }
    return ret;
}

bool GovernanceModel::hasProposals()
{
    return budget.HasAnyProposal() || !waitingPropsForConfirmations.empty();
}

CAmount GovernanceModel::getMaxAvailableBudgetAmount() const
{
    return budget.GetTotalBudget(clientModel->getNumBlocks());
}

int GovernanceModel::getNumBlocksPerBudgetCycle() const
{
    return Params().GetConsensus().nBudgetCycleBlocks;
}

int GovernanceModel::getProposalVoteUpdateMinTime() const
{
    return BUDGET_VOTE_UPDATE_MIN;
}

int GovernanceModel::getPropMaxPaymentsCount() const
{
    return Params().GetConsensus().nMaxProposalPayments;
}

CAmount GovernanceModel::getProposalFeeAmount() const
{
    return PROPOSAL_FEE_TX;
}

int GovernanceModel::getNextSuperblockHeight() const
{
    const int nBlocksPerCycle = getNumBlocksPerBudgetCycle();
    const int chainHeight = clientModel->getNumBlocks();
    return chainHeight - chainHeight % nBlocksPerCycle + nBlocksPerCycle;
}

std::vector<VoteInfo> GovernanceModel::getLocalMNsVotesForProposal(const ProposalInfo& propInfo)
{
    // First, get the local masternodes
    std::vector<COutPoint> vecLocalMn;
    for (int i = 0; i < mnModel->rowCount(); ++i) {
        vecLocalMn.emplace_back(
                uint256S(mnModel->index(i, MNModel::COLLATERAL_ID, QModelIndex()).data().toString().toStdString()),
                mnModel->index(i, MNModel::COLLATERAL_OUT_INDEX, QModelIndex()).data().toInt()
        );
    }

    std::vector<VoteInfo> localVotes;
    {
        LOCK(budget.cs); // future: encapsulate this mutex lock.
        // Get the budget proposal, get the votes, then loop over it and return the ones that correspond to the local masternodes here.
        CBudgetProposal* prop = budget.FindProposal(propInfo.id);
        const auto& mapVotes = prop->GetVotes();
        for (const auto& it : mapVotes) {
            for (const auto& mn : vecLocalMn) {
                /// TODO - THIS NEEDS ATTENTION BUT WILL WORK IN THE MEANTIME
                if (it.second.GetVin().prevout == mn && it.second.IsValid()) {
                    localVotes.emplace_back(mn, (VoteInfo::VoteDirection) it.second.GetDirection(), "", it.second.GetTime());
                    break;
                }
            }
        }
    }
    return localVotes;
}

OperationResult GovernanceModel::validatePropName(const QString& name) const
{
    std::string strName = SanitizeString(name.toStdString());
    if (strName != name.toStdString()) { // invalid characters
        return {false, _("Invalid name, invalid characters")};
    }
    if (strName.size() > (int)PROP_NAME_MAX_SIZE) { // limit
        return {false, strprintf(_("Invalid name, maximum size of %d exceeded"), PROP_NAME_MAX_SIZE)};
    }
    return {true};
}

OperationResult GovernanceModel::validatePropURL(const QString& url) const
{
    std::string strURL = SanitizeString(url.toStdString());
    if (strURL != url.toStdString()) {
        return {false, _("Invalid URL, invalid characters")};
    }
    std::string strError;
    return {validateURL(strURL, strError, PROP_URL_MAX_SIZE), strError};
}

OperationResult GovernanceModel::validatePropAmount(CAmount amount) const
{
    if (amount < PROPOSAL_MIN_AMOUNT) { // Future: move constant to a budget interface.
        return {false, strprintf(_("Amount below the minimum of %s RPD"), FormatMoney(PROPOSAL_MIN_AMOUNT))};
    }

    if (amount > getMaxAvailableBudgetAmount()) {
        return {false, strprintf(_("Amount exceeding the maximum available of %s RPD"), FormatMoney(getMaxAvailableBudgetAmount()))};
    }
    return {true};
}

OperationResult GovernanceModel::validatePropPaymentCount(int paymentCount) const
{
    if (paymentCount < 1) return { false, _("Invalid payment count, must be greater than zero.")};
    int nMaxPayments = getPropMaxPaymentsCount();
    if (paymentCount > nMaxPayments) {
        return { false, strprintf(_("Invalid payment count, cannot be greater than %d"), nMaxPayments)};
    }
    return {true};
}

OperationResult GovernanceModel::createProposal(const std::string& strProposalName,
                                                const std::string& strURL,
                                                int nPaymentCount,
                                                CAmount nAmount,
                                                const std::string& strPaymentAddr)
{
    // First get the next superblock height
    int nBlockStart = getNextSuperblockHeight();

    // Parse address
    CTxDestination dest = DecodeDestination(strPaymentAddr);
    CScript scriptPubKey = GetScriptForDestination(dest);

    // Validate proposal
    int nBlockEnd = nBlockStart + (nPaymentCount * Params().GetConsensus().nBudgetCycleBlocks);
    CBudgetProposal proposal(strProposalName, strURL, nBlockStart, nBlockEnd, scriptPubKey, nAmount, UINT256_ZERO);

    // Craft and send transaction.
    auto opRes = walletModel->createAndSendProposalFeeTx(proposal);
    if (!opRes) return opRes;
    scheduleBroadcast(proposal);

    return {true};
}

OperationResult GovernanceModel::voteForProposal(const ProposalInfo& prop,
                                                 bool isVotePositive,
                                                 const std::vector<std::string>& mnVotingAlias)
{
    UniValue ret; // future: don't use UniValue here.
    for (const auto& mnAlias : mnVotingAlias) {
        bool fLegacyMN = true; // For now, only legacy MNs
        ret = mnBudgetVoteInner(nullptr,
                          prop.id,
                          false,
                          isVotePositive ? CBudgetVote::VoteDirection::VOTE_YES : CBudgetVote::VoteDirection::VOTE_NO,
                          mnAlias);
        if (ret.exists("detail") && ret["detail"].isArray()) {
            const UniValue& obj = ret["detail"].get_array()[0];
            if (obj["result"].getValStr() != "success") {
                return {false, obj["error"].getValStr()};
            }
        }
    }
    // add more information with ret["overall"]
    return {true};
}

void GovernanceModel::scheduleBroadcast(const CBudgetProposal& proposal)
{
    // Cache the proposal to be sent as soon as it gets the minimum required confirmations
    // without requiring user interaction
    waitingPropsForConfirmations.emplace_back(proposal);

    // Launch timer if it's not already running
    if (!pollTimer) pollTimer = new QTimer(this);
    if (!pollTimer->isActive()) {
        connect(pollTimer, &QTimer::timeout, this, &GovernanceModel::pollGovernanceChanged);
        pollTimer->start(MODEL_UPDATE_DELAY * 60 * (walletModel->isTestNetwork() ? 0.5 : 3.5)); // Every 3.5 minutes
    }
}

void GovernanceModel::pollGovernanceChanged()
{
    int chainHeight = clientModel->getNumBlocks();

    // Try to broadcast any pending for confirmations proposal
    auto it = waitingPropsForConfirmations.begin();
    while (it != waitingPropsForConfirmations.end())
    {
        // Remove expired proposals
        if (it->IsExpired(clientModel->getNumBlocks())) {
            continue;
        }

        // Try to add it
        if (!budget.AddProposal(*it)) {
            LogPrint(BCLog::QT, "Cannot broadcast budget proposal - %s", it->IsInvalidReason());
            // Remove proposals which due a reorg lost their fee tx
            if (it->IsInvalidReason().find("Can't find collateral tx") != std::string::npos) {
                continue;
            }
            // Check if the proposal didn't exceed the superblock start height
            if (chainHeight >= it->GetBlockStart()) {
               continue;
            }
        }

        CBudgetProposalBroadcast propToSend(*it);
        propToSend.Relay();
    }

    // If there are no more waiting proposals, turn the timer off.
    if (waitingPropsForConfirmations.empty()) {
        pollTimer->stop();
    }
}

void GovernanceModel::stop()
{
    if (pollTimer && pollTimer->isActive()) {
        pollTimer->stop();
    }
}

void GovernanceModel::txLoaded(const QString& id, const int txType, const int txStatus)
{
    if (txType == TransactionRecord::SendToNobody) {
        // If the tx is not longer available in the mainchain, drop it.
        if (txStatus == TransactionStatus::Conflicted ||
            txStatus == TransactionStatus::NotAccepted) {
            return;
        }
        // If this is a proposal fee, parse it.
        const auto& wtx = walletModel->getTx(uint256S(id.toStdString()));
        assert(wtx);
        const auto& it = wtx->mapValue.find("proposal");
        if (it != wtx->mapValue.end()) {
            const std::vector<unsigned char> vec = ParseHex(it->second);
            if (vec.empty()) return;
            CDataStream ss(vec, SER_DISK, CLIENT_VERSION);
            CBudgetProposal proposal;
            ss >> proposal;
            if (!budget.HaveProposal(proposal.GetHash()) &&
                !proposal.IsExpired(clientModel->getNumBlocks()) &&
                proposal.GetBlockStart() > clientModel->getNumBlocks()) {
                scheduleBroadcast(proposal);
            }
        }
    }
}
