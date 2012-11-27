/*
 *      Copyright (C) 2005-2012 Team XBMC
 *      http://www.xbmc.org
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
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */


#include "LanguageHook.h"
#include "XBPython.h"

#include "interfaces/legacy/AddonUtils.h"
#include "utils/GlobalsHandling.h"
#include "PyContext.h"

namespace XBMCAddon
{
  namespace Python
  {
    static AddonClass::Ref<LanguageHook> instance;

    static CCriticalSection ccrit;
    static bool isInited = false;
    static xbmcutil::InitFlag flag(isInited);

    // vtab instantiation
    LanguageHook::~LanguageHook() { }

    void LanguageHook::makePendingCalls()
    {
      PythonCallbackHandler::makePendingCalls();
    }

    void LanguageHook::delayedCallOpen()
    {
      TRACE;
      PyGILLock::releaseGil();
    }

    void LanguageHook::delayedCallClose()
    {
      TRACE;
      PyGILLock::acquireGil();
    }

    LanguageHook* LanguageHook::getInstance() 
    {
      if (!isInited) // in this case we're being called from a static initializer
      {
        if (instance.isNull())
          instance = new LanguageHook();
      }
      else
      {
        CSingleLock lock (ccrit);
        if (instance.isNull())
          instance = new LanguageHook();
      }

      return instance.get();
    }

    /**
     * PythonCallbackHandler expects to be instantiated PER AddonClass instance
     *  that is to be used as a callback. This is why this cannot be instantited
     *  once.
     *
     * There is an expectation that this method is called from the Python thread
     *  that instantiated an AddonClass that has the potential for a callback.
     *
     * See RetardedAsynchCallbackHandler for more details.
     * See PythonCallbackHandler for more details
     * See PythonCallbackHandler::PythonCallbackHandler for more details
     */
    XBMCAddon::CallbackHandler* LanguageHook::getCallbackHandler()
    { 
      return new PythonCallbackHandler();
    }

    String LanguageHook::getAddonId()
    {
      const char* id = NULL;

      // Get a reference to the main module
      // and global dictionary
      PyObject* main_module = PyImport_AddModule((char*)"__main__");
      PyObject* global_dict = PyModule_GetDict(main_module);
      // Extract a reference to the function "func_name"
      // from the global dictionary
      PyObject* pyid = PyDict_GetItemString(global_dict, "__xbmcaddonid__");
      id = PyString_AsString(pyid);
      return id;
    }

    String LanguageHook::getAddonVersion()
    {
      // Get a reference to the main module
      // and global dictionary
      PyObject* main_module = PyImport_AddModule((char*)"__main__");
      PyObject* global_dict = PyModule_GetDict(main_module);
      // Extract a reference to the function "func_name"
      // from the global dictionary
      PyObject* pyversion = PyDict_GetItemString(global_dict, "__xbmcapiversion__");
      String version(PyString_AsString(pyversion));
      return version;
    }

    void LanguageHook::registerPlayerCallback(IPlayerCallback* player) { g_pythonParser.RegisterPythonPlayerCallBack(player); }
    void LanguageHook::unregisterPlayerCallback(IPlayerCallback* player) { g_pythonParser.UnregisterPythonPlayerCallBack(player); }
    void LanguageHook::registerMonitorCallback(XBMCAddon::xbmc::Monitor* monitor) { g_pythonParser.RegisterPythonMonitorCallBack(monitor); }
    void LanguageHook::unregisterMonitorCallback(XBMCAddon::xbmc::Monitor* monitor) { g_pythonParser.UnregisterPythonMonitorCallBack(monitor); }

    bool LanguageHook::waitForEvent(CEvent& hEvent, unsigned int milliseconds)
    { 
      return g_pythonParser.WaitForEvent(hEvent,milliseconds);
    }
  }
}
