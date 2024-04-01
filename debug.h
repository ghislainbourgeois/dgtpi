#ifndef DEBUG_H
#define DEBUG_H

// variables for debug stats
#ifdef debug
typedef struct {
	int displaySF;
	int displayAF;
	int endDisplaySF;
	int endDisplayAF;
	int changeStateSF;
	int changeStateAF;
	int setCCSF;
	int setCCAF;
	int setNRunSF;
	int setNRunAF;

	int rxTimeout;
	int rxWrongAdr;
	int rxBufferFull;
	int rxSizeMismatch;
	int rxCRCFault;
	int rxMaxBuf;

	int sendTotal;

} debug_t;
#endif

#endif
