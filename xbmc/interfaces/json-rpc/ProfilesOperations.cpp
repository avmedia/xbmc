/*
 *      Copyright (C) 2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "ProfilesOperations.h"
#include "ApplicationMessenger.h"
#include "guilib/LocalizeStrings.h"
#include "profiles/ProfilesManager.h"
#include "utils/md5.h"

using namespace JSONRPC;

JSONRPC_STATUS CProfilesOperations::GetProfiles(const CStdString &method, ITransportLayer *transport, IClient *client, const CVariant &parameterObject, CVariant &result)
{
  CFileItemList listItems;

  for (unsigned int i = 0; i < CProfilesManager::Get().GetNumberOfProfiles(); ++i)
  {
    const CProfile *profile = CProfilesManager::Get().GetProfile(i);
    CFileItemPtr item(new CFileItem(profile->getName()));
    item->SetArt("thumb", profile->getThumb());
    listItems.Add(item);
  }

  HandleFileItemList("profileid", false, "profiles", listItems, parameterObject, result);

  for (CVariant::const_iterator_array propertyiter = parameterObject["properties"].begin_array(); propertyiter != parameterObject["properties"].end_array(); ++propertyiter)
  {
    if (propertyiter->isString() &&
        propertyiter->asString() == "lockmode")
    {
      for (CVariant::iterator_array profileiter = result["profiles"].begin_array(); profileiter != result["profiles"].end_array(); ++profileiter)
      {
        CStdString profilename = (*profileiter)["label"].asString();
        int index = CProfilesManager::Get().GetProfileIndex(profilename);
        const CProfile *profile = CProfilesManager::Get().GetProfile(index);
        LockType locktype = LOCK_MODE_UNKNOWN;
        if (index == 0)
          locktype = CProfilesManager::Get().GetMasterProfile().getLockMode();
        else
          locktype = profile->getLockMode();
        (*profileiter)["lockmode"] = locktype;
      }
      break;
    }
  }
  return OK;
}

JSONRPC_STATUS CProfilesOperations::GetCurrentProfile(const CStdString &method, ITransportLayer *transport, IClient *client, const CVariant &parameterObject, CVariant &result)
{
  const CProfile& currentProfile = CProfilesManager::Get().GetCurrentProfile();
  CVariant profileVariant = CVariant(CVariant::VariantTypeObject);
  profileVariant["label"] = currentProfile.getName();
  for (CVariant::const_iterator_array propertyiter = parameterObject["properties"].begin_array(); propertyiter != parameterObject["properties"].end_array(); ++propertyiter)
  {
    if (propertyiter->isString())
    {
      if (propertyiter->asString() == "lockmode")
        profileVariant["lockmode"] = currentProfile.getLockMode();
      else if (propertyiter->asString() == "thumbnail")
        profileVariant["thumbnail"] = currentProfile.getThumb();
    }
  }

  result = profileVariant;

  return OK;
}

JSONRPC_STATUS CProfilesOperations::LoadProfile(const CStdString &method, ITransportLayer *transport, IClient *client, const CVariant &parameterObject, CVariant &result)
{
  CStdString profilename = parameterObject["profile"].asString();
  int index = CProfilesManager::Get().GetProfileIndex(profilename);
  
  if (index < 0)
    return InvalidParams;

	// Init prompt
	bool bPrompt = false;
	bPrompt = parameterObject["prompt"].asBoolean();
    
	bool bCanceled(false);
  bool bLoadProfile(false);

  if (CProfilesManager::Get().GetMasterProfile().getLockMode() == LOCK_MODE_EVERYONE ||            // Password not needed
      (bPrompt && g_passwordManager.IsProfileLockUnlocked(index, bCanceled, bPrompt)))  // Password needed and user asked to enter it
    bLoadProfile = true;
	else if (!bCanceled && parameterObject.isMember("password"))  // Password needed and user provided it
	{
    const CVariant &passwordObject = parameterObject["password"];
	  CStdString strToVerify;  // Holds user saved password hash
		if (index == 0)
		  strToVerify = CProfilesManager::Get().GetMasterProfile().getLockCode();
		else
		{
	    CProfile *profile = CProfilesManager::Get().GetProfile(index);
		  strToVerify = profile->getLockCode();
		}

		CStdString password = passwordObject["value"].asString();
		
		// Create password hash from the provided password if md5 is not used
    CStdString md5pword2;
    CStdString encryption = passwordObject["encryption"].asString();
    if (encryption.Equals("none"))
		{
			XBMC::XBMC_MD5 md5state;
			md5state.append(password);
			md5state.getDigest(md5pword2);
		}
		else if (encryption.Equals("md5"))
			md5pword2 = password;

		// Verify profided password
    if (strToVerify.Equals(md5pword2))
		  bLoadProfile = true;
	}

  if (bLoadProfile)
  {
    CApplicationMessenger::Get().LoadProfile(index);
    return ACK;
  }
  return InvalidParams;
}
