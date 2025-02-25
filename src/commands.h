#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdint.h>
#include <stddef.h>

typedef enum AlephiumCommand
    {
    CMD_BLOCKFLOW_CHAIN_INFO,
    CMD_INFOS_SELF_CLIQUE,
    CMD_TRANSACTIONS,
    CMD_BLOCKS,
    CMD_INFOS_CHAIN_PARAMS,
    CMD_INFOS_NODE,
    CMD_INFOS_VERSION,
    CMD_BLOCKFLOW_BLOCKS_BLOCKHASH,
    CMD_BLOCKFLOW_BLOCKS_INTERVAL,
    CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS_BLOCKHASH,
    CMD_BLOCKFLOW_BLOCKS_WITH_EVENTS_INTERVAL,
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

typedef struct CommandStringPair
    {
    AlephiumCommand		 command;
    const char			*path;
    } CommandStringPair;

extern size_t writeCallback
    (
    void				*contents,
    size_t				 size,
    size_t				 nmemb,
    void				*userp
    );

extern const CommandStringPair commandTable[];

#endif /* COMMANDS_H */