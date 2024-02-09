#ifndef TOKENCORE_RPCPAYLOAD_H
#define TOKENCORE_RPCPAYLOAD_H

#include <univalue.h>

UniValue createtokenpayloadsimplesend(const UniValue& params, bool fHelp);
UniValue createtokenpayloadsendall(const UniValue& params, bool fHelp);
UniValue createtokenpayloaddexsell(const UniValue& params, bool fHelp);
UniValue createtokenpayloaddexaccept(const UniValue& params, bool fHelp);
UniValue createtokenpayloadsto(const UniValue& params, bool fHelp);
UniValue createtokenpayloadissuancefixed(const UniValue& params, bool fHelp);
UniValue createtokenpayloadissuancecrowdsale(const UniValue& params, bool fHelp);
UniValue createtokenpayloadissuancemanaged(const UniValue& params, bool fHelp);
UniValue createtokenpayloadclosecrowdsale(const UniValue& params, bool fHelp);
UniValue createtokenpayloadgrant(const UniValue& params, bool fHelp);
UniValue createtokenpayloadrevoke(const UniValue& params, bool fHelp);
UniValue createtokenpayloadchangeissuer(const UniValue& params, bool fHelp);
UniValue createtokenpayloadtrade(const UniValue& params, bool fHelp);
UniValue token_createpayload_canceltradesbyprice(const UniValue& params, bool fHelp);
UniValue token_createpayload_canceltradesbypair(const UniValue& params, bool fHelp);
UniValue token_createpayload_cancelalltrades(const UniValue& params, bool fHelp);
UniValue createtokenpayloadfreeze(const UniValue& params, bool fHelp);
UniValue createtokenpayloadunfreeze(const UniValue& params, bool fHelp);
UniValue createtokenpayloadenablefreezing(const UniValue& params, bool fHelp);
UniValue createtokenpayloaddisablefreezing(const UniValue& params, bool fHelp);
UniValue createtokenpayloadcancelalltrades(const UniValue& params, bool fHelp);

void RegisterTokenPayloadCreationRPCCommands();

#endif // TOKENCORE_RPCPAYLOAD_H
