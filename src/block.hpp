#if !defined BLOCK_H
#define BLOCK_H

#include "commands.h"
#include <cjson/cJSON.h>
#include <string>
#include <vector>

class Block
{


public:
	Block(cJSON* block)
		: chainFrom(-1)
		, chainTo(-1)
		, timestamp(0)
		, hash()
		, deps()
	{
		GET_OBJECT_ITEM(block, chainFrom);
		GET_OBJECT_ITEM(block, chainTo);
		GET_OBJECT_ITEM(block, timestamp);
		GET_OBJECT_ITEM(block, hash);
		GET_OBJECT_ITEM(block, deps);


		if (chainFrom && chainTo && timestamp && hash && deps )
		{ 
			this->chainFrom = chainFrom->valueint;
			this->chainTo = chainTo->valueint;
			this->timestamp = timestamp->valueint;
			this->hash += hash->valuestring;

			if (cJSON_IsArray(deps))
			{
				cJSON* dep;
				cJSON_ArrayForEach(dep, deps)
				{
					this->deps.push_back(dep->valuestring);
				}
			}

		}

	}

	~Block() {};

	int from_group()			  { return chainFrom; }
	int to_group()				  { return chainTo; }
	int64_t get_timestamp() const { return timestamp; }
	const char* get_hash() const  { return hash.c_str(); }

private:

	int		chainTo;
	int		chainFrom;
	std::string hash;
	int64_t timestamp;
	std::vector<std::string > deps;

};

#endif	/* BLOCK_H */