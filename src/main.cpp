#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "commands.h"
#include "config.h"
#include "dag.hpp"

extern "C" CURL * curl;
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
int64_t                 lastPollTs; /* Last timestamp per shard */
Dag                     dag;
int                     i;
int                     j;

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

lastPollTs = (int64_t)time(NULL) * 1000 - 60000; /* Start 1 min back */

/* Poll every 16 seconds */
while( 1 )
    {
    ResponseData    response = { NULL, 0, 0 };
    cJSON          *obj;
    int64_t         now = (int64_t)time(NULL) * 1000;

    printf( "\nPolling blockflow at %lld\n", now );
    /* Poll blocks since last timestamp */
    obj = get_blockflow_blocks_with_events( lastPollTs, now );
    if( obj )
        {
        GET_OBJECT_ITEM( obj, blocksAndEvents );
        if( blocksAndEvents && cJSON_IsArray(blocksAndEvents) )
            {
            int         count;
            int         totalBlocks;
            cJSON      *iter;
            cJSON      *shard;

            count = cJSON_GetArraySize( blocksAndEvents );
            totalBlocks = 0;          

            for( i = 0; i < count; i++ )
                {
                int blocksEventsCount;
                
                shard = cJSON_GetArrayItem( blocksAndEvents, i );
                //format_output( shard );

                if( shard && cJSON_IsArray( shard ) )
                    {
                    blocksEventsCount = cJSON_GetArraySize( shard );
                    for( j = 0; j < blocksEventsCount; ++j )
                        {
                        iter = cJSON_GetArrayItem( shard, j );

                        GET_OBJECT_ITEM( iter, block );

                        if( block )
                            {
                            
                            dag.add_block( block );
                            ++totalBlocks;
                            }
                        }
                    }
                }

            dag.print();
            printf( "Polled %d blocks\n", totalBlocks );
            }

        lastPollTs = now; /* Update last poll time */
        cJSON_Delete( obj );
        }
    
    Sleep( 16000 ); /* 16 seconds */
    }

dag.free();

curl_easy_cleanup( curl );
free_configs( &config_array );

return( 0 );

}	/* main() */