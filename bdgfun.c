/*
*      This file is part of the tknet project. 
*    which be used under the terms of the GNU General Public 
*    License version 3.0 as published by the Free Software
*    Foundation and appearing in the file LICENSE.GPL included 
*    in the packaging of this file.  Please review the following 
*    information to ensure the GNU General Public License 
*    version 3.0 requirements will be met: 
*    http://www.gnu.org/copyleft/gpl.html
*
*    Copyright  (C)   2012   Zhong Wei <clock126@126.com>  .
*/ 

#include "tknet.h"

DEF_STRUCT_CONSTRUCTOR( Peer ,
		out_cons->addr.IPv4 = 0;
		out_cons->addr.port = 0;
		out_cons->NATType = NAT_T_UNKNOWN;
		strcpy(out_cons->NameID,"NO Name");
		)

DEF_STRUCT_CONSTRUCTOR( BridgeProc ,
		out_cons->pPeerDataRoot = NULL;
		out_cons->pSeedPeerCache = NULL;
		ProcessCons(&out_cons->proc);
		out_cons->pSock = NULL;
		out_cons->pProcList = NULL;
		out_cons->Else = NULL;

		PeerCons(&out_cons->s);
		PeerCons(&out_cons->a);
		PeerCons(&out_cons->b);
		PeerCons(&out_cons->sx);
		)

static struct BridgeProc sta_BdgSubServerProc;

CONNECT_CALLBK g_ConnectionNotify = NULL;

static void
BdgClientProcNotify(struct Process *pa_)
{
	struct BridgeProc *pProc = GET_STRUCT_ADDR(pa_ , struct BridgeProc , proc);
	DEF_AND_CAST(pBCPPa,struct BridgeClientProcPa,pProc->Else);
	
	PROMPT(Usual,"Client proc end:\n");
	ProcessTraceSteps(pa_);
	PROMPT(Usual,"\n");

	//NEVER free the client proc in the main loop
	//instead, we set the ifConnected flag and free client
	//proc by FreeBdgClientProc() after loop.
	pBCPPa->ifConnected = 0;
}

void
BdgSubServerProcNotify(struct Process *pa_)
{
	struct BridgeProc *pBdgProc = GET_STRUCT_ADDR(pa_ , struct BridgeProc , proc);
	struct PeerData *pPD = GET_STRUCT_ADDR(pBdgProc,struct PeerData,BdgProc);

	PROMPT(Usual,"Sub Server proc end:\n");
	ProcessTraceSteps(pa_);
	PROMPT(Usual,"\n");
	PeerDataDele(pPD,pBdgProc->pSeedPeerCache);
}

struct PeerData*
NewPeerDataWithBdgProc(struct NetAddr pa_addr,uchar pa_NATType,char *pa_pName,struct BridgeProc *pa_pBdgProc)
{
	struct PeerData *pPD = tkmalloc(struct PeerData);
	PeerDataCons(pPD);

	pPD->addr = pa_addr;
	pPD->NATType = pa_NATType;
	strcpy(pPD->NameID,pa_pName);

	pPD->BdgProc.pPeerDataRoot = pa_pBdgProc->pPeerDataRoot;
	pPD->BdgProc.pSeedPeerCache = pa_pBdgProc->pSeedPeerCache;
	pPD->BdgProc.pSock = pa_pBdgProc->pSock;
	pPD->BdgProc.pProcList = pa_pBdgProc->pProcList;

	ProcessConsAndSetSteps(&(pPD->BdgProc.proc), &sta_BdgSubServerProc.proc );
	pPD->BdgProc.proc.NotifyCallbk = &BdgSubServerProcNotify;
	
	return pPD;
}

void
SetPeerByPeerData(struct Peer *pa_pPeer,struct PeerData *pa_pPD)
{
	strcpy(pa_pPeer->NameID,pa_pPD->NameID);
	pa_pPeer->addr = pa_pPD->addr;
	pa_pPeer->NATType = pa_pPD->NATType;
}

void 
FreeBridgeServer(struct BridgeProc *pa_pBdgProc)
{
	ProcessFree(&pa_pBdgProc->proc);
}

void 
FreeSubBridgeServerTemplate()
{
	ProcessFree(&sta_BdgSubServerProc.proc);
}

struct BridgeMsg*
BdgMsgRead(struct Process *in_proc , uchar pa_option , uchar pa_msg , struct NetAddr *pa_pAddr)
{
	struct BridgeProc *pBdgProc = GET_STRUCT_ADDR(in_proc,struct BridgeProc,proc);
	struct NetAddr FromAddr = GetAddrFromSockAddr(&pBdgProc->pSock->AddrRecvfrom);
	DEF_AND_CAST(pMsg,struct TkNetMsg,pBdgProc->pSock->RecvBuff);
	DEF_AND_CAST(pBdgMsg,struct BridgeMsg,&(pMsg->msg.BdgMsg));

	if( pMsg->flag != TK_NET_BDG_MSG_FLAG ||
		pBdgProc->pSock->RecvLen <= 0 )
	{
		return NULL;
	}
	else if( pa_option == BDG_READ_OPT_ANY )
	{
		return pBdgMsg;
	}
	else if( pa_option == BDG_READ_OPT_ADDR_FILTER && 
			ifNetAddrEqual(&FromAddr,pa_pAddr) )
	{
		return pBdgMsg;
	}
	else if( pa_option == BDG_READ_OPT_MSG_FILTER && 
			pBdgMsg->info == pa_msg )
	{
		return pBdgMsg;
	}
	else
	{
		return NULL;
	}
}

void
BdgMsgWrite(struct Process *in_proc ,struct BridgeMsg *in_msg , struct NetAddr *pa_pAddr)
{
	struct BridgeProc *pBdgProc = GET_STRUCT_ADDR(in_proc,struct BridgeProc,proc);
	struct TkNetMsg   SendingMsg;
	
	SendingMsg.flag = TK_NET_BDG_MSG_FLAG;
	SendingMsg.msg.BdgMsg = *in_msg;

	SockLocateTa(pBdgProc->pSock,htonl(pa_pAddr->IPv4),pa_pAddr->port);
	SockWrite(pBdgProc->pSock,BYS(SendingMsg));
}

void 
ConsAndStartBridgeServer(struct BridgeProc *pa_pBdgProc , struct PeerData *pa_pPeerDataRoot , struct ProcessingList *pa_pProcList , struct Sock *pa_pMainSock , struct Iterator *pa_pSeedPeerCache)
{
	BdgSubServerProcInit();
	BridgeProcCons(pa_pBdgProc);
	pa_pBdgProc->pPeerDataRoot = pa_pPeerDataRoot;	
	pa_pBdgProc->pSeedPeerCache = pa_pSeedPeerCache;	
	pa_pBdgProc->pSock = pa_pMainSock;
	pa_pBdgProc->pProcList = pa_pProcList;	

	PROCESS_ADD_STEP( &pa_pBdgProc->proc , BridgeMain , g_WaitLevel[4] );
	ProcessStart( &pa_pBdgProc->proc , pa_pProcList );
}

void 
BridgeClientTryBdgServerProc(struct BridgeProc *pa_pBdgProc,struct BridgeHelloStepPa *pa_pProcPa , struct Sock *pa_pMainSock)
{
	BridgeProcCons(pa_pBdgProc);
	pa_pBdgProc->pSock = pa_pMainSock;
	pa_pBdgProc->Else = pa_pProcPa;	
	PROCESS_ADD_STEP( &pa_pBdgProc->proc , BdgClientTryBdgServer , g_WaitLevel[2] );
}

void 
FreeBdgClientProc(struct BridgeProc *pa_pBdgProc)
{
	ProcessFree(&pa_pBdgProc->proc);
	if(pa_pBdgProc->Else)
		tkfree(pa_pBdgProc->Else);
}

void 
BdgSubServerProcInit()
{
	ProcessCons(&sta_BdgSubServerProc.proc);
	PROCESS_ADD_STEP( &sta_BdgSubServerProc.proc , BdgBeginSubServer , g_WaitLevel[2]);
	PROCESS_ADD_STEP( &sta_BdgSubServerProc.proc , BdgConnectRequireServer ,g_WaitLevel[3]);
	PROCESS_ADD_STEP( &sta_BdgSubServerProc.proc , BdgConnectRequireReply , g_WaitLevel[2]);
	PROCESS_ADD_STEP( &sta_BdgSubServerProc.proc , BdgConnectDecision , g_WaitLevel[2] );
	PROCESS_ADD_STEP( &sta_BdgSubServerProc.proc , BdgPunchingServer , g_WaitLevel[2] );
	PROCESS_ADD_STEP( &sta_BdgSubServerProc.proc , BdgConnectAddrServer , g_WaitLevel[2] );
	PROCESS_ADD_STEP( &sta_BdgSubServerProc.proc , BdgErrReturnServer , g_WaitLevel[2] );
}


struct BridgeClientProcPa*
BridgeMakeClientProc(struct BridgeProc *pa_pBdgProc, struct Sock *pa_pMainSock ,
		struct ProcessingList *pa_pProcList,struct NetAddr *pa_pAddr, 
		char *pa_pMyNameID ,uchar pa_MyNatType , 
		const char **pa_ppTargetNameID , BOOL pa_ifSkipRegister)
//TaName can be NULL
{
	struct BridgeClientProcPa *pBCPPa = tkmalloc(struct BridgeClientProcPa);

	BridgeProcCons(pa_pBdgProc);

	pa_pBdgProc->pSock = pa_pMainSock;
	pa_pBdgProc->pProcList = pa_pProcList;
	pa_pBdgProc->s.addr = *pa_pAddr;
	pa_pBdgProc->a.NATType = pa_MyNatType;
	pa_pBdgProc->Else = pBCPPa;//Give a extra struct to client process
	pa_pBdgProc->proc.NotifyCallbk = &BdgClientProcNotify;

	pBCPPa->pMyNameID = pa_pMyNameID;
	pBCPPa->ppTargetNameID = pa_ppTargetNameID;
	pBCPPa->DirectConnectAddr.IPv4 = 0;
	pBCPPa->DirectConnectAddr.port = 0;
	pBCPPa->ifSkipRegister = pa_ifSkipRegister;
	pBCPPa->ifFastSendWait = 0;
	pBCPPa->ifConnected = 0;

	if(!pa_ifSkipRegister)
	{
		PROCESS_ADD_STEP( &pa_pBdgProc->proc , BdgClientRegister , g_WaitLevel[2] );
	}
	PROCESS_ADD_STEP( &pa_pBdgProc->proc , BdgClientWait , g_WaitLevel[3] );
	PROCESS_ADD_STEP( &pa_pBdgProc->proc , BdgClientConnectRequire , g_WaitLevel[2] );
	PROCESS_ADD_STEP( &pa_pBdgProc->proc , BdgClientDoConnectAddr , g_WaitLevel[2] );
	PROCESS_ADD_STEP( &pa_pBdgProc->proc , BdgClientMultiSendNotify , g_WaitLevel[0] );

	return pBCPPa;
}
