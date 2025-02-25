#ifndef DAG_H
#define DAG_H

#include <stdint.h>
#include <stddef.h>

#define MAX_GROUPS		4
#define MAX_SHARDS		16

typedef struct DagEdge
    {
    int				 fromGroup;
    int				 toGroup;
    int				 blockHeight; /* -1 for initial shard */
    struct DagEdge		*next;
    } DagEdge;

typedef struct DagNode
    {
    int				 groupId;
    DagEdge			*edges; /* Linked list of edges */
    } DagNode;

typedef struct Dag
    {
    DagNode			 nodes[MAX_GROUPS];
    } Dag;

void dag_init
    (
    Dag				*dag
    );

void dag_add_block
    (
    Dag				*dag,
    int				 fromGroup,
    int				 toGroup,
    int				 blockHeight
    );

void dag_print
    (
    Dag				*dag
    );

void dag_free
    (
    Dag				*dag
    );

#endif /* DAG_H */