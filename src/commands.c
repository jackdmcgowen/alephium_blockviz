#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>
#include <curl/curl.h>
#include "commands.h"

CURL            *curl;

#define CHECK_CURL( x )                                                      \
    {                                                                        \
    CURLcode        res;                                                     \
    res = x;                                                                 \
    if( res != CURLE_OK )                                                    \
        {                                                                    \
        fprintf( stderr, "curl failed: %s\n", curl_easy_strerror(res) );     \
        }                                                                    \
    }

const char* const commandTable[] =
    {
        [ CMD_INFOS_CHAIN_PARAMS                     ] = "%s/infos/chain-params"                                  ,
        [ CMD_INFOS_NODE                             ] = "%s/infos/node"                                          ,
        [ CMD_INFOS_SELF_CLIQUE                      ] = "%s/infos/self-clique"                                   ,
        [ CMD_INFOS_VERSION                          ] = "%s/infos/version"                                       ,
        [ CMD_BLOCKFLOW_BLOCKS_BLOCKHASH             ] = "%s/blockflow/blocks/%.64s"                              ,
        [ CMD_BLOCKFLOW_BLOCKS_INTERVAL              ] = "%s/blockflow/blocks/?fromTs=%lld&toTs=%lld"             ,
        [ CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS_BLOCKHASH ] = "%s/blockflow/blocks-with-events/%.64s"                  ,
        [ CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS_INTERVAL  ] = "%s/blockflow/blocks-with-events/?fromTs=%lld&toTs=%lld" ,
        [ CMD_BLOCKFLOW_CHAIN_INFO                   ] = "%s/blockflow/chain-info/?fromGroup=%d&toGroup=%d"       ,
        [ CMD_BLOCKFLOW_HASHES                       ] = "%s/blockflow/hashes/?fromGroup=%d&toGroup=%d&height=%d" ,
        [ CMD_BLOCKFLOW_HEADERS_BLOCKHASH            ] = "%s/blockflow/headers/%.64s"
    };
static_assert( sizeof(commandTable) / sizeof(commandTable[0]) == CMD_COUNT, "Command table size mismatch" );


void build_request
    (
    char           *url,
    const char     *format,
    ...
    )
{
va_list                 argv;
va_start               ( argv, format );
vsnprintf( url, 128, format, argv );
va_end( argv );

}   /* build_request() */


cJSON *read_response
    (
    ResponseData       *response
    )
{
cJSON                  *obj;

switch( response->httpCode )
    {
    case 404:
        printf( "HTTP 404 - Not Found\n" );
        return( NULL );

    case 200: /* OK */
        break;

    default:
        printf( "HTTP error: %u\n", response->httpCode );
        return( NULL );
    }

obj = cJSON_ParseWithLength( response->buffer, response->length );
if( !obj )
    {
    printf( "JSON parse failed\n" );
    }

if( response->buffer )
    {
    free( response->buffer );
    }

return( obj );

}   /* read_response() */


void write_url
    (
    const char * const url,
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


cJSON *get_infos_chain_params
    (
    void
    )
{
char				    url[128];
ResponseData	        response = { 0 };
cJSON                  *obj;

build_request_0( CMD_INFOS_CHAIN_PARAMS );
write_url( url, &response );
obj = read_response( &response );

return( obj );

}   /* get_infos_chain_params() */


cJSON *get_infos_node
    (
    void
    )
{
char				    url[128];
ResponseData	        response = { 0 };
cJSON                  *obj;

build_request_0( CMD_INFOS_NODE );
write_url( url, &response );
obj = read_response( &response );

return( obj );

}   /* get_infos_node() */


cJSON *get_infos_self_clique
    (
    void
    )
{
char				    url[128];
ResponseData	        response = { 0 };
cJSON                  *obj;

build_request_0( CMD_INFOS_SELF_CLIQUE );
write_url( url, &response );
obj = read_response( &response );

return( obj );

}   /* get_infos_self_clique() */


cJSON *get_infos_version
    (
    void
    )
{
char				    url[128];
ResponseData	        response = { 0 };
cJSON                  *obj;

build_request_0( CMD_INFOS_VERSION );
write_url( url, &response );
obj = read_response( &response );

return( obj );

}   /* get_infos_version() */


cJSON *get_blockflow_chain_info
    (
    int                 fromGroup,
    int                 toGroup
    )
{
char				    url[128];
ResponseData	        response = { 0 };
cJSON                  *obj;

build_request_2( CMD_BLOCKFLOW_CHAIN_INFO, fromGroup, toGroup );
write_url( url, &response );
obj = read_response( &response );

return( obj );

}   /* get_blockflow_chain_info() */


cJSON *get_blockflow_hashes
    (
    int                 fromGroup,
    int                 toGroup,
    int                 height
    )
{
char				    url[128];
ResponseData	        response = { 0 };
cJSON                  *obj;

build_request_3( CMD_BLOCKFLOW_HASHES, fromGroup, toGroup, height );
write_url( url, &response );
obj = read_response( &response );

return( obj );

}   /* get_blockflow_hashes() */


cJSON* get_blockflow_blocks
    (
    int64_t             fromTs,
    int64_t             toTs
    )
{
char				    url[128];
ResponseData	        response = { 0 };
cJSON                  *obj;

build_request_2( CMD_BLOCKFLOW_BLOCKS_INTERVAL, fromTs, toTs );
write_url( url, &response );
obj = read_response( &response );

return( obj );

}   /* get_blockflow_blocks() */


cJSON* get_blockflow_blocks_with_events
    (
    int64_t             fromTs,
    int64_t             toTs
    )
{
char				    url[128];
ResponseData	        response = { 0 };
cJSON                  *obj;

build_request_2( CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS_INTERVAL, fromTs, toTs );
write_url( url, &response );
obj = read_response( &response );

return( obj );

}   /* get_blockflow_blocks_with_events() */


cJSON* get_blockflow_blocks_blockhash
    (
    const char * const  blockHash
    )
{
char				    url[128];
ResponseData	        response = { 0 };
cJSON                  *obj;

build_request_1( CMD_BLOCKFLOW_BLOCKS_BLOCKHASH, blockHash );
write_url( url, &response );
obj = read_response( &response );

return( obj );

}   /* get_blockflow_blocks_blockhash() */


cJSON* get_blockflow_blocks_with_events_blockhash
    (
    const char * const  blockHash
    )
{
char				    url[128];
ResponseData	        response = { 0 };
cJSON                  *obj;

build_request_1( CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS_BLOCKHASH, blockHash );
write_url( url, &response );
obj = read_response( &response );

return( obj );

}   /* get_blockflow_blocks_with_events_blockhash() */

cJSON* get_blockflow_headers_blockhash
    (
    const char * const  blockHash
    )
{
char				    url[128];
ResponseData	        response = { 0 };
cJSON                  *obj;

build_request_1(CMD_BLOCKFLOW_HEADERS_BLOCKHASH, blockHash );
write_url( url, &response );
obj = read_response( &response );

return( obj );

}   /* get_blockflow_headers_blockhash() */


int get_height
    (
    int                 fromGroup,
    int                 toGroup
    )
{
int                     height;

height = -1;
cJSON* obj = get_blockflow_chain_info( fromGroup, toGroup );

if( obj )
    {
    GET_OBJECT_ITEM( obj, currentHeight );
    height = currentHeight->valueint;

    cJSON_Delete( obj );
    }

return( height );

}   /* get_height() */
