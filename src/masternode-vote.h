// Copyright (c) 2021 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_VOTE_H
#define MASTERNODE_VOTE_H

#include "uint256.h"
#include "masternode-budget.h"

#include <string>
#include <list>

class CWallet;

UniValue mnLocalBudgetVoteInner(const uint256& budgetHash, bool fFinal, const CBudgetVote::VoteDirection& nVote);
UniValue mnBudgetVoteInner(CWallet* const pwallet, const uint256& budgetHash, bool fFinal, const CBudgetVote::VoteDirection& nVote, const Optional<std::string>& mnAliasFilter);

#endif // MASTERNODE_VOTE_H
