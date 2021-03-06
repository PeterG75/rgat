/*
Copyright 2017 Nia Catlin

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

#include "stdafx.h"
#include "headers\binaryTargets.h"

//finds container for the target with the specified path - returns false
//if it doesn't exist, creates it and returns true (new binary target)
bool binaryTargets::getTargetByPath(boost::filesystem::path path, binaryTarget **target)
{

	targetsLock.lock();


	binaryTarget* result = NULL;
	bool newBinary;
	auto it = targets.find(path);
	if (it != targets.end()) 
	{ 
		result = it->second;
		newBinary = false;
	}
	else
	{
		binaryTarget *newTarget = new binaryTarget(path.generic_path());
		result = targets.emplace(make_pair(path, newTarget)).first->second;

		targetsList.push_back(result);
		newBinary = true;

	}
	targetsLock.unlock();

	*target = result;
	return newBinary;
}

void binaryTargets::registerChild(PID_TID parentPID, traceRecord *trace)
{
	vector<traceRecord *> matchingRecords;

	targetsLock.lock();

	for (auto tarIt = targets.begin();  tarIt != targets.end(); tarIt++)
	{
		traceRecord *possibleParentTrace = tarIt->second->getRecordWithPID(parentPID, 0);
		if (possibleParentTrace)
			matchingRecords.push_back(possibleParentTrace);
	}
	targetsLock.unlock();

	if (!matchingRecords.empty())
	{
		traceRecord *mostRecentPossibleParent = matchingRecords.back();
		mostRecentPossibleParent->children.push_back(trace);
		trace->parentTrace = mostRecentPossibleParent;
	}
}

vector<binaryTarget *> binaryTargets::getTargetsList() 
{ 
	vector<binaryTarget *> tempTargets;
	targetsLock.lock();
	tempTargets = targetsList;
	targetsLock.unlock();
	return tempTargets;
}

void binaryTargets::clear()
{
	targetsLock.lock();
	targets.clear();
	targetsList.clear();
	activeTarget = NULL;
	targetsLock.unlock();
}