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
    CURL               *curl
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
            commandTable[CMD_BLOCKFLOW_CHAIN_INFO].path, fromGroup, toGroup );

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
            cJSON* json;
            cJSON* height;

            json = cJSON_ParseWithLength( response[i].buffer, response[i].length );
            if (json)
                {
                height = cJSON_GetObjectItem( json, "currentHeight" );

                printf( "Chain Height for shard [%d,%d]: %d\n", fromGroup, toGroup, height->valueint );

                cJSON_Delete( json );
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

// Load the configs from the JSON file
const char* filename = "config.json";
ConfigArray config_array = load_configs( filename );

//Get the base URL address
baseUrl = config_array.configs[0].url;

curl = curl_easy_init();
if( !curl )
    {
    printf( "curl init failed\n" );
    return( -1 );
    }

get_shard_heights( baseUrl, curl );

/* Other commands */
for( int i = CMD_INFOS_SELF_CLIQUE; i < CMD_COUNT; i++ )
    {
    ResponseData		 response;
    cJSON				*json;

    if( commandTable[i].command == CMD_TRANSACTIONS 
     || commandTable[i].command == CMD_BLOCKS )
        continue;

    memset( &response, 0, sizeof(response) );
    memset( path, 0, sizeof(path) );

    if( commandTable[i].command == CMD_BLOCKFLOW_BLOCKS 
     || commandTable[i].command == CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS )
        {
        /* Poll last 5 minutes (300,000 ms) */
        int64_t		 now = (long long)time(NULL) * 1000;

        snprintf( path, sizeof(path), commandTable[i].path, now - 300000, now );
        }
    else
        {
        snprintf( path, sizeof(path), commandTable[i].path );
        }

    snprintf( url, sizeof(url), "%s%s", baseUrl, path );

    write_url( url, curl, &response );
    if( check_httpcode( response.httpCode ) )
        {
        json = cJSON_ParseWithLength( response.buffer, response.length );
        if( json )
            {
            format_output( json );

            switch( commandTable[i].command )
                {
                case CMD_INFOS_SELF_CLIQUE:
                    printf("Self-clique fetched\n");
                    break;

                case CMD_TRANSACTIONS:
                    printf( "Transactions fetched\n" );
                    break;

                case CMD_BLOCKS:
                    printf( "Blocks fetched\n" );
                    break;

                case CMD_INFOS_CHAIN_PARAMS:
                    printf( "Chain params fetched\n" );
                    break;

                case CMD_INFOS_NODE:
                    printf( "Node info fetched\n" );
                    break;

                case CMD_INFOS_VERSION:
                    printf( "Version info fetched\n" );

                case CMD_BLOCKFLOW_BLOCKS:
                    {
                    cJSON			*blocks;
                    blocks = cJSON_GetObjectItem( json, "blocks" );
                    if( blocks && cJSON_IsArray( blocks ) )
                        {
                        int		 blockCount;
                        blockCount = cJSON_GetArraySize( blocks );
                        printf( "Polled %d blocks\n", blockCount );
                        }
                    break;
                    }
                case CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS:
                    {
                    cJSON			*blocks;
                    blocks = cJSON_GetObjectItem( json, "blocksAndEvents" );
                    if( blocks && cJSON_IsArray( blocks ) )
                        {
                        int		 blockCount;
                        blockCount = cJSON_GetArraySize( blocks );
                        printf( "Polled %d blocks with events\n", blockCount );
                        }
                    break;
                    }
                }
            cJSON_Delete( json );
            }
        else
            {
            printf( "JSON parse failed\n" );
            }
        }

    free( response.buffer );
    }

curl_easy_cleanup( curl );

free_configs( &config_array );

return( 0 );

}	/* main() */