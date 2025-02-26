#include <stdio.h>
#include <stdlib.h>
#include <cjson/cJSON.h>
#include "dag.hpp"
#include "commands.h"


void Dag::add_block( cJSON* block )
{
    GET_OBJECT_ITEM( block, chainFrom );
    GET_OBJECT_ITEM( block, chainTo );
    GET_OBJECT_ITEM( block, height );
    GET_OBJECT_ITEM( block, timestamp );
    GET_OBJECT_ITEM( block, hash      );

    if( chainFrom->valueint < 0 || chainFrom->valueint >= MAX_GROUPS 
     || chainTo->valueint < 0   || chainTo->valueint >= MAX_GROUPS )
    {
        printf( "Invalid group pair [%d,%d]\n", chainFrom->valueint, chainTo->valueint );
        return;
    }

    cJSON* blockCopy = cJSON_Duplicate( block, 1 ); // Deep copy to own the block
    if( blockCopy )
    {
        blocks[hash->valuestring] = blockCopy; // Overwrites if height exists
    }
    else
    {
        printf("Failed to copy block JSON\n");
    }
}

void Dag::print()
{
    printf("DAG Structure:\n");

    for(const auto& it: blocks)
    {
        char* formatted;

        formatted = cJSON_Print( it.second );
        if( formatted )
        {
            printf( "%s\n", formatted );
            ::free( formatted );
        }
    }
}

void Dag::free()
{
for (auto& it : blocks)
    {
    cJSON_Delete( it.second ); // Free owned JSON
    }
blocks.clear();

}   /* Dag::free() */
