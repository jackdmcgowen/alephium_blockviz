#if !defined ALPH_BLOCK_H
#define ALPH_BLOCK_H

#include "commands.h"
#include <cjson/cJSON.h>
#include <string>
#include <vector>
#include <algorithm>

#define ALPH_LOOKBACK_WINDOW_SECONDS ( 10 * 60 )
#define ALPH_TARGET_POLL_SECONDS ( 16 )
#define ALPH_TARGET_BLOCK_SECONDS ( 8 )

#define ALPH_NUM_GROUPS ( 4 )

struct UTXO {
	int hint;
	std::string key;

	std::string attoAlphAmount;
	std::string address;

	// Converts Alephium attoAlphAmount (string of digits, 1 ALPH = 10^18 attoAlph)
	// to a human-readable string with decimal point.
	// Handles very large values safely (no floating point).
	// Examples:
	//   "0"                   -> "0"
	//   "1000000000000000000" -> "1"
	//   "1234567890123456789" -> "1.234567890123456789"
	//   "500000000000000000"  -> "0.5"
	//   "1230000000000000000" -> "1.23"
	//   "42"                  -> "0.000000000000000042"
	std::string toAmount()
	{
		std::string num = attoAlphAmount;

		// Remove any leading zeros (though API usually doesn't send them)
		num.erase(0, num.find_first_not_of('0'));
		if (num.empty()) {
			return "0";
		}

		const size_t decimals = 18;

		std::string result;

		if (num.length() <= decimals) {
			// Amount < 1 ALPH
			result = "0.";
			result.append(decimals - num.length(), '0');
			result += num;
		}
		else {
			// Amount >= 1 ALPH
			result = num.substr(0, num.length() - decimals);
			std::string frac = num.substr(num.length() - decimals);

			// Only add decimal part if it's non-zero
			if (frac.find_first_not_of('0') != std::string::npos) {
				result += "." + frac;
			}
		}

		// Remove trailing zeros after decimal point (and the dot if no decimals remain)
		size_t dot_pos = result.find('.');
		if (dot_pos != std::string::npos) {
			// Trim trailing zeros
			while (!result.empty() && result.back() == '0') {
				result.pop_back();
			}
			// If last char is now '.', remove it
			if (!result.empty() && result.back() == '.') {
				result.pop_back();
			}
		}

		return( result.empty() ? "0" : result );

	}	/* toAmount() */


	//tokens
	//lockTime
	//message
};

class AlphTxn
{

public:
	AlphTxn()
		: txid()
		, version(0)
		, networkId(0)
		, scriptOpt()
		, gasAmount(0)
		, gasPrice() { }

	~AlphTxn() {}

	std::vector<UTXO> inputs;
	std::vector<UTXO> outputs;

	std::string txid;
	int version;
	int networkId;
	std::string scriptOpt;
	int gasAmount;
	std::string gasPrice;

};

class AlphBlock
{

public:
	uint8_t chainFrom;
	uint8_t chainTo;
	int		height;
	int64_t timestamp;
	std::string hash;
	std::vector<std::string> deps;
	std::vector<AlphTxn> txns;
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
			this->hash = hash->valuestring;
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
					AlphTxn txn;

					cJSON* unsig = cJSON_GetObjectItem(tx, "unsigned");
					GET_OBJECT_ITEM(unsig, txID);

					txn.txid = txID->valuestring;
					GET_OBJECT_ITEM(unsig, version);
					txn.version = version->valueint;

					GET_OBJECT_ITEM(unsig, networkId);
					txn.networkId = networkId->valueint;

					//GET_OBJECT_ITEM(unsig, scriptOpt);
					//if( scriptOpt )
					//	txn.scriptOpt = scriptOpt->valuestring;

					GET_OBJECT_ITEM(unsig, gasAmount);
					txn.gasAmount = gasAmount->valueint;

					GET_OBJECT_ITEM(unsig, gasPrice);
					txn.gasPrice = gasPrice->valuestring;

					GET_OBJECT_ITEM(unsig, inputs);
					cJSON* in;
					cJSON_ArrayForEach(in, inputs)
					{
						UTXO input; memset(&input, 0, sizeof(input));

						GET_OBJECT_ITEM(in, outputRef);

						GET_OBJECT_ITEM(outputRef, hint);
						input.hint = hint->valueint;

						GET_OBJECT_ITEM(outputRef, key);
						input.key = key->valuestring;

						txn.inputs.push_back(input);
					}

					GET_OBJECT_ITEM(unsig, fixedOutputs);
					cJSON* out;
					cJSON_ArrayForEach(out, fixedOutputs)
					{
						UTXO output; memset(&output, 0, sizeof(output));

						//GET_OBJECT_ITEM(out, outputRef);

						GET_OBJECT_ITEM(out, hint);
						output.hint = hint->valueint;

						GET_OBJECT_ITEM(out, key);
						output.key = key->valuestring;

						GET_OBJECT_ITEM(out, attoAlphAmount);
						output.attoAlphAmount = attoAlphAmount->valuestring;

						GET_OBJECT_ITEM(out, address);
						output.address = address->valuestring;

						txn.outputs.push_back(output);
					}

					this->txns.push_back(txn);

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