/*
 *  File: FGManager2.cpp
 *	Copyright (C) 2012 Eduard Kytmanov
 *
 */

#include "FGManager2.h"
#include "DSPlayer.h"
#include "FGLoader.h"
#include "DVDFileInfo.h"
#include "utils/XMLUtils.h"
#include "settings/GUISettings.h"
#include "filtercorefactory/filtercorefactory.h"
#include "Filters/RendererSettings.h"
#include "video/VideoInfoTag.h"
#include "PixelShaderList.h"


HRESULT CFGManager2::RenderFileXbmc(const CFileItem& pFileItem)
{

	CFileItem FileItem = pFileItem;
	bool bIsAutoRender = g_guiSettings.GetBool("dsplayer.autofiltersettings");

	if(FileItem.IsDVDFile() || !bIsAutoRender)
		return __super::RenderFileXbmc(FileItem);

	CSingleLock lock(*this);

	HRESULT hr = S_OK;

	//Clearing config need to be done before the file is loaded. Some config are set when the pin are getting connected
	g_dsconfig.ClearConfig();

	// We *need* those informations for filter loading. If the user wants it, be sure it's loaded
	// before using it.
	bool hasStreamDetails = false;
	if (g_guiSettings.GetBool("myvideos.extractflags") && FileItem.HasVideoInfoTag() && !FileItem.GetVideoInfoTag()->HasStreamDetails())
	{
		CLog::Log(LOGDEBUG,"%s - trying to extract filestream details from video file %s", __FUNCTION__, FileItem.GetPath().c_str());
		hasStreamDetails = CDVDFileInfo::GetFileStreamDetails(&FileItem);
	} else{
		hasStreamDetails = FileItem.HasVideoInfoTag() && FileItem.GetVideoInfoTag()->HasStreamDetails();
	}

	CURL url(FileItem.GetPath());

	CStdString pWinFilePath = url.Get();
	if ( (pWinFilePath.Left(6)).Equals("smb://", false) )
		pWinFilePath.Replace("smb://", "\\\\");

	if (! FileItem.IsInternetStream())
		pWinFilePath.Replace("/", "\\");

	Com::SmartPtr<IBaseFilter> pBF;
	CStdStringW strFileW;
	g_charsetConverter.utf8ToW(pWinFilePath, strFileW);
	if(FAILED(hr = g_dsGraph->pFilterGraph->AddSourceFilter(strFileW.c_str(), NULL, &pBF)))
	{
		return hr;
	}

	CStdString filter = "";

	START_PERFORMANCE_COUNTER
		CFilterCoreFactory::GetAudioRendererFilter(FileItem, filter);
	m_CfgLoader->InsertAudioRenderer(filter); // First added, last connected
	END_PERFORMANCE_COUNTER("Loading audio renderer");

	START_PERFORMANCE_COUNTER
		m_CfgLoader->InsertVideoRenderer();
	END_PERFORMANCE_COUNTER("Loading video renderer");

	START_PERFORMANCE_COUNTER
		if(FAILED(hr = ConnectFilter(pBF, NULL)))
			return hr;
	END_PERFORMANCE_COUNTER("Render filters");

	RemoveUnconnectedFilters(g_dsGraph->pFilterGraph);

	g_dsconfig.ConfigureFilters();
#ifdef _DSPLAYER_DEBUG
	LogFilterGraph();
#endif

	if(!IsSplitter(pBF))
		CGraphFilters::Get()->Source.pBF = pBF;

	do{
		if(IsSplitter(pBF))
		{
			CGraphFilters::Get()->Splitter.pBF = pBF;
			break;
		}
		Com::SmartPtr<IBaseFilter> pNext;
		hr =  GetNextFilter(pBF, DOWNSTREAM, &pNext);
		pBF = pNext;
	}while(hr == S_OK);

	// Init Streams manager, and load streams
	START_PERFORMANCE_COUNTER
		CStreamsManager::Get()->InitManager();
	CStreamsManager::Get()->LoadStreams();
	END_PERFORMANCE_COUNTER("Loading streams informations");

	if (! hasStreamDetails) {
		if (g_guiSettings.GetBool("myvideos.extractflags")) // Only warn user if the option is enabled
			CLog::Log(LOGWARNING, __FUNCTION__" DVDPlayer failed to fetch streams details. Using DirectShow ones");

		FileItem.GetVideoInfoTag()->m_streamDetails.AddStream( new CDSStreamDetailVideo((const CDSStreamDetailVideo &)(*CStreamsManager::Get()->GetVideoStreamDetail())) );

		std::vector<CDSStreamDetailAudio *>& streams = CStreamsManager::Get()->GetAudios();
		for (std::vector<CDSStreamDetailAudio *>::const_iterator it = streams.begin(); it != streams.end(); ++it)
			FileItem.GetVideoInfoTag()->m_streamDetails.AddStream(new CDSStreamDetailAudio((const CDSStreamDetailAudio &)(**it)));

		FileItem.GetVideoInfoTag()->m_streamDetails.Reset();
	}


	// Shaders
	{
		std::vector<uint32_t> shaders;
		START_PERFORMANCE_COUNTER
			if (SUCCEEDED(CFilterCoreFactory::GetShaders(FileItem, shaders, CGraphFilters::Get()->IsUsingDXVADecoder())))
			{
				for (std::vector<uint32_t>::const_iterator it = shaders.begin();
					it != shaders.end(); ++it)
				{
					g_dsSettings.pixelShaderList->EnableShader(*it);
				}
			}
			END_PERFORMANCE_COUNTER("Loading shaders");
	}

	CLog::Log(LOGDEBUG,"%s All filters added to the graph", __FUNCTION__);


	return S_OK;  
}