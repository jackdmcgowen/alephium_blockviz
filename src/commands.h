#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdint.h>
#include <stddef.h>

struct cJSON;

typedef enum AlephiumCommand
    {
    CMD_INFOS_CHAIN_PARAMS,
    CMD_INFOS_NODE,
    CMD_INFOS_SELF_CLIQUE,
    CMD_INFOS_VERSION,
    CMD_BLOCKFLOW_BLOCKS_BLOCKHASH,
    CMD_BLOCKFLOW_BLOCKS_INTERVAL,
    CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS_BLOCKHASH,
    CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS_INTERVAL,
    CMD_BLOCKFLOW_CHAIN_INFO,
    CMD_BLOCKFLOW_HASHES,
    CMD_BLOCKFLOW_HEADERS_BLOCKHASH,

    CMD_COUNT /* Sentinel */
    } AlephiumCommand;

typedef struct ResponseData
    {
    char				*buffer;
    size_t				 length;
    size_t				 capacity;
    uint32_t             httpCode;
    } ResponseData;

#define GET_OBJECT_ITEM( obj, x ) cJSON *x = cJSON_GetObjectItem( obj, #x )

void build_request
    (
    char           *url,
    const char     *format,
    ...
    );
#define build_request_0( x )          build_request( url, commandTable[x], baseUrl )
#define build_request_1( x, a )       build_request( url, commandTable[x], baseUrl, a )
#define build_request_2( x, a, b )    build_request( url, commandTable[x], baseUrl, a, b )
#define build_request_3( x, a, b, c ) build_request( url, commandTable[x], baseUrl, a, b, c )

void write_url
    (
    const char * const url,
    ResponseData *response
    );

extern size_t writeCallback
    (
    void				*contents,
    size_t				 size,
    size_t				 nmemb,
    void				*userp
    );

cJSON *read_response
    (
    ResponseData       *response
    );

cJSON *get_infos_chain_params
    (
    void
    );

cJSON *get_infos_node
    (
    void
    );

cJSON *get_infos_self_clique
    (
    void
    );

cJSON *get_infos_version
    (
    void
    );

cJSON *get_blockflow_chain_info
    (
    int                 fromGroup,
    int                 toGroup
    );

cJSON *get_blockflow_hashes
    (
    int                 fromGroup,
    int                 toGroup,
    int                 height
    );

cJSON* get_blockflow_blocks
    (
    int64_t             fromTs,
    int64_t             toTs
    );

cJSON* get_blockflow_blocks_with_events
    (
    int64_t             fromTs,
    int64_t             toTs
    );

cJSON* get_blockflow_blocks_blockhash
    (
    const char * const  blockHash
    );

cJSON* get_blockflow_blocks_with_events_blockhash
    (
    const char * const  blockHash
    );

cJSON* get_blockflow_headers_blockhash
    (
    const char * const  blockHash
    );

int get_height
    (
    int                 fromGroup,
    int                 toGroup
    );

extern const char * const commandTable[];
extern const char * baseUrl;

#endif /* COMMANDS_H */