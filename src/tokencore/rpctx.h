#ifndef TOKENCORE_RPCTX
#define TOKENCORE_RPCTX

#include <univalue.h>

UniValue token_sendrawtx(const UniValue& params, bool fHelp);
UniValue sendtoken(const UniValue& params, bool fHelp);
UniValue sendalltokens(const UniValue& params, bool fHelp);
UniValue token_senddexsell(const UniValue& params, bool fHelp);
UniValue token_senddexaccept(const UniValue& params, bool fHelp);
UniValue token_sendissuancecrowdsale(const UniValue& params, bool fHelp);
UniValue sendtokenissuancefixed(const UniValue& params, bool fHelp);
UniValue sendtokenissuancemanaged(const UniValue& params, bool fHelp);
UniValue token_sendsto(const UniValue& params, bool fHelp);
UniValue token_sendgrant(const UniValue& params, bool fHelp);
UniValue token_sendrevoke(const UniValue& params, bool fHelp);
UniValue token_sendclosecrowdsale(const UniValue& params, bool fHelp);
UniValue trade_MP(const UniValue& params, bool fHelp);
UniValue sendtokentrade(const UniValue& params, bool fHelp);
UniValue sendtokencanceltradesbyprice(const UniValue& params, bool fHelp);
UniValue sendtokencanceltradesbypair(const UniValue& params, bool fHelp);
UniValue sendtokencancelalltrades(const UniValue& params, bool fHelp);
UniValue sendtokenchangeissuer(const UniValue& params, bool fHelp);
UniValue token_sendactivation(const UniValue& params, bool fHelp);
UniValue token_sendalert(const UniValue& params, bool fHelp);

void RegisterTokenTransactionCreationRPCCommands();

#endif // TOKENCORE_RPCTX
