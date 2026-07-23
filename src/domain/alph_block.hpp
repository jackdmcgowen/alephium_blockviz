#if !defined ALPH_BLOCK_H
#define ALPH_BLOCK_H

#include "network/commands.h"
#include <cjson/cJSON.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define ALPH_TARGET_POLL_SECONDS ( 16 )
#define ALPH_TARGET_BLOCK_SECONDS ( 8 )
// Atomic timeline subsegment (HTTP interval + disk chunk key), seconds.
#define ALPH_SUBSEGMENT_SECONDS ( 64 )
// Genesis-aligned G-segment length: exactly 10 subsegments (640s).
#define ALPH_LOOKBACK_WINDOW_SECONDS ( 10 * ALPH_SUBSEGMENT_SECONDS )
#define ALPH_SUBSEGMENT_MS \
    ( static_cast<int64_t>(ALPH_SUBSEGMENT_SECONDS) * 1000 )

// Three-ring segment management (genesis-aligned G windows of LOOKBACK seconds):
//   Schedule/load ring : which G windows may be prefetched (network/disk schedule)
//   Admit ring         : which G bodies may enter BlockScene RAM (lazy disk admit)
//   Render ring        : draw / planes corridor (≤ admit)
//   Live               : open tip subsegment on the same 64s grid (not a separate length)
#define ALPH_LOAD_RING_SEGMENTS   ( 15 )
#define ALPH_RENDER_RING_HALF     ( 3 )
#define ALPH_RENDER_RING_SEGMENTS ( (2 * ALPH_RENDER_RING_HALF) + 1 ) /* 7 */
// Disk/network body admit: |lookback_k - cam_k| ≤ half (or live edge). Schedule may be wider.
#define ALPH_DISK_ADMIT_RING_HALF ( ALPH_RENDER_RING_HALF ) /* 3 → 7-wide admit */
#define ALPH_DISK_ADMIT_RING_SEGMENTS ( (2 * ALPH_DISK_ADMIT_RING_HALF) + 1 )
// Live HTTP unit = full open subsegment (same as ALPH_SUBSEGMENT_MS).
#define ALPH_LIVE_POLL_EDGE_MS    ( ALPH_SUBSEGMENT_MS )

// Fallback genesis / chain-start if height-0 block fetch fails.
// Docs: genesis block ts themed 2009-01-03; mainnet launch 2021-11-08.
// Prefer API-resolved timestamp from height 0 when available.
#define ALPH_GENESIS_TIMESTAMP_MS_FALLBACK  ( 1230940800000LL ) /* 2009-01-03 UTC */
#define ALPH_MAINNET_LAUNCH_MS_FALLBACK     ( 1636329600000LL ) /* 2021-11-08 UTC */

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
	// Sum of output attoAlphAmount (digits only); empty if unknown.
	std::string alph_out_atto;

};

// Non-negative decimal digit strings (atto). Empty treated as "0".
inline std::string alph_add_atto(const std::string& a_in, const std::string& b_in)
{
	std::string a = a_in.empty() ? "0" : a_in;
	std::string b = b_in.empty() ? "0" : b_in;
	// Strip non-digits / leading zeros noise from API.
	auto clean = [](std::string& s) {
		std::string o;
		o.reserve(s.size());
		for (char c : s)
			if (c >= '0' && c <= '9')
				o.push_back(c);
		while (o.size() > 1 && o[0] == '0')
			o.erase(o.begin());
		if (o.empty())
			o = "0";
		s = std::move(o);
	};
	clean(a);
	clean(b);
	if (a.size() < b.size())
		std::swap(a, b);
	std::string out;
	out.resize(a.size() + 1, '0');
	int carry = 0;
	int i = static_cast<int>(a.size()) - 1;
	int j = static_cast<int>(b.size()) - 1;
	int k = static_cast<int>(out.size()) - 1;
	while (i >= 0 || j >= 0 || carry)
	{
		int sum = carry;
		if (i >= 0)
			sum += a[static_cast<size_t>(i--)] - '0';
		if (j >= 0)
			sum += b[static_cast<size_t>(j--)] - '0';
		out[static_cast<size_t>(k--)] = static_cast<char>('0' + (sum % 10));
		carry = sum / 10;
	}
	size_t start = 0;
	while (start + 1 < out.size() && out[start] == '0')
		++start;
	return out.substr(start);
}

inline int alph_cmp_atto(const std::string& a_in, const std::string& b_in)
{
	std::string a = a_in.empty() ? "0" : a_in;
	std::string b = b_in.empty() ? "0" : b_in;
	auto clean = [](std::string& s) {
		std::string o;
		for (char c : s)
			if (c >= '0' && c <= '9')
				o.push_back(c);
		while (o.size() > 1 && o[0] == '0')
			o.erase(o.begin());
		if (o.empty())
			o = "0";
		s = std::move(o);
	};
	clean(a);
	clean(b);
	if (a.size() != b.size())
		return a.size() < b.size() ? -1 : 1;
	if (a < b)
		return -1;
	if (a > b)
		return 1;
	return 0;
}

// Human ALPH (e.g. 1.5) → atto digit string (filter thresholds; not consensus-critical).
inline std::string alph_from_double_to_atto(double alph)
{
	if (!(alph > 0.0) || !std::isfinite(alph))
		return "0";
	const double whole_d = std::floor(alph);
	double frac = alph - whole_d;
	unsigned long long whole = static_cast<unsigned long long>(whole_d);
	frac = frac * 1e18 + 0.5;
	unsigned long long frac_u = static_cast<unsigned long long>(frac);
	if (frac_u >= 1000000000000000000ULL)
	{
		frac_u = 0;
		++whole;
	}
	std::string s = std::to_string(whole);
	char fracbuf[32];
	std::snprintf(fracbuf, sizeof(fracbuf), "%018llu",
	              static_cast<unsigned long long>(frac_u));
	if (s == "0")
	{
		// Pure fraction: strip leading zeros of 18-digit frac.
		std::string f(fracbuf);
		while (f.size() > 1 && f[0] == '0')
			f.erase(f.begin());
		return f;
	}
	return s + fracbuf;
}

inline std::string alph_atto_to_display(const std::string& atto)
{
	UTXO u;
	u.attoAlphAmount = atto.empty() ? "0" : atto;
	return u.toAmount();
}

inline std::string alph_sum_txn_outputs(const AlphTxn& tx)
{
	if (!tx.alph_out_atto.empty())
		return tx.alph_out_atto;
	std::string sum = "0";
	for (const UTXO& o : tx.outputs)
		sum = alph_add_atto(sum, o.attoAlphAmount);
	return sum;
}

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
	// Preserved across detail slim (txns payload cleared); -1 = never parsed.
	int txn_count = -1;
	// Sum of all txn output ALPH (atto digits); survives slim; empty = unknown.
	std::string alph_out_atto;

	uint8_t chain_idx() const { return static_cast<uint8_t>(chainFrom * ALPH_NUM_GROUPS + chainTo); }

	AlphBlock()
		: chainFrom(0xFF)
		, chainTo(0xFF)
		, height(-1)
		, timestamp(0)
		, hash()
		, deps()
		, txns()
	    , uncles()
		, txn_count(-1)
		, alph_out_atto() {}

	AlphBlock(cJSON* block)
		: chainFrom(-1)
		, chainTo(-1)
		, height(-1)
		, timestamp(0)
		, hash()
		, deps()
		, txns()
		, uncles()
		, txn_count(-1)
		, alph_out_atto()
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
			if (transactions && cJSON_IsArray(transactions))
			{
				// API array size is authoritative for billboard/UI even after slim clears txns.
				this->txn_count = cJSON_GetArraySize(transactions);
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

					txn.alph_out_atto = alph_sum_txn_outputs(txn);
					this->txns.push_back(std::move(txn));

				}
				// Block total ALPH out (all txn outputs); kept when txns slimmed.
				this->alph_out_atto = "0";
				for (const AlphTxn& t : this->txns)
					this->alph_out_atto = alph_add_atto(this->alph_out_atto, t.alph_out_atto);
			}

		}

	}

	~AlphBlock() {};

	bool operator<(const AlphBlock& rhs) const { 
		return timestamp < rhs.timestamp; 
	}

};

inline std::string alph_sum_block_outputs(const AlphBlock& b)
{
	if (!b.alph_out_atto.empty())
		return b.alph_out_atto;
	std::string sum = "0";
	for (const AlphTxn& tx : b.txns)
		sum = alph_add_atto(sum, alph_sum_txn_outputs(tx));
	return sum;
}

#endif	/* ALPH_BLOCK_H */