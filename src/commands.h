
#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdint.h>
#include <stddef.h>

typedef int AlephiumCommand;
enum
    {
    CMD_BLOCKFLOW_CHAIN_INFO,
    CMD_INFOS_SELF_CLIQUE,
    CMD_INFOS_CHAIN,
    CMD_INFOS_CHAIN_PARAMS,
    CMD_TRANSACTIONS,
    CMD_BLOCKS,

    CMD_COUNT
    };

typedef struct
    {
    AlephiumCommand		 command;
    const char          *path;
    size_t             (*callback)(void*, size_t, size_t, void*);
    } CommandStringPair;

size_t writeCallbackBlockflowChainInfo
    (
    void                *contents,
    size_t				 size,
    size_t				 nmemb,
    void                *userp
    );

size_t writeCallbackInfosSelfClique
    (
    void                *contents,
    size_t				 size,
    size_t				 nmemb,
    void                *userp
    );

size_t writeCallbackInfosChain
    (
    void                *contents,
    size_t				 size,
    size_t				 nmemb,
    void                *userp
    );

size_t writeCallbackTransactions
    (
    void* contents,
    size_t				 size,
    size_t				 nmemb,
    void* userp
    );

size_t writeCallbackBlocks
    (
    void* contents,
    size_t				 size,
    size_t				 nmemb,
    void* userp
    );

size_t writeCallbackInfosChainParams
    (
    void* contents,
    size_t				 size,
    size_t				 nmemb,
    void* userp
    );

extern const CommandStringPair commandTable[];

#endif /* COMMANDS_H */
