#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include "commands.h"

#define CHECK_CURL( x )                                                      \
    {                                                                        \
    CURLcode        res;                                                     \
    res = x;                                                                 \
    if( res != CURLE_OK )                                                    \
        {                                                                    \
        fprintf( stderr, "curl failed: %s\n", curl_easy_strerror(res) );     \
        }                                                                    \
    }

static const CommandStringPair commandTable[] =
    {
        { CMD_BLOCKFLOW_CHAIN_INFO,	"/blockflow/chain-info",	writeCallbackBlockflowChainInfo	},
        { CMD_INFOS_SELF_CLIQUE,	"/infos/self-clique",		writeCallbackInfosSelfClique	},
        { CMD_INFOS_CHAIN,		    "/infos/chain",			    writeCallbackInfosChain		    },
        { CMD_INFOS_CHAIN_PARAMS,	"/infos/chain-params",		writeCallbackInfosChainParams	},
        { CMD_TRANSACTIONS,		    "/transactions",		    writeCallbackTransactions	    },
        { CMD_BLOCKS,			    "/blocks",			        writeCallbackBlocks		        }
    };
static_assert( sizeof(commandTable) / sizeof(commandTable[0]) == CMD_COUNT, "Command table size mismatch" );

int main
    (
    void
    )
{
char                    url[128];
CURL                   *curl;
int                     i;

const char* base_url = "https://node.testnet.alephium.org";

curl = curl_easy_init();
if( !curl )
    {
    printf( "curl init failed\n" );
    return( -1 );
    }

for( i = 0; i < CMD_COUNT; ++i )
    {
    uint32_t        httpCode;

    memset( url, 0, sizeof(url) );

    if (commandTable[i].command == CMD_BLOCKFLOW_CHAIN_INFO)
        {
        snprintf(url, sizeof(url), "%s%s%s", base_url, commandTable[i].path, "/?fromGroup=0&toGroup=0" );
        }
    else
        {
        snprintf( url, sizeof(url), "%s%s", base_url, commandTable[i].path );
        }

    curl_easy_setopt( curl, CURLOPT_URL, url );
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, commandTable[i].callback );
    
    CHECK_CURL( curl_easy_perform( curl ) );
    curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpCode );
        
    switch( httpCode )
        {
        case 404:
            printf( "HTTP 404 - Not Found\n" );
            break;

        case 200: /* OK */
            break;

        default:
            printf( "HTTP error: %ld\n", httpCode );
            break;
        }
    }

return( 0 );

}   /* main() */
