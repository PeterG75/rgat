#include "stdafx.h"
#include <traceStructs.h>
#include "serialise.h"

#include <rapidjson\document.h>
#include <rapidjson\filewritestream.h>
#include <rapidjson\writer.h>
#include <rapidjson\filereadstream.h>
#include <rapidjson\reader.h>
#include <boost\filesystem.hpp>

using namespace rapidjson;


#pragma comment(lib, "legacy_stdio_definitions.lib") //capstone uses _sprintf
#pragma comment(lib, "capstone.lib")

bool unpackBasicBlock(PROCESS_DATA * piddata, const Value& blockEntry);

void PROCESS_DATA::getExternDictReadLock()
{
	AcquireSRWLockShared(&externDictRWLock);
}

void PROCESS_DATA::getExternDictWriteLock()
{
	AcquireSRWLockExclusive(&externDictRWLock);
}

void PROCESS_DATA::dropExternDictReadLock()
{
	ReleaseSRWLockShared(&externDictRWLock);
}

void PROCESS_DATA::dropExternDictWriteLock()
{
	ReleaseSRWLockExclusive(&externDictRWLock);
}

void PROCESS_DATA::getExternCallerReadLock()
{
	AcquireSRWLockShared(&externCallerRWLock);
}

void PROCESS_DATA::getExternCallerWriteLock()
{
	AcquireSRWLockExclusive(&externCallerRWLock);
}

void PROCESS_DATA::dropExternCallerReadLock()
{
	ReleaseSRWLockShared(&externCallerRWLock);
}

void PROCESS_DATA::dropExternCallerWriteLock()
{
	ReleaseSRWLockExclusive(&externCallerRWLock);
}

bool PROCESS_DATA::get_sym(unsigned int globalmodNum, ADDRESS_OFFSET offset, string &sym)
{
	bool found;
	getDisassemblyWriteLock();
	if (modsymsPlain[globalmodNum][offset].empty())
	{
		sym = "";
		found = false;
	}
	else
	{
		sym = modsymsPlain[globalmodNum][offset];
		found = true;
	}
	dropDisassemblyWriteLock();

	return found;
}

pair<ADDRESS_OFFSET, BLOCK_DESCRIPTOR *> PROCESS_DATA::blockDetails(BLOCK_IDENTIFIER blockid)
{
	pair<ADDRESS_OFFSET, BLOCK_DESCRIPTOR *> blockPair;

	getDisassemblyReadLock();
	blockPair = blockList.at(blockid);
	dropDisassemblyReadLock();

	return blockPair;
}

bool PROCESS_DATA::get_modpath(unsigned int globalmodnum, boost::filesystem::path *path)
{

	if (globalmodnum >= modpaths.size()) return false;

	getDisassemblyReadLock();
	*path = modpaths.at(globalmodnum);
	dropDisassemblyReadLock();

	return true;
}

bool PROCESS_DATA::get_extern_at_address(MEM_ADDRESS address, int moduleNum, ROUTINE_STRUCT **BB)
{
	getExternDictReadLock();
	map<MEM_ADDRESS, ROUTINE_STRUCT*>::iterator externIt = externdict.find(address);
	if (externIt == externdict.end())
	{
		dropExternDictReadLock();
		if (BB == NULL) 
			return false;

		getExternDictWriteLock();
		ROUTINE_STRUCT *newExtern = new ROUTINE_STRUCT;
		newExtern->globalmodnum = moduleNum; 
		dropExternDictWriteLock();

		*BB = newExtern;
		return true;
	}

	if (BB)
		*BB = externIt->second;
	dropExternDictReadLock();
	return true;
}



using namespace rapidjson;

bool unpackExtern(PROCESS_DATA * piddata, const Value& externEntry)
{

	Value::ConstMemberIterator externIt = externEntry.FindMember("A");
	if (externIt == externEntry.MemberEnd())
	{
		cerr << "Error, address not found in extern entry" << endl;
		return false;
	}
	MEM_ADDRESS externAddr = externIt->value.GetUint64();

	ROUTINE_STRUCT *BBEntry = new ROUTINE_STRUCT;

	externIt = externEntry.FindMember("M");
	if (externIt == externEntry.MemberEnd())
	{
		cerr << "[rgat]Error: module ID not found in extern entry" << endl;
		delete BBEntry;
		return false;
	}
	BBEntry->globalmodnum = externIt->value.GetUint();

	externIt = externEntry.FindMember("S");
	if (externIt == externEntry.MemberEnd())
	{
		cerr << "[rgat]Error: symbol presence not recorded in extern entry" << endl;
		delete BBEntry;
		return false;
	}
	BBEntry->hasSymbol = externIt->value.GetBool();

	externIt = externEntry.FindMember("C");
	if (externIt != externEntry.MemberEnd())
	{
		const Value& callerArray = externIt->value;
		Value::ConstValueIterator callerArrayIt = callerArray.Begin();
		for (; callerArrayIt != callerArray.End(); callerArrayIt++)
		{
			EDGELIST threadExternCalls;
			const Value& callingThreadEntry = *callerArrayIt;
			PID_TID threadID = callingThreadEntry[0].GetUint64();
			const Value& callingThreadEdges = callingThreadEntry[1];

			Value::ConstValueIterator callerEdgesIt = callingThreadEdges.Begin();
			for (; callerEdgesIt != callingThreadEdges.End(); callerEdgesIt++)
			{
				const Value& Edge = *callerEdgesIt;
				NODEINDEX source = Edge[0].GetUint64();
				NODEINDEX target = Edge[1].GetUint64();
				threadExternCalls.push_back(make_pair(source, target));
			}
			BBEntry->thread_callers[threadID] = threadExternCalls;
		}
	}

	piddata->externdict[externAddr] = BBEntry;
	return true;
}

//calls to dll functions
bool PROCESS_DATA::loadExterns(const Value& processDataJSON)
{
	Value::ConstMemberIterator procDataIt = processDataJSON.FindMember("Externs");
	if (procDataIt == processDataJSON.MemberEnd())
		return false;
	const Value& externsArray = procDataIt->value;

	stringstream externLoadMsg;
	externLoadMsg << "Loading " << externsArray.Capacity() << " externs";

	cout << "[rgat]" << externLoadMsg.str() << endl;
	//display_only_status_message(externLoadMsg.str(), clientState);

	Value::ConstValueIterator externIt = externsArray.Begin();
	for (; externIt != externsArray.End(); externIt++)
	{
		if (!unpackExtern(this, *externIt))
			return false;
	}
	return true;
}



bool PROCESS_DATA::unpackModuleSymbolArray(const rapidjson::Value& modSymArray, int globalmodNum)
{
	Value::ConstValueIterator modSymArrIt = modSymArray.Begin();
	for (; modSymArrIt != modSymArray.End(); modSymArrIt++)
	{
		const Value& symbolsArray = *modSymArrIt;
		if ((symbolsArray.Capacity() != 2) ||
			!symbolsArray[0].IsUint64() ||
			!symbolsArray[1].IsString())
		{
			cout << "[rgat]ERROR: Symbols load failed: bad symbol entry in module" << globalmodNum << endl;
			return false;
		}

		MEM_ADDRESS symAddress = symbolsArray[0].GetUint64();
		string symPlain = symbolsArray[1].GetString();

		modsymsPlain[globalmodNum][symAddress] = symPlain;
	}
	return true;
}

bool PROCESS_DATA::loadSymbols(const Value& saveJSON)
{
	Value::ConstMemberIterator symbolsIt = saveJSON.FindMember("ModuleSymbols");
	if (symbolsIt == saveJSON.MemberEnd())
		return false;

	const Value& symbolsArray = symbolsIt->value;

	stringstream symLoadMsg;
	symLoadMsg << "Loading " << symbolsArray.Capacity() << " symbols";

	cout << "[rgat]" << symLoadMsg.str() << endl;
	//display_only_status_message(symLoadMsg.str(), clientState);

	Value::ConstValueIterator modSymsIt = symbolsArray.Begin();
	for (; modSymsIt != symbolsArray.End(); modSymsIt++)
	{
		Value::ConstMemberIterator symDataIt = modSymsIt->FindMember("ModuleID");
		if (symDataIt == modSymsIt->MemberEnd())
		{
			cout << "[rgat]ERROR: Symbols load failed: No module ID" << endl;
			return false;
		}

		int moduleID = symDataIt->value.GetInt();

		symDataIt = modSymsIt->FindMember("Symbols");
		if (symDataIt == modSymsIt->MemberEnd())
		{
			cout << "[rgat]ERROR: Symbols load failed: No symbols array for module " << moduleID << endl;
			return false;
		}
		const Value& modSymArray = symDataIt->value;

		if (!unpackModuleSymbolArray(modSymArray, moduleID)) return false;
	}
	return true;
}

struct ADDR_DATA
{
	MEM_ADDRESS address;
	int moduleID;
	bool hasSym;
};

//for disassembling saved instructions
//takes a capstone context, opcode string, target instruction data struct and the address of the instruction
size_t disassemble_ins(csh hCapstone, string opcodes, INS_DATA *insdata, MEM_ADDRESS insaddr)
{
	cs_insn *insn;
	size_t count;
	uint8_t *opcodesPtr = (uint8_t*)opcodes.data();
	
	count = cs_disasm(hCapstone, opcodesPtr, opcodes.size(), insaddr, 0, &insn);
	if (count != 1) {
		cerr << "[rgat]ERROR: BB thread failed disassembly for opcodes: " << opcodes << " count: " << count << " error: " << cs_errno(hCapstone) << endl;
		return NULL;
	}

	insdata->mnemonic = string(insn->mnemonic);
	insdata->op_str = string(insn->op_str);
	insdata->ins_text = string(insdata->mnemonic + " " + insdata->op_str);
	insdata->numbytes = (int)floor(opcodes.length() / 2);
	insdata->address = insaddr;

	if (insdata->mnemonic == "call")
	{
		try {
			insdata->branchAddress = std::stoull(insdata->op_str, 0, 16);
		}
		catch (...) { insdata->branchAddress = NULL; }
		insdata->itype = eNodeType::eInsCall;
	}
	else if (insdata->mnemonic == "ret") //todo: iret
		insdata->itype = eNodeType::eInsReturn;
	else if (insdata->mnemonic == "jmp")
	{
		try { insdata->branchAddress = std::stoull(insdata->op_str, 0, 16); } //todo: not a great idea actually... just point to the outgoing neighbours for labels
		catch (...) { insdata->branchAddress = NULL; }
		insdata->itype = eNodeType::eInsJump;
	}
	else
	{
		insdata->itype = eNodeType::eInsUndefined;
		//assume all j+ instructions aside from jmp are conditional (todo: bother to check)
		if (insdata->mnemonic[0] == 'j')
		{
			insdata->conditional = true;
			insdata->branchAddress = std::stoull(insdata->op_str, 0, 16);
			insdata->condDropAddress = insaddr + insdata->numbytes;
		}
	}

	cs_free(insn, count);
	return count;
}

//for disassembling live instructions
//takes a capstone context, paritally filled instruction data struct (ie:with opcodes) and the address of the instruction
size_t disassemble_ins(csh hCapstone, INS_DATA *insdata, MEM_ADDRESS insaddr)
{
	cs_insn *insn;
	size_t count = cs_disasm(hCapstone, insdata->opcodes.get(), insdata->numbytes, insaddr, 0, &insn);
	if (count != 1) {
		cerr << "[rgat]ERROR: BB thread failed disassembly for opcodes:? "  << " count: " << count << " error: " << cs_errno(hCapstone) << endl;
		return NULL;
	}

	insdata->mnemonic = string(insn->mnemonic);
	insdata->op_str = string(insn->op_str);
	insdata->ins_text = string(insdata->mnemonic + " " + insdata->op_str);
	insdata->address = insaddr;

	if (insdata->mnemonic == "call")
	{
		try {
			insdata->branchAddress = std::stoull(insdata->op_str, 0, 16);
		}
		catch (...) { insdata->branchAddress = NULL; }
		insdata->itype = eNodeType::eInsCall;
	}
	else if (insdata->mnemonic == "ret") //todo: iret
		insdata->itype = eNodeType::eInsReturn;
	else if (insdata->mnemonic == "jmp")
	{
		try { insdata->branchAddress = std::stoull(insdata->op_str, 0, 16); } //todo: not a great idea actually... just point to the outgoing neighbours for labels
		catch (...) { insdata->branchAddress = NULL; }
		insdata->itype = eNodeType::eInsJump;
	}
	else
	{
		insdata->itype = eNodeType::eInsUndefined;
		//assume all j+ instructions aside from jmp are conditional (todo: bother to check)
		if (insdata->mnemonic[0] == 'j')
		{
			insdata->conditional = true;
			insdata->branchAddress = std::stoull(insdata->op_str, 0, 16);
			insdata->condDropAddress = insaddr + insdata->numbytes;
		}
	}

	cs_free(insn, count);
	return count;
}

bool unpackOpcodes(PROCESS_DATA *piddata, const Value& opcodesData, ADDR_DATA *addressdata, INSLIST *mutationVector, csh hCapstone)
{

	Value::ConstValueIterator opcodesEntryIt = opcodesData.Begin();
	for (; opcodesEntryIt != opcodesData.End(); opcodesEntryIt++)
	{
		const Value& opcodesEntry = *opcodesEntryIt;
		if (opcodesEntry.Capacity() != 2)
		{
			cerr << "[rgat] Load Error: Bad mutation entry" << endl;
			return false;
		}

		string opcodesStringb64 = opcodesEntry[0].GetString();
		if (opcodesStringb64.empty())
			return false;

		string opcodesString = base64_decode(opcodesStringb64);

		INS_DATA *ins = new INS_DATA;
		ins->globalmodnum = addressdata->moduleID;
		ins->hasSymbol = addressdata->hasSym;
		ins->numbytes = opcodesString.size();

		ins->opcodes = std::unique_ptr<uint8_t[]>(new uint8_t[ins->numbytes]);
		memcpy(ins->opcodes.get(), opcodesString.data(), ins->numbytes);

		disassemble_ins(hCapstone, ins, addressdata->address);

		const Value& threadNodes = opcodesEntry[1];
		Value::ConstValueIterator threadNodesIt = threadNodes.Begin();
		for (; threadNodesIt != threadNodes.End(); threadNodesIt++)
		{
			const Value& threadNodesEntry = *threadNodesIt;
			if (threadNodesEntry.Capacity() != 2)
			{
				cerr << "[rgat] Load Error: Bad thread nodes entry" << endl;
				delete ins;
				return false;
			}

			PID_TID excutingThread = threadNodesEntry[0].GetUint64();
			unsigned int GraphVertID = threadNodesEntry[1].GetInt();
			ins->threadvertIdx.emplace(excutingThread, GraphVertID);
		}

		mutationVector->push_back(ins);
	}
	return true;
}

bool unpackAddress(PROCESS_DATA *piddata, const Value& addressMutations, csh hCapstone)
{
	INSLIST mutationVector;

	if (addressMutations.Capacity() != 3)
	{
		cerr << "[rgat] Load Error: Bad address entry" << endl;
		return false;
	}

	ADDR_DATA addressStruct;
	addressStruct.address = addressMutations[0].GetUint64();
	addressStruct.moduleID = addressMutations[1].GetInt();
	const Value& mutationData = addressMutations[2];

	if (piddata->modsymsPlain.count(addressStruct.moduleID) && piddata->modsymsPlain.at(addressStruct.moduleID).count(addressStruct.address))
		addressStruct.hasSym = true;
	else
		addressStruct.hasSym = false;

	if (!unpackOpcodes(piddata, mutationData, &addressStruct, &mutationVector, hCapstone))
	{
		cerr << "[rgat] Load Error: Failed to unpack opcodes" << endl;
		return false;
	}

	piddata->disassembly.insert(make_pair(addressStruct.address, mutationVector));
	return true;
}

bool PROCESS_DATA::loadDisassembly(const Value& saveJSON)
{
	Value::ConstMemberIterator procDataIt = saveJSON.FindMember("Disassembly");
	if (procDataIt == saveJSON.MemberEnd())
		return false;
	const Value& disassemblyArray = procDataIt->value;

	stringstream disasLoadMsg;
	disasLoadMsg << "Loading disassembly for " << disassemblyArray.Capacity() << " addresses";

	cout << "[rgat]" << disasLoadMsg.str() << endl;
	//display_only_status_message(disasLoadMsg.str(), clientState);
	//display_only_status_message("Loading Disassembly", clientState);

	csh hCapstone;
	cs_mode disasBitwidth;

	if (bitwidth == 32)
		disasBitwidth = CS_MODE_32;
	else if (bitwidth == 64)
		disasBitwidth = CS_MODE_64;
	else
	{
		cerr << "[rgat]ERROR: Bad bitwidth " << bitwidth << endl;
		return false;
	}

	cs_err capOpenResult = cs_open(CS_ARCH_X86, disasBitwidth, &hCapstone);
	if (capOpenResult != CS_ERR_OK)
	{
		cerr << "[rgat]ERROR: Failed to open Capstone instance: " << capOpenResult << endl;
		return false;
	}

	Value::ConstValueIterator disassemblyIt = disassemblyArray.Begin();
	for (; disassemblyIt != disassemblyArray.End(); disassemblyIt++)
	{
		if (!unpackAddress(this, *disassemblyIt, hCapstone))
		{
			cerr << "[rgat]Error: Failed to unpack mutations" << endl;
			cs_close(&hCapstone);
			return false;
		}
	}

	cs_close(&hCapstone);
	return true;
}




bool PROCESS_DATA::load(const rapidjson::Document& saveJSON)
{
	Value::ConstMemberIterator procDataIt = saveJSON.FindMember("ProcessData");


	if (procDataIt == saveJSON.MemberEnd())
	{
		cout << "[rgat]ERROR: Process data load failed" << endl;
		return false;
	}
	const Value& procDataJSON = procDataIt->value;


	if (!loadModules(procDataJSON))
	{
		cerr << "[rgat]ERROR: Failed to load module paths" << endl;
		return false;
	}
	if (!loadSymbols(procDataJSON))
	{
		cerr << "[rgat]ERROR: Failed to load symbols" << endl;
		return false;
	}

	if (!loadDisassembly(procDataJSON))
	{
		cerr << "[rgat]ERROR: Disassembly reconstruction failed" << endl;
		return false;
	}
	
	if (!loadBlockData(procDataJSON))
	{
		cerr << "[rgat]ERROR: Basic block reconstruction failed" << endl;
		return false;
	}

	if (!loadExterns(procDataJSON))
	{
		cerr << "[rgat]ERROR: Extern call loading failed" << endl;
		return false;
	}

	return true;
}


int PROCESS_DATA::find_containing_module(MEM_ADDRESS address)
{
	const size_t numModules = modBounds.size();
	for (int modNo = 0; modNo < numModules; ++modNo)
	{
		pair<MEM_ADDRESS, MEM_ADDRESS> *moduleBounds = modBounds.at(modNo);
		if (!moduleBounds) continue;
		if (address >= moduleBounds->first && address <= moduleBounds->second)
		{
			return modNo;
		}
	}

	return -1;
}

/*
Find the disassembly for [blockaddr]
If it doesn't exist it will loop for a bit waiting for the disassembly to appear, unless [diegflag] becomes true
If the address is instrumented code, the mutation matching blockID will similarly be looked up and returned
If the address is uninstrumented code, the extern block will be retrieved and its address placed in [*externBlock]
*/
INSLIST* PROCESS_DATA::getDisassemblyBlock(MEM_ADDRESS blockaddr, BLOCK_IDENTIFIER blockID, bool *dieFlag, ROUTINE_STRUCT **externBlock)
{
	int iterations = 0;

	while (true)
	{

		if (blockID == -1)
		{
			int moduleNo = find_containing_module(blockaddr);
			get_extern_at_address(blockaddr, moduleNo, externBlock);
			return NULL;
		}

		if (blockID < blockList.size())
		{
			getDisassemblyReadLock();
			INSLIST *result;
			BLOCK_DESCRIPTOR *blkd = blockList.at(blockID).second;

			result = blkd->inslist;

			dropDisassemblyReadLock();
			return result;
		}


		if (iterations> 3)
			Sleep(1);

		if (iterations++ > 20)
			cerr << "[rgat]Warning: Long wait for disassembly of address 0x" << std::hex << blockaddr << endl;

		if (*dieFlag) return 0;
	}
}

void PROCESS_DATA::save(rapidjson::Writer<rapidjson::FileWriteStream>& writer)
{
	writer.StartObject();

	saveMetaData(writer);
	saveModules(writer);
	saveSymbols(writer);
	saveDisassembly(writer);
	saveBlockData(writer);
	saveExternDict(writer);

	writer.EndObject();
}



void PROCESS_DATA::saveDisassembly(rapidjson::Writer<rapidjson::FileWriteStream>& writer)
{
	writer.Key("Disassembly");
	writer.StartArray();

	getDisassemblyReadLock();
	map <MEM_ADDRESS, INSLIST>::iterator disasIt = disassembly.begin();
	for (; disasIt != disassembly.end(); ++disasIt)
	{
		writer.StartArray();

		writer.Int64(disasIt->first); //address

		writer.Int(disasIt->second.front()->globalmodnum); //module

		writer.StartArray(); //opcode data for each mutation found at address
		INSLIST::iterator mutationIt = disasIt->second.begin();
		for (; mutationIt != disasIt->second.end(); ++mutationIt)
		{
			INS_DATA *ins = *mutationIt;
			writer.StartArray();

			string opcodeString = base64_encode(ins->opcodes.get(), ins->numbytes);
			writer.String(opcodeString.c_str());

			//threads containing it
			writer.StartArray();
			unordered_map<PID_TID, NODEINDEX>::iterator threadVertIt = ins->threadvertIdx.begin();
			for (; threadVertIt != ins->threadvertIdx.end(); ++threadVertIt)
			{
				writer.StartArray();

				//could make file smaller by doing a lookup table.
				writer.Int64(threadVertIt->first); //thread id
				writer.Uint64(threadVertIt->second); //node idx

				writer.EndArray();
			}
			writer.EndArray(); //end array of indexes for this mutation

			writer.EndArray(); //end mutation
		}
		writer.EndArray(); //end array of mutations for this address

		writer.EndArray(); //end address

	}
	dropDisassemblyReadLock();
	writer.EndArray(); // end array of disassembly data for trace
}

void PROCESS_DATA::saveExternDict(rapidjson::Writer<rapidjson::FileWriteStream>& writer)
{
	writer.Key("Externs");
	writer.StartArray();

	map <MEM_ADDRESS, ROUTINE_STRUCT *>::iterator externIt = externdict.begin();
	for (; externIt != externdict.end(); ++externIt)
	{
		writer.StartObject();

		writer.Key("A");	//address
		writer.Int64(externIt->first);

		writer.Key("M");	//module number
		writer.Int(externIt->second->globalmodnum);

		writer.Key("S");	//has symbol?
		writer.Bool(externIt->second->hasSymbol);

		//todo: should this object even be written if empty?
		if (!externIt->second->thread_callers.empty())
		{
			writer.Key("C");	//thread callers
			writer.StartArray();
			map<DWORD, EDGELIST>::iterator threadCallIt = externIt->second->thread_callers.begin();
			for (; threadCallIt != externIt->second->thread_callers.end(); ++threadCallIt)
			{
				writer.StartArray();

				//thread id
				writer.Uint64(threadCallIt->first);

				//edges
				writer.StartArray();
				EDGELIST::iterator edgeIt = threadCallIt->second.begin();
				for (; edgeIt != threadCallIt->second.end(); ++edgeIt)
				{
					writer.StartArray();
					//source, target
					writer.Uint64(edgeIt->first);
					writer.Uint64(edgeIt->second);

					writer.EndArray();
				}
				writer.EndArray(); //end edge array

				writer.EndArray(); //end thread callers object for this thread
			}
			writer.EndArray(); //end thread callers array for this address
		}
		writer.EndObject(); //end object for this extern entry
	}

	writer.EndArray(); //end externs array
}

void PROCESS_DATA::saveBlockData(rapidjson::Writer<rapidjson::FileWriteStream>& writer)
{
	writer.Key("BasicBlocks");
	writer.StartArray();

	getDisassemblyReadLock();

	vector <pair<ADDRESS_OFFSET, BLOCK_DESCRIPTOR *>>::iterator blockIt = blockList.begin();

	for (; blockIt != blockList.end(); ++blockIt)
	{
		writer.StartArray();

		//block address
		writer.Uint64(blockIt->first);
		
		BLOCK_DESCRIPTOR *blkdesc = blockIt->second;
		//instruction list
		writer.StartArray();
		INSLIST::iterator inslistIt = blkdesc->inslist->begin();
		for (; inslistIt != blkdesc->inslist->end(); ++inslistIt)
		{
			writer.Uint64((*inslistIt)->address);
			writer.Uint((*inslistIt)->mutationIndex);
		}
		writer.EndArray();	//end instructions array for this address


		writer.EndArray(); //end basic block object for this address
	}
	dropDisassemblyReadLock();
	writer.EndArray(); //end array of basic blocks
}

//tie the disassembled instructions together into basic blocks
bool PROCESS_DATA::loadBlockData(const Value& saveJSON)
{

	Value::ConstMemberIterator procDataIt = saveJSON.FindMember("BasicBlocks");
	if (procDataIt == saveJSON.MemberEnd())
		return false;
	const Value& basicBlockArray = procDataIt->value;

	stringstream BBLoadMsg;
	BBLoadMsg << "Loading " << basicBlockArray.Capacity() << " basic blocks";

	cout << "[rgat]" << BBLoadMsg.str() << endl;
	//display_only_status_message(BBLoadMsg.str(), clientState);


	Value::ConstValueIterator basicBlockIt = basicBlockArray.Begin();
	for (; basicBlockIt != basicBlockArray.End(); basicBlockIt++)
	{
		if (!unpackBasicBlock(this, *basicBlockIt))
			return false;
	}

	return true;
}

bool unpackBasicBlock(PROCESS_DATA * piddata, const Value& blockEntry)
{
	/*
	if (blockEntry.Capacity() != 2)
	{
		cerr << "[rgat]Error: Failed to unpack basic block ( "<< blockEntry.Capacity()<<" items)" << endl;
		return false;
	}
	*/


	BLOCK_DESCRIPTOR *blkdesc = new BLOCK_DESCRIPTOR;
	MEM_ADDRESS blockaddress = blockEntry[0].GetUint64();

	//piddata->addressBlockMap[blockaddress][blockID] = blkdesc;
	piddata->blockList.push_back(make_pair(blockaddress, blkdesc));


	const Value& insListArray = blockEntry[1];
	if (!insListArray.IsArray() || insListArray.Capacity() % 2 != 0)
		return false;

	blkdesc->inslist = new INSLIST;
	Value::ConstValueIterator inslistIt = insListArray.Begin();
	for (; inslistIt != insListArray.End(); inslistIt++)
	{
		ADDRESS_OFFSET insAddress = inslistIt->GetUint64();
		inslistIt++;
		unsigned int mutationIndex = inslistIt->GetUint();
		blkdesc->inslist->push_back(piddata->disassembly.at(insAddress).at(mutationIndex));
	}


	return true;
}



void PROCESS_DATA::saveMetaData(rapidjson::Writer<rapidjson::FileWriteStream>& writer)
{
	writer.Key("BitWidth");
	if (bitwidth == 32)
	{
		writer.Uint(32);
	}
	else if (bitwidth == 64)
	{
		writer.Uint(64);
	}
	else
	{
		cerr << "[rgat] Error: Trace not locked while saving. Proto-graph has invalid bitwidth marker " << bitwidth << endl;
		assert(false);
		return;
	}

	writer.Key("RGATVersionMaj");
	writer.Uint(RGAT_VERSION_MAJ);
	writer.Key("RGATVersionMin");
	writer.Uint(RGAT_VERSION_MIN);
	writer.Key("RGATVersionFeature");
	writer.Uint(RGAT_VERSION_FEATURE);
}


//big, but worth doing in case environments differ
void PROCESS_DATA::saveSymbols(rapidjson::Writer<rapidjson::FileWriteStream>& writer)
{
	writer.Key("ModuleSymbols");
	writer.StartArray();

	map <int, std::map<MEM_ADDRESS, string>>::iterator modSymIt = modsymsPlain.begin();
	for (; modSymIt != modsymsPlain.end(); ++modSymIt)
	{
		writer.StartObject();

		writer.Key("ModuleID");
		writer.Int(modSymIt->first);

		writer.Key("Symbols");
		writer.StartArray();
		map<MEM_ADDRESS, string> ::iterator symIt = modSymIt->second.begin();
		for (; symIt != modSymIt->second.end(); symIt++)
		{
			writer.StartArray();
			writer.Uint64(symIt->first); //symbol address
			writer.String(symIt->second.c_str()); //symbol string
			writer.EndArray();
		}
		writer.EndArray();

		writer.EndObject();
	}

	writer.EndArray();
}

void PROCESS_DATA::saveModules(rapidjson::Writer<rapidjson::FileWriteStream>& writer)
{
	writer.Key("ModulePaths");
	writer.StartArray();

	vector<boost::filesystem::path>::iterator pathIt = modpaths.begin();
	for (; pathIt != modpaths.end(); pathIt++)
	{
		string pathstr = pathIt->string();
		const unsigned char* cus_pathstring = reinterpret_cast<const unsigned char*>(pathstr.c_str());
		writer.StartObject();
		writer.Key("B64");
		writer.String(base64_encode(cus_pathstring, (unsigned int)pathIt->size()).c_str());
		writer.EndObject();
	}

	writer.EndArray();


	writer.Key("ModuleBounds");
	writer.StartArray();

	size_t numMods = modpaths.size();
	vector <pair<MEM_ADDRESS, MEM_ADDRESS> *>::iterator modBoundsIt = modBounds.begin();
	for (size_t i = 0; i < numMods; i++)
	{
		pair<MEM_ADDRESS, MEM_ADDRESS> *bounds = modBoundsIt[i];
		assert(bounds != NULL);
		writer.StartArray();
		writer.Int64(bounds->first);
		writer.Int64(bounds->second);
		writer.EndArray();
	}

	writer.EndArray();

}

bool PROCESS_DATA::loadModules(const rapidjson::Value& processDataJSON)
{
	//display_only_status_message("Loading Modules", clientState);
	cout << "[rgat]Loading Module Paths" << endl;

	Value::ConstMemberIterator procDataIt = processDataJSON.FindMember("ModulePaths");
	if (procDataIt == processDataJSON.MemberEnd())
	{
		cerr << "[rgat] Failed to find ModulePaths in trace" << endl;
		return false;
	}

	const Value& modPathArray = procDataIt->value;

	stringstream pathLoadMsg;
	pathLoadMsg << "Loading " << modPathArray.Capacity() << " modules";

	cout << "[rgat]" << pathLoadMsg.str() << endl;
	//display_only_status_message(pathLoadMsg.str(), clientState);

	Value::ConstValueIterator modPathIt = modPathArray.Begin();
	for (; modPathIt != modPathArray.End(); modPathIt++)
	{

		Value::ConstMemberIterator pathDataIt = modPathIt->FindMember("B64");
		if (pathDataIt == modPathIt->MemberEnd())
		{
			cout << "[rgat]ERROR: Module Paths load failed: No path string" << endl;
			return false;
		}

		string b64path = pathDataIt->value.GetString();
		string plainpath = base64_decode(b64path);

		modpaths.push_back(plainpath);
	}


	procDataIt = processDataJSON.FindMember("ModuleBounds");
	if (procDataIt == processDataJSON.MemberEnd())
	{
		cerr << "[rgat] Failed to find ModuleBounds in trace" << endl;
		return false;
	}
	const Value& modsBoundArray = procDataIt->value;

	modBounds.clear();
	Value::ConstValueIterator modsBoundIt = modsBoundArray.Begin();
	for (; modsBoundIt != modsBoundArray.End(); modsBoundIt++)
	{
		const Value& moduleBounds = *modsBoundIt;
		auto boundPair = new pair<MEM_ADDRESS, MEM_ADDRESS>;
		boundPair->first = moduleBounds[0].GetUint64();
		boundPair->second = moduleBounds[1].GetUint64();
		modBounds.push_back(boundPair);
	}

	return true;
}

#define LARGEST_X86_INSTRUCTION 15
MEM_ADDRESS PROCESS_DATA::instruction_before(MEM_ADDRESS addr)
{
	//first lookup in cache
	auto prevInsIt = previousInstructionsCache.find(addr);
	if (prevInsIt != previousInstructionsCache.end())
		return prevInsIt->second;

	if (disassembly.empty()) return NULL;

	//x86 has variable length instructions so we have to 
	//search backwards, byte by byte
	getDisassemblyReadLock();
	MEM_ADDRESS testaddr, addrMinus;
	for (addrMinus = 1; addrMinus < (LARGEST_X86_INSTRUCTION+1); addrMinus++)
	{
		testaddr = addr - addrMinus;
		if (disassembly.count(testaddr) > 0)
		{
			break;
		}
	}
	dropDisassemblyReadLock();

	if (addrMinus > LARGEST_X86_INSTRUCTION)
	{
		//cerr << "[rgat]Error: Unable to find instruction before 0x" << hex << addr << endl;
		return NULL;
	}

	previousInstructionsCache[addr] = testaddr;
	return testaddr;
}