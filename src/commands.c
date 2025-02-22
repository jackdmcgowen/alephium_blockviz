#include <cjson/cJSON.h>
#include <stdio.h>
#include "commands.h"

const CommandStringPair commandTable[] =
    {
        { CMD_BLOCKFLOW_CHAIN_INFO, "/blockflow/chain-info",    writeCallbackBlockflowChainInfo    },
        { CMD_INFOS_SELF_CLIQUE,    "/infos/self-clique",       writeCallbackInfosSelfClique       },
        { CMD_INFOS_CHAIN,          "/infos/chain",             writeCallbackInfosChain            },
        { CMD_INFOS_CHAIN_PARAMS,   "/infos/chain-params",      writeCallbackInfosChainParams      },
        { CMD_TRANSACTIONS,         "/transactions",            writeCallbackTransactions          },
        { CMD_BLOCKS,               "/blocks",                  writeCallbackBlocks                }
    };
static_assert( sizeof(commandTable) / sizeof(commandTable[0]) == CMD_COUNT, "Command table size mismatch" );


size_t writeCallbackBlockflowChainInfo
    (
    void				*contents,
    size_t				 size,
    size_t				 nmemb,
    void				*userp
    )
{
cJSON					*json;
cJSON					*height;
int					*shardPair;
size_t					 len;

len = size * nmemb;

printf( "Raw response: %.*s\n", (int)len, (char*)contents );

json = cJSON_ParseWithLength( contents, len );
if( !json )
    {
    printf( "JSON parse failed\n" );
    return( len );
    }

height = cJSON_GetObjectItem( json, "currentHeight" );
shardPair = (int*)userp;

if( height )
    {
    printf( "Chain Height for shard [%d,%d]: %d\n", shardPair[0], shardPair[1], height->valueint );
    }
else
    {
    printf( "Height data missing\n" );
    }

cJSON_Delete( json );
return( len );

}	/* writeCallbackBlockflowChainInfo() */


size_t writeCallbackInfosSelfClique
    (
    void				*contents,
    size_t				 size,
    size_t				 nmemb,
    void				*userp
    )
{
cJSON					*json;
cJSON					*numNodes;
size_t					 len;

len = size * nmemb;

printf( "Raw response: %.*s\n", (int)len, (char*)contents );

json = cJSON_ParseWithLength( contents, len );
if( !json )
    {
    printf( "JSON parse failed\n" );
    return( len );
    }

numNodes = cJSON_GetObjectItem( json, "numNodes" );

if( numNodes )
    {
    int				 totalShards;
    totalShards = numNodes->valueint * numNodes->valueint;
    printf( "Nodes: %d, Total Shards (G*G): %d\n", numNodes->valueint, totalShards );
    }
else
    {
    printf( "Nodes data missing\n" );
    }

cJSON_Delete( json );
return( len );

}	/* writeCallbackInfosSelfClique() */


size_t writeCallbackInfosChain
    (
    void				*contents,
    size_t				 size,
    size_t				 nmemb,
    void				*userp
    )
{
cJSON					*json;
cJSON					*shards;
size_t					 len;

len = size * nmemb;

printf( "Raw response: %.*s\n", (int)len, (char*)contents );

json = cJSON_ParseWithLength( contents, len );
if( !json )
    {
    printf( "JSON parse failed\n" );
    return( len );
    }

shards = cJSON_GetObjectItem( json, "currentShards" );

if( shards )
    {
    printf( "Current Shards: %d\n", shards->valueint );
    }
else
    {
    printf( "Shards data missing\n" );
    }

cJSON_Delete( json );
return( len );

}	/* writeCallbackInfosChain() */


size_t writeCallbackTransactions
    (
    void				*contents,
    size_t				 size,
    size_t				 nmemb,
    void				*userp
    )
{
cJSON					*json;
size_t					 len;

len = size * nmemb;

printf( "Raw response: %.*s\n", (int)len, (char*)contents );

json = cJSON_ParseWithLength( contents, len );
if( !json )
    {
    printf( "JSON parse failed\n" );
    return( len );
    }

printf( "Transactions fetched\n" );
cJSON_Delete( json );
return( len );

}	/* writeCallbackTransactions() */


size_t writeCallbackBlocks
    (
    void				*contents,
    size_t				 size,
    size_t				 nmemb,
    void				*userp
    )
{
cJSON					*json;
size_t					 len;

len = size * nmemb;

printf( "Raw response: %.*s\n", (int)len, (char*)contents );

json = cJSON_ParseWithLength( contents, len );
if( !json )
    {
    printf( "JSON parse failed\n" );
    return( len );
    }

printf( "Blocks fetched\n" );
cJSON_Delete( json );
return( len );

}	/* writeCallbackBlocks() */


size_t writeCallbackInfosChainParams
    (
    void				*contents,
    size_t				 size,
    size_t				 nmemb,
    void				*userp
    )
{
cJSON					*json;
size_t					 len;

len = size * nmemb;

printf( "Raw response: %.*s\n", (int)len, (char*)contents );

json = cJSON_ParseWithLength( contents, len );
if( !json )
    {
    printf( "JSON parse failed\n" );
    return( len );
    }

printf( "Chain params fetched\n" );
cJSON_Delete( json );
return( len );

}	/* writeCallbackInfosChainParams() */