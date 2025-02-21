#include <curl/curl.h>
#include <cJSON.h>
#include <stdio.h>

size_t write_callback
	(
	void 				*contents,
	size_t 				 size,
	size_t 				 nmemb,
	void 				*userp
	) 
{
cJSON                  *json;
cJSON                  *shards;
cJSON                  *height;

json = cJSON_ParseWithLength( contents, size * nmemb );
if( !json ) 
    {
    printf( "JSON parse failed\n" );
    return size * nmemb;
    }
    
shards = cJSON_GetObjectItem( json, "currentShards" );
height = cJSON_GetObjectItem( json, "currentHeight" );

if( shards && height )
    {
    printf( "Shards: %d, Chain Height: %d\n", shards->valueint, height->valueint );
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

curl = curl_easy_init();

if( curl )
    {
    CURLcode        res;
    
    curl_easy_setopt( curl, CURLOPT_URL, "http://testnet.alephium.org/blockflow/chain-info" );
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, write_callback );
    
    res = curl_easy_perform( curl );
    
    if( res != CURLE_OK )
        {
        fprintf( stderr, "curl failed: %s\n", curl_easy_strerror( res ) );
        }
        
    curl_easy_cleanup( curl );
    }
else
    {
    printf( "curl init failed\n" );
    }
        
return( 0 );

}   /* main() */
