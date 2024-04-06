// Master Protocol transaction code

#include "tokencore/tx.h"

#include "tokencore/activation.h"
#include "tokencore/dbfees.h"
#include "tokencore/errors.h"
#include "tokencore/dbspinfo.h"
#include "tokencore/dbstolist.h"
#include "tokencore/dbtradelist.h"
#include "tokencore/dbtxlist.h"
#include "tokencore/dex.h"
#include "tokencore/log.h"
#include "tokencore/mdex.h"
#include "tokencore/notifications.h"
#include "tokencore/tokencore.h"
#include "tokencore/parsing.h"
#include "tokencore/rules.h"
#include "tokencore/sp.h"
#include "tokencore/sto.h"
#include "tokencore/utilsrpdchain.h"
#include "tokencore/version.h"

#include "rpc/server.h"

#include "amount.h"
#include "base58.h"
#include "main.h"
#include "sync.h"
#include "utiltime.h"

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <utility>
#include <vector>
#include <tuple>
#include <sstream>

using boost::algorithm::token_compress_on;

using namespace mastercore;

static const std::regex IPFS_CHARACTERS("Qm[1-9A-HJ-NP-Za-km-z]{44,}|b[A-Za-z2-7]{58,}|B[A-Z2-7]{58,}|z[1-9A-HJ-NP-Za-km-z]{48,}|F[0-9A-F]{50,}");
static const std::regex TOKEN_SUB_CHARACTERS("^[r]?[A-Z0-9._]{3,20}#[A-Z0-9._]{3,20}$");
static const std::regex PROTECTED_TICKERS("^RPD$|^RPDCHAIN$|^RPDX$");
static const std::regex TOKEN_TICKER_CHARACTERS("^[r]?[A-Z0-9._]{3,20}$");
static const std::regex USERNAME_CHARACTERS("^[a-z0-9._]{3,20}.rpd$");
static const std::regex PROTECTED_USERNAMES("^rpdchain.rpd$|^rpd.rpd$|^rns.rpd$|^rpdx.rpd$");

std::vector<std::string> SplitSubTicker(const std::string &s) {
    std::vector<std::string> elements;
    std::stringstream ss(s);
    std::string step;

    while (std::getline(ss, step, '#')) {
        elements.push_back(step);
    }

    return elements;
}

bool IsTokenTickerValid(const std::string& ticker)
{
    return std::regex_match(ticker, TOKEN_TICKER_CHARACTERS)
        && !std::regex_match(ticker, PROTECTED_TICKERS);
}

bool IsSubTickerValid(const std::string& ticker)
{
    return std::regex_match(ticker, TOKEN_SUB_CHARACTERS);
}

bool IsUsernameValid(const std::string& username)
{
    return std::regex_match(username, USERNAME_CHARACTERS)
        && !std::regex_match(username, PROTECTED_USERNAMES);
}

bool IsTokenIPFSValid(const std::string& string)
{
    return std::regex_match(string, IPFS_CHARACTERS) || string == "";
}

/** Returns a label for the given transaction type. */
std::string mastercore::strTransactionType(uint16_t txType)
{
    switch (txType) {
        case TOKEN_TYPE_SIMPLE_SEND: return "Simple Send";
        case TOKEN_TYPE_SEND_TO_MANY: return "Send To Many";
        case TOKEN_TYPE_RESTRICTED_SEND: return "Restricted Send";
        case TOKEN_TYPE_SEND_TO_OWNERS: return "Send To Owners";
        case TOKEN_TYPE_SEND_ALL: return "Send All";
        case TOKEN_TYPE_SAVINGS_MARK: return "Savings";
        case TOKEN_TYPE_SAVINGS_COMPROMISED: return "Savings COMPROMISED";
        case TOKEN_TYPE_RATELIMITED_MARK: return "Rate-Limiting";
        case TOKEN_TYPE_AUTOMATIC_DISPENSARY: return "Automatic Dispensary";
        case TOKEN_TYPE_TRADE_OFFER: return "DEx Sell Offer";
        case TOKEN_TYPE_METADEX_TRADE: return "MetaDEx trade";
        case TOKEN_TYPE_METADEX_CANCEL_PRICE: return "MetaDEx cancel-price";
        case TOKEN_TYPE_METADEX_CANCEL_PAIR: return "MetaDEx cancel-pair";
        case TOKEN_TYPE_METADEX_CANCEL_ECOSYSTEM: return "MetaDEx cancel-ecosystem";
        case TOKEN_TYPE_ACCEPT_OFFER_RPD: return "DEx Accept Offer";
        case TOKEN_TYPE_CREATE_PROPERTY_FIXED: return "Create Property - Fixed";
        case TOKEN_TYPE_CREATE_PROPERTY_VARIABLE: return "Create Property - Variable";
        case TOKEN_TYPE_PROMOTE_PROPERTY: return "Promote Property";
        case TOKEN_TYPE_CLOSE_CROWDSALE: return "Close Crowdsale";
        case TOKEN_TYPE_CREATE_PROPERTY_MANUAL: return "Create Property - Manual";
        case TOKEN_TYPE_GRANT_PROPERTY_TOKENS: return "Grant Property Tokens";
        case TOKEN_TYPE_REVOKE_PROPERTY_TOKENS: return "Revoke Property Tokens";
        case TOKEN_TYPE_CHANGE_ISSUER_ADDRESS: return "Change Issuer Address";
        case TOKEN_TYPE_ENABLE_FREEZING: return "Enable Freezing";
        case TOKEN_TYPE_DISABLE_FREEZING: return "Disable Freezing";
        case TOKEN_TYPE_FREEZE_PROPERTY_TOKENS: return "Freeze Property Tokens";
        case TOKEN_TYPE_UNFREEZE_PROPERTY_TOKENS: return "Unfreeze Property Tokens";
        case TOKEN_TYPE_NOTIFICATION: return "Notification";
        case TOKEN_TYPE_RPD_PAYMENT: return "RPD Payment";
        case TOKENCORE_MESSAGE_TYPE_ALERT: return "ALERT";
        case TOKENCORE_MESSAGE_TYPE_DEACTIVATION: return "Feature Deactivation";
        case TOKENCORE_MESSAGE_TYPE_ACTIVATION: return "Feature Activation";

        default: return "* unknown type *";
    }
}

/** Helper to convert class number to string. */
static std::string intToClass(int encodingClass)
{
    switch (encodingClass) {
        case TOKEN_CLASS_A:
            return "A";
        case TOKEN_CLASS_B:
            return "B";
        case TOKEN_CLASS_C:
            return "C";
    }

    return "-";
}

/** Checks whether a pointer to the payload is past it's last position. */
bool CMPTransaction::isOverrun(const char* p)
{
    ptrdiff_t pos = (char*) p - (char*) &pkt;
    return (pos > pkt_size);
}

// -------------------- PACKET PARSING -----------------------

/** Parses the packet or payload. */
bool CMPTransaction::interpret_Transaction()
{
    if (!interpret_TransactionType()) {
        PrintToLog("Failed to interpret type and version\n");
        return false;
    }

    switch (type) {
        case TOKEN_TYPE_SIMPLE_SEND:
            return interpret_SimpleSend();

        case TOKEN_TYPE_SEND_TO_MANY:
            return interpret_SendToMany();

        case TOKEN_TYPE_SEND_TO_OWNERS:
            return interpret_SendToOwners();

        case TOKEN_TYPE_SEND_ALL:
            return interpret_SendAll();

        case TOKEN_TYPE_TRADE_OFFER:
            return interpret_TradeOffer();

        case TOKEN_TYPE_ACCEPT_OFFER_RPD:
            return interpret_AcceptOfferRPD();

        case TOKEN_TYPE_METADEX_TRADE:
            return interpret_MetaDExTrade();

        case TOKEN_TYPE_METADEX_CANCEL_PRICE:
            return interpret_MetaDExCancelPrice();

        case TOKEN_TYPE_METADEX_CANCEL_PAIR:
            return interpret_MetaDExCancelPair();

        case TOKEN_TYPE_METADEX_CANCEL_ECOSYSTEM:
            return interpret_MetaDExCancelEcosystem();

        case TOKEN_TYPE_CREATE_PROPERTY_FIXED:
            return interpret_CreatePropertyFixed();

        case TOKEN_TYPE_CREATE_PROPERTY_VARIABLE:
            return interpret_CreatePropertyVariable();

        case TOKEN_TYPE_CREATE_PROPERTY_MANUAL:
            return interpret_CreatePropertyManaged();

        case TOKEN_TYPE_CLOSE_CROWDSALE:
            return interpret_CloseCrowdsale();

        case TOKEN_TYPE_GRANT_PROPERTY_TOKENS:
            return interpret_GrantTokens();

        case TOKEN_TYPE_REVOKE_PROPERTY_TOKENS:
            return interpret_RevokeTokens();

        case TOKEN_TYPE_CHANGE_ISSUER_ADDRESS:
            return interpret_ChangeIssuer();

        case TOKEN_TYPE_ENABLE_FREEZING:
            return interpret_EnableFreezing();

        case TOKEN_TYPE_DISABLE_FREEZING:
            return interpret_DisableFreezing();

        case TOKEN_TYPE_FREEZE_PROPERTY_TOKENS:
            return interpret_FreezeTokens();

        case TOKEN_TYPE_UNFREEZE_PROPERTY_TOKENS:
            return interpret_UnfreezeTokens();

        case TOKENCORE_MESSAGE_TYPE_DEACTIVATION:
            return interpret_Deactivation();

        case TOKEN_TYPE_RPD_PAYMENT:
            return interpret_RPDPayment();

        case TOKENCORE_MESSAGE_TYPE_ACTIVATION:
            return interpret_Activation();

        case TOKENCORE_MESSAGE_TYPE_ALERT:
            return interpret_Alert();
    }

    return false;
}

/** Version and type */
bool CMPTransaction::interpret_TransactionType()
{
    if (pkt_size < 4) {
        return false;
    }
    uint16_t txVersion = 0;
    uint16_t txType = 0;
    memcpy(&txVersion, &pkt[0], 2);
    SwapByteOrder16(txVersion);
    memcpy(&txType, &pkt[2], 2);
    SwapByteOrder16(txType);
    version = txVersion;
    type = txType;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t------------------------------\n");
        PrintToLog("\t         version: %d, class %s\n", txVersion, intToClass(encodingClass));
        PrintToLog("\t            type: %d (%s)\n", txType, strTransactionType(txType));
    }

    return true;
}

/** Tx 1 */
bool CMPTransaction::interpret_SimpleSend()
{
    if (pkt_size < 16) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;

    // Special case: if can't find the receiver -- assume send to self!
    if (receiver.empty()) {
        receiver = sender;
    }

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t           value: %s\n", FormatMP(property, nValue));
    }

    return true;
}

/** Tx 3 */
bool CMPTransaction::interpret_SendToOwners()
{
    int expectedSize = (version == MP_TX_PKT_V0) ? 16 : 20;
    if (pkt_size < expectedSize) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;
    if (version > MP_TX_PKT_V0) {
        memcpy(&distribution_property, &pkt[16], 4);
        SwapByteOrder32(distribution_property);
    }

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t             property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t                value: %s\n", FormatMP(property, nValue));
        if (version > MP_TX_PKT_V1) {
            PrintToLog("\t distributionproperty: %d (%s)\n", distribution_property, strMPProperty(distribution_property));
        }
    }

    return true;
}

/** Tx 4 */
bool CMPTransaction::interpret_SendAll()
{
    if (pkt_size < 5) {
        return false;
    }
    memcpy(&ecosystem, &pkt[4], 1);

    property = ecosystem; // provide a hint for the UI, TODO: better handling!

    // Special case: if can't find the receiver -- assume send to self!
    if (receiver.empty()) {
        receiver = sender;
    }

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t       ecosystem: %d\n", (int)ecosystem);
    }

    return true;
}

/** Tx 5 */
bool CMPTransaction::interpret_SendToMany()
{
    // minimum case without any recipients, still needs propertyid
    if (pkt_size < 9) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);

    memcpy(&numberOfSTMReceivers, &pkt[8], 1);

    // check total size for all receivers
    if (pkt_size < (9 + (numberOfSTMReceivers * (1 + 8)))) {
        return false;
    }

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t       receivers: %d\n", numberOfSTMReceivers);
    }

    size_t pos = 9;
    for (uint8_t i = 0; i < numberOfSTMReceivers; ++i) {
        uint8_t outputN = 0;
        memcpy(&outputN, &pkt[pos], 1);

        uint64_t valueN = 0;
        memcpy(&valueN, &pkt[pos+1], 8);
        SwapByteOrder64(valueN);

        outputValuesForSTM.push_back(std::make_tuple(outputN, valueN));
        nValue += valueN;

        pos = pos + 9;

        if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
            PrintToLog("\t               output: %d\n", outputN);
            PrintToLog("\t               value: %s\n", FormatMP(property, valueN));
        }
    }

    nNewValue = nValue;

    return true;
}

/** Tx 20 */
bool CMPTransaction::interpret_TradeOffer()
{
    int expectedSize = (version == MP_TX_PKT_V0) ? 33 : 34;
    if (pkt_size < expectedSize) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;
    memcpy(&amount_desired, &pkt[16], 8);
    memcpy(&blocktimelimit, &pkt[24], 1);
    memcpy(&min_fee, &pkt[25], 8);
    if (version > MP_TX_PKT_V0) {
        memcpy(&subaction, &pkt[33], 1);
    }
    SwapByteOrder64(amount_desired);
    SwapByteOrder64(min_fee);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t           value: %s\n", FormatMP(property, nValue));
        PrintToLog("\t  amount desired: %s\n", FormatDivisibleMP(amount_desired));
        PrintToLog("\tblock time limit: %d\n", blocktimelimit);
        PrintToLog("\t         min fee: %s\n", FormatDivisibleMP(min_fee));
        if (version > MP_TX_PKT_V0) {
            PrintToLog("\t      sub-action: %d\n", subaction);
        }
    }

    return true;
}

/** Tx 22 */
bool CMPTransaction::interpret_AcceptOfferRPD()
{
    if (pkt_size < 16) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t           value: %s\n", FormatMP(property, nValue));
    }

    return true;
}

/** Tx 25 */
bool CMPTransaction::interpret_MetaDExTrade()
{
    if (pkt_size < 28) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;
    memcpy(&desired_property, &pkt[16], 4);
    SwapByteOrder32(desired_property);
    memcpy(&desired_value, &pkt[20], 8);
    SwapByteOrder64(desired_value);

    action = CMPTransaction::ADD; // depreciated

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t           value: %s\n", FormatMP(property, nValue));
        PrintToLog("\tdesired property: %d (%s)\n", desired_property, strMPProperty(desired_property));
        PrintToLog("\t   desired value: %s\n", FormatMP(desired_property, desired_value));
    }

    return true;
}

/** Tx 26 */
bool CMPTransaction::interpret_MetaDExCancelPrice()
{
    if (pkt_size < 28) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;
    memcpy(&desired_property, &pkt[16], 4);
    SwapByteOrder32(desired_property);
    memcpy(&desired_value, &pkt[20], 8);
    SwapByteOrder64(desired_value);

    action = CMPTransaction::CANCEL_AT_PRICE; // depreciated

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t           value: %s\n", FormatMP(property, nValue));
        PrintToLog("\tdesired property: %d (%s)\n", desired_property, strMPProperty(desired_property));
        PrintToLog("\t   desired value: %s\n", FormatMP(desired_property, desired_value));
    }

    return true;
}

/** Tx 27 */
bool CMPTransaction::interpret_MetaDExCancelPair()
{
    if (pkt_size < 12) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&desired_property, &pkt[8], 4);
    SwapByteOrder32(desired_property);

    nValue = 0; // depreciated
    nNewValue = nValue; // depreciated
    desired_value = 0; // depreciated
    action = CMPTransaction::CANCEL_ALL_FOR_PAIR; // depreciated

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\tdesired property: %d (%s)\n", desired_property, strMPProperty(desired_property));
    }

    return true;
}

/** Tx 28 */
bool CMPTransaction::interpret_MetaDExCancelEcosystem()
{
    if (pkt_size < 5) {
        return false;
    }
    memcpy(&ecosystem, &pkt[4], 1);

    property = ecosystem; // depreciated
    desired_property = ecosystem; // depreciated
    nValue = 0; // depreciated
    nNewValue = nValue; // depreciated
    desired_value = 0; // depreciated
    action = CMPTransaction::CANCEL_EVERYTHING; // depreciated

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t       ecosystem: %d\n", (int)ecosystem);
    }

    return true;
}

/** Tx 50 */
bool CMPTransaction::interpret_CreatePropertyFixed()
{
    if (pkt_size < 25) {
        return false;
    }
    const char* p = 11 + (char*) &pkt;
    std::vector<std::string> spstr;
    memcpy(&ecosystem, &pkt[4], 1);
    memcpy(&prop_type, &pkt[5], 2);
    SwapByteOrder16(prop_type);
    memcpy(&prev_prop_id, &pkt[7], 4);
    SwapByteOrder32(prev_prop_id);
    for (int i = 0; i < 7; i++) {
        spstr.push_back(std::string(p));
        p += spstr.back().size() + 1;
    }
    int i = 0;
    memcpy(category, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(subcategory, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(name, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(ticker, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(url, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(data, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(royaltiesReceiver, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(&nValue, p, 8);
    SwapByteOrder64(nValue);
    p += 8;
    nNewValue = nValue;

    memcpy(&royaltiesPercentage, p++, 1);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t         ecosystem: %d\n", ecosystem);
        PrintToLog("\t     property type: %d (%s)\n", prop_type, strPropertyType(prop_type));
        PrintToLog("\t  prev property id: %d\n", prev_prop_id);
        PrintToLog("\t          category: %s\n", category);
        PrintToLog("\t       subcategory: %s\n", subcategory);
        PrintToLog("\t              name: %s\n", name);
        PrintToLog("\t            ticker: %s\n", ticker);
        PrintToLog("\t               url: %s\n", url);
        PrintToLog("\t              data: %s\n", data);
        PrintToLog("\t             value: %s\n", FormatByType(nValue, prop_type));

        PrintToLog("\troyalties receiver: %s\n", royaltiesReceiver);
        PrintToLog("\t        percentage: %d\n", royaltiesPercentage);
    }

    if (isOverrun(p)) {
        PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
        return false;
    }

    return true;
}

/** Tx 51 */
bool CMPTransaction::interpret_CreatePropertyVariable()
{
    if (pkt_size < 39) {
        return false;
    }
    const char* p = 11 + (char*) &pkt;
    std::vector<std::string> spstr;
    memcpy(&ecosystem, &pkt[4], 1);
    memcpy(&prop_type, &pkt[5], 2);
    SwapByteOrder16(prop_type);
    memcpy(&prev_prop_id, &pkt[7], 4);
    SwapByteOrder32(prev_prop_id);
    for (int i = 0; i < 6; i++) {
        spstr.push_back(std::string(p));
        p += spstr.back().size() + 1;
    }
    int i = 0;
    memcpy(category, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(subcategory, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(name, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(ticker, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(url, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(data, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(&property, p, 4);
    SwapByteOrder32(property);
    p += 4;
    memcpy(&nValue, p, 8);
    SwapByteOrder64(nValue);
    p += 8;
    nNewValue = nValue;
    memcpy(&deadline, p, 8);
    SwapByteOrder64(deadline);
    p += 8;
    memcpy(&early_bird, p++, 1);
    memcpy(&percentage, p++, 1);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t       ecosystem: %d\n", ecosystem);
        PrintToLog("\t   property type: %d (%s)\n", prop_type, strPropertyType(prop_type));
        PrintToLog("\tprev property id: %d\n", prev_prop_id);
        PrintToLog("\t        category: %s\n", category);
        PrintToLog("\t     subcategory: %s\n", subcategory);
        PrintToLog("\t            name: %s\n", name);
        PrintToLog("\t          ticker: %s\n", ticker);
        PrintToLog("\t             url: %s\n", url);
        PrintToLog("\t            data: %s\n", data);
        PrintToLog("\tproperty desired: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t tokens per unit: %s\n", FormatByType(nValue, prop_type));
        PrintToLog("\t        deadline: %d\n", deadline);
        PrintToLog("\tearly bird bonus: %d\n", early_bird);
        PrintToLog("\t    issuer bonus: %d\n", percentage);
    }

    if (isOverrun(p)) {
        PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
        return false;
    }

    return true;
}

/** Tx 53 */
bool CMPTransaction::interpret_CloseCrowdsale()
{
    if (pkt_size < 8) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
    }

    return true;
}

/** Tx 54 */
bool CMPTransaction::interpret_CreatePropertyManaged()
{
    if (pkt_size < 17) {
        return false;
    }
    const char* p = 11 + (char*) &pkt;
    std::vector<std::string> spstr;
    memcpy(&ecosystem, &pkt[4], 1);
    memcpy(&prop_type, &pkt[5], 2);
    SwapByteOrder16(prop_type);
    memcpy(&prev_prop_id, &pkt[7], 4);
    SwapByteOrder32(prev_prop_id);
    for (int i = 0; i < 6; i++) {
        spstr.push_back(std::string(p));
        p += spstr.back().size() + 1;
    }
    int i = 0;
    memcpy(category, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(subcategory, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(name, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(ticker, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(url, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;
    memcpy(data, spstr[i].c_str(), SP_STRING_FIELD_LEN); i++;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t       ecosystem: %d\n", ecosystem);
        PrintToLog("\t   property type: %d (%s)\n", prop_type, strPropertyType(prop_type));
        PrintToLog("\tprev property id: %d\n", prev_prop_id);
        PrintToLog("\t        category: %s\n", category);
        PrintToLog("\t     subcategory: %s\n", subcategory);
        PrintToLog("\t            name: %s\n", name);
        PrintToLog("\t          ticker: %s\n", ticker);
        PrintToLog("\t             url: %s\n", url);
        PrintToLog("\t            data: %s\n", data);
    }

    if (isOverrun(p)) {
        PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
        return false;
    }

    return true;
}

/** Tx 55 */
bool CMPTransaction::interpret_GrantTokens()
{
    if (pkt_size < 16) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;

    // Special case: if can't find the receiver -- assume grant to self!
    if (receiver.empty()) {
        receiver = sender;
    }

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t           value: %s\n", FormatMP(property, nValue));
    }

    return true;
}

/** Tx 56 */
bool CMPTransaction::interpret_RevokeTokens()
{
    if (pkt_size < 16) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t           value: %s\n", FormatMP(property, nValue));
    }

    return true;
}

/** Tx 70 */
bool CMPTransaction::interpret_ChangeIssuer()
{
    if (pkt_size < 8) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
    }

    return true;
}

/** Tx 71 */
bool CMPTransaction::interpret_EnableFreezing()
{
    if (pkt_size < 8) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
    }

    return true;
}

/** Tx 72 */
bool CMPTransaction::interpret_DisableFreezing()
{
    if (pkt_size < 8) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
    }

    return true;
}

/** Tx 185 */
bool CMPTransaction::interpret_FreezeTokens()
{
    if (pkt_size < 37) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;

    /**
        Note, TX185 is a virtual reference transaction type.
              With virtual reference transactions a hash160 in the payload sets the receiver.
              Reference outputs are ignored.
    **/
    unsigned char address_version;
    uint160 address_hash160;
    memcpy(&address_version, &pkt[16], 1);
    memcpy(&address_hash160, &pkt[17], 20);
    receiver = HashToAddress(address_version, address_hash160);
    if (receiver.empty()) {
        return false;
    }
    CTxDestination recAddress = DecodeDestination(receiver);
    if (!IsValidDestination(recAddress)) {
        return false;
    }

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t  value (unused): %s\n", FormatMP(property, nValue));
        PrintToLog("\t         address: %s\n", receiver);
    }

    return true;
}

/** Tx 186 */
bool CMPTransaction::interpret_UnfreezeTokens()
{
    if (pkt_size < 37) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;

    /**
        Note, TX186 virtual reference transaction type.
              With virtual reference transactions a hash160 in the payload sets the receiver.
              Reference outputs are ignored.
    **/
    unsigned char address_version;
    uint160 address_hash160;
    memcpy(&address_version, &pkt[16], 1);
    memcpy(&address_hash160, &pkt[17], 20);
    receiver = HashToAddress(address_version, address_hash160);
    if (receiver.empty()) {
        return false;
    }
    CTxDestination recAddress = DecodeDestination(receiver);
    if (!IsValidDestination(recAddress)) {
        return false;
    }

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t  value (unused): %s\n", FormatMP(property, nValue));
        PrintToLog("\t         address: %s\n", receiver);
    }

    return true;
}

/** Tx 65533 */
bool CMPTransaction::interpret_Deactivation()
{
    if (pkt_size < 6) {
        return false;
    }
    memcpy(&feature_id, &pkt[4], 2);
    SwapByteOrder16(feature_id);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t      feature id: %d\n", feature_id);
    }

    return true;
}

/** Tx 80 */
bool CMPTransaction::interpret_RPDPayment()
{
    if (pkt_size < 36) {
        return false;
    }

    const char* p = 4 + (char*) &pkt;
    std::string hash(p);
    linked_txid = ParseHashV(hash, "txid");

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t     linked txid: %s\n", linked_txid.GetHex());
    }

    return true;
}

/** Tx 65534 */
bool CMPTransaction::interpret_Activation()
{
    if (pkt_size < 14) {
        return false;
    }
    memcpy(&feature_id, &pkt[4], 2);
    SwapByteOrder16(feature_id);
    memcpy(&activation_block, &pkt[6], 4);
    SwapByteOrder32(activation_block);
    memcpy(&min_client_version, &pkt[10], 4);
    SwapByteOrder32(min_client_version);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t      feature id: %d\n", feature_id);
        PrintToLog("\tactivation block: %d\n", activation_block);
        PrintToLog("\t minimum version: %d\n", min_client_version);
    }

    return true;
}

/** Tx 65535 */
bool CMPTransaction::interpret_Alert()
{
    if (pkt_size < 11) {
        return false;
    }

    memcpy(&alert_type, &pkt[4], 2);
    SwapByteOrder16(alert_type);
    memcpy(&alert_expiry, &pkt[6], 4);
    SwapByteOrder32(alert_expiry);

    const char* p = 10 + (char*) &pkt;
    std::string spstr(p);
    memcpy(alert_text, spstr.c_str(), std::min(spstr.length(), sizeof(alert_text)-1));

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t      alert type: %d\n", alert_type);
        PrintToLog("\t    expiry value: %d\n", alert_expiry);
        PrintToLog("\t   alert message: %s\n", alert_text);
    }

    if (isOverrun(p)) {
        PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
        return false;
    }

    return true;
}

// ---------------------- CORE LOGIC -------------------------

/**
 * Interprets the payload and executes the logic.
 *
 * @return  0  if the transaction is fully valid
 *         <0  if the transaction is invalid
 */
int CMPTransaction::interpretPacket()
{
    if (rpcOnly) {
        PrintToLog("%s(): ERROR: attempt to execute logic in RPC mode\n", __func__);
        return (PKT_ERROR -1);
    }

    if (!interpret_Transaction()) {
        return (PKT_ERROR -2);
    }

    LOCK(cs_tally);

    if (isAddressFrozen(sender, property)) {
        PrintToLog("%s(): REJECTED: address %s is frozen for property %d\n", __func__, sender, property);
        return (PKT_ERROR -3);
    }

    switch (type) {
        case TOKEN_TYPE_SIMPLE_SEND:
            return logicMath_SimpleSend();

        case TOKEN_TYPE_SEND_TO_MANY:
            return logicMath_SendToMany();

        case TOKEN_TYPE_SEND_TO_OWNERS:
            return logicMath_SendToOwners();

        case TOKEN_TYPE_SEND_ALL:
            return logicMath_SendAll();

        case TOKEN_TYPE_TRADE_OFFER:
            return logicMath_TradeOffer();

        case TOKEN_TYPE_ACCEPT_OFFER_RPD:
            return logicMath_AcceptOffer_RPD();

        case TOKEN_TYPE_METADEX_TRADE:
            return logicMath_MetaDExTrade();

        case TOKEN_TYPE_METADEX_CANCEL_PRICE:
            return logicMath_MetaDExCancelPrice();

        case TOKEN_TYPE_METADEX_CANCEL_PAIR:
            return logicMath_MetaDExCancelPair();

        case TOKEN_TYPE_METADEX_CANCEL_ECOSYSTEM:
            return logicMath_MetaDExCancelEcosystem();

        // Create tokens

        case TOKEN_TYPE_CREATE_PROPERTY_FIXED:
            return logicMath_CreatePropertyFixed();

        case TOKEN_TYPE_CREATE_PROPERTY_VARIABLE:
            return logicMath_CreatePropertyVariable();

        case TOKEN_TYPE_CREATE_PROPERTY_MANUAL:
            return logicMath_CreatePropertyManaged();

        // End create tokens

        case TOKEN_TYPE_CLOSE_CROWDSALE:
            return logicMath_CloseCrowdsale();

        case TOKEN_TYPE_GRANT_PROPERTY_TOKENS:
            return logicMath_GrantTokens();

        case TOKEN_TYPE_REVOKE_PROPERTY_TOKENS:
            return logicMath_RevokeTokens();

        case TOKEN_TYPE_CHANGE_ISSUER_ADDRESS:
            return logicMath_ChangeIssuer();

        case TOKEN_TYPE_RPD_PAYMENT:
            return logicMath_RPDPayment();

        case TOKEN_TYPE_ENABLE_FREEZING:
            return logicMath_EnableFreezing();

        case TOKEN_TYPE_DISABLE_FREEZING:
            return logicMath_DisableFreezing();

        case TOKEN_TYPE_FREEZE_PROPERTY_TOKENS:
            return logicMath_FreezeTokens();

        case TOKEN_TYPE_UNFREEZE_PROPERTY_TOKENS:
            return logicMath_UnfreezeTokens();

        case TOKENCORE_MESSAGE_TYPE_DEACTIVATION:
            return logicMath_Deactivation();

        case TOKENCORE_MESSAGE_TYPE_ACTIVATION:
            return logicMath_Activation();

        case TOKENCORE_MESSAGE_TYPE_ALERT:
            return logicMath_Alert();
    }

    return (PKT_ERROR -100);
}

/** Passive effect of crowdsale participation. */
int CMPTransaction::logicHelper_CrowdsaleParticipation()
{
    CMPCrowd* pcrowdsale = getCrowd(receiver);

    // No active crowdsale
    if (pcrowdsale == NULL) {
        return (PKT_ERROR_CROWD -1);
    }
    // Active crowdsale, but not for this property
    if (pcrowdsale->getCurrDes() != property) {
        return (PKT_ERROR_CROWD -2);
    }

    CMPSPInfo::Entry sp;
    assert(pDbSpInfo->getSP(pcrowdsale->getPropertyId(), sp));
    PrintToLog("INVESTMENT SEND to Crowdsale Issuer: %s\n", receiver);

    // Holds the tokens to be credited to the sender and issuer
    std::pair<int64_t, int64_t> tokens;

    // Passed by reference to determine, if max_tokens has been reached
    bool close_crowdsale = false;

    // Units going into the calculateFundraiser function must match the unit of
    // the fundraiser's property_type. By default this means satoshis in and
    // satoshis out. In the condition that the fundraiser is divisible, but
    // indivisible tokens are accepted, it must account for .0 Div != 1 Indiv,
    // but actually 1.0 Div == 100000000 Indiv. The unit must be shifted or the
    // values will be incorrect, which is what is checked below.
    bool inflateAmount = isPropertyDivisible(property) ? false : true;

    // Calculate the amounts to credit for this fundraiser
    calculateFundraiser(inflateAmount, nValue, sp.early_bird, sp.deadline, blockTime,
            sp.num_tokens, sp.percentage, getTotalTokens(pcrowdsale->getPropertyId()),
            tokens, close_crowdsale);

    if (msc_debug_sp) {
        uint32_t crowdPropertyId = pcrowdsale->getPropertyId();

        PrintToLog("%s(): granting via crowdsale to user: %s %d (%s)\n",
                __func__, FormatMP(crowdPropertyId, tokens.first), crowdPropertyId, strMPProperty(crowdPropertyId));
        PrintToLog("%s(): granting via crowdsale to issuer: %s %d (%s)\n",
                __func__, FormatMP(crowdPropertyId, tokens.second), crowdPropertyId, strMPProperty(crowdPropertyId));
    }

    // Update the crowdsale object
    pcrowdsale->incTokensUserCreated(tokens.first);
    pcrowdsale->incTokensIssuerCreated(tokens.second);

    // Data to pass to txFundraiserData
    int64_t txdata[] = {(int64_t) nValue, blockTime, tokens.first, tokens.second};
    std::vector<int64_t> txDataVec(txdata, txdata + sizeof(txdata) / sizeof(txdata[0]));

    // Insert data about crowdsale participation
    pcrowdsale->insertDatabase(txid, txDataVec);

    // Credit tokens for this fundraiser
    if (tokens.first > 0) {
        assert(update_tally_map(sender, pcrowdsale->getPropertyId(), tokens.first, BALANCE));
    }
    if (tokens.second > 0) {
        assert(update_tally_map(receiver, pcrowdsale->getPropertyId(), tokens.second, BALANCE));
    }

    // Number of tokens has changed, update fee distribution thresholds
    NotifyTotalTokensChanged(pcrowdsale->getPropertyId(), block);

    // Close crowdsale, if we hit MAX_TOKENS
    if (close_crowdsale) {
        eraseMaxedCrowdsale(receiver, blockTime, block);
    }

    // Indicate, if no tokens were transferred
    if (!tokens.first && !tokens.second) {
        return (PKT_ERROR_CROWD -3);
    }

    return 0;
}

/** Tx 0 */
int CMPTransaction::logicMath_SimpleSend()
{
    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_SEND -22);
    }

    if (nValue <= 0 || MAX_INT_8_BYTES < nValue) {
        PrintToLog("%s(): rejected: value out of range or zero: %d", __func__, nValue);
        return (PKT_ERROR_SEND -23);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_SEND -24);
    }

    int64_t nBalance = GetTokenBalance(sender, property, BALANCE);
    if (nBalance < (int64_t) nValue) {
        PrintToLog("%s(): rejected: sender %s has insufficient balance of property %d [%s < %s]\n",
                __func__,
                sender,
                property,
                FormatMP(property, nBalance),
                FormatMP(property, nValue));
        return (PKT_ERROR_SEND -25);
    }

    // ------------------------------------------

    // Move the tokens
    assert(update_tally_map(sender, property, -nValue, BALANCE));
    assert(update_tally_map(receiver, property, nValue, BALANCE));

    // Is there an active crowdsale running from this recepient?
    logicHelper_CrowdsaleParticipation();

    return 0;
}

/** Tx 3 */
int CMPTransaction::logicMath_SendToOwners()
{
    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_STO -22);
    }

    if (nValue <= 0 || MAX_INT_8_BYTES < nValue) {
        PrintToLog("%s(): rejected: value out of range or zero: %d\n", __func__, nValue);
        return (PKT_ERROR_STO -23);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_STO -24);
    }

    if (version > MP_TX_PKT_V0) {
        if (!IsPropertyIdValid(distribution_property)) {
            PrintToLog("%s(): rejected: distribution property %d does not exist\n", __func__, distribution_property);
            return (PKT_ERROR_STO -24);
        }
    }

    int64_t nBalance = GetTokenBalance(sender, property, BALANCE);
    if (nBalance < (int64_t) nValue) {
        PrintToLog("%s(): rejected: sender %s has insufficient balance of property %d [%s < %s]\n",
                __func__,
                sender,
                FormatMP(property, nBalance),
                FormatMP(property, nValue),
                property);
        return (PKT_ERROR_STO -25);
    }

    // ------------------------------------------

    uint32_t distributeTo = (version == MP_TX_PKT_V0) ? property : distribution_property;
    OwnerAddrType receiversSet = STO_GetReceivers(sender, distributeTo, nValue);
    uint64_t numberOfReceivers = receiversSet.size();

    // make sure we found some owners
    if (numberOfReceivers <= 0) {
        PrintToLog("%s(): rejected: no other owners of property %d [owners=%d <= 0]\n", __func__, distributeTo, numberOfReceivers);
        return (PKT_ERROR_STO -26);
    }

    // split up what was taken and distribute between all holders
    int64_t sent_so_far = 0;
    for (OwnerAddrType::reverse_iterator it = receiversSet.rbegin(); it != receiversSet.rend(); ++it) {
        const std::string& address = it->second;

        int64_t will_really_receive = it->first;
        sent_so_far += will_really_receive;

        // real execution of the loop
        assert(update_tally_map(sender, property, -will_really_receive, BALANCE));
        assert(update_tally_map(address, property, will_really_receive, BALANCE));

        // add to stodb
        pDbStoList->recordSTOReceive(address, txid, block, property, will_really_receive);

        if (sent_so_far != (int64_t)nValue) {
            PrintToLog("sent_so_far= %14d, nValue= %14d, n_owners= %d\n", sent_so_far, nValue, numberOfReceivers);
        } else {
            PrintToLog("SendToOwners: DONE HERE\n");
        }
    }

    // sent_so_far must equal nValue here
    assert(sent_so_far == (int64_t)nValue);

    // Number of tokens has changed, update fee distribution thresholds
    if (version == MP_TX_PKT_V0) NotifyTotalTokensChanged(TOKEN_PROPERTY_MSC, block); // fee was burned

    return 0;
}

/** Tx 4 */
int CMPTransaction::logicMath_SendAll()
{
    if (!IsTransactionTypeAllowed(block, ecosystem, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                ecosystem,
                block);
        return (PKT_ERROR_SEND_ALL -22);
    }

    // ------------------------------------------

    CMPTally* ptally = getTally(sender);
    if (ptally == NULL) {
        PrintToLog("%s(): rejected: sender %s has no tokens to send\n", __func__, sender);
        return (PKT_ERROR_SEND_ALL -54);
    }

    uint32_t propertyId = ptally->init();
    int numberOfPropertiesSent = 0;

    while (0 != (propertyId = ptally->next())) {
        // only transfer tokens in the specified ecosystem
        if (ecosystem == TOKEN_PROPERTY_MSC && isTestEcosystemProperty(propertyId)) {
            continue;
        }
        if (ecosystem == TOKEN_PROPERTY_TMSC && isMainEcosystemProperty(propertyId)) {
            continue;
        }

        // do not transfer tokens from a frozen property
        if (isAddressFrozen(sender, propertyId)) {
            PrintToLog("%s(): sender %s is frozen for property %d - the property will not be included in processing.\n", __func__, sender, propertyId);
            continue;
        }

        int64_t moneyAvailable = ptally->getMoney(propertyId, BALANCE);
        if (moneyAvailable > 0) {
            ++numberOfPropertiesSent;
            assert(update_tally_map(sender, propertyId, -moneyAvailable, BALANCE));
            assert(update_tally_map(receiver, propertyId, moneyAvailable, BALANCE));
            pDbTransactionList->recordSendAllSubRecord(txid, numberOfPropertiesSent, propertyId, moneyAvailable);
        }
    }

    if (!numberOfPropertiesSent) {
        PrintToLog("%s(): rejected: sender %s has no tokens to send\n", __func__, sender);
        return (PKT_ERROR_SEND_ALL -55);
    }

    nNewValue = numberOfPropertiesSent;

    return 0;
}

/** Tx 5 */
int CMPTransaction::logicMath_SendToMany()
{
    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_SEND_MANY -22);
    }

    if (nValue <= 0 || MAX_INT_8_BYTES < nValue) {
        PrintToLog("%s(): rejected: value out of range or zero: %d", __func__, nValue);
        return (PKT_ERROR_SEND_MANY -23);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_SEND_MANY -24);
    }

    int64_t nBalance = GetTokenBalance(sender, property, BALANCE);
    if (nBalance < (int64_t) nValue) {
        PrintToLog("%s(): rejected: sender %s has insufficient balance of property %d [%s < %s]\n",
                __func__,
                sender,
                property,
                FormatMP(property, nBalance),
                FormatMP(property, nValue));
        return (PKT_ERROR_SEND_MANY -25);
    }

    // ------------------------------------------

    uint64_t totalAmount = 0;
    uint8_t totalOutputs = 0;

    // Check if all outputs refer to valid destinations
    for (const std::tuple<uint8_t, uint64_t>& entry : outputValuesForSTM) {
        uint8_t output = std::get<0>(entry);
        uint64_t amount = std::get<1>(entry);

        std::string receiver;
        if (!getValidStmAddressAt(output, receiver)) {
            PrintToLog("%s(): rejected: output %d doesn't refer to valid destination\n", __func__, output);
            return (PKT_ERROR_SEND_MANY -26);
        }
        if (receiver.empty()) {
            PrintToLog("%s(): rejected: receiver of output %d doesn't refer to valid destination\n", __func__, output);
            return (PKT_ERROR_SEND_MANY -27);
        }

        totalAmount += amount;
        totalOutputs += 1;
    }

    if (totalAmount != nValue) {
        PrintToLog("%s(): rejected: mismatch of total amounts [%d != %d]\n", __func__, totalAmount, nValue);
        return (PKT_ERROR_SEND_MANY -28);
    }

    if (totalOutputs != numberOfSTMReceivers) {
        PrintToLog("%s(): rejected: mismatch of number of valid receivers [%d != %d]\n", __func__, totalOutputs, numberOfSTMReceivers);
        return (PKT_ERROR_SEND_MANY -29);
    }

    // Move the tokens
    for (const std::tuple<uint8_t, uint64_t>& entry : outputValuesForSTM) {
        uint8_t output = std::get<0>(entry);
        uint64_t amount = std::get<1>(entry);

        std::string receiver;
        assert(getValidStmAddressAt(output, receiver));

        assert(update_tally_map(sender, property, -amount, BALANCE));
        assert(update_tally_map(receiver, property, amount, BALANCE));
    }

    return 0;
}

/** Tx 20 */
int CMPTransaction::logicMath_TradeOffer()
{
    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TRADEOFFER -22);
    }

    if (MAX_INT_8_BYTES < nValue) {
        PrintToLog("%s(): rejected: value out of range or zero: %d\n", __func__, nValue);
        return (PKT_ERROR_TRADEOFFER -23);
    }

    // ------------------------------------------

    int rc = PKT_ERROR_TRADEOFFER;

    // figure out which Action this is based on amount for sale, version & etc.
    switch (version)
    {
        case MP_TX_PKT_V0:
        {
            if (0 != nValue) {
                if (!DEx_offerExists(sender, property)) {
                    rc = DEx_offerCreate(sender, property, nValue, block, amount_desired, min_fee, blocktimelimit, txid, &nNewValue);
                } else {
                    rc = DEx_offerUpdate(sender, property, nValue, block, amount_desired, min_fee, blocktimelimit, txid, &nNewValue);
                }
            } else {
                // what happens if nValue is 0 for V0 ?  ANSWER: check if exists and it does -- cancel, otherwise invalid
                if (DEx_offerExists(sender, property)) {
                    rc = DEx_offerDestroy(sender, property);
                } else {
                    PrintToLog("%s(): rejected: sender %s has no active sell offer for property: %d\n", __func__, sender, property);
                    rc = (PKT_ERROR_TRADEOFFER -49);
                }
            }

            break;
        }

        case MP_TX_PKT_V1:
        {
            if (DEx_offerExists(sender, property)) {
                if (CANCEL != subaction && UPDATE != subaction) {
                    PrintToLog("%s(): rejected: sender %s has an active sell offer for property: %d\n", __func__, sender, property);
                    rc = (PKT_ERROR_TRADEOFFER -48);
                    break;
                }
            } else {
                // Offer does not exist
                if (NEW != subaction) {
                    PrintToLog("%s(): rejected: sender %s has no active sell offer for property: %d\n", __func__, sender, property);
                    rc = (PKT_ERROR_TRADEOFFER -49);
                    break;
                }
            }

            switch (subaction) {
                case NEW:
                    rc = DEx_offerCreate(sender, property, nValue, block, amount_desired, min_fee, blocktimelimit, txid, &nNewValue);
                    break;

                case UPDATE:
                    rc = DEx_offerUpdate(sender, property, nValue, block, amount_desired, min_fee, blocktimelimit, txid, &nNewValue);
                    break;

                case CANCEL:
                    rc = DEx_offerDestroy(sender, property);
                    break;

                default:
                    rc = (PKT_ERROR -999);
                    break;
            }
            break;
        }

        default:
            rc = (PKT_ERROR -500); // neither V0 nor V1
            break;
    };

    return rc;
}

/** Tx 22 */
int CMPTransaction::logicMath_AcceptOffer_RPD()
{
    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (DEX_ERROR_ACCEPT -22);
    }

    if (nValue <= 0 || MAX_INT_8_BYTES < nValue) {
        PrintToLog("%s(): rejected: value out of range or zero: %d\n", __func__, nValue);
        return (DEX_ERROR_ACCEPT -23);
    }

    // ------------------------------------------

    // the min fee spec requirement is checked in the following function
    int rc = DEx_acceptCreate(sender, receiver, property, nValue, block, tx_fee_paid, &nNewValue);

    return rc;
}

/** Tx 25 */
int CMPTransaction::logicMath_MetaDExTrade()
{
    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_METADEX -22);
    }

    if (property == desired_property) {
        PrintToLog("%s(): rejected: property for sale %d and desired property %d must not be equal\n",
                __func__,
                property,
                desired_property);
        return (PKT_ERROR_METADEX -29);
    }

    if (isTestEcosystemProperty(property) != isTestEcosystemProperty(desired_property)) {
        PrintToLog("%s(): rejected: property for sale %d and desired property %d not in same ecosystem\n",
                __func__,
                property,
                desired_property);
        return (PKT_ERROR_METADEX -30);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property for sale %d does not exist\n", __func__, property);
        return (PKT_ERROR_METADEX -31);
    }

    if (!IsPropertyIdValid(desired_property)) {
        PrintToLog("%s(): rejected: desired property %d does not exist\n", __func__, desired_property);
        return (PKT_ERROR_METADEX -32);
    }

    if (nNewValue <= 0 || MAX_INT_8_BYTES < nNewValue) {
        PrintToLog("%s(): rejected: amount for sale out of range or zero: %d\n", __func__, nNewValue);
        return (PKT_ERROR_METADEX -33);
    }

    if (desired_value <= 0 || MAX_INT_8_BYTES < desired_value) {
        PrintToLog("%s(): rejected: desired amount out of range or zero: %d\n", __func__, desired_value);
        return (PKT_ERROR_METADEX -34);
    }

    if (!IsFeatureActivated(FEATURE_TRADEALLPAIRS, block)) {
        // Trading non-Token pairs is not allowed before trading all pairs is activated
        if ((property != TOKEN_PROPERTY_MSC) && (desired_property != TOKEN_PROPERTY_MSC) &&
            (property != TOKEN_PROPERTY_TMSC) && (desired_property != TOKEN_PROPERTY_TMSC)) {
            PrintToLog("%s(): rejected: one side of a trade [%d, %d] must be OMN or TOMN\n", __func__, property, desired_property);
            return (PKT_ERROR_METADEX -35);
        }
    }

    int64_t nBalance = GetTokenBalance(sender, property, BALANCE);
    if (nBalance < (int64_t) nNewValue) {
        PrintToLog("%s(): rejected: sender %s has insufficient balance of property %d [%s < %s]\n",
                __func__,
                sender,
                property,
                FormatMP(property, nBalance),
                FormatMP(property, nNewValue));
        return (PKT_ERROR_METADEX -25);
    }

    // ------------------------------------------

    pDbTradeList->recordNewTrade(txid, sender, property, desired_property, block, tx_idx);
    int rc = MetaDEx_ADD(sender, property, nNewValue, block, desired_property, desired_value, txid, tx_idx);
    return rc;
}

/** Tx 26 */
int CMPTransaction::logicMath_MetaDExCancelPrice()
{
    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_METADEX -22);
    }

    if (property == desired_property) {
        PrintToLog("%s(): rejected: property for sale %d and desired property %d must not be equal\n",
                __func__,
                property,
                desired_property);
        return (PKT_ERROR_METADEX -29);
    }

    if (isTestEcosystemProperty(property) != isTestEcosystemProperty(desired_property)) {
        PrintToLog("%s(): rejected: property for sale %d and desired property %d not in same ecosystem\n",
                __func__,
                property,
                desired_property);
        return (PKT_ERROR_METADEX -30);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property for sale %d does not exist\n", __func__, property);
        return (PKT_ERROR_METADEX -31);
    }

    if (!IsPropertyIdValid(desired_property)) {
        PrintToLog("%s(): rejected: desired property %d does not exist\n", __func__, desired_property);
        return (PKT_ERROR_METADEX -32);
    }

    if (nNewValue <= 0 || MAX_INT_8_BYTES < nNewValue) {
        PrintToLog("%s(): rejected: amount for sale out of range or zero: %d\n", __func__, nNewValue);
        return (PKT_ERROR_METADEX -33);
    }

    if (desired_value <= 0 || MAX_INT_8_BYTES < desired_value) {
        PrintToLog("%s(): rejected: desired amount out of range or zero: %d\n", __func__, desired_value);
        return (PKT_ERROR_METADEX -34);
    }

    // ------------------------------------------

    int rc = MetaDEx_CANCEL_AT_PRICE(txid, block, sender, property, nNewValue, desired_property, desired_value);

    return rc;
}

/** Tx 27 */
int CMPTransaction::logicMath_MetaDExCancelPair()
{
    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_METADEX -22);
    }

    if (property == desired_property) {
        PrintToLog("%s(): rejected: property for sale %d and desired property %d must not be equal\n",
                __func__,
                property,
                desired_property);
        return (PKT_ERROR_METADEX -29);
    }

    if (isTestEcosystemProperty(property) != isTestEcosystemProperty(desired_property)) {
        PrintToLog("%s(): rejected: property for sale %d and desired property %d not in same ecosystem\n",
                __func__,
                property,
                desired_property);
        return (PKT_ERROR_METADEX -30);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property for sale %d does not exist\n", __func__, property);
        return (PKT_ERROR_METADEX -31);
    }

    if (!IsPropertyIdValid(desired_property)) {
        PrintToLog("%s(): rejected: desired property %d does not exist\n", __func__, desired_property);
        return (PKT_ERROR_METADEX -32);
    }

    // ------------------------------------------

    int rc = MetaDEx_CANCEL_ALL_FOR_PAIR(txid, block, sender, property, desired_property);

    return rc;
}

/** Tx 28 */
int CMPTransaction::logicMath_MetaDExCancelEcosystem()
{
    if (!IsTransactionTypeAllowed(block, ecosystem, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_METADEX -22);
    }

    if (TOKEN_PROPERTY_MSC != ecosystem && TOKEN_PROPERTY_TMSC != ecosystem) {
        PrintToLog("%s(): rejected: invalid ecosystem: %d\n", __func__, ecosystem);
        return (PKT_ERROR_METADEX -21);
    }

    int rc = MetaDEx_CANCEL_EVERYTHING(txid, block, sender, ecosystem);

    return rc;
}

/** Tx 50 */
int CMPTransaction::logicMath_CreatePropertyFixed()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_SP -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (TOKEN_PROPERTY_MSC != ecosystem && TOKEN_PROPERTY_TMSC != ecosystem) {
        PrintToLog("%s(): rejected: invalid ecosystem: %d\n", __func__, (uint32_t) ecosystem);
        return (PKT_ERROR_SP -21);
    }

    if (!IsTransactionTypeAllowed(block, ecosystem, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_SP -22);
    }

    if (nValue <= 0 || MAX_INT_8_BYTES < nValue) {
        PrintToLog("%s(): rejected: value out of range or zero: %d\n", __func__, nValue);
        return (PKT_ERROR_SP -23);
    }

    if (TOKEN_PROPERTY_TYPE_INDIVISIBLE != prop_type && TOKEN_PROPERTY_TYPE_DIVISIBLE != prop_type) {
        PrintToLog("%s(): rejected: invalid property type: %d\n", __func__, prop_type);
        return (PKT_ERROR_SP -36);
    }

    if ('\0' == name[0]) {
        PrintToLog("%s(): rejected: property name must not be empty\n", __func__);
        return (PKT_ERROR_SP -37);
    }

    if ('\0' == ticker[0]) {
        PrintToLog("%s(): rejected: property ticker must not be empty\n", __func__);
        return (PKT_ERROR_SP -51);
    }

    if (pDbSpInfo->findSPByTicker(ticker) > 0) {
        PrintToLog("%s(): rejected: token with ticker %s already exists\n", __func__, ticker);
        return (PKT_ERROR_SP -71);
    }

    bool isNFT = prop_type == TOKEN_PROPERTY_TYPE_INDIVISIBLE && nValue == 1;
    bool isToken = IsTokenTickerValid(ticker);
    bool isUsername = IsUsernameValid(ticker);
    bool isSub = IsSubTickerValid(ticker);

    if (!isToken && !isUsername && !isSub)
    {
        PrintToLog("%s(): rejected: token ticker %s is invalid\n", __func__, ticker);
        return (PKT_ERROR_SP -72);
    }

    // Subtoken validation logic
    if (isSub)
    {
        // Split here
        std::vector<std::string> result = SplitSubTicker(ticker);

        if (result.size() != 2)
        {
            PrintToLog("%s(): rejected: subtoken ticker %s invalid\n", __func__, ticker);
            return (PKT_ERROR_SP -72);
        }

        std::string rootTicker = result[0];
        int rootPropertyId = pDbSpInfo->findSPByTicker(rootTicker);

        if (rootPropertyId == 0) {
            PrintToLog("%s(): rejected: token with root ticker %s doesn't exists\n", __func__, rootTicker);
            return (PKT_ERROR_SP -72);
        }

        CMPSPInfo::Entry rootSP;
        pDbSpInfo->getSP(rootPropertyId, rootSP);

        if (rootSP.issuer != sender)
        {
            PrintToLog("%s(): rejected: root token issuer missmatch %s - %s\n", __func__, rootSP.issuer, sender);
            return (PKT_ERROR_SP -72);
        }
    }

    // Username and subtoken must have 1 unit of supply
    if (isUsername || isSub)
    {
        if (!isNFT)
        {
            PrintToLog("%s(): rejected: only 1 indivisible token can be created\n", __func__);
            return (PKT_ERROR_SP -23);
        }
    }

    CAmount nIssuanceCost = governance->GetCost(GOVERNANCE_COST_FIXED);

    if (isSub)
        nIssuanceCost = governance->GetCost(GOVERNANCE_COST_SUB);

    if (isUsername)
        nIssuanceCost = governance->GetCost(GOVERNANCE_COST_USERNAME);

    if (nDonation < nIssuanceCost) {
        PrintToLog("%s(): rejected: token creation fee is missing\n", __func__);
        return (PKT_ERROR_SP -73);
    }

    if (!IsTokenIPFSValid(data))
    {
        PrintToLog("%s(): rejected: token IPFS hash %s is invalid\n", __func__, name);
        return (PKT_ERROR_SP -74);
    }

    // Limit royalties percentage
    if (royaltiesPercentage > 99)
    {
        PrintToLog("%s(): rejected: royalties percentage can't be more than 99%\n", __func__);
        return (PKT_ERROR_SP -78);
    }

    // Royalties verification
    if (!isNFT && (royaltiesReceiver[0] != '\0' || royaltiesPercentage > 0))
    {
        PrintToLog("%s(): rejected: royalties can be set only for token with 1 unit of supply\n", __func__);
        return (PKT_ERROR_SP -79);
    }

    if (royaltiesReceiver[0] == '\0' && royaltiesPercentage > 0)
    {
        PrintToLog("%s(): rejected: royalties receiver can't be empty if royalties percentage greater than zero\n", __func__);
        return (PKT_ERROR_SP -80);
    }

    if (royaltiesReceiver[0] != '\0' && royaltiesPercentage == 0)
    {
        PrintToLog("%s(): rejected: royalties percentage can't be zero if royalties receiver not empty\n", __func__);
        return (PKT_ERROR_SP -81);
    }

    if (royaltiesReceiver[0] != '\0')
    {
        if (IsUsernameValid(royaltiesReceiver))
        {
            // Make sure username exists
            if (pDbSpInfo->findSPByTicker(royaltiesReceiver) == 0) {
                PrintToLog("%s(): rejected: royalties receiver username %s not registered yet\n", __func__, royaltiesReceiver);
                return (PKT_ERROR_SP -82);
            }
        } else {
            CTxDestination royaltiesAddress = DecodeDestination(royaltiesReceiver);
            if (!IsValidDestination(royaltiesAddress)) {
                PrintToLog("%s(): rejected: invalid royalties address %s\n", __func__, royaltiesReceiver);
                return (PKT_ERROR_SP -83);
            }
        }
    }

    // ------------------------------------------

    CMPSPInfo::Entry newSP;
    newSP.issuer = sender;
    newSP.updateIssuer(block, tx_idx, sender);
    newSP.txid = txid;
    newSP.prop_type = prop_type;
    newSP.num_tokens = nValue;
    newSP.category.assign(category);
    newSP.subcategory.assign(subcategory);
    newSP.name.assign(name);
    newSP.ticker.assign(ticker);
    newSP.url.assign(url);
    newSP.data.assign(data);
    newSP.fixed = true;
    newSP.creation_block = blockHash;
    newSP.update_block = newSP.creation_block;

    newSP.royalties_percentage = royaltiesPercentage;
    newSP.royalties_receiver = royaltiesReceiver;

    const uint32_t propertyId = pDbSpInfo->putSP(ecosystem, newSP);
    assert(propertyId > 0);
    assert(update_tally_map(sender, propertyId, nValue, BALANCE));

    NotifyTotalTokensChanged(propertyId, block);

    return 0;
}

/** Tx 51 */
int CMPTransaction::logicMath_CreatePropertyVariable()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_SP -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (TOKEN_PROPERTY_MSC != ecosystem && TOKEN_PROPERTY_TMSC != ecosystem) {
        PrintToLog("%s(): rejected: invalid ecosystem: %d\n", __func__, (uint32_t) ecosystem);
        return (PKT_ERROR_SP -21);
    }

    if (IsFeatureActivated(FEATURE_SPCROWDCROSSOVER, block)) {
    /**
     * Ecosystem crossovers shall not be allowed after the feature was enabled.
     */
    if (isTestEcosystemProperty(ecosystem) != isTestEcosystemProperty(property)) {
        PrintToLog("%s(): rejected: ecosystem %d of tokens to issue and desired property %d not in same ecosystem\n",
                __func__,
                ecosystem,
                property);
        return (PKT_ERROR_SP -50);
    }
    }

    if (!IsTransactionTypeAllowed(block, ecosystem, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_SP -22);
    }

    if (nValue <= 0 || MAX_INT_8_BYTES < nValue) {
        PrintToLog("%s(): rejected: value out of range or zero: %d\n", __func__, nValue);
        return (PKT_ERROR_SP -23);
    }

    if (version < MP_TX_PKT_V2 && !IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_SP -24);
    }

    if (TOKEN_PROPERTY_TYPE_INDIVISIBLE != prop_type && TOKEN_PROPERTY_TYPE_DIVISIBLE != prop_type) {
        PrintToLog("%s(): rejected: invalid property type: %d\n", __func__, prop_type);
        return (PKT_ERROR_SP -36);
    }

    if ('\0' == name[0]) {
        PrintToLog("%s(): rejected: property name must not be empty\n", __func__);
        return (PKT_ERROR_SP -37);
    }

    if ('\0' == ticker[0]) {
        PrintToLog("%s(): rejected: property ticker must not be empty\n", __func__);
        return (PKT_ERROR_SP -51);
    }

    if (!deadline || (int64_t) deadline < blockTime) {
        PrintToLog("%s(): rejected: deadline must not be in the past [%d < %d]\n", __func__, deadline, blockTime);
        return (PKT_ERROR_SP -38);
    }

    if (NULL != getCrowd(sender)) {
        PrintToLog("%s(): rejected: sender %s has an active crowdsale\n", __func__, sender);
        return (PKT_ERROR_SP -39);
    }

    if (pDbSpInfo->findSPByTicker(ticker) > 0) {
        PrintToLog("%s(): rejected: token with ticker %s already exists\n", __func__, ticker);
        return (PKT_ERROR_SP -71);
    }

    if (!IsTokenTickerValid(ticker))
    {
        PrintToLog("%s(): rejected: token ticker %s is invalid\n", __func__, ticker);
        return (PKT_ERROR_SP -72);
    }

    if (nDonation < governance->GetCost(GOVERNANCE_COST_VARIABLE)) {
        PrintToLog("%s(): rejected: token creation fee is missing\n", __func__);
        return (PKT_ERROR_SP -73);
    }

    if (!IsTokenIPFSValid(data))
    {
        PrintToLog("%s(): rejected: token IPFS hash %s is invalid\n", __func__, name);
        return (PKT_ERROR_SP -74);
    }

    // ------------------------------------------

    CMPSPInfo::Entry newSP;
    newSP.issuer = sender;
    newSP.updateIssuer(block, tx_idx, sender);
    newSP.txid = txid;
    newSP.prop_type = prop_type;
    newSP.num_tokens = nValue;
    newSP.category.assign(category);
    newSP.subcategory.assign(subcategory);
    newSP.name.assign(name);
    newSP.ticker.assign(ticker);
    newSP.url.assign(url);
    newSP.data.assign(data);
    newSP.fixed = false;
    newSP.property_desired = property;
    newSP.deadline = deadline;
    newSP.early_bird = early_bird;
    newSP.percentage = percentage;
    newSP.creation_block = blockHash;
    newSP.update_block = newSP.creation_block;

    const uint32_t propertyId = pDbSpInfo->putSP(ecosystem, newSP);
    assert(propertyId > 0);
    my_crowds.insert(std::make_pair(sender, CMPCrowd(propertyId, nValue, property, deadline, early_bird, percentage, 0, 0)));

    PrintToLog("CREATED CROWDSALE id: %d value: %d property: %d\n", propertyId, nValue, property);

    return 0;
}

/** Tx 53 */
int CMPTransaction::logicMath_CloseCrowdsale()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_SP -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_SP -22);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_SP -24);
    }

    CrowdMap::iterator it = my_crowds.find(sender);
    if (it == my_crowds.end()) {
        PrintToLog("%s(): rejected: sender %s has no active crowdsale\n", __func__, sender);
        return (PKT_ERROR_SP -40);
    }

    const CMPCrowd& crowd = it->second;
    if (property != crowd.getPropertyId()) {
        PrintToLog("%s(): rejected: property identifier mismatch [%d != %d]\n", __func__, property, crowd.getPropertyId());
        return (PKT_ERROR_SP -41);
    }

    // ------------------------------------------

    CMPSPInfo::Entry sp;
    assert(pDbSpInfo->getSP(property, sp));

    int64_t missedTokens = GetMissedIssuerBonus(sp, crowd);

    sp.historicalData = crowd.getDatabase();
    sp.update_block = blockHash;
    sp.close_early = true;
    sp.timeclosed = blockTime;
    sp.txid_close = txid;
    sp.missedTokens = missedTokens;

    assert(pDbSpInfo->updateSP(property, sp));
    if (missedTokens > 0) {
        assert(update_tally_map(sp.issuer, property, missedTokens, BALANCE));
    }
    my_crowds.erase(it);

    if (msc_debug_sp) PrintToLog("CLOSED CROWDSALE id: %d=%X\n", property, property);

    return 0;
}

/** Tx 54 */
int CMPTransaction::logicMath_CreatePropertyManaged()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_SP -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (TOKEN_PROPERTY_MSC != ecosystem && TOKEN_PROPERTY_TMSC != ecosystem) {
        PrintToLog("%s(): rejected: invalid ecosystem: %d\n", __func__, (uint32_t) ecosystem);
        return (PKT_ERROR_SP -21);
    }

    if (!IsTransactionTypeAllowed(block, ecosystem, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_SP -22);
    }

    if (TOKEN_PROPERTY_TYPE_INDIVISIBLE != prop_type && TOKEN_PROPERTY_TYPE_DIVISIBLE != prop_type) {
        PrintToLog("%s(): rejected: invalid property type: %d\n", __func__, prop_type);
        return (PKT_ERROR_SP -36);
    }

    if ('\0' == name[0]) {
        PrintToLog("%s(): rejected: property name must not be empty\n", __func__);
        return (PKT_ERROR_SP -37);
    }

    if ('\0' == ticker[0]) {
        PrintToLog("%s(): rejected: property ticker must not be empty\n", __func__);
        return (PKT_ERROR_SP -51);
    }

    if (pDbSpInfo->findSPByTicker(ticker) > 0) {
        PrintToLog("%s(): rejected: token with ticker %s already exists\n", __func__, ticker);
        return (PKT_ERROR_SP -71);
    }

    if (!IsTokenTickerValid(ticker))
    {
        PrintToLog("%s(): rejected: token ticker %s is invalid\n", __func__, ticker);
        return (PKT_ERROR_SP -72);
    }

    if (nDonation < governance->GetCost(GOVERNANCE_COST_MANAGED)) {
        PrintToLog("%s(): rejected: token creation fee is missing\n", __func__);
        return (PKT_ERROR_SP -73);
    }

    if (!IsTokenIPFSValid(data))
    {
        PrintToLog("%s(): rejected: token IPFS hash %s is invalid\n", __func__, name);
        return (PKT_ERROR_SP -74);
    }

    // ------------------------------------------

    CMPSPInfo::Entry newSP;
    newSP.issuer = sender;
    newSP.updateIssuer(block, tx_idx, sender);
    newSP.txid = txid;
    newSP.prop_type = prop_type;
    newSP.category.assign(category);
    newSP.subcategory.assign(subcategory);
    newSP.name.assign(name);
    newSP.ticker.assign(ticker);
    newSP.url.assign(url);
    newSP.data.assign(data);
    newSP.fixed = false;
    newSP.manual = true;
    newSP.creation_block = blockHash;
    newSP.update_block = newSP.creation_block;

    uint32_t propertyId = pDbSpInfo->putSP(ecosystem, newSP);
    assert(propertyId > 0);

    PrintToLog("CREATED MANUAL PROPERTY id: %d admin: %s\n", propertyId, sender);

    return 0;
}

/** Tx 55 */
int CMPTransaction::logicMath_GrantTokens()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_SP -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TOKENS -22);
    }

    if (nValue <= 0 || MAX_INT_8_BYTES < nValue) {
        PrintToLog("%s(): rejected: value out of range or zero: %d\n", __func__, nValue);
        return (PKT_ERROR_TOKENS -23);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_TOKENS -24);
    }

    CMPSPInfo::Entry sp;
    assert(pDbSpInfo->getSP(property, sp));

    if (!sp.manual) {
        PrintToLog("%s(): rejected: property %d is not managed\n", __func__, property);
        return (PKT_ERROR_TOKENS -42);
    }

    if (sender != sp.getIssuer(block)) {
        PrintToLog("%s(): rejected: sender %s is not issuer of property %d [issuer=%s]\n", __func__, sender, property, sp.issuer);
        return (PKT_ERROR_TOKENS -43);
    }

    int64_t nTotalTokens = getTotalTokens(property);
    if (nValue > (MAX_INT_8_BYTES - nTotalTokens)) {
        PrintToLog("%s(): rejected: no more than %s tokens can ever exist [%s + %s > %s]\n",
                __func__,
                FormatMP(property, MAX_INT_8_BYTES),
                FormatMP(property, nTotalTokens),
                FormatMP(property, nValue),
                FormatMP(property, MAX_INT_8_BYTES));
        return (PKT_ERROR_TOKENS -44);
    }

    // ------------------------------------------

    std::vector<int64_t> dataPt;
    dataPt.push_back(nValue);
    dataPt.push_back(0);
    sp.historicalData.insert(std::make_pair(txid, dataPt));
    sp.update_block = blockHash;

    // Persist the number of granted tokens
    assert(pDbSpInfo->updateSP(property, sp));

    // Move the tokens
    assert(update_tally_map(receiver, property, nValue, BALANCE));

    /**
     * As long as the feature to disable the side effects of "granting tokens"
     * is not activated, "granting tokens" can trigger crowdsale participations.
     */
    if (!IsFeatureActivated(FEATURE_GRANTEFFECTS, block)) {
        // Is there an active crowdsale running from this recepient?
        logicHelper_CrowdsaleParticipation();
    }

    NotifyTotalTokensChanged(property, block);

    return 0;
}

/** Tx 56 */
int CMPTransaction::logicMath_RevokeTokens()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_TOKENS -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TOKENS -22);
    }

    if (nValue <= 0 || MAX_INT_8_BYTES < nValue) {
        PrintToLog("%s(): rejected: value out of range or zero: %d\n", __func__, nValue);
        return (PKT_ERROR_TOKENS -23);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_TOKENS -24);
    }

    CMPSPInfo::Entry sp;
    assert(pDbSpInfo->getSP(property, sp));

    if (!sp.manual) {
        PrintToLog("%s(): rejected: property %d is not managed\n", __func__, property);
        return (PKT_ERROR_TOKENS -42);
    }

    int64_t nBalance = GetTokenBalance(sender, property, BALANCE);
    if (nBalance < (int64_t) nValue) {
        PrintToLog("%s(): rejected: sender %s has insufficient balance of property %d [%s < %s]\n",
                __func__,
                sender,
                property,
                FormatMP(property, nBalance),
                FormatMP(property, nValue));
        return (PKT_ERROR_TOKENS -25);
    }

    // ------------------------------------------

    std::vector<int64_t> dataPt;
    dataPt.push_back(0);
    dataPt.push_back(nValue);
    sp.historicalData.insert(std::make_pair(txid, dataPt));
    sp.update_block = blockHash;

    assert(update_tally_map(sender, property, -nValue, BALANCE));
    assert(pDbSpInfo->updateSP(property, sp));

    NotifyTotalTokensChanged(property, block);

    return 0;
}

/** Tx 70 */
int CMPTransaction::logicMath_ChangeIssuer()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_TOKENS -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TOKENS -22);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_TOKENS -24);
    }

    CMPSPInfo::Entry sp;
    assert(pDbSpInfo->getSP(property, sp));

    if (sender != sp.getIssuer(block)) {
        PrintToLog("%s(): rejected: sender %s is not issuer of property %d [issuer=%s]\n", __func__, sender, property, sp.issuer);
        return (PKT_ERROR_TOKENS -43);
    }

    if (NULL != getCrowd(sender)) {
        PrintToLog("%s(): rejected: sender %s has an active crowdsale\n", __func__, sender);
        return (PKT_ERROR_TOKENS -39);
    }

    if (receiver.empty()) {
        PrintToLog("%s(): rejected: receiver is empty\n", __func__);
        return (PKT_ERROR_TOKENS -45);
    }

    if (NULL != getCrowd(receiver)) {
        PrintToLog("%s(): rejected: receiver %s has an active crowdsale\n", __func__, receiver);
        return (PKT_ERROR_TOKENS -46);
    }

    // ------------------------------------------

    sp.updateIssuer(block, tx_idx, receiver);

    sp.issuer = receiver;
    sp.update_block = blockHash;

    assert(pDbSpInfo->updateSP(property, sp));

    return 0;
}

/** Tx 71 */
int CMPTransaction::logicMath_EnableFreezing()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_TOKENS -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TOKENS -22);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_TOKENS -24);
    }

    CMPSPInfo::Entry sp;
    assert(pDbSpInfo->getSP(property, sp));

    if (!sp.manual) {
        PrintToLog("%s(): rejected: property %d is not managed\n", __func__, property);
        return (PKT_ERROR_TOKENS -42);
    }

    if (sender != sp.getIssuer(block)) {
        PrintToLog("%s(): rejected: sender %s is not issuer of property %d [issuer=%s]\n", __func__, sender, property, sp.issuer);
        return (PKT_ERROR_TOKENS -43);
    }

    if (isFreezingEnabled(property, block)) {
        PrintToLog("%s(): rejected: freezing is already enabled for property %d\n", __func__, property);
        return (PKT_ERROR_TOKENS -49);
    }

    int liveBlock = 0;
    if (!IsFeatureActivated(FEATURE_FREEZENOTICE, block)) {
        liveBlock = block;
    } else {
        const CConsensusParams& params = ConsensusParams();
        liveBlock = params.TOKEN_FREEZE_WAIT_PERIOD + block;
    }

    enableFreezing(property, liveBlock);

    return 0;
}

/** Tx 72 */
int CMPTransaction::logicMath_DisableFreezing()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_TOKENS -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TOKENS -22);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_TOKENS -24);
    }

    CMPSPInfo::Entry sp;
    assert(pDbSpInfo->getSP(property, sp));

    if (!sp.manual) {
        PrintToLog("%s(): rejected: property %d is not managed\n", __func__, property);
        return (PKT_ERROR_TOKENS -42);
    }

    if (sender != sp.getIssuer(block)) {
        PrintToLog("%s(): rejected: sender %s is not issuer of property %d [issuer=%s]\n", __func__, sender, property, sp.issuer);
        return (PKT_ERROR_TOKENS -43);
    }

    if (!isFreezingEnabled(property, block)) {
        PrintToLog("%s(): rejected: freezing is not enabled for property %d\n", __func__, property);
        return (PKT_ERROR_TOKENS -47);
    }

    disableFreezing(property);

    return 0;
}

/** Tx 185 */
int CMPTransaction::logicMath_FreezeTokens()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_TOKENS -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TOKENS -22);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_TOKENS -24);
    }

    CMPSPInfo::Entry sp;
    assert(pDbSpInfo->getSP(property, sp));

    if (!sp.manual) {
        PrintToLog("%s(): rejected: property %d is not managed\n", __func__, property);
        return (PKT_ERROR_TOKENS -42);
    }

    if (sender != sp.getIssuer(block)) {
        PrintToLog("%s(): rejected: sender %s is not issuer of property %d [issuer=%s]\n", __func__, sender, property, sp.issuer);
        return (PKT_ERROR_TOKENS -43);
    }

    if (!isFreezingEnabled(property, block)) {
        PrintToLog("%s(): rejected: freezing is not enabled for property %d\n", __func__, property);
        return (PKT_ERROR_TOKENS -47);
    }

    if (isAddressFrozen(receiver, property)) {
        PrintToLog("%s(): rejected: address %s is already frozen for property %d\n", __func__, receiver, property);
        return (PKT_ERROR_TOKENS -50);
    }

    freezeAddress(receiver, property);

    return 0;
}

/** Tx 186 */
int CMPTransaction::logicMath_UnfreezeTokens()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_TOKENS -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TOKENS -22);
    }

    if (!IsPropertyIdValid(property)) {
        PrintToLog("%s(): rejected: property %d does not exist\n", __func__, property);
        return (PKT_ERROR_TOKENS -24);
    }

    CMPSPInfo::Entry sp;
    assert(pDbSpInfo->getSP(property, sp));

    if (!sp.manual) {
        PrintToLog("%s(): rejected: property %d is not managed\n", __func__, property);
        return (PKT_ERROR_TOKENS -42);
    }

    if (sender != sp.getIssuer(block)) {
        PrintToLog("%s(): rejected: sender %s is not issuer of property %d [issuer=%s]\n", __func__, sender, property, sp.issuer);
        return (PKT_ERROR_TOKENS -43);
    }

    if (!isFreezingEnabled(property, block)) {
        PrintToLog("%s(): rejected: freezing is not enabled for property %d\n", __func__, property);
        return (PKT_ERROR_TOKENS -47);
    }

    if (!isAddressFrozen(receiver, property)) {
        PrintToLog("%s(): rejected: address %s is not frozen for property %d\n", __func__, receiver, property);
        return (PKT_ERROR_TOKENS -48);
    }

    unfreezeAddress(receiver, property);

    return 0;
}

/** Tx 65533 */
int CMPTransaction::logicMath_Deactivation()
{
    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR -22);
    }

    // is sender authorized
    bool authorized = CheckDeactivationAuthorization(sender);

    PrintToLog("\t          sender: %s\n", sender);
    PrintToLog("\t      authorized: %s\n", authorized);

    if (!authorized) {
        PrintToLog("%s(): rejected: sender %s is not authorized to deactivate features\n", __func__, sender);
        return (PKT_ERROR -51);
    }

    // authorized, request feature deactivation
    bool DeactivationSuccess = DeactivateFeature(feature_id, block);

    if (!DeactivationSuccess) {
        PrintToLog("%s(): DeactivateFeature failed\n", __func__);
        return (PKT_ERROR -54);
    }

    // successful deactivation - did we deactivate the MetaDEx?  If so close out all trades
    if (feature_id == FEATURE_METADEX) {
        MetaDEx_SHUTDOWN();
    }
    if (feature_id == FEATURE_TRADEALLPAIRS) {
        MetaDEx_SHUTDOWN_ALLPAIR();
    }

    return 0;
}

/** Tx 80 */
int CMPTransaction::logicMath_RPDPayment()
{
    uint256 blockHash;
    {
        LOCK(cs_main);

        CBlockIndex* pindex = chainActive[block];
        if (pindex == NULL) {
            PrintToLog("%s(): ERROR: block %d not in the active chain\n", __func__, block);
            return (PKT_ERROR_TOKENS -20);
        }
        blockHash = pindex->GetBlockHash();
    }

    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR_TOKENS -22);
    }

    CTransaction linked_tx;
    uint256 linked_blockHash = 0;
    int linked_blockHeight = 0;
    int linked_blockTime = 0;

    if (!GetTransaction(linked_txid, linked_tx, linked_blockHash, true)) {
        PrintToLog("%s(): rejected: linked transaction %s does not exist\n",
                __func__,
                linked_txid.GetHex());
        return MP_TX_NOT_FOUND;
    }

    if (linked_blockHash == 0) { // linked transaction is unconfirmed (and thus not yet added to state), cannot process payment
        PrintToLog("%s(): rejected: linked transaction %s does not exist (unconfirmed)\n",
                __func__,
                linked_txid.GetHex());
        return MP_TX_NOT_FOUND;
    }

    CBlockIndex* pBlockIndex = GetBlockIndex(linked_blockHash);
    if (NULL != pBlockIndex) {
        linked_blockHeight = pBlockIndex->nHeight;
        linked_blockTime = pBlockIndex->nTime;
    }

    CMPTransaction mp_obj;
    int parseRC = ParseTransaction(linked_tx, linked_blockHeight, 0, mp_obj, linked_blockTime);
    if (parseRC < 0) {
        PrintToLog("%s(): rejected: linked transaction %s is not an token layer transaction\n",
                __func__,
                linked_txid.GetHex());
        return MP_TX_IS_NOT_TOKEN_PROTOCOL;
    }

    if (!mp_obj.interpret_Transaction()) {
        PrintToLog("%s(): rejected: linked transaction %s is not an token layer transaction\n",
                __func__,
                linked_txid.GetHex());
        return MP_TX_IS_NOT_TOKEN_PROTOCOL;
    }

    bool linked_valid = false;
    {
        // LOCK(cs_tally);
        linked_valid = pDbTransactionList->getValidMPTX(linked_txid);
    }
    if (!linked_valid) {
        PrintToLog("%s(): rejected: linked transaction %s is an invalid transaction\n",
                __func__,
                linked_txid.GetHex());
        return MP_TX_IS_NOT_TOKEN_PROTOCOL -101;
    }

    uint16_t linked_type = mp_obj.getType();
    uint16_t linked_version = mp_obj.getVersion();
    if (!IsRPDPaymentAllowed(linked_type, linked_version)) {
        PrintToLog("%s(): rejected: linked transaction %s doesn't support RPD payments\n",
                __func__,
                linked_txid.GetHex());
        return (PKT_ERROR_TOKENS -61);
    }

    std::string linked_sender = mp_obj.getSender();
    nValue = GetRPDPaymentAmount(txid, linked_sender);
    PrintToLog("\tlinked tx sender: %s\n", linked_sender);
    PrintToLog("\t  psyment amount: %s\n", FormatDivisibleMP(nValue));

    if (nValue == 0) {
        PrintToLog("%s(): rejected: no payment to sender of linked transaction\n",
                __func__,
                linked_sender,
                linked_txid.GetHex());
        return (PKT_ERROR_TOKENS -62);
    }

    // empty receiver & receiver == linked_sender checks are skipped, since we don't care if the payment was last vout

    if (linked_type == TOKEN_TYPE_CREATE_PROPERTY_VARIABLE) {
        CMPCrowd* pcrowdsale = getCrowd(receiver);
        if (pcrowdsale == NULL) {
            PrintToLog("%s(): rejected: receiver %s does not have an active crowdsale\n", __func__, receiver);
            return (PKT_ERROR_TOKENS -47);
        }

        // confirm the crowdsale that the receiver has open now is the same as the transaction referenced in the payment
        // CMPCrowd class doesn't contain txid, work around by comparing propid for current crowdsale & propid for linked crowdsale
        uint32_t crowdPropertyId = pcrowdsale->getPropertyId();
        uint32_t linkPropertyId = pDbSpInfo->findSPByTX(linked_txid); // TODO: Is this safe to lookup the crowdsale this way??
        if (linkPropertyId != crowdPropertyId) {
            PrintToLog("%s(): rejected: active crowdsale for receiver %s did not originate from linked txid\n", __func__, receiver);
            return (PKT_ERROR_TOKENS -48);
        }

        logicHelper_CrowdsaleParticipation();
    }

    return 0;
}

/** Tx 65534 */
int CMPTransaction::logicMath_Activation()
{
    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR -22);
    }

    // is sender authorized - temporarily use alert auths but ## TO BE MOVED TO FOUNDATION P2SH KEY ##
    bool authorized = CheckActivationAuthorization(sender);

    PrintToLog("\t          sender: %s\n", sender);
    PrintToLog("\t      authorized: %s\n", authorized);

    if (!authorized) {
        PrintToLog("%s(): rejected: sender %s is not authorized for feature activations\n", __func__, sender);
        return (PKT_ERROR -51);
    }

    // authorized, request feature activation
    bool activationSuccess = ActivateFeature(feature_id, activation_block, min_client_version, block);

    if (!activationSuccess) {
        PrintToLog("%s(): ActivateFeature failed to activate this feature\n", __func__);
        return (PKT_ERROR -54);
    }

    return 0;
}

/** Tx 65535 */
int CMPTransaction::logicMath_Alert()
{
    if (!IsTransactionTypeAllowed(block, property, type, version)) {
        PrintToLog("%s(): rejected: type %d or version %d not permitted for property %d at block %d\n",
                __func__,
                type,
                version,
                property,
                block);
        return (PKT_ERROR -22);
    }

    // is sender authorized?
    bool authorized = CheckAlertAuthorization(sender);

    PrintToLog("\t          sender: %s\n", sender);
    PrintToLog("\t      authorized: %s\n", authorized);

    if (!authorized) {
        PrintToLog("%s(): rejected: sender %s is not authorized for alerts\n", __func__, sender);
        return (PKT_ERROR -51);
    }

    if (alert_type == ALERT_CLIENT_VERSION_EXPIRY && TOKENCORE_VERSION < alert_expiry) {
        // regular alert keys CANNOT be used to force a client upgrade on mainnet - at least 3 signatures from board/devs are required
        if (sender == "34kwkVRSvFVEoUwcQSgpQ4ZUasuZ54DJLD" || isNonMainNet()) {
            std::string msgText = "Client upgrade is required!  Shutting down due to unsupported consensus state!";
            PrintToLog(msgText);
            PrintToConsole(msgText);
            if (!GetBoolArg("-overrideforcedshutdown", false)) {
                boost::filesystem::path persistPath = GetDataDir() / "tokens" / "persist";
                if (boost::filesystem::exists(persistPath)) boost::filesystem::remove_all(persistPath); // prevent the node being restarted without a reparse after forced shutdown
                AbortNode(msgText, msgText);
            }
        }
    }

    if (alert_type == 65535) { // set alert type to FFFF to clear previously sent alerts
        DeleteAlerts(sender);
    } else {
        AddAlert(sender, alert_type, alert_expiry, alert_text);
    }

    // we have a new alert, fire a notify event if needed
    AlertNotify(alert_text, true);

    return 0;
}
