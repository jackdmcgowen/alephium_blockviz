#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
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


static void format_json_output
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

int main
    (
    void
    )
{
CURL					*curl;
int					 fromGroup;
int					 toGroup;
char				 url[128];
const char			*baseUrl;

baseUrl = "https://node.testnet.alephium.org";

curl = curl_easy_init();
if( !curl )
    {
    printf( "curl init failed\n" );
    return( -1 );
    }

/* Loop all shards for CMD_BLOCKFLOW_CHAIN_INFO */
for( fromGroup = 0; fromGroup < 4; fromGroup++ )
    {
    for( toGroup = 0; toGroup < 4; toGroup++ )
        {
        uint32_t		 httpCode;
        ResponseData		 response = { NULL, 0 };
        int				 shardPair[2];
        cJSON				*json;
        cJSON				*height;

        shardPair[0] = fromGroup;
        shardPair[1] = toGroup;

        snprintf( url, sizeof(url), "%s%s?fromGroup=%d&toGroup=%d", baseUrl, commandTable[CMD_BLOCKFLOW_CHAIN_INFO].path, fromGroup, toGroup );
        curl_easy_setopt( curl, CURLOPT_URL, url );
        curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, writeCallback );
        curl_easy_setopt( curl, CURLOPT_WRITEDATA, &response );

        CHECK_CURL( curl_easy_perform( curl ) );
        curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpCode );

        if( check_httpcode( httpCode ) )
            {
            json = cJSON_ParseWithLength( response.buffer, response.length );
            if( json )
                {
                //format_json_output( json );
                height = cJSON_GetObjectItem( json, "currentHeight" );
                if( height )
                    {
                    printf( "Chain Height for shard [%d,%d]: %d\n", shardPair[0], shardPair[1], height->valueint );
                    }
                else
                    {
                    printf( "Height data missing\n" );
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
    }

/* Other commands */
for( int i = CMD_INFOS_SELF_CLIQUE; i < CMD_COUNT; i++ )
    {
    uint32_t		 httpCode;
    ResponseData		 response = { NULL, 0 };
    cJSON				*json;

    if (i == CMD_TRANSACTIONS || i == CMD_BLOCKS)
        continue;

    if( i == CMD_BLOCKFLOW_BLOCKS )
        {
        /* Poll last 1 minutes (60,000 ms) */
        long long		 now = (long long)time(NULL) * 1000;
        snprintf( url, sizeof(url), "%s%s?fromTs=%lld&toTs=%lld", baseUrl, commandTable[i].path, now - 60000, now );
        }
    else
        {
        snprintf( url, sizeof(url), "%s%s", baseUrl, commandTable[i].path );
        }

    curl_easy_setopt( curl, CURLOPT_URL, url );
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, writeCallback );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, &response );

    CHECK_CURL( curl_easy_perform( curl ) );
    curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpCode );

    if( check_httpcode( httpCode ) )
        {
        json = cJSON_ParseWithLength( response.buffer, response.length );
        if( json )
            {
            //format_json_output( json );

            switch( i )
                {
                case CMD_INFOS_SELF_CLIQUE:
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
                    else
                        {
                        printf( "Blocks data missing\n" );
                        }
                    break;
                    }
                case CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS:
                    {
                    cJSON			*blocks;
                    blocks = cJSON_GetObjectItem( json, "blocks" );
                    if( blocks && cJSON_IsArray( blocks ) )
                        {
                        int		 blockCount;
                        blockCount = cJSON_GetArraySize( blocks );
                        printf( "Polled %d blocks with events\n", blockCount );
                        }
                    else
                        {
                        printf( "Blocks data missing\n" );
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

return( 0 );

}	/* main() */