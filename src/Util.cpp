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
    pMan->Rfr.clear();
    pMan->Rwr_Gain.clear();
    delete pMan;
}


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
    pMan->Rfr.clear();
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
        pMan->Rfr[ Abc_ObjId( pNode ) ] = pFForm != NULL;
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

void WHY_Rewrite ( WHY_Man * pMan ) {
    int verbose = 1;

    // Evaluate
    WHY_EvalRefactor( pMan );
    WHY_EvalRewrite( pMan );
    WHY_EvalResub( pMan );
    if ( verbose ) {

        int count = 0;
        int sum = 0;

        // general info
        cerr << "Network: " << endl;
        cerr << "\tNode: " << Abc_NtkNodeNum( pMan->pNtk ) << endl;

        // info for rewrite
        map<unsigned int, int>::iterator entry;

        for (entry = pMan->Rwr_Gain.begin(); entry != pMan->Rwr_Gain.end(); entry++) {
            sum += max(entry->second,0);
            count += (entry->second > 0);
        }


        //map<int, int>::iterator iter;
    	//for(iter = _map.begin(); iter != _map.end(); iter++) {
        //	cout << iter->first << " : " << iter->second << endl;
    	//}

        cerr << "map length: " << pMan->Rwr_Gain.size() << endl;
        cerr << "Rewrite:" << endl;
        cerr << "\tTotal Gain: " << sum << endl;
        cerr << "\tTotal Node: " << count << endl;

        count = 0;
        map<unsigned int, bool>::iterator entry2;
        // info for refactor
        for (entry2 = pMan->Rfr.begin(); entry2 != pMan->Rfr.end(); entry2++ ) {
            count += entry2->second;
        }
        cerr << "Refractor:" << endl;
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
    }


}
