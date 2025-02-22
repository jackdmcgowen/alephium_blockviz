#include <cjson/cJSON.h>
#include "commands.h"

size_t writeCallbackBlockflowChainInfo
(
    void* contents,
    size_t				 size,
    size_t				 nmemb,
    void* userp
)
{
    cJSON* json;
    cJSON* height;
    int* shardPair;

    printf("Raw response: %.*s\n", (int)(size * nmemb), (char*)contents);

    json = cJSON_ParseWithLength(contents, size * nmemb);
    if (!json)
    {
        printf("JSON parse failed\n");
        return(size * nmemb);
    }

    height = cJSON_GetObjectItem(json, "currentHeight");
    shardPair = (int*)userp;

    if (height)
    {
        printf("Chain Height for shard [%d,%d]: %d\n", shardPair[0], shardPair[1], height->valueint);
    }
    else
    {
        printf("Height data missing\n");
    }

    cJSON_Delete(json);
    return(size * nmemb);

}	/* writeCallbackBlockflowChainInfo() */

size_t writeCallbackInfosSelfClique
(
    void* contents,
    size_t				 size,
    size_t				 nmemb,
    void* userp
)
{
    cJSON* json;
    cJSON* numNodes;

    printf("Raw response: %.*s\n", (int)(size * nmemb), (char*)contents);

    json = cJSON_ParseWithLength(contents, size * nmemb);
    if (!json)
    {
        printf("JSON parse failed\n");
        return(size * nmemb);
    }

    numNodes = cJSON_GetObjectItem(json, "numNodes");

    if (numNodes)
    {
        int				 totalShards;
        totalShards = numNodes->valueint * numNodes->valueint;
        printf("Nodes: %d, Total Shards (G*G): %d\n", numNodes->valueint, totalShards);
    }
    else
    {
        printf("Nodes data missing\n");
    }

    cJSON_Delete(json);
    return(size * nmemb);

}	/* writeCallbackInfosSelfClique() */

size_t writeCallbackInfosChain
(
    void* contents,
    size_t				 size,
    size_t				 nmemb,
    void* userp
)
{
    cJSON* json;
    cJSON* shards;

    printf("Raw response: %.*s\n", (int)(size * nmemb), (char*)contents);

    json = cJSON_ParseWithLength(contents, size * nmemb);
    if (!json)
    {
        printf("JSON parse failed\n");
        return(size * nmemb);
    }

    shards = cJSON_GetObjectItem(json, "currentShards");

    if (shards)
    {
        printf("Current Shards: %d\n", shards->valueint);
    }
    else
    {
        printf("Shards data missing\n");
    }

    cJSON_Delete(json);
    return(size * nmemb);

}	/* writeCallbackInfosChain() */

size_t writeCallbackTransactions
(
    void* contents,
    size_t				 size,
    size_t				 nmemb,
    void* userp
)
{
    cJSON* json;

    printf("Raw response: %.*s\n", (int)(size * nmemb), (char*)contents);

    json = cJSON_ParseWithLength(contents, size * nmemb);
    if (!json)
    {
        printf("JSON parse failed\n");
        return(size * nmemb);
    }

    printf("Transactions fetched\n");
    cJSON_Delete(json);
    return(size * nmemb);

}	/* writeCallbackTransactions() */

size_t writeCallbackBlocks
(
    void* contents,
    size_t				 size,
    size_t				 nmemb,
    void* userp
)
{
    cJSON* json;

    printf("Raw response: %.*s\n", (int)(size * nmemb), (char*)contents);

    json = cJSON_ParseWithLength(contents, size * nmemb);
    if (!json)
    {
        printf("JSON parse failed\n");
        return(size * nmemb);
    }

    printf("Blocks fetched\n");
    cJSON_Delete(json);
    return(size * nmemb);

}	/* writeCallbackBlocks() */

size_t writeCallbackInfosChainParams
(
    void* contents,
    size_t				 size,
    size_t				 nmemb,
    void* userp
)
{
    cJSON* json;

    printf("Raw response: %.*s\n", (int)(size * nmemb), (char*)contents);

    json = cJSON_ParseWithLength(contents, size * nmemb);
    if (!json)
    {
        printf("JSON parse failed\n");
        return(size * nmemb);
    }

    printf("Chain params fetched\n");
    cJSON_Delete(json);
    return(size * nmemb);

}	/* writeCallbackInfosChainParams() */