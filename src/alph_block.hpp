#if !defined ALPH_BLOCK_H
#define ALPH_BLOCK_H

#include "commands.h"
#include <cjson/cJSON.h>
#include <string>
#include <vector>

class AlphBlock
{


public:
	int		chainFrom;
	int		chainTo;
	int64_t timestamp;
	std::string hash;
	std::vector<std::string > deps;

	AlphBlock(cJSON* block)
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
			this->timestamp = static_cast<int64_t>(timestamp->valuedouble); //53-bits of precision
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

	~AlphBlock() {};

	bool operator<(const AlphBlock& rhs) const { 
		return timestamp < rhs.timestamp; 
	}

};

#endif	/* ALPH_BLOCK_H */