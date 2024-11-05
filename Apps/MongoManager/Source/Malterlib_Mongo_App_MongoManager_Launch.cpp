// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoManager_Server.h"

namespace NMib::NMongo::NMongoManager
{
	void CMongoManagerActor::fs_SetupEnvironment(CProcessLaunchParams &_Params)
	{
		_Params.m_bMergeEnvironment = true;
		_Params.m_Environment["LANGUAGE"] = "en_US.UTF-8";
		_Params.m_Environment["LANG"] = "en_US.UTF-8";
		_Params.m_Environment["LC_ALL"] = "en_US.UTF-8";
	}

	CStr CMongoManagerActor::fp_ConcatOutput(CStr const &_StdOut, CStr const &_StdErr) const
	{
		if (_StdOut.f_IsEmpty() && _StdErr.f_IsEmpty())
			return CStr();
		CStr Ret;
		CStr StdOut = _StdOut.f_Trim();
		if (!StdOut.f_IsEmpty())
			fg_AddStrSep(Ret, StdOut, DMibNewLine);
		CStr StdErr = _StdErr.f_Trim();
		if (!StdErr.f_IsEmpty())
			fg_AddStrSep(Ret, StdErr, DMibNewLine);
		return DMibNewLine + Ret;
	}

	TCFuture<CStr> CMongoManagerActor::fp_LaunchTool
		(
			CStr _Executable
			, CStr _WorkingDir
			, TCVector<CStr> _Params
			, CStr _LogCategory
			, ELogVerbosity _LogVerbosity
			, bool _bSeparateStdErr
			, CStr _Home
			, CStr _User
			, CStr _Group
#ifdef DPlatformFamily_Windows
			, CStrSecure _UserPassword
#endif
		)
	{
		if (mp_pCanDestroyTracker.f_IsEmpty() || mp_bStopped)
			co_return "";
		
		auto *pToolLaunch = &mp_ToolLaunches.f_Insert();
		pToolLaunch->m_ProcessLaunch = fg_ConstructActor<CProcessLaunchActor>();
		
		CProcessLaunchActor::CSimpleLaunch Launch = NMib::NProcess::CProcessLaunchParams::fs_LaunchExecutable(_Executable, _Params, _WorkingDir, {});
		
		switch (_LogVerbosity)
		{
		case ELogVerbosity_None:
			break;
		case ELogVerbosity_Errors:
			Launch.m_ToLog = CProcessLaunchActor::ELogFlag_Error;
			break;
		case ELogVerbosity_Messages:
			Launch.m_ToLog = CProcessLaunchActor::ELogFlag_Error | CProcessLaunchActor::ELogFlag_Info;
			break;
		case ELogVerbosity_All:
			Launch.m_ToLog = CProcessLaunchActor::ELogFlag_All;
			break;
		}
		Launch.m_LogName = _LogCategory;
		Launch.m_Params.m_bCreateNewProcessGroup = true;
		
		auto &LaunchParams = Launch.m_Params;
		
		fs_SetupEnvironment(LaunchParams);

		LaunchParams.m_bSeparateStdErr = _bSeparateStdErr;
		LaunchParams.m_bAllowExecutableLocate = true;
		LaunchParams.m_bShowLaunched = false;
		
		if (!_User.f_IsEmpty())
		{
			LaunchParams.m_RunAsUser = _User;
#ifdef DPlatformFamily_Windows
			LaunchParams.m_RunAsUserPassword = _UserPassword;
#endif
			LaunchParams.m_RunAsGroup = _Group;
		}

		if (!_Home.f_IsEmpty())
		{
			LaunchParams.m_Environment["HOME"] = _Home;
			LaunchParams.m_Environment["TMPDIR"] = _Home + "/.tmp";
#ifdef DPlatformFamily_Windows
			LaunchParams.m_Environment["TMP"] = _Home + "/.tmp";
			LaunchParams.m_Environment["TEMP"] = _Home + "/.tmp";
#endif
		}
		
		TCSharedPointer<bool> pDestroyed = pToolLaunch->m_pDestroyed;
		auto pCleanup = g_OnScopeExitActor / [this, pDestroyed, pToolLaunch]
			{
				if (!*pDestroyed)
					mp_ToolLaunches.f_Remove(*pToolLaunch);
			}
		;
		
		auto LaunchResult = co_await pToolLaunch->m_ProcessLaunch(&CProcessLaunchActor::f_LaunchSimple, fg_Move(Launch));
		if (LaunchResult.m_ExitCode != 0)
		{
			CStr ErrorOut;
			if (_bSeparateStdErr)
				ErrorOut = LaunchResult.f_GetErrorOut().f_TrimRight();
			else
				ErrorOut = LaunchResult.f_GetStdOut().f_TrimRight();
			co_return DErrorInstance(fg_Format("Tool exited with: {}\n{}", LaunchResult.m_ExitCode, ErrorOut));
		}

		co_return LaunchResult.f_GetStdOut();
	}

	TCFuture<CStr> CMongoManagerActor::fp_RunToolForVersionCheck(CStr _Tool, TCVector<CStr> _Arguments)
	{
		co_return co_await fp_LaunchTool
			(
				_Tool
				, CFile::fs_GetProgramDirectory()
				, _Arguments
				, "VersionCheck"
				, ELogVerbosity_None
				, true
				, CStr()
				, CStr()
				, CStr()
#ifdef DPlatformFamily_Windows
				, CStrSecure()
#endif
			)
		;
	}
}
