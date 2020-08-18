#include <ctime>
#include "rewrite.h"

int main(int argc, char * argv[])
{
    WHY_Man * pMan = WHY_Start();
    WHY_ReadBlif ( pMan, argv[1] );
    // WHY_PrintStats ( pMan );
    WHY_Operate ( pMan );
    // WHY_PrintStats ( pMan );
    WHY_Stop( pMan );
    cout << endl;
    return 0;
}