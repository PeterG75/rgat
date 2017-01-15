/*
Copyright 2016 Nia Catlin

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
Constants used to interpret and plot the trace data
*/

#pragma once
#define BBBUFSIZE 1024*1024
#define MAX_OPCODES 30

#define TAGCACHESIZE 1024*1024
#define TRACE_TAG_MARKER 'j'
#define LOOP_MARKER 'R'
#define LOOP_START_MARKER 'S'
#define LOOP_END_MARKER 'E'

#define MUTEXWAITPERIOD 6000

#define OPUNDEF 0
#define OPJMP 1
#define OPRET 2
#define OPCALL 3

#define BAD 0
#define NONFLOW 1
#define JUMP 2
#define CALL 3
#define RETURN 4
#define EXTERNAL 5
#define EXCEPTION_GENERATOR 6
#define NOSYM 7
#define FIRST_IN_THREAD 99

#define MOD_UNKNOWN 0 
#define MOD_INSTRUMENTED 1	//plot this
#define MOD_UNINSTRUMENTED 2 //client didn't instrument further - usually windows dlls

//can leave a conditional 2 ways, keep record of what has happened
#define ISCONDITIONAL 1
#define CONDFELLTHROUGH 2
#define CONDTAKEN 4
#define CONDCOMPLETE (ISCONDITIONAL | CONDFELLTHROUGH | CONDTAKEN)

#define ARG_NOTB64 '0'
#define ARG_BASE64 '1'

typedef unsigned int NODEINDEX;
typedef pair<NODEINDEX, NODEINDEX> NODEPAIR;
typedef vector<NODEPAIR> EDGELIST;
typedef pair<int, string> ARGIDXDATA;
typedef vector<ARGIDXDATA> ARGLIST;
typedef UINT64 MEM_ADDRESS;
typedef DWORD PID_TID;

//random number generated when the block was first seen in cache
typedef unsigned long BLOCK_IDENTIFIER;