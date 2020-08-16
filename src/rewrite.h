#ifndef REWRITE_H
#define REWRITE_H

#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>
#include <sstream>
#include <queue>
#include <map>
#include "abcApi.h"


using namespace std;
extern "C" Aig_Man_t * Abc_NtkToDar( Abc_Ntk_t * pNtk, int fExors, int fRegisters );
extern "C" Abc_Ntk_t * Abc_NtkFromDar( Abc_Ntk_t * pNtkOld, Aig_Man_t * pMan );
extern "C" void Dec_GraphUpdateNetwork( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int fUpdateLevel, int nGain );

// rewrite
extern "C" int Abc_NtkRewrite( Abc_Ntk_t * pNtk, int fUpdateLevel, int fUseZeros, int fVerbose, int fVeryVerbose, int fPlaceEnable );
Cut_Man_t * Abc_NtkStartCutManForRewrite( Abc_Ntk_t * pNtk );

// refactor
struct Abc_ManRef_t_
{
    // user specified parameters
    int              nNodeSizeMax;      // the limit on the size of the supernode
    int              nConeSizeMax;      // the limit on the size of the containing cone
    int              fVerbose;          // the verbosity flag
    // internal data structures
    Vec_Ptr_t *      vVars;             // truth tables
    Vec_Ptr_t *      vFuncs;            // functions
    Vec_Int_t *      vMemory;           // memory
    Vec_Str_t *      vCube;             // temporary
    Vec_Int_t *      vForm;             // temporary
    Vec_Ptr_t *      vVisited;          // temporary
    Vec_Ptr_t *      vLeaves;           // temporary
    // node statistics
    int              nLastGain;
    int              nNodesConsidered;
    int              nNodesRefactored;
    int              nNodesGained;
    int              nNodesBeg;
    int              nNodesEnd;
    // runtime statistics
    abctime          timeCut;
    abctime          timeTru;
    abctime          timeDcs;
    abctime          timeSop;
    abctime          timeFact;
    abctime          timeEval;
    abctime          timeRes;
    abctime          timeNtk;
    abctime          timeTotal;
};
typedef struct Abc_ManRef_t_   Abc_ManRef_t;
extern "C" int Abc_NtkRefactor( Abc_Ntk_t * pNtk, int nNodeSizeMax, int nConeSizeMax, int fUpdateLevel, int fUseZeros, int fUseDcs, int fVerbose );
extern "C" Dec_Graph_t * Abc_NodeRefactor( Abc_ManRef_t * p, Abc_Obj_t * pNode, Vec_Ptr_t * vFanins, int fUpdateLevel, int fUseZeros, int fUseDcs, int fVerbose );
extern "C" Abc_ManRef_t * Abc_NtkManRefStart( int nNodeSizeMax, int nConeSizeMax, int fUseDcs, int fVerbose );
extern "C" void Abc_NtkManRefStop( Abc_ManRef_t * p );


enum MODE {
    SEQ,
    RAN,
    PRI,
};

Abc_Ntk_t* rewrite ( Abc_Ntk_t* pNtk, MODE mode = SEQ );


struct WHY_Man {

    Abc_Ntk_t * pNtk;
    map<unsigned int, int> Rwr_Gain;
    map<unsigned int, bool> Rfr;
    map<unsigned int, int> Rsb_Gain;
};

/* Util.cpp */

WHY_Man * WHY_Start ( );
void WHY_ReadBlif ( WHY_Man * pMan, string Filename );
void WHY_Rewrite ( WHY_Man * pMan );
void WHY_PrintStats ( WHY_Man * pMan );
void WHY_WriteBlif ( WHY_Man * pMan, char * Filename );
void WHY_Stop ( WHY_Man * pMan );
#endif
