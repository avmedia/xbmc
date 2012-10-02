/*
 *  File: FGManager2.h
 *	Copyright (C) 2012 Eduard Kytmanov
 *
 */

#include "fgmanager.h"
#include "StreamsManager.h"



class CFGManager2 : public CFGManager

{
public:
	HRESULT RenderFileXbmc(const CFileItem& pFileItem);
};
