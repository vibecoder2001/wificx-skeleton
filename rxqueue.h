#pragma once

typedef struct _MTK_RXQUEUE {
	MTK_ADAPTER* Adapter;

	NET_RING_COLLECTION const* Rings;

	// Non-functional placeholder buffer (see MTK_TXQUEUE comment).
	PVOID FakeRxRing;
	SIZE_T FakeRxRingSize;

	NET_EXTENSION ChecksumExtension;
	NET_EXTENSION VirtualAddressExtension;
	NET_EXTENSION HashValueExtension;

	ULONG QueueId;
} MTK_RXQUEUE, * PMTK_RXQUEUE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(MTK_RXQUEUE, MtkGetRxQueueContext);

NTSTATUS MtkRxQueueInitialize(_In_ NETPACKETQUEUE rxQueue, _In_ MTK_ADAPTER* adapter);

_Requires_lock_held_(adapter->Lock)
void MtkAdapterUpdateRcr(_In_ MTK_ADAPTER* adapter);

EVT_WDF_OBJECT_CONTEXT_DESTROY EvtRxQueueDestroy;

EVT_PACKET_QUEUE_SET_NOTIFICATION_ENABLED EvtRxQueueSetNotificationEnabled;
EVT_PACKET_QUEUE_ADVANCE EvtRxQueueAdvance;
EVT_PACKET_QUEUE_CANCEL EvtRxQueueCancel;
EVT_PACKET_QUEUE_START EvtRxQueueStart;
EVT_PACKET_QUEUE_STOP EvtRxQueueStop;