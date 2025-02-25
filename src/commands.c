#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "commands.h"

static const char *BASE_URL = "https://node.testnet.alephium.org";

const CommandStringPair commandTable[] =
    {
        { CMD_BLOCKFLOW_CHAIN_INFO,         "/blockflow/chain-info/?fromGroup=%d&toGroup=%d"        },
        { CMD_INFOS_SELF_CLIQUE,            "/infos/self-clique"                                    },
        { CMD_INFOS_CHAIN_PARAMS,           "/infos/chain-params"                                   },
        { CMD_INFOS_NODE,                   "/infos/node"                                           },
        { CMD_INFOS_VERSION,                "/infos/version"                                        },
        { CMD_TRANSACTIONS,                 "/transactions"                                         },
        { CMD_BLOCKS,                       "/blocks"                                               },
        { CMD_BLOCKFLOW_BLOCKS_BLOCKHASH,             "/blockflow/blocks/%.64s"                               },
        { CMD_BLOCKFLOW_BLOCKS_INTERVAL,              "/blockflow/blocks/?fromTs=%lld&toTs=%lld"              },
        { CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS_INTERVAL,  "/blockflow/blocks-with-events/?fromTs=%lld&toTs=%lld"  },
        { CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS_BLOCKHASH, "/blockflow/blocks-with-events/%.64s"                   },
        { CMD_BLOCKFLOW_HASHES,                       "/blockflow/hashes/?fromGroup=%d&toGroup=%d&height=%d"  },
        { CMD_BLOCKFLOW_HEADERS_BLOCKHASH,            "/blockflow/headers/%.64s"                              }
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

/* Initial allocation if empty */
if( !response->buffer )
    {
    response->capacity = len > 16384 ? len : 16384; /* Start at 16KB or larger */
    response->buffer = malloc( response->capacity );
    if( !response->buffer )
        {
        printf( "Initial memory allocation failed\n" );
        return( 0 );
        }
    response->length = 0;
    }

/* Grow if needed - double capacity */
if( response->length + len >= response->capacity )
    {
    size_t				newCapacity;
    newCapacity = response->capacity * 2;
    if( newCapacity < response->length + len )
        {
        newCapacity = response->length + len + 16384; /* Ensure enough space */
        }

    newBuffer = realloc( response->buffer, newCapacity );
    if( !newBuffer )
        {
        printf( "Memory reallocation failed\n" );
        return( 0 );
        }

    response->buffer = newBuffer;
    response->capacity = newCapacity;
    }

/* Append data */
memcpy( response->buffer + response->length, contents, len );
response->length += len;
response->buffer[response->length] = '\0';

return( len );

}	/* writeCallback() */
