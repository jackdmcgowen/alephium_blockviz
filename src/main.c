#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "commands.h"
#include "config.h"

CURL            *curl;
const char     *baseUrl;

static void format_output
    (
    cJSON				*json
    )
{
char				*formatted;

formatted = cJSON_Print( json );
if( formatted )
    {
    printf( "%s\n", formatted );
    free( formatted );
    }

}	/* format_json_output() */


void get_heights
    (
    int                 heights[4][4]
    )
{
int					    fromGroup;
int					    toGroup;
int                     h;

for( fromGroup = 0; fromGroup < 4; fromGroup++ )
    {
    for( toGroup = 0; toGroup < 4; toGroup++ )
        {
        heights[fromGroup][toGroup] = 0;
        h = get_height( fromGroup, toGroup );
        printf( "Chain Height for shard [%d,%d]: %d\n", fromGroup, toGroup, h );
        heights[fromGroup][toGroup] = h;
        }
    }

}   /* get_heights() */


int main
    (
    void
    )
{
int                     heights[4][4];

  /* Load the configs from the JSON file */
ConfigArray config_array = load_configs( "config.json" );

  /* Get the base URL address */
baseUrl = config_array.configs[0].url;

curl = curl_easy_init();
if( !curl )
    {
    printf( "curl init failed\n" );
    return( -1 );
    }

get_heights( heights );

/* Other commands */
for( int i = 0; i < CMD_COUNT; i++ )
    {
    ResponseData		 response = { 0 };
    cJSON				*obj = NULL;
    int64_t		         now = (int64_t)time(NULL) * 1000;
    const char* blockhash = "00000000000005f9fee8769b1948f5272635b5079059e31dd6e0ab3031424b50";

    switch( i )
        {
        case CMD_BLOCKFLOW_CHAIN_INFO:
            obj = get_blockflow_chain_info( 0, 0 );
            printf( "Chain params fetched\n" );
            format_output( obj );
            break;

        case CMD_INFOS_CHAIN_PARAMS:
            obj = get_infos_chain_params();
            printf( "Chain params fetched\n" );
            format_output( obj );
            break;

        case CMD_INFOS_NODE:
            obj = get_infos_node();
            printf( "Node info fetched\n" );
            format_output( obj );
            break;

        case CMD_INFOS_SELF_CLIQUE:
            obj = get_infos_self_clique();
            printf( "Self-clique fetched\n" );
            format_output( obj );
            break;

        case CMD_INFOS_VERSION:
            obj = get_infos_version();
            printf( "Version info fetched\n" );
            format_output( obj );
            break;

        case CMD_BLOCKFLOW_HASHES:
            obj = get_blockflow_hashes( 0, 0, heights[0][0] );
            {
            GET_OBJECT_ITEM( obj, headers );
            if( headers && cJSON_IsArray( headers ) )
                {
                int		 headerCount;
                headerCount = cJSON_GetArraySize( headers );
                printf( "Polled %d hashes\n", headerCount );
                format_output( headers );
                }
            }
            break;

        case CMD_BLOCKFLOW_BLOCKS_INTERVAL:
            obj = get_blockflow_blocks( now - (300 * 1000), now );
            {
            GET_OBJECT_ITEM( obj, blocks );
            if( blocks && cJSON_IsArray( blocks ) )
                {
                int		 blockCount;
                blockCount = cJSON_GetArraySize( blocks );
                printf( "Polled %d blocks\n", blockCount );
                format_output( blocks );
                }
            }
            break;

        case CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS_INTERVAL:
            /* Poll last 5 minutes (300,000 ms) */
            obj = get_blockflow_blocks_with_events( now - (300 * 1000), now );
            {
            GET_OBJECT_ITEM( obj, blocksAndEvents );
            if( blocksAndEvents && cJSON_IsArray(blocksAndEvents) )
                {
                int		 blockCount;
                blockCount = cJSON_GetArraySize( blocksAndEvents );
                printf( "Polled %d blocks with events\n", blockCount );
                format_output( blocksAndEvents );
                }
            }
            break;

        case CMD_BLOCKFLOW_BLOCKS_BLOCKHASH:
            obj = get_blockflow_blocks_blockhash( blockhash );
            {
            GET_OBJECT_ITEM(obj, blocks);
            if( blocks && cJSON_IsArray( blocks ) )
                {
                int		 blockCount;
                blockCount = cJSON_GetArraySize( blocks );
                printf( "Polled %d blocks\n", blockCount );
                format_output( blocks );
                }
            }
            break;

        case CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS_BLOCKHASH:
            obj = get_blockflow_blocks_with_events_blockhash( blockhash );
            {
            GET_OBJECT_ITEM( obj, blocksAndEvents );
            if( blocksAndEvents && cJSON_IsArray(blocksAndEvents) )
                {
                int		 blockCount;
                blockCount = cJSON_GetArraySize( blocksAndEvents );
                printf( "Polled %d blocks with events\n", blockCount );
                format_output( blocksAndEvents );
                }
            }
            break;

        case CMD_BLOCKFLOW_HEADERS_BLOCKHASH:
            obj = get_blockflow_headers_blockhash( blockhash );
            format_output( obj );
            break;

        default:
            printf( "Unknown Command %d \n", i );
            break;
        }
    
    if( obj )
        {
        cJSON_Delete( obj );
        }

    }

curl_easy_cleanup( curl );

free_configs( &config_array );

return( 0 );

}	/* main() */