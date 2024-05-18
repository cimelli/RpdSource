// Copyright (c) 2021 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "masternode-vote.h"

#include "masternode-budget.h"
#include "masternodeman.h"
#include "masternodeconfig.h"
#include "util.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h" // future: use interface instead.
#endif

static UniValue packRetStatus(const std::string& nodeType, const std::string& result, const std::string& error)
{
    UniValue statusObj(UniValue::VOBJ);
    statusObj.pushKV("node", nodeType);
    statusObj.pushKV("result", result);
    statusObj.pushKV("error", error);
    return statusObj;
}

static UniValue packErrorRetStatus(const std::string& nodeType, const std::string& error)
{
    return packRetStatus(nodeType, "failed", error);
}

static UniValue packVoteReturnValue(const UniValue& details, int success, int failed)
{
    UniValue returnObj(UniValue::VOBJ);
    returnObj.pushKV("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", success, failed));
    returnObj.pushKV("detail", details);
    return returnObj;
}

// key, alias and collateral outpoint of a masternode. Struct used to sign proposal/budget votes
struct MnKeyData
{
    std::string mnAlias;
    const COutPoint* collateralOut;

    MnKeyData() = delete;
    MnKeyData(const std::string& _mnAlias, const COutPoint* _collateralOut, const CKey& _key):
        mnAlias(_mnAlias),
        collateralOut(_collateralOut),
        key(_key)
    {}

    bool Sign(CSignedMessage* msg) const
    {
        return msg->Sign(key, key.GetPubKey());
    }

private:
    CKey key;
};

typedef std::list<MnKeyData> mnKeyList;

static UniValue voteProposal(const uint256& propHash, const CBudgetVote::VoteDirection& nVote,
                             const mnKeyList& mnKeys, UniValue resultsObj, int failed)
{
    int success = 0;
    for (const auto& k : mnKeys) {
        CBudgetVote vote(CTxIn(*k.collateralOut), propHash, nVote);
        if (!k.Sign(&vote)) {
            resultsObj.push_back(packErrorRetStatus(k.mnAlias, "Failure to sign."));
            failed++;
            continue;
        }
        std::string strError = "";
        if (!budget.UpdateProposal(vote, nullptr, strError)) {
            resultsObj.push_back(packErrorRetStatus(k.mnAlias, strError));
            failed++;
            continue;
        }
        budget.mapSeenMasternodeBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
        vote.Relay();
        resultsObj.push_back(packRetStatus(k.mnAlias, "success", ""));
        success++;
    }

    return packVoteReturnValue(resultsObj, success, failed);
}

static UniValue voteFinalBudget(const uint256& budgetHash,
                                const mnKeyList& mnKeys, UniValue resultsObj, int failed)
{
    int success = 0;
    for (const auto& k : mnKeys) {
        CFinalizedBudgetVote vote(CTxIn(*k.collateralOut), budgetHash);
        if (!k.Sign(&vote)) {
            resultsObj.push_back(packErrorRetStatus(k.mnAlias, "Failure to sign."));
            failed++;
            continue;
        }
        std::string strError = "";
        if (!budget.UpdateFinalizedBudget(vote, nullptr, strError)) {
            resultsObj.push_back(packErrorRetStatus(k.mnAlias, strError));
            failed++;
            continue;
        }
        budget.mapSeenFinalizedBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
        vote.Relay();
        resultsObj.push_back(packRetStatus(k.mnAlias, "success", ""));
        success++;
    }

    return packVoteReturnValue(resultsObj, success, failed);
}

static mnKeyList getMNKeys(const Optional<std::string>& mnAliasFilter,
                           UniValue& resultsObj, int& failed)
{
    mnKeyList mnKeys;
    for (const CMasternodeConfig::CMasternodeEntry& mne : masternodeConfig.getEntries()) {
        if (mnAliasFilter && *mnAliasFilter != mne.getAlias()) continue;
        CKey mnKey; CPubKey mnPubKey;
        const std::string& mnAlias = mne.getAlias();
        if (!CMessageSigner::GetKeysFromSecret(mne.getPrivKey(), mnKey, mnPubKey)) {
            resultsObj.push_back(packErrorRetStatus(mnAlias, "Could not get key from masternode.conf"));
            failed++;
            continue;
        }
        CMasternode* pmn = mnodeman.Find(mnPubKey);
        if (!pmn) {
            resultsObj.push_back(packErrorRetStatus(mnAlias, "Can't find masternode by pubkey"));
            failed++;
            continue;
        }
        mnKeys.emplace_back(mnAlias, &pmn->vin.prevout, mnKey);
    }
    return mnKeys;
}

static mnKeyList getMNKeysForActiveMasternode(UniValue& resultsObj)
{
    // local node must be a masternode
    if (!fMasterNode) {
        throw std::runtime_error(_("This is not a masternode. 'local' option disabled."));
    }

    if (activeMasternode.vin == nullopt) {
        throw std::runtime_error(_("Active Masternode not initialized."));
    }

    CKey mnKey; CPubKey mnPubKey;
    if (!activeMasternode.GetKeys(mnKey, mnPubKey)) {
        resultsObj.push_back(packErrorRetStatus("local", "Can't find masternode by pubkey"));
        return mnKeyList();
    }

    CMasternode* pmn = mnodeman.Find(mnPubKey);
    if (!pmn) {
        resultsObj.push_back(packErrorRetStatus("local", "Can't find masternode by pubkey"));
        return mnKeyList();
    }

    return {MnKeyData("local", &pmn->vin.prevout, mnKey)};
}

// vote on proposal (finalized budget, if fFinal=true) with all possible keys or a single mn (mnAliasFilter)
UniValue mnBudgetVoteInner(CWallet* const pwallet, const uint256& budgetHash, bool fFinal, const CBudgetVote::VoteDirection& nVote, const Optional<std::string>& mnAliasFilter)
{
    UniValue resultsObj(UniValue::VARR);
    int failed = 0;

    mnKeyList mnKeys = getMNKeys(mnAliasFilter, resultsObj, failed);

    if (mnKeys.empty()) {
        return packVoteReturnValue(resultsObj, 0, failed);
    }

    return (fFinal ? voteFinalBudget(budgetHash, mnKeys, resultsObj, failed)
                   : voteProposal(budgetHash, nVote, mnKeys, resultsObj, failed));
}

// vote on proposal (finalized budget, if fFinal=true) with the active local masternode
UniValue mnLocalBudgetVoteInner(const uint256& budgetHash, bool fFinal, const CBudgetVote::VoteDirection& nVote)
{
    UniValue resultsObj(UniValue::VARR);

    mnKeyList mnKeys = getMNKeysForActiveMasternode(resultsObj);

    if (mnKeys.empty()) {
        return packVoteReturnValue(resultsObj, 0, 1);
    }

    return (fFinal ? voteFinalBudget(budgetHash, mnKeys, resultsObj, 0)
                   : voteProposal(budgetHash, nVote, mnKeys, resultsObj, 0));
}
