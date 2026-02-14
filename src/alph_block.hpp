#if !defined ALPH_BLOCK_H
#define ALPH_BLOCK_H

#include "commands.h"
#include <cjson/cJSON.h>
#include <string>
#include <vector>

#define ALPH_LOOKBACK_WINDOW_SECONDS ( 10 * 60 )
#define ALPH_TARGET_POLL_SECONDS ( 16 )
#define ALPH_TARGET_BLOCK_SECONDS ( 8 )

#define ALPH_NUM_GROUPS ( 4 )

class AlphBlock
{

public:
	uint8_t chainFrom;
	uint8_t chainTo;
	int		height;
	int64_t timestamp;
	std::string hash;
	std::vector<std::string> deps;
	std::vector<std::string> txns;
	std::vector<std::string> uncles;

	uint8_t chain_idx() { return( chainFrom * ALPH_NUM_GROUPS + chainTo ); }

	AlphBlock()
		: chainFrom(0xFF)
		, chainTo(0xFF)
		, height(-1)
		, timestamp(0)
		, hash()
		, deps()
		, txns()
	    , uncles() {}

	AlphBlock(cJSON* block)
		: chainFrom(-1)
		, chainTo(-1)
		, timestamp(0)
		, hash()
		, deps()
		, txns()
		, uncles()
	{
		GET_OBJECT_ITEM(block, chainFrom);
		GET_OBJECT_ITEM(block, chainTo);
		GET_OBJECT_ITEM(block, timestamp);
		GET_OBJECT_ITEM(block, hash);
		GET_OBJECT_ITEM(block, deps);
		GET_OBJECT_ITEM(block, height);
		GET_OBJECT_ITEM(block, ghostUncles);

		if (chainFrom && chainTo && timestamp && hash && deps && height )
		{ 
			this->chainFrom = chainFrom->valueint;
			this->chainTo = chainTo->valueint;
			this->timestamp = static_cast<int64_t>(timestamp->valuedouble); //53-bits of precision
			this->hash += hash->valuestring;
			this->height = height->valueint;

			if (cJSON_IsArray(deps))
			{
				cJSON* dep;
				cJSON_ArrayForEach(dep, deps)
				{
					this->deps.push_back(dep->valuestring);
				}
			}

			cJSON* unc;
			cJSON_ArrayForEach(unc, ghostUncles)
			{
				GET_OBJECT_ITEM(unc, blockHash);
				this->uncles.push_back(blockHash->valuestring);
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