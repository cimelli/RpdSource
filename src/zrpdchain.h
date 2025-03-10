// Copyright (c) 2018-2020 The RPDCHAIN developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RPDCHAIN_ZRPDCHAIN_H
#define RPDCHAIN_ZRPDCHAIN_H

#include "chain.h"
#include "libzerocoin/Coin.h"
#include "libzerocoin/Denominations.h"
#include "libzerocoin/CoinSpend.h"
#include <list>
#include <string>

class CBlock;
class CBlockIndex;
class CBigNum;
struct CMintMeta;
class CTransaction;
class CTxIn;
class CTxOut;
class CValidationState;
class CZerocoinMint;
class uint256;

bool BlockToMintValueVector(const CBlock& block, const libzerocoin::CoinDenomination denom, std::vector<CBigNum>& vValues);
bool BlockToPubcoinList(const CBlock& block, std::list<libzerocoin::PublicCoin>& listPubcoins, bool fFilterInvalid);
bool BlockToZerocoinMintList(const CBlock& block, std::list<CZerocoinMint>& vMints, bool fFilterInvalid);
void FindMints(std::vector<CMintMeta> vMintsToFind, std::vector<CMintMeta>& vMintsToUpdate, std::vector<CMintMeta>& vMissingMints);
bool GetZerocoinMint(const CBigNum& bnPubcoin, uint256& txHash);
bool IsPubcoinInBlockchain(const uint256& hashPubcoin, uint256& txid);
bool IsSerialInBlockchain(const CBigNum& bnSerial, int& nHeightTx);
bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, uint256& txidSpend);
bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, uint256& txidSpend, CTransaction& tx);
bool RemoveSerialFromDB(const CBigNum& bnSerial);
std::string ReindexZerocoinDB();
libzerocoin::CoinSpend TxInToZerocoinSpend(const CTxIn& txin);
bool TxOutToPublicCoin(const CTxOut& txout, libzerocoin::PublicCoin& pubCoin, CValidationState& state);
std::list<libzerocoin::CoinDenomination> ZerocoinSpendListFromBlock(const CBlock& block, bool fFilterInvalid);

/** Global variable for the zerocoin supply */
extern std::map<libzerocoin::CoinDenomination, int64_t> mapZerocoinSupply;
int64_t GetZerocoinSupply();
bool UpdateZRPDSupplyConnect(const CBlock& block, CBlockIndex* pindex, bool fJustCheck);
bool UpdateZRPDSupplyDisconnect(const CBlock& block, CBlockIndex* pindex);

#endif //RPDCHAIN_ZRPDCHAIN_H
