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

static const char* BASE_URL = "https://node.testnet.alephium.org";

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

int main
    (
    void
    )
{
CURL					*curl;
int					 fromGroup;
int					 toGroup;
char				 url[128];
uint32_t		     httpCode;

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
        int				 shardPair[2];

        shardPair[0] = fromGroup;
        shardPair[1] = toGroup;

        snprintf
            ( 
            url, sizeof(url),
            "%s%s/?fromGroup=%d&toGroup=%d",
            BASE_URL,
            commandTable[CMD_BLOCKFLOW_CHAIN_INFO].path,
            fromGroup,
            toGroup
            );
        curl_easy_setopt( curl, CURLOPT_URL, url );
        curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, commandTable[CMD_BLOCKFLOW_CHAIN_INFO].callback );
        curl_easy_setopt( curl, CURLOPT_WRITEDATA, shardPair );

        CHECK_CURL( curl_easy_perform( curl ) );
        curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpCode );

        check_httpcode( httpCode );
        }
    }

/* Other commands */
for( int i = CMD_INFOS_SELF_CLIQUE; i < CMD_COUNT; i++ )
    {
    snprintf( url, sizeof(url), "%s%s", BASE_URL, commandTable[i].path );
    curl_easy_setopt( curl, CURLOPT_URL, url );
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, commandTable[i].callback );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, NULL );

    CHECK_CURL( curl_easy_perform( curl ) );
    curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpCode );

    check_httpcode( httpCode );
    }

curl_easy_cleanup( curl );
return( 0 );

}	/* main() */