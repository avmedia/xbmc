/*
 *  File: FGManager2.h
 *	Copyright (C) 2012 Eduard Kytmanov
 *
 */

#ifndef HAS_DS_PLAYER
#error DSPlayer's header file included without HAS_DS_PLAYER defined
#endif

#include "fgmanager.h"
#include "StreamsManager.h"



class CFGManager2 : public CFGManager

{
public:
	HRESULT RenderFileXbmc(const CFileItem& pFileItem);
};
