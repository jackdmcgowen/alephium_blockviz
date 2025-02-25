#include <stdio.h>
#include <stdlib.h>
#include "dag.h"

void dag_init
    (
    Dag				*dag
    )
{
int					 i;

for( i = 0; i < MAX_GROUPS; i++ )
    {
    dag->nodes[i].groupId = i;
    dag->nodes[i].edges = NULL;

    /* Add initial 16 shards as edges */
    for( int j = 0; j < MAX_GROUPS; j++ )
        {
        DagEdge		*edge;

        edge = malloc( sizeof(DagEdge) );
        if( !edge )
            {
            printf( "Failed to allocate edge [%d,%d]\n", i, j );
            continue;
            }

        edge->fromGroup = i;
        edge->toGroup = j;
        edge->blockHeight = -1; /* Initial shard */
        edge->next = dag->nodes[i].edges;
        dag->nodes[i].edges = edge;
        }
    }

}	/* dag_init() */


void dag_add_block
    (
    Dag				*dag,
    int				 fromGroup,
    int				 toGroup,
    int				 blockHeight
    )
{
DagEdge				*edge;

if( fromGroup < 0 || fromGroup >= MAX_GROUPS || toGroup < 0 || toGroup >= MAX_GROUPS )
    {
    printf( "Invalid group pair [%d,%d]\n", fromGroup, toGroup );
    return;
    }

edge = malloc( sizeof(DagEdge) );
if( !edge )
    {
    printf( "Failed to allocate block edge [%d,%d]\n", fromGroup, toGroup );
    return;
    }

edge->fromGroup = fromGroup;
edge->toGroup = toGroup;
edge->blockHeight = blockHeight;
edge->next = dag->nodes[fromGroup].edges;
dag->nodes[fromGroup].edges = edge;

}	/* dag_add_block() */


void dag_print
    (
    Dag				*dag
    )
{
int					 i;

printf( "DAG Structure:\n" );
for( i = 0; i < MAX_GROUPS; i++ )
    {
    DagEdge			*edge;

    printf( "Group %d:\n", i );
    for( edge = dag->nodes[i].edges; edge != NULL; edge = edge->next )
        {
        if( edge->blockHeight == -1 )
            {
            printf( "  Shard [%d,%d]\n", edge->fromGroup, edge->toGroup );
            }
        else
            {
            printf( "  Block [%d,%d] height %d\n", edge->fromGroup, edge->toGroup, edge->blockHeight );
            }
        }
    }

}	/* dag_print() */


void dag_free
    (
    Dag				*dag
    )
{
int					 i;

for( i = 0; i < MAX_GROUPS; i++ )
    {
    DagEdge			*edge;
    DagEdge			*next;

    for( edge = dag->nodes[i].edges; edge != NULL; edge = next )
        {
        next = edge->next;
        free( edge );
        }
    dag->nodes[i].edges = NULL;
    }

}	/* dag_free() */