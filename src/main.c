#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <stdio.h>

size_t write_callback
	(
	void 				*contents,
	size_t 				 size,
	size_t 				 nmemb,
	void 				*userp
	) 
{
char                   *payload;
cJSON                  *json;
//cJSON                  *shards;
cJSON                  *height;

printf( "%.*s\n", (int)(size * nmemb), (char*)contents );

json = cJSON_ParseWithLength( contents, size * nmemb );
if( !json ) 
    {
    printf( "JSON parse failed\n" );
    return( size * nmemb );
    }
    
//shards = cJSON_GetObjectItem( json, "currentShards" );
height = cJSON_GetObjectItem( json, "currentHeight" );

if( /* shards  && */ height)
    {
    printf( /*"Shards: %d, */"Chain Height : % d\n", /* shards->valueint, */ height->valueint);
    }
else
    {
    printf( "Data missing\n" );
    }
    
cJSON_Delete( json );

return( size * nmemb );

}   /* write_callback() */

int main
    (
    void
    )
{
CURL                   *curl;

const char* testnet_url = "https://node.testnet.alephium.org/blockflow/chain-info/?fromGroup=0&toGroup=3";

curl = curl_easy_init();

if( curl )
    {
    CURLcode        res;
    uint32_t        httpCode;
    
    curl_easy_setopt( curl, CURLOPT_URL, testnet_url );
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, write_callback );
    
    res = curl_easy_perform( curl );
    
    if( res != CURLE_OK )
        {
        fprintf( stderr, "curl failed: %s\n", curl_easy_strerror( res ) );
        }
    else
    {
    curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpCode );
    switch( httpCode )
        {
        case 404:
            printf( "HTTP 404 - Not Found\n" );
            break;

        case 200:
            printf( "HTTP 200 - OK\n" );
            break;

        default:
            printf( "HTTP error: %ld\n", httpCode );
            break;

        }
    }
        
    curl_easy_cleanup( curl );
    }
else
    {
    printf( "curl init failed\n" );
    }
        
return( 0 );

}   /* main() */
