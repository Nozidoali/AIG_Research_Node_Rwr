#include "rewrite.h"
#include "resub.h"

/**
 * Initialize the manager
 */
WHY_Man * WHY_Start ( ) {
    // initilize ABC
    Abc_Start();
    WHY_Man * pMan = new WHY_Man;
    return pMan;
}

/**
 * Read the network from the given file
 */
void WHY_ReadBlif ( WHY_Man * pMan, string Filename ) {

    Abc_Frame_t * pAbc = Abc_FrameGetGlobalFrame();
    ostringstream command("");
    command << "read_blif " << Filename;
    Cmd_CommandExecute( pAbc, command.str().c_str() );
    Abc_Ntk_t * pNtk = Abc_NtkDup( Abc_FrameReadNtk(pAbc) );
    pMan->pNtk = Abc_NtkStrash( pNtk, 0, 1, 0 );
    assert ( Abc_NtkIsStrash( pMan->pNtk ) );
    Abc_NtkDelete(pNtk);
    // pMan->pAig = Abc_NtkToDar( pNtk, 0, 0 );
}

/**
 * Write the rewrite network
 */
void WHY_WriteBlif ( WHY_Man * pMan, char * Filename ) {
    // if no nerwork in the pNtkNew, we check the AigNew
    Abc_Ntk_t * pNtkNetlist = Abc_NtkToNetlist( pMan->pNtk );
    Io_WriteBlif( pNtkNetlist, Filename, 0, 0, 0 );
    Abc_NtkDelete( pNtkNetlist );
}

void WHY_PrintStats ( WHY_Man * pMan ) {
    // old size / old level
    cout << Abc_NtkNodeNum( pMan->pNtk ) << "," << Abc_NtkLevel( pMan->pNtk ) << ",";
}

/**
 * Release the memory allocated
 */
void WHY_Stop ( WHY_Man * pMan ) {
    Abc_NtkDelete( pMan->pNtk );
    // recycle memory
    Abc_Stop();
    pMan->Rfr_Gain.clear();
    pMan->Rsb_Gain.clear();
    pMan->Rwr_Gain.clear();
    delete pMan;
}

// evaluate
void WHY_EvalRewrite ( WHY_Man * pMan ) {

    // clean the old resuls
    pMan->Rwr_Gain.clear();
    // begin
    Abc_Ntk_t * pNtk = pMan->pNtk;
    ProgressBar * pProgress;
    Cut_Man_t * pManCut;
    Rwr_Man_t * pManRwr;
    Abc_Obj_t * pNode;
    Dec_Graph_t * pGraph;
    int i, nNodes, nGain, fCompl;
    abctime clk, clkStart = Abc_Clock();
    assert( Abc_NtkIsStrash(pNtk) );
    // cleanup the AIG
    Abc_AigCleanup((Abc_Aig_t *)pNtk->pManFunc);
    // start the rewriting manager
    pManRwr = Rwr_ManStart( 0 );
    if ( pManRwr == NULL )
        return;
    // compute the reverse levels if level update is requested
    Abc_NtkStartReverseLevels( pNtk, 0 );
    // start the cut manager
    pManCut = Abc_NtkStartCutManForRewrite( pNtk );
    pNtk->pManCut = pManCut;

    // resynthesize each node once
    pManRwr->nNodesBeg = Abc_NtkNodeNum(pNtk);
    nNodes = Abc_NtkObjNumMax(pNtk);
    pProgress = Extra_ProgressBarStart( stdout, nNodes );
    Abc_NtkForEachNode( pNtk, pNode, i )
    {
        Extra_ProgressBarUpdate( pProgress, i, NULL );
        // stop if all nodes have been tried once
        if ( i >= nNodes )
            break;
        // skip persistant nodes
        if ( Abc_NodeIsPersistant(pNode) )
            continue;
        // skip the nodes with many fanouts
        if ( Abc_ObjFanoutNum(pNode) > 1000 )
            continue;

        // for each cut, try to resynthesize it
        nGain = Rwr_NodeRewrite( pManRwr, pManCut, pNode, 1, 1, 0 );
        pMan->Rwr_Gain[ Abc_ObjId( pNode ) ] = nGain;
    }
    Extra_ProgressBarStop( pProgress );
    // print stats
    pManRwr->nNodesEnd = Abc_NtkNodeNum(pNtk);

    // delete the managers
    Rwr_ManStop( pManRwr );
    Cut_ManStop( pManCut );
    pNtk->pManCut = NULL;

    Abc_NtkReassignIds( pNtk );
    // fix the levels
    Abc_NtkStopReverseLevels( pNtk );
}
void WHY_EvalRefactor ( WHY_Man * pMan ) {
    // clean up
    pMan->Rfr_Gain.clear();
    // start
    Abc_Ntk_t * pNtk = pMan->pNtk;
    Abc_ManRef_t * pManRef;
    Abc_ManCut_t * pManCut;
    Dec_Graph_t * pFForm;
    Vec_Ptr_t * vFanins;
    Abc_Obj_t * pNode;
    int i, nNodes;

    assert( Abc_NtkIsStrash(pNtk) );
    // cleanup the AIG
    Abc_AigCleanup((Abc_Aig_t *)pNtk->pManFunc);
    // start the managers
    pManCut = Abc_NtkManCutStart( 10, 16, 2, 1000 );
    pManRef = Abc_NtkManRefStart( 10, 16, 1, 0 );
    pManRef->vLeaves   = Abc_NtkManCutReadCutLarge( pManCut );
    // compute the reverse levels if level update is requested
    Abc_NtkStartReverseLevels( pNtk, 0 );

    // resynthesize each node once
    pManRef->nNodesBeg = Abc_NtkNodeNum(pNtk);
    nNodes = Abc_NtkObjNumMax(pNtk);
    Abc_NtkForEachNode( pNtk, pNode, i )
    {

        // skip persistant nodes
        if ( Abc_NodeIsPersistant(pNode) )
            continue;
        // skip the nodes with many fanouts
        if ( Abc_ObjFanoutNum(pNode) > 1000 )
            continue;
        // stop if all nodes have been tried once
        if ( i >= nNodes )
            break;
        // compute a reconvergence-driven cut
        vFanins = Abc_NodeFindCut( pManCut, pNode, 1 );
        // evaluate this cut
        pFForm = Abc_NodeRefactor( pManRef, pNode, vFanins, 1, 1, 1, 0 );

        // record if a valid graph can be found
        pMan->Rfr_Gain[ Abc_ObjId( pNode ) ] = pManRef->nLastGain;
        if ( pFForm == NULL )
            continue;

        Dec_GraphFree( pFForm );
    }
    pManRef->nNodesEnd = Abc_NtkNodeNum(pNtk);
    // delete the managers
    Abc_NtkManCutStop( pManCut );
    Abc_NtkManRefStop( pManRef );
    // put the nodes into the DFS order and reassign their IDs
    Abc_NtkReassignIds( pNtk );
    // fix the levels
    Abc_NtkStopReverseLevels( pNtk );
}
void WHY_EvalResub ( WHY_Man * pMan ) {

     // clean up
     pMan->Rsb_Gain.clear();
     // start
     Abc_Ntk_t * pNtk = pMan->pNtk;
     Abc_ManRes_t * pManRes;
     Abc_ManCut_t * pManCut;
     Odc_Man_t * pManOdc = NULL;
     Dec_Graph_t * pFForm;
     Vec_Ptr_t * vLeaves;
     Abc_Obj_t * pNode;
     abctime clk, clkStart = Abc_Clock();
     int i, nNodes;

     assert( Abc_NtkIsStrash(pNtk) );
     // cleanup the AIG
     Abc_AigCleanup((Abc_Aig_t *)pNtk->pManFunc);
     // start the managers
     pManCut = Abc_NtkManCutStart( 8, 100000, 100000, 100000 );
     pManRes = Abc_ManResubStart( 8, 150 );

     // compute the reverse levels if level update is requested
     Abc_NtkStartReverseLevels( pNtk, 0 );

     if ( Abc_NtkLatchNum(pNtk) ) {
         Abc_NtkForEachLatch(pNtk, pNode, i)
         pNode->pNext = (Abc_Obj_t *)pNode->pData;
     }

     // resynthesize each node once
     pManRes->nNodesBeg = Abc_NtkNodeNum(pNtk);
     nNodes = Abc_NtkObjNumMax(pNtk);
     Abc_NtkForEachNode( pNtk, pNode, i )
     {
         // skip persistant nodes
         if ( Abc_NodeIsPersistant(pNode) )
             continue;
         // skip the nodes with many fanouts
         if ( Abc_ObjFanoutNum(pNode) > 1000 )
             continue;
         // stop if all nodes have been tried once
         if ( i >= nNodes )
             break;
         // compute a reconvergence-driven cut
         vLeaves = Abc_NodeFindCut( pManCut, pNode, 0 );

         // get the don't-cares
         if ( pManOdc )
         {
             Abc_NtkDontCareClear( pManOdc );
             Abc_NtkDontCareCompute( pManOdc, pNode, vLeaves, pManRes->pCareSet );
         }

         // evaluate this cut
         pFForm = Abc_ManResubEval( pManRes, pNode, vLeaves, 1, 1, 0 );

         //pMan->Rsb_Gain[ Abc_ObjId( pNode ) ] = pFForm != NULL;

         if ( pFForm == NULL )
             continue;
         pMan->Rsb_Gain[ Abc_ObjId( pNode ) ] = pManRes->nLastGain;

         Dec_GraphFree( pFForm );
     }
     pManRes->nNodesEnd = Abc_NtkNodeNum(pNtk);

     // delete the managers
     Abc_ManResubStop( pManRes );
     Abc_NtkManCutStop( pManCut );
     if ( pManOdc ) Abc_NtkDontCareFree( pManOdc );

     // clean the data field
     Abc_NtkForEachObj( pNtk, pNode, i )
         pNode->pData = NULL;

     // put the nodes into the DFS order and reassign their IDs
     Abc_NtkReassignIds( pNtk );
     // fix the levels
     Abc_NtkStopReverseLevels( pNtk );
 }

// udpate
void WHY_RunRewrite ( WHY_Man * pMan, bool (*func)(WHY_Man *, int) ) {
    extern void           Dec_GraphUpdateNetwork( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int fUpdateLevel, int nGain );
    ProgressBar * pProgress;
    Cut_Man_t * pManCut;
    Rwr_Man_t * pManRwr;
    Abc_Obj_t * pNode;
//    Vec_Ptr_t * vAddedCells = NULL, * vUpdatedNets = NULL;
    Dec_Graph_t * pGraph;
    int i, nNodes, nGain, fCompl;
    abctime clk, clkStart = Abc_Clock();

    Abc_Ntk_t * pNtk = pMan->pNtk;
    assert( Abc_NtkIsStrash(pNtk) );
    // cleanup the AIG
    Abc_AigCleanup((Abc_Aig_t *)pNtk->pManFunc);

    // start the rewriting manager
    pManRwr = Rwr_ManStart( 0 );
    if ( pManRwr == NULL )
        return;
    // compute the reverse levels if level update is requested
    Abc_NtkStartReverseLevels( pNtk, 0 );
    // start the cut manager
clk = Abc_Clock();
    pManCut = Abc_NtkStartCutManForRewrite( pNtk );
Rwr_ManAddTimeCuts( pManRwr, Abc_Clock() - clk );
    pNtk->pManCut = pManCut;

    // resynthesize each node once
    pManRwr->nNodesBeg = Abc_NtkNodeNum(pNtk);
    nNodes = Abc_NtkObjNumMax(pNtk);
    pProgress = Extra_ProgressBarStart( stdout, nNodes );
    Abc_NtkForEachNode( pNtk, pNode, i )
    {

        Extra_ProgressBarUpdate( pProgress, i, NULL );
        // stop if all nodes have been tried once
        if ( i >= nNodes )
            break;
        // skip persistant nodes
        if ( Abc_NodeIsPersistant(pNode) )
            continue;
        // skip the nodes with many fanouts
        if ( Abc_ObjFanoutNum(pNode) > 1000 )
            continue;
        // added by why
        if ( !func( pMan, Abc_ObjId( pNode ) ) )
            continue;
        // for each cut, try to resynthesize it
        nGain = Rwr_NodeRewrite( pManRwr, pManCut, pNode, 1, 1, 0 );
        if ( !(nGain > 0 || (nGain == 0 && 1)) )
            continue;
        // if we end up here, a rewriting step is accepted

        // get hold of the new subgraph to be added to the AIG
        pGraph = (Dec_Graph_t *)Rwr_ManReadDecs(pManRwr);
        fCompl = Rwr_ManReadCompl(pManRwr);
        // complement the FF if needed
        if ( fCompl ) Dec_GraphComplement( pGraph );
clk = Abc_Clock();
        Dec_GraphUpdateNetwork( pNode, pGraph, 1, nGain );
Rwr_ManAddTimeUpdate( pManRwr, Abc_Clock() - clk );
        if ( fCompl ) Dec_GraphComplement( pGraph );

    }
    Extra_ProgressBarStop( pProgress );
Rwr_ManAddTimeTotal( pManRwr, Abc_Clock() - clkStart );
    // print stats
    pManRwr->nNodesEnd = Abc_NtkNodeNum(pNtk);
    // delete the managers
    Rwr_ManStop( pManRwr );
    Cut_ManStop( pManCut );
    pNtk->pManCut = NULL;
    // put the nodes into the DFS order and reassign their IDs
    {
    Abc_NtkReassignIds( pNtk );
    }
    // fix the levels
    Abc_NtkStopReverseLevels( pNtk );
    // check
    if ( !Abc_NtkCheck( pNtk ) )
    {
        printf( "Abc_NtkRewrite: The network check has failed.\n" );
        return;
    }
    return;
}
void WHY_RunRefactor ( WHY_Man * pMan, bool (*func)(WHY_Man *, int) ) {
    extern void           Dec_GraphUpdateNetwork( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int fUpdateLevel, int nGain );
    
    Abc_Ntk_t * pNtk = pMan->pNtk;
    int nNodeSizeMax = 10;
    int nConeSizeMax = 16;
    int fUpdateLevel = 1;
    int fUseZeros = 1;
    int fUseDcs = 1;
    int fVerbose = 0;

    ProgressBar * pProgress;
    Abc_ManRef_t * pManRef;
    Abc_ManCut_t * pManCut;
    Dec_Graph_t * pFForm;
    Vec_Ptr_t * vFanins;
    Abc_Obj_t * pNode;
    abctime clk, clkStart = Abc_Clock();
    int i, nNodes;

    assert( Abc_NtkIsStrash(pNtk) );
    // cleanup the AIG
    Abc_AigCleanup((Abc_Aig_t *)pNtk->pManFunc);
    // start the managers
    pManCut = Abc_NtkManCutStart( nNodeSizeMax, nConeSizeMax, 2, 1000 );
    pManRef = Abc_NtkManRefStart( nNodeSizeMax, nConeSizeMax, fUseDcs, fVerbose );
    pManRef->vLeaves   = Abc_NtkManCutReadCutLarge( pManCut );
    // compute the reverse levels if level update is requested
    if ( fUpdateLevel )
        Abc_NtkStartReverseLevels( pNtk, 0 );

    // resynthesize each node once
    pManRef->nNodesBeg = Abc_NtkNodeNum(pNtk);
    nNodes = Abc_NtkObjNumMax(pNtk);
    pProgress = Extra_ProgressBarStart( stdout, nNodes );
    Abc_NtkForEachNode( pNtk, pNode, i )
    {
        Extra_ProgressBarUpdate( pProgress, i, NULL );
        // skip the constant node
//        if ( Abc_NodeIsConst(pNode) )
//            continue;
        // skip persistant nodes
        if ( Abc_NodeIsPersistant(pNode) )
            continue;
        // skip the nodes with many fanouts
        if ( Abc_ObjFanoutNum(pNode) > 1000 )
            continue;
        // added by why
        if ( !func( pMan, Abc_ObjId( pNode ) ) )
            continue;
        // stop if all nodes have been tried once
        if ( i >= nNodes )
            break;
        // compute a reconvergence-driven cut
clk = Abc_Clock();
        vFanins = Abc_NodeFindCut( pManCut, pNode, fUseDcs );
pManRef->timeCut += Abc_Clock() - clk;
        // evaluate this cut
clk = Abc_Clock();
        pFForm = Abc_NodeRefactor( pManRef, pNode, vFanins, fUpdateLevel, fUseZeros, fUseDcs, fVerbose );
pManRef->timeRes += Abc_Clock() - clk;
        if ( pFForm == NULL )
            continue;
        // acceptable replacement found, update the graph
clk = Abc_Clock();
        Dec_GraphUpdateNetwork( pNode, pFForm, fUpdateLevel, pManRef->nLastGain );
pManRef->timeNtk += Abc_Clock() - clk;
        Dec_GraphFree( pFForm );
    }
    Extra_ProgressBarStop( pProgress );
pManRef->timeTotal = Abc_Clock() - clkStart;
    pManRef->nNodesEnd = Abc_NtkNodeNum(pNtk);

    // delete the managers
    Abc_NtkManCutStop( pManCut );
    Abc_NtkManRefStop( pManRef );
    // put the nodes into the DFS order and reassign their IDs
    Abc_NtkReassignIds( pNtk );
//    Abc_AigCheckFaninOrder( pNtk->pManFunc );
    // fix the levels
    if ( fUpdateLevel )
        Abc_NtkStopReverseLevels( pNtk );
    else
        Abc_NtkLevel( pNtk );
    // check
    if ( !Abc_NtkCheck( pNtk ) )
    {
        printf( "Abc_NtkRefactor: The network check has failed.\n" );
        return;
    }
    return;
}
void WHY_RunResub ( WHY_Man * pMan, bool (*func)(WHY_Man *, int) ) {
    Abc_Ntk_t * pNtk = pMan->pNtk;
    int nCutMax = 8;
    int nStepsMax = 1;
    int nLevelsOdc = 0;
    int fUpdateLevel = 1;
    int fVerbose = 0;
    int fVeryVerbose = 0;

    extern void           Dec_GraphUpdateNetwork( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int fUpdateLevel, int nGain );
    ProgressBar * pProgress;
    Abc_ManRes_t * pManRes;
    Abc_ManCut_t * pManCut;
    Odc_Man_t * pManOdc = NULL;
    Dec_Graph_t * pFForm;
    Vec_Ptr_t * vLeaves;
    Abc_Obj_t * pNode;
    abctime clk, clkStart = Abc_Clock();
    int i, nNodes;

    assert( Abc_NtkIsStrash(pNtk) );

    // cleanup the AIG
    Abc_AigCleanup((Abc_Aig_t *)pNtk->pManFunc);
    // start the managers
    pManCut = Abc_NtkManCutStart( nCutMax, 100000, 100000, 100000 );
    pManRes = Abc_ManResubStart( nCutMax, ABC_RS_DIV1_MAX );
    if ( nLevelsOdc > 0 )
    pManOdc = Abc_NtkDontCareAlloc( nCutMax, nLevelsOdc, fVerbose, fVeryVerbose );

    // compute the reverse levels if level update is requested
    if ( fUpdateLevel )
        Abc_NtkStartReverseLevels( pNtk, 0 );

    if ( Abc_NtkLatchNum(pNtk) ) {
        Abc_NtkForEachLatch(pNtk, pNode, i)
            pNode->pNext = (Abc_Obj_t *)pNode->pData;
    }

    // resynthesize each node once
    pManRes->nNodesBeg = Abc_NtkNodeNum(pNtk);
    nNodes = Abc_NtkObjNumMax(pNtk);
    pProgress = Extra_ProgressBarStart( stdout, nNodes );
    Abc_NtkForEachNode( pNtk, pNode, i )
    {
        Extra_ProgressBarUpdate( pProgress, i, NULL );
        // skip the constant node
//        if ( Abc_NodeIsConst(pNode) )
//            continue;
        // skip persistant nodes
        if ( Abc_NodeIsPersistant(pNode) )
            continue;
        // skip the nodes with many fanouts
        if ( Abc_ObjFanoutNum(pNode) > 1000 )
            continue;
        // added by why
        if ( !func( pMan, Abc_ObjId( pNode ) ) )
            continue;
        // stop if all nodes have been tried once
        if ( i >= nNodes )
            break;

        // compute a reconvergence-driven cut
clk = Abc_Clock();
        vLeaves = Abc_NodeFindCut( pManCut, pNode, 0 );
//        vLeaves = Abc_CutFactorLarge( pNode, nCutMax );
pManRes->timeCut += Abc_Clock() - clk;
/*
        if ( fVerbose && vLeaves )
        printf( "Node %6d : Leaves = %3d. Volume = %3d.\n", pNode->Id, Vec_PtrSize(vLeaves), Abc_CutVolumeCheck(pNode, vLeaves) );
        if ( vLeaves == NULL )
            continue;
*/
        // get the don't-cares
        if ( pManOdc )
        {
clk = Abc_Clock();
            Abc_NtkDontCareClear( pManOdc );
            Abc_NtkDontCareCompute( pManOdc, pNode, vLeaves, pManRes->pCareSet );
pManRes->timeTruth += Abc_Clock() - clk;
        }

        // evaluate this cut
clk = Abc_Clock();
        pFForm = Abc_ManResubEval( pManRes, pNode, vLeaves, nStepsMax, fUpdateLevel, fVerbose );
//        Vec_PtrFree( vLeaves );
//        Abc_ManResubCleanup( pManRes );
pManRes->timeRes += Abc_Clock() - clk;
        if ( pFForm == NULL )
            continue;
        pManRes->nTotalGain += pManRes->nLastGain;
/*
        if ( pManRes->nLeaves == 4 && pManRes->nMffc == 2 && pManRes->nLastGain == 1 )
        {
            printf( "%6d :  L = %2d. V = %2d. Mffc = %2d. Divs = %3d.   Up = %3d. Un = %3d. B = %3d.\n", 
                   pNode->Id, pManRes->nLeaves, Abc_CutVolumeCheck(pNode, vLeaves), pManRes->nMffc, pManRes->nDivs, 
                   pManRes->vDivs1UP->nSize, pManRes->vDivs1UN->nSize, pManRes->vDivs1B->nSize );
            Abc_ManResubPrintDivs( pManRes, pNode, vLeaves );
        }
*/
        // acceptable replacement found, update the graph
clk = Abc_Clock();
        Dec_GraphUpdateNetwork( pNode, pFForm, fUpdateLevel, pManRes->nLastGain );
pManRes->timeNtk += Abc_Clock() - clk;
        Dec_GraphFree( pFForm );
    }
    Extra_ProgressBarStop( pProgress );
pManRes->timeTotal = Abc_Clock() - clkStart;
    pManRes->nNodesEnd = Abc_NtkNodeNum(pNtk);

    // delete the managers
    Abc_ManResubStop( pManRes );
    Abc_NtkManCutStop( pManCut );
    if ( pManOdc ) Abc_NtkDontCareFree( pManOdc );

    // clean the data field
    Abc_NtkForEachObj( pNtk, pNode, i )
        pNode->pData = NULL;

    if ( Abc_NtkLatchNum(pNtk) ) {
        Abc_NtkForEachLatch(pNtk, pNode, i)
            pNode->pData = pNode->pNext, pNode->pNext = NULL;
    }

    // put the nodes into the DFS order and reassign their IDs
    Abc_NtkReassignIds( pNtk );
//    Abc_AigCheckFaninOrder( pNtk->pManFunc );
    // fix the levels
    if ( fUpdateLevel )
        Abc_NtkStopReverseLevels( pNtk );
    else
        Abc_NtkLevel( pNtk );
    // check
    if ( !Abc_NtkCheck( pNtk ) )
    {
        printf( "Abc_NtkRefactor: The network check has failed.\n" );
        return;
    }
    return;
}

void WHY_Operate ( WHY_Man * pMan ) {
    int verbose = 1;

    // Evaluate
    WHY_EvalRefactor( pMan );
    WHY_EvalRewrite( pMan );
    WHY_EvalResub( pMan );

    if ( verbose ) {

        // general info
        cerr << "Before: " << endl;
        cerr << "\tNode: " << Abc_NtkNodeNum( pMan->pNtk ) << endl;

        // info for rewrite
        map<unsigned int, int>::iterator entry;
        int count = 0;
        int sum = 0;

        for (entry = pMan->Rwr_Gain.begin(); entry != pMan->Rwr_Gain.end(); entry++) {
            sum += max(entry->second,0);
            count += (entry->second > 0);
        }

        cerr << "Rewrite:" << endl;
        cerr << "\tTotal Gain: " << sum << endl;
        cerr << "\tTotal Node: " << count << endl;

        // info for refactor
        sum = 0;
        count = 0;

        for (entry = pMan->Rfr_Gain.begin(); entry != pMan->Rfr_Gain.end(); entry++ ) {
            sum += max(entry->second,0);
            count += (entry->second > 0);
        }
        cerr << "Refractor:" << endl;
        cerr << "\tTotal Gain: " << sum << endl;
        cerr << "\tTotal Node: " << count << endl;

        // info for resub
        sum = 0;
        count = 0;

        for (entry = pMan->Rsb_Gain.begin(); entry != pMan->Rsb_Gain.end(); entry++) {
            sum += max(entry->second,0);
            count += (entry->second > 0);
        }

        cerr << "Resub:" << endl;
        cerr << "\tTotal Gain: " << sum << endl;
        cerr << "\tTotal Node: " << count << endl;

        auto Rwr = [] ( WHY_Man * pMan, int id ) {
            return true;
        };
        auto Rfr = [] ( WHY_Man * pMan, int id ) {
            if ( pMan->Rwr_Gain[id] < 1 || pMan->Rfr_Gain[id] > pMan->Rwr_Gain[id] )
                return true;
            return false;
        };
        auto Rsb = [] ( WHY_Man * pMan, int id ) {
            if ( pMan->Rwr_Gain[id] < 1 || pMan->Rsb_Gain[id] > pMan->Rfr_Gain[id] )
                return true;
            return false;
        };

        // operate
        WHY_RunResub    ( pMan, Rsb );
        WHY_RunRefactor ( pMan, Rfr );
        WHY_RunRewrite  ( pMan, Rwr );

        // general info
        cerr << "After: " << endl;
        cerr << "\tNode: " << Abc_NtkNodeNum( pMan->pNtk ) << endl;

    }


}
