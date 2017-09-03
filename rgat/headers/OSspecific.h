/*
Copyright 2016-2017 Nia Catlin

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

/*
This is intended to be the location for OS abstractions
Need to migrate all Windows API (and -soon- Linux) routines here
*/
#pragma once
#include <stdafx.h>

#include "clientConfig.h"
#include "locks.h"

#ifdef WINDOWS
#include <shlwapi.h>
#include "targetver.h"
#endif


#include "traceConstants.h"


enum eExeCheckResult { eNotInitialised, eNotExecutable, eBinary32Bit, eBinary64Bit, eBinaryOther };

string getModulePath();
PID_TID getParentPID(PID_TID childPid);
eExeCheckResult check_excecutable_type(string executable);

bool getDRPath(clientConfig *config, boost::filesystem::path *drpath);