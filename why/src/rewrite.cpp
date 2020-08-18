#include "rewrite.h"

extern "C" Vec_Int_t * Saig_ManComputeSwitchProbs( Aig_Man_t * p, int nFrames, int nPref, int fProbOne );
int RAN_ManRewrite( Aig_Man_t * pAig, Dar_RwrPar_t * pPars )
{

    Dar_Man_t * p;
//    Bar_Progress_t * pProgress;
    Dar_Cut_t * pCut;
    Aig_Obj_t * pObj, * pObjNew;
    int i, k, nNodesOld, nNodeBefore, nNodeAfter, Required;
    abctime clk = 0, clkStart;
    int Counter = 0;
    int nMffcSize;//, nMffcGains[MAX_VAL+1][MAX_VAL+1] = {{0}};
    // prepare the library
    Dar_LibPrepare( pPars->nSubgMax );
    // create rewriting manager
    p = Dar_ManStart( pAig, pPars );
    if ( pPars->fPower )
        pAig->vProbs = Saig_ManComputeSwitchProbs( pAig, 48, 16, 1 );
    // remove dangling nodes
    Aig_ManCleanup( pAig );
    // if updating levels is requested, start fanout and timing
    if ( p->pPars->fFanout )
        Aig_ManFanoutStart( pAig );
    if ( p->pPars->fUpdateLevel )
        Aig_ManStartReverseLevels( pAig, 0 );
    // set elementary cuts for the PIs
//    Dar_ManCutsStart( p );
    // resynthesize each node once
    clkStart = Abc_Clock();
    p->nNodesInit = Aig_ManNodeNum(pAig);
    nNodesOld = Vec_PtrSize( pAig->vObjs );

//    pProgress = Bar_ProgressStart( stdout, nNodesOld );
//    pProgress = Bar_ProgressStart( stdout, 100 );
//    Aig_ManOrderStart( pAig );
//    Aig_ManForEachNodeInOrder( pAig, pObj )

    vector<Aig_Obj_t *> vec;
    Aig_ManForEachObj( pAig, pObj, i ) {
        vec.push_back( pObj );
    }
    srand( time(NULL) );
    random_shuffle( vec.begin(), vec.end() );

    for ( int i=0;i<vec.size();i++ ) {
        pObj = vec[i];
    // Aig_ManForEachObj( pAig, pObj, i )
    // {
        if ( pAig->Time2Quit && !(i & 256) && Abc_Clock() > pAig->Time2Quit )
            break;
//        Bar_ProgressUpdate( pProgress, 100*pAig->nAndPrev/pAig->nAndTotal, NULL );
//        Bar_ProgressUpdate( pProgress, i, NULL );
        if ( !Aig_ObjIsNode(pObj) )
            continue;
        if ( i > nNodesOld )
//        if ( p->pPars->fUseZeros && i > nNodesOld )
            break;
        if ( pPars->fRecycle && ++Counter % 50000 == 0 && Aig_DagSize(pObj) < Vec_PtrSize(p->vCutNodes)/100 )
        {
//            printf( "Counter = %7d.  Node = %7d.  Dag = %5d. Vec = %5d.\n",
//                Counter, i, Aig_DagSize(pObj), Vec_PtrSize(p->vCutNodes) );
//            fflush( stdout );
            Dar_ManCutsRestart( p, pObj );
        }

        // consider freeing the cuts
//        if ( (i & 0xFFF) == 0 && Aig_MmFixedReadMemUsage(p->pMemCuts)/(1<<20) > 100 )
//            Dar_ManCutsStart( p );

        // compute cuts for the node
        p->nNodesTried++;
clk = Abc_Clock();
        Dar_ObjSetCuts( pObj, NULL );
        Dar_ObjComputeCuts_rec( p, pObj );
p->timeCuts += Abc_Clock() - clk;

        // check if there is a trivial cut
        Dar_ObjForEachCut( pObj, pCut, k )
            if ( pCut->nLeaves == 0 || (pCut->nLeaves == 1 && pCut->pLeaves[0] != pObj->Id && Aig_ManObj(p->pAig, pCut->pLeaves[0])) )
                break;
        if ( k < (int)pObj->nCuts )
        {
            assert( pCut->nLeaves < 2 );
            if ( pCut->nLeaves == 0 ) // replace by constant
            {
                assert( pCut->uTruth == 0 || pCut->uTruth == 0xFFFF );
                pObjNew = Aig_NotCond( Aig_ManConst1(p->pAig), pCut->uTruth==0 );
            }
            else
            {
                assert( pCut->uTruth == 0xAAAA || pCut->uTruth == 0x5555 );
                pObjNew = Aig_NotCond( Aig_ManObj(p->pAig, pCut->pLeaves[0]), pCut->uTruth==0x5555 );
            }
            // remove the old cuts
            Dar_ObjSetCuts( pObj, NULL );
            // replace the node
            Aig_ObjReplace( pAig, pObj, pObjNew, p->pPars->fUpdateLevel );
            continue;
        }

        // evaluate the cuts
        p->GainBest = -1;
        nMffcSize   = -1;
        Required    = pAig->vLevelR? Aig_ObjRequiredLevel(pAig, pObj) : ABC_INFINITY;
        Dar_ObjForEachCut( pObj, pCut, k )
        {
            int nLeavesOld = pCut->nLeaves;
            if ( pCut->nLeaves == 3 )
                pCut->pLeaves[pCut->nLeaves++] = 0;
            Dar_LibEval( p, pObj, pCut, Required, &nMffcSize );
            pCut->nLeaves = nLeavesOld;
        }
        // check the best gain
        if ( !(p->GainBest > 0 || (p->GainBest == 0 && p->pPars->fUseZeros)) )
        {
//            Aig_ObjOrderAdvance( pAig );
            continue;
        }
//        nMffcGains[p->GainBest < MAX_VAL ? p->GainBest : MAX_VAL][nMffcSize < MAX_VAL ? nMffcSize : MAX_VAL]++;
        // remove the old cuts
        Dar_ObjSetCuts( pObj, NULL );
        // if we end up here, a rewriting step is accepted
        nNodeBefore = Aig_ManNodeNum( pAig );
        pObjNew = Dar_LibBuildBest( p ); // pObjNew can be complemented!
        pObjNew = Aig_NotCond( pObjNew, Aig_ObjPhaseReal(pObjNew) ^ pObj->fPhase );
        assert( (int)Aig_Regular(pObjNew)->Level <= Required );
        // replace the node
        Aig_ObjReplace( pAig, pObj, pObjNew, p->pPars->fUpdateLevel );
        // compare the gains
        nNodeAfter = Aig_ManNodeNum( pAig );
        assert( p->GainBest <= nNodeBefore - nNodeAfter );
        // count gains of this class
        p->ClassGains[p->ClassBest] += nNodeBefore - nNodeAfter;
    }
//    Aig_ManOrderStop( pAig );
/*
    printf( "Distribution of gain (row) by MFFC size (column) %s 0-costs:\n", p->pPars->fUseZeros? "with":"without" );
    for ( k = 0; k <= MAX_VAL; k++ )
        printf( "<%4d> ", k );
    printf( "\n" );
    for ( i = 0; i <= MAX_VAL; i++ )
    {
        for ( k = 0; k <= MAX_VAL; k++ )
            printf( "%6d ", nMffcGains[i][k] );
        printf( "\n" );
    }
*/

p->timeTotal = Abc_Clock() - clkStart;
p->timeOther = p->timeTotal - p->timeCuts - p->timeEval;

//    Bar_ProgressStop( pProgress );
    Dar_ManCutsFree( p );
    // put the nodes into the DFS order and reassign their IDs
//    Aig_NtkReassignIds( p );
    // fix the levels
//    Aig_ManVerifyLevel( pAig );
    if ( p->pPars->fFanout )
        Aig_ManFanoutStop( pAig );
    if ( p->pPars->fUpdateLevel )
    {
//        Aig_ManVerifyReverseLevel( pAig );
        Aig_ManStopReverseLevels( pAig );
    }
    if ( pAig->vProbs )
    {
        Vec_IntFree( pAig->vProbs );
        pAig->vProbs = NULL;
    }
    // stop the rewriting manager
    Dar_ManStop( p );
    Aig_ManCheckPhase( pAig );
    // check
    if ( !Aig_ManCheck( pAig ) )
    {
        printf( "Aig_ManRewrite: The network check has failed.\n" );
        return 0;
    }
    return 1;
}
/*
int PRI_ManRewrite( Aig_Man_t * pAig, Dar_RwrPar_t * pPars )
{

    Dar_Man_t * p;
//    Bar_Progress_t * pProgress;
    Dar_Cut_t * pCut;
    Aig_Obj_t * pObj, * pObjNew;
    int i, k, nNodesOld, nNodeBefore, nNodeAfter, Required;
    abctime clk = 0, clkStart;
    int Counter = 0;
    int nMffcSize;//, nMffcGains[MAX_VAL+1][MAX_VAL+1] = {{0}};
    // prepare the library
    Dar_LibPrepare( pPars->nSubgMax );
    // create rewriting manager
    p = Dar_ManStart( pAig, pPars );
    if ( pPars->fPower )
        pAig->vProbs = Saig_ManComputeSwitchProbs( pAig, 48, 16, 1 );
    // remove dangling nodes
    Aig_ManCleanup( pAig );
    // if updating levels is requested, start fanout and timing
    if ( p->pPars->fFanout )
        Aig_ManFanoutStart( pAig );
    if ( p->pPars->fUpdateLevel )
        Aig_ManStartReverseLevels( pAig, 0 );
    // set elementary cuts for the PIs
//    Dar_ManCutsStart( p );
    // resynthesize each node once
    clkStart = Abc_Clock();
    p->nNodesInit = Aig_ManNodeNum(pAig);
    nNodesOld = Vec_PtrSize( pAig->vObjs );

//    pProgress = Bar_ProgressStart( stdout, nNodesOld );
//    pProgress = Bar_ProgressStart( stdout, 100 );
//    Aig_ManOrderStart( pAig );
//    Aig_ManForEachNodeInOrder( pAig, pObj )

    struct Node
    {
        Aig_Obj_t * pObj;
        int gain;
        Node ( Aig_Obj_t * _pObj, int _gain ) : pObj(_pObj), gain(_gain) {}
        bool operator < ( const Node x ) const {
            return gain > x.gain;
        }
    };

    priority_queue<Node> vec;
    Aig_ManForEachObj( pAig, pObj, i ) {
        if ( !Aig_ObjIsNode(pObj) )
            continue;
        if ( pPars->fRecycle && ++Counter % 50000 == 0 && Aig_DagSize(pObj) < Vec_PtrSize(p->vCutNodes)/100 )
        {
//            printf( "Counter = %7d.  Node = %7d.  Dag = %5d. Vec = %5d.\n",
//                Counter, i, Aig_DagSize(pObj), Vec_PtrSize(p->vCutNodes) );
//            fflush( stdout );
            Dar_ManCutsRestart( p, pObj );
        }

        // consider freeing the cuts
//        if ( (i & 0xFFF) == 0 && Aig_MmFixedReadMemUsage(p->pMemCuts)/(1<<20) > 100 )
//            Dar_ManCutsStart( p );

        // compute cuts for the node
        p->nNodesTried++;
clk = Abc_Clock();
        Dar_ObjSetCuts( pObj, NULL );
        Dar_ObjComputeCuts_rec( p, pObj );
p->timeCuts += Abc_Clock() - clk;

        // check if there is a trivial cut
        Dar_ObjForEachCut( pObj, pCut, k )
            if ( pCut->nLeaves == 0 || (pCut->nLeaves == 1 && pCut->pLeaves[0] != pObj->Id && Aig_ManObj(p->pAig, pCut->pLeaves[0])) )
                break;
        if ( k < (int)pObj->nCuts )
        {
            assert( pCut->nLeaves < 2 );
            if ( pCut->nLeaves == 0 ) // replace by constant
            {
                assert( pCut->uTruth == 0 || pCut->uTruth == 0xFFFF );
                pObjNew = Aig_NotCond( Aig_ManConst1(p->pAig), pCut->uTruth==0 );
            }
            else
            {
                assert( pCut->uTruth == 0xAAAA || pCut->uTruth == 0x5555 );
                pObjNew = Aig_NotCond( Aig_ManObj(p->pAig, pCut->pLeaves[0]), pCut->uTruth==0x5555 );
            }
            // remove the old cuts
            Dar_ObjSetCuts( pObj, NULL );
            // replace the node
            Aig_ObjReplace( pAig, pObj, pObjNew, p->pPars->fUpdateLevel );
            continue;
        }

        // evaluate the cuts
        p->GainBest = -1;
        nMffcSize   = -1;
        Required    = pAig->vLevelR? Aig_ObjRequiredLevel(pAig, pObj) : ABC_INFINITY;
        Dar_ObjForEachCut( pObj, pCut, k )
        {
            int nLeavesOld = pCut->nLeaves;
            if ( pCut->nLeaves == 3 )
                pCut->pLeaves[pCut->nLeaves++] = 0;
            Dar_LibEval( p, pObj, pCut, Required, &nMffcSize );
            pCut->nLeaves = nLeavesOld;
        }
        int gain = p->GainBest;
        vec.push( Node( pObj, gain ) );
    }

    while ( !vec.empty() ) {
        pObj = vec.top().pObj;
        vec.pop();
    // Aig_ManForEachObj( pAig, pObj, i )
    // {
        if ( pAig->Time2Quit && !(i & 256) && Abc_Clock() > pAig->Time2Quit )
            break;
//        Bar_ProgressUpdate( pProgress, 100*pAig->nAndPrev/pAig->nAndTotal, NULL );
//        Bar_ProgressUpdate( pProgress, i, NULL );
        if ( !Aig_ObjIsNode(pObj) )
            continue;
        if ( i > nNodesOld )
//        if ( p->pPars->fUseZeros && i > nNodesOld )
            break;
        if ( pPars->fRecycle && ++Counter % 50000 == 0 && Aig_DagSize(pObj) < Vec_PtrSize(p->vCutNodes)/100 )
        {
//            printf( "Counter = %7d.  Node = %7d.  Dag = %5d. Vec = %5d.\n",
//                Counter, i, Aig_DagSize(pObj), Vec_PtrSize(p->vCutNodes) );
//            fflush( stdout );
            Dar_ManCutsRestart( p, pObj );
        }

        // consider freeing the cuts
//        if ( (i & 0xFFF) == 0 && Aig_MmFixedReadMemUsage(p->pMemCuts)/(1<<20) > 100 )
//            Dar_ManCutsStart( p );

        // compute cuts for the node
        p->nNodesTried++;
clk = Abc_Clock();
        Dar_ObjSetCuts( pObj, NULL );
        Dar_ObjComputeCuts_rec( p, pObj );
p->timeCuts += Abc_Clock() - clk;

        // check if there is a trivial cut
        Dar_ObjForEachCut( pObj, pCut, k )
            if ( pCut->nLeaves == 0 || (pCut->nLeaves == 1 && pCut->pLeaves[0] != pObj->Id && Aig_ManObj(p->pAig, pCut->pLeaves[0])) )
                break;
        if ( k < (int)pObj->nCuts )
        {
            assert( pCut->nLeaves < 2 );
            if ( pCut->nLeaves == 0 ) // replace by constant
            {
                assert( pCut->uTruth == 0 || pCut->uTruth == 0xFFFF );
                pObjNew = Aig_NotCond( Aig_ManConst1(p->pAig), pCut->uTruth==0 );
            }
            else
            {
                assert( pCut->uTruth == 0xAAAA || pCut->uTruth == 0x5555 );
                pObjNew = Aig_NotCond( Aig_ManObj(p->pAig, pCut->pLeaves[0]), pCut->uTruth==0x5555 );
            }
            // remove the old cuts
            Dar_ObjSetCuts( pObj, NULL );
            // replace the node
            Aig_ObjReplace( pAig, pObj, pObjNew, p->pPars->fUpdateLevel );
            continue;
        }

        // evaluate the cuts
        p->GainBest = -1;
        nMffcSize   = -1;
        Required    = pAig->vLevelR? Aig_ObjRequiredLevel(pAig, pObj) : ABC_INFINITY;
        Dar_ObjForEachCut( pObj, pCut, k )
        {
            int nLeavesOld = pCut->nLeaves;
            if ( pCut->nLeaves == 3 )
                pCut->pLeaves[pCut->nLeaves++] = 0;
            Dar_LibEval( p, pObj, pCut, Required, &nMffcSize );
            pCut->nLeaves = nLeavesOld;
        }
        // check the best gain
        if ( !(p->GainBest > 0 || (p->GainBest == 0 && p->pPars->fUseZeros)) )
        {
//            Aig_ObjOrderAdvance( pAig );
            continue;
        }
//        nMffcGains[p->GainBest < MAX_VAL ? p->GainBest : MAX_VAL][nMffcSize < MAX_VAL ? nMffcSize : MAX_VAL]++;
        // remove the old cuts
        Dar_ObjSetCuts( pObj, NULL );
        // if we end up here, a rewriting step is accepted
        nNodeBefore = Aig_ManNodeNum( pAig );
        pObjNew = Dar_LibBuildBest( p ); // pObjNew can be complemented!
        pObjNew = Aig_NotCond( pObjNew, Aig_ObjPhaseReal(pObjNew) ^ pObj->fPhase );
        assert( (int)Aig_Regular(pObjNew)->Level <= Required );
        // replace the node
        Aig_ObjReplace( pAig, pObj, pObjNew, p->pPars->fUpdateLevel );
        // compare the gains
        nNodeAfter = Aig_ManNodeNum( pAig );
        assert( p->GainBest <= nNodeBefore - nNodeAfter );
        // count gains of this class
        p->ClassGains[p->ClassBest] += nNodeBefore - nNodeAfter;
    }
//    Aig_ManOrderStop( pAig );


p->timeTotal = Abc_Clock() - clkStart;
p->timeOther = p->timeTotal - p->timeCuts - p->timeEval;

//    Bar_ProgressStop( pProgress );
    Dar_ManCutsFree( p );
    // put the nodes into the DFS order and reassign their IDs
//    Aig_NtkReassignIds( p );
    // fix the levels
//    Aig_ManVerifyLevel( pAig );
    if ( p->pPars->fFanout )
        Aig_ManFanoutStop( pAig );
    if ( p->pPars->fUpdateLevel )
    {
//        Aig_ManVerifyReverseLevel( pAig );
        Aig_ManStopReverseLevels( pAig );
    }
    if ( pAig->vProbs )
    {
        Vec_IntFree( pAig->vProbs );
        pAig->vProbs = NULL;
    }
    // stop the rewriting manager
    Dar_ManStop( p );
    Aig_ManCheckPhase( pAig );
    // check
    if ( !Aig_ManCheck( pAig ) )
    {
        printf( "Aig_ManRewrite: The network check has failed.\n" );
        return 0;
    }
    return 1;
}
*/
Aig_Man_t * RAN_ManCompress2( Aig_Man_t * pAig, int fBalance, int fUpdateLevel, int fFanout, int fPower, int fVerbose )
//alias compress2   "b -l; rw -l; rf -l; b -l; rw -l; rwz -l; b -l; rfz -l; rwz -l; b -l"
{
    Aig_Man_t * pTemp;

    Dar_RwrPar_t ParsRwr, * pParsRwr = &ParsRwr;
    Dar_RefPar_t ParsRef, * pParsRef = &ParsRef;

    Dar_ManDefaultRwrParams( pParsRwr );
    Dar_ManDefaultRefParams( pParsRef );

    pParsRwr->fUpdateLevel = fUpdateLevel;
    pParsRef->fUpdateLevel = fUpdateLevel;
    pParsRwr->fFanout = fFanout;
    pParsRwr->fPower = fPower;

    pParsRwr->fVerbose = 0;//fVerbose;
    pParsRef->fVerbose = 0;//fVerbose;

    pAig = Aig_ManDupDfs( pAig );
    if ( fVerbose ) printf( "Starting:  " ), Aig_ManPrintStats( pAig );
/*
    // balance
    if ( fBalance )
    {
    pAig = Dar_ManBalance( pTemp = pAig, fUpdateLevel );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "Balance:   " ), Aig_ManPrintStats( pAig );
    }
*/
    // rewrite
//    Dar_ManRewrite( pAig, pParsRwr );
    pParsRwr->fUpdateLevel = 0;  // disable level update
    // Dar_ManRewrite( pAig, pParsRwr );
    RAN_ManRewrite( pAig, pParsRwr );
    pParsRwr->fUpdateLevel = fUpdateLevel;  // reenable level update if needed

    pAig = Aig_ManDupDfs( pTemp = pAig );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "Rewrite:   " ), Aig_ManPrintStats( pAig );

    // refactor
    Dar_ManRefactor( pAig, pParsRef );
    pAig = Aig_ManDupDfs( pTemp = pAig );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "Refactor:  " ), Aig_ManPrintStats( pAig );

    // balance
//    if ( fBalance )
    {
    pAig = Dar_ManBalance( pTemp = pAig, fUpdateLevel );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "Balance:   " ), Aig_ManPrintStats( pAig );
    }

    // rewrite
    // Dar_ManRewrite( pAig, pParsRwr );
    RAN_ManRewrite( pAig, pParsRwr );
    pAig = Aig_ManDupDfs( pTemp = pAig );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "Rewrite:   " ), Aig_ManPrintStats( pAig );

    pParsRwr->fUseZeros = 1;
    pParsRef->fUseZeros = 1;

    // rewrite
    // Dar_ManRewrite( pAig, pParsRwr );
    RAN_ManRewrite( pAig, pParsRwr );
    pAig = Aig_ManDupDfs( pTemp = pAig );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "RewriteZ:  " ), Aig_ManPrintStats( pAig );

    // balance
    if ( fBalance )
    {
    pAig = Dar_ManBalance( pTemp = pAig, fUpdateLevel );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "Balance:   " ), Aig_ManPrintStats( pAig );
    }

    // refactor
    Dar_ManRefactor( pAig, pParsRef );
    pAig = Aig_ManDupDfs( pTemp = pAig );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "RefactorZ: " ), Aig_ManPrintStats( pAig );

    // rewrite
    // Dar_ManRewrite( pAig, pParsRwr );
    RAN_ManRewrite( pAig, pParsRwr );
    pAig = Aig_ManDupDfs( pTemp = pAig );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "RewriteZ:  " ), Aig_ManPrintStats( pAig );

    // balance
    if ( fBalance )
    {
    pAig = Dar_ManBalance( pTemp = pAig, fUpdateLevel );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "Balance:   " ), Aig_ManPrintStats( pAig );
    }
    return pAig;
}
/*
Aig_Man_t * PRI_ManCompress2( Aig_Man_t * pAig, int fBalance, int fUpdateLevel, int fFanout, int fPower, int fVerbose )
//alias compress2   "b -l; rw -l; rf -l; b -l; rw -l; rwz -l; b -l; rfz -l; rwz -l; b -l"
{
    Aig_Man_t * pTemp;

    Dar_RwrPar_t ParsRwr, * pParsRwr = &ParsRwr;
    Dar_RefPar_t ParsRef, * pParsRef = &ParsRef;

    Dar_ManDefaultRwrParams( pParsRwr );
    Dar_ManDefaultRefParams( pParsRef );

    pParsRwr->fUpdateLevel = fUpdateLevel;
    pParsRef->fUpdateLevel = fUpdateLevel;
    pParsRwr->fFanout = fFanout;
    pParsRwr->fPower = fPower;

    pParsRwr->fVerbose = 0;//fVerbose;
    pParsRef->fVerbose = 0;//fVerbose;

    pAig = Aig_ManDupDfs( pAig );
    if ( fVerbose ) printf( "Starting:  " ), Aig_ManPrintStats( pAig );
    // rewrite
//    Dar_ManRewrite( pAig, pParsRwr );
    pParsRwr->fUpdateLevel = 0;  // disable level update
    // Dar_ManRewrite( pAig, pParsRwr );
    PRI_ManRewrite( pAig, pParsRwr );
    pParsRwr->fUpdateLevel = fUpdateLevel;  // reenable level update if needed

    pAig = Aig_ManDupDfs( pTemp = pAig );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "Rewrite:   " ), Aig_ManPrintStats( pAig );

    // refactor
    Dar_ManRefactor( pAig, pParsRef );
    pAig = Aig_ManDupDfs( pTemp = pAig );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "Refactor:  " ), Aig_ManPrintStats( pAig );

    // balance
//    if ( fBalance )
    {
    pAig = Dar_ManBalance( pTemp = pAig, fUpdateLevel );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "Balance:   " ), Aig_ManPrintStats( pAig );
    }

    // rewrite
    // Dar_ManRewrite( pAig, pParsRwr );
    PRI_ManRewrite( pAig, pParsRwr );
    pAig = Aig_ManDupDfs( pTemp = pAig );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "Rewrite:   " ), Aig_ManPrintStats( pAig );

    pParsRwr->fUseZeros = 1;
    pParsRef->fUseZeros = 1;

    // rewrite
    // Dar_ManRewrite( pAig, pParsRwr );
    PRI_ManRewrite( pAig, pParsRwr );
    pAig = Aig_ManDupDfs( pTemp = pAig );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "RewriteZ:  " ), Aig_ManPrintStats( pAig );

    // balance
    if ( fBalance )
    {
    pAig = Dar_ManBalance( pTemp = pAig, fUpdateLevel );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "Balance:   " ), Aig_ManPrintStats( pAig );
    }

    // refactor
    Dar_ManRefactor( pAig, pParsRef );
    pAig = Aig_ManDupDfs( pTemp = pAig );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "RefactorZ: " ), Aig_ManPrintStats( pAig );

    // rewrite
    // Dar_ManRewrite( pAig, pParsRwr );
    PRI_ManRewrite( pAig, pParsRwr );
    pAig = Aig_ManDupDfs( pTemp = pAig );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "RewriteZ:  " ), Aig_ManPrintStats( pAig );

    // balance
    if ( fBalance )
    {
    pAig = Dar_ManBalance( pTemp = pAig, fUpdateLevel );
    Aig_ManStop( pTemp );
    if ( fVerbose ) printf( "Balance:   " ), Aig_ManPrintStats( pAig );
    }
    return pAig;
}
*/
Abc_Ntk_t * rewrite ( Abc_Ntk_t* pNtk, MODE mode ) {

    Aig_Man_t * pMan, * pTemp;
    Abc_Ntk_t * pNtkAig;
    abctime clk;
    assert( Abc_NtkIsStrash(pNtk) );

    pMan = Abc_NtkToDar( pNtk, 0, 0 );
    if ( pMan == NULL )
        return NULL;

//    Aig_ManPrintStats( pMan );

clk = Abc_Clock();

    switch ( mode )
    {
    case SEQ:
        pMan = Dar_ManCompress2( pTemp = pMan, 0, 0, 1, 0, 0 );
        break;
    case RAN:
        pMan = RAN_ManCompress2( pTemp = pMan, 0, 0, 1, 0, 0 );
        break;
    //case PRI:
    //    pMan = PRI_ManCompress2( pTemp = pMan, 0, 0, 1, 0, 0 );
    default:
        break;
    }
    Aig_ManStop( pTemp );
//ABC_PRT( "time", Abc_Clock() - clk );

//    Aig_ManPrintStats( pMan );
    pNtkAig = Abc_NtkFromDar( pNtk, pMan );
    Aig_ManStop( pMan );

    return pNtkAig;

}

Cut_Man_t * Abc_NtkStartCutManForRewrite( Abc_Ntk_t * pNtk )
{
    static Cut_Params_t Params, * pParams = &Params;
    Cut_Man_t * pManCut;
    Abc_Obj_t * pObj;
    int i;
    // start the cut manager
    memset( pParams, 0, sizeof(Cut_Params_t) );
    pParams->nVarsMax  = 4;     // the max cut size ("k" of the k-feasible cuts)
    pParams->nKeepMax  = 250;   // the max number of cuts kept at a node
    pParams->fTruth    = 1;     // compute truth tables
    pParams->fFilter   = 1;     // filter dominated cuts
    pParams->fSeq      = 0;     // compute sequential cuts
    pParams->fDrop     = 0;     // drop cuts on the fly
    pParams->fVerbose  = 0;     // the verbosiness flag
    pParams->nIdsMax   = Abc_NtkObjNumMax( pNtk );
    pManCut = Cut_ManStart( pParams );
    if ( pParams->fDrop )
        Cut_ManSetFanoutCounts( pManCut, Abc_NtkFanoutCounts(pNtk) );
    // set cuts for PIs
    Abc_NtkForEachCi( pNtk, pObj, i )
        if ( Abc_ObjFanoutNum(pObj) > 0 )
            Cut_NodeSetTriv( pManCut, pObj->Id );
    return pManCut;
}

