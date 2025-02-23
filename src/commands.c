#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "commands.h"

static const char *BASE_URL = "https://node.testnet.alephium.org";

const CommandStringPair commandTable[] =
    {
        { CMD_BLOCKFLOW_CHAIN_INFO,         "/blockflow/chain-info"         },
        { CMD_INFOS_SELF_CLIQUE,            "/infos/self-clique"            },
        { CMD_INFOS_CHAIN_PARAMS,           "/infos/chain-params"           },
        { CMD_TRANSACTIONS,                 "/transactions"                 },
        { CMD_BLOCKS,                       "/blocks"                       },
        { CMD_BLOCKFLOW_BLOCKS,             "/blockflow/blocks"             },
        { CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS, "/blockflow/blocks-with-events" }
    };
static_assert( sizeof(commandTable) / sizeof(commandTable[0]) == CMD_COUNT, "Command table size mismatch" );

size_t writeCallback
    (
    void				*contents,
    size_t				 size,
    size_t				 nmemb,
    void				*userp
    )
{
ResponseData			*response;
size_t					 len;
char					*newBuffer;

len = size * nmemb;
response = (ResponseData*)userp;

newBuffer = realloc( response->buffer, response->length + len + 1 );
if( !newBuffer )
    {
    printf( "Memory allocation failed\n" );
    return( 0 );
    }

response->buffer = newBuffer;
memcpy( response->buffer + response->length, contents, len );
response->length += len;
response->buffer[response->length] = '\0';

return( len );

}	/* writeCallback() */