#if !defined ALPH_BLOCK_H
#define ALPH_BLOCK_H

#include "commands.h"
#include <cjson/cJSON.h>
#include <string>
#include <vector>

#define ALPH_TARGET_POLL_SECONDS ( 16 )
#define ALPH_TARGET_BLOCK_SECONDS ( 8 )

class AlphBlock
{

public:
	int		chainFrom;
	int		chainTo;
	int64_t timestamp;
	std::string hash;
	std::vector<std::string > deps;
	std::vector<std::string> txns;

	AlphBlock()
		: chainFrom(-1)
		, chainTo(-1)
		, timestamp(0)
		, hash()
		, deps()
		, txns() {}

	AlphBlock(cJSON* block)
		: chainFrom(-1)
		, chainTo(-1)
		, timestamp(0)
		, hash()
		, deps()
		, txns()
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

			GET_OBJECT_ITEM(block, transactions);
			if (transactions)
			{
				cJSON* tx;
				cJSON_ArrayForEach(tx, transactions)
				{
					this->txns.push_back(cJSON_Print(tx));
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