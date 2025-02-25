#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "commands.h"
#include "config.h"

#define CHECK_CURL( x )                                                      \
    {                                                                        \
    CURLcode        res;                                                     \
    res = x;                                                                 \
    if( res != CURLE_OK )                                                    \
        {                                                                    \
        fprintf( stderr, "curl failed: %s\n", curl_easy_strerror(res) );     \
        }                                                                    \
    }

#define GET_OBJECT_ITEM( obj, x ) cJSON *x = cJSON_GetObjectItem( obj, #x )

static int check_httpcode
    (
    uint32_t			 httpCode
    )
{
int					 success;

success = 0;

switch( httpCode )
    {
    case 404:
        printf( "HTTP 404 - Not Found\n" );
        break;

    case 200: /* OK */
        success = 1;
        break;

    default:
        printf( "HTTP error: %u\n", httpCode );
        break;
    }

return( success );

}	/* check_httpcode() */


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


static cJSON *read_response
    (
    ResponseData       *response
    )
{
cJSON                  *obj;

obj = cJSON_ParseWithLength( response->buffer, response->length );

if( !obj )
    {
    printf( "JSON parse failed\n" );
    }

return( obj );

}   /* read_response() */


void write_url
    (
    const char * const url,
    CURL            * curl,
    ResponseData *response
    )
{
response->httpCode = 0;
curl_easy_setopt( curl, CURLOPT_URL, url );
curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, writeCallback );
curl_easy_setopt( curl, CURLOPT_WRITEDATA, response );

CHECK_CURL( curl_easy_perform( curl ) );
curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &response->httpCode );

}   /* write_url() */


void get_shard_heights
    (
    char *              baseUrl,
    CURL               *curl,
    int                *heights
    )
{
char				    url[128];
int					    fromGroup;
int					    toGroup;
ResponseData	       response[16];

memset( response, 0, 16 * sizeof(ResponseData) );

/* Loop all shards for CMD_BLOCKFLOW_CHAIN_INFO */
for( fromGroup = 0; fromGroup < 4; fromGroup++ )
    {
    for( toGroup = 0; toGroup < 4; toGroup++ )
        {
        char path[128] = { 0 };

        snprintf( path, sizeof(path),
            commandTable[CMD_BLOCKFLOW_CHAIN_INFO], fromGroup, toGroup );

        snprintf( url, sizeof(url), "%s%s", baseUrl, path );

        write_url( url, curl, &response[ fromGroup * 4 + toGroup] );
        }
    }

for (fromGroup = 0; fromGroup < 4; fromGroup++)
    {
    for (toGroup = 0; toGroup < 4; toGroup++)
        {
        int i = fromGroup * 4 + toGroup;

        if( check_httpcode( response[i].httpCode ) )
            {
            cJSON* obj;

            obj = read_response( &response[i] );
            if( obj )
                {
                GET_OBJECT_ITEM( obj, currentHeight );

                heights[i] = currentHeight->valueint;
                printf( "Chain Height for shard [%d,%d]: %d\n", fromGroup, toGroup, currentHeight->valueint );

                cJSON_Delete( obj );
                }

            }

        if( response[i].buffer )
            {
            free( response[i].buffer );
            }
        }
    }
}


int main
    (
    void
    )
{
CURL					*curl;
char				    url[128];
char                    path[128];
const char *            baseUrl;
int                     heights[16];

  /* Load the configs from the JSON file */
const char* filename = "config.json";
ConfigArray config_array = load_configs( filename );

  /* Get the base URL address */
baseUrl = config_array.configs[0].url;

curl = curl_easy_init();
if( !curl )
    {
    printf( "curl init failed\n" );
    return( -1 );
    }

memset( heights, 0, sizeof(heights) );

get_shard_heights( baseUrl, curl, heights );

/* Other commands */
for( int i = 0; i < CMD_COUNT; i++ )
    {
    ResponseData		 response;
    cJSON				*obj;


    if( i == CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS_INTERVAL
     || i == CMD_BLOCKFLOW_BLOCKS_INTERVAL
     || i == CMD_BLOCKFLOW_CHAIN_INFO )
        continue;

    memset( &response, 0, sizeof(response) );
    memset( path, 0, sizeof(path) );

    switch( i )
        {
        case CMD_BLOCKFLOW_HASHES:
            snprintf( path, sizeof(path), commandTable[i], 0, 0, heights[0] );
            break;

        case CMD_BLOCKFLOW_BLOCKS_INTERVAL:
            /* Poll last 5 minutes (300,000 ms) */
            int64_t		 now = (int64_t)time( NULL ) * 1000;
            snprintf( path, sizeof(path), commandTable[i], now - (300 * 1000), now );
            break;

        case CMD_BLOCKFLOW_BLOCKS_BLOCKHASH:
        case CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS_BLOCKHASH:

        case CMD_BLOCKFLOW_HEADERS_BLOCKHASH:
            const char* blockhash = "00000000000005f9fee8769b1948f5272635b5079059e31dd6e0ab3031424b50";
            snprintf( path, sizeof(path), commandTable[i], blockhash );
            break;

        default:
            snprintf( path, sizeof(path), commandTable[i] );
            break;
        }

    snprintf( url, sizeof(url), "%s%s", baseUrl, path );

    write_url( url, curl, &response );
    if( !check_httpcode( response.httpCode ) )
        {
        continue;
        }

    obj = read_response( &response );
    if( !obj )
        {
        continue;
        } 
    
    switch( i )
        {
        case CMD_INFOS_SELF_CLIQUE:
            printf("Self-clique fetched\n");
            format_output( obj );
            break;

        case CMD_INFOS_CHAIN_PARAMS:
            printf( "Chain params fetched\n" );
            format_output( obj );
            break;

        case CMD_INFOS_NODE:
            printf( "Node info fetched\n" );
            format_output( obj );
            break;

        case CMD_INFOS_VERSION:
            printf( "Version info fetched\n" );
            format_output( obj );
            break;

        case CMD_BLOCKFLOW_BLOCKS_BLOCKHASH:
        case CMD_BLOCKFLOW_BLOCKS_INTERVAL:
            {
            GET_OBJECT_ITEM( obj, blocks );

            if( blocks && cJSON_IsArray( blocks ) )
                {
                int		 blockCount;
                blockCount = cJSON_GetArraySize( blocks );
                printf( "Polled %d blocks\n", blockCount );
                format_output( blocks );
                }
            break;
            }
        case CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS_BLOCKHASH:
        case CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS_INTERVAL:
            {
            GET_OBJECT_ITEM( obj, blocksAndEvents );
            if(blocksAndEvents && cJSON_IsArray(blocksAndEvents) )
                {
                int		 blockCount;
                blockCount = cJSON_GetArraySize( blocksAndEvents );
                printf( "Polled %d blocks with events\n", blockCount );
                format_output( blocksAndEvents );
                }
            break;
            }
        case CMD_BLOCKFLOW_HASHES:
            {
            GET_OBJECT_ITEM( obj, headers );
            if ( headers && cJSON_IsArray( headers ) )
                {
                int		 headerCount;
                headerCount = cJSON_GetArraySize( headers );
                printf( "Polled %d hashes\n", headerCount );
                format_output( headers );
                }
            break;
            }
        case CMD_BLOCKFLOW_HEADERS_BLOCKHASH:
            {
            format_output( obj );
            break;
            }
        }
            
    cJSON_Delete( obj );
    free( response.buffer );

    }

curl_easy_cleanup( curl );

free_configs( &config_array );

return( 0 );

}	/* main() */