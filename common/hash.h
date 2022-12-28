#ifndef HASH_H_
#define HASH_H_

#include <assert.h>

#include <vector>
#include <string>

static const uint32_t hash_prime[5] = {
		8971, 1531, 4079, 3643, 6079
};

uint32_t hashh(std::string data, uint32_t seed){
	assert(seed < 5);
	uint32_t ret = 2654435761U;
	for(uint32_t i = 0;i < data.size();++i){
		ret += (data[i] * hash_prime[seed]);
		ret *= 3266489917U;
		ret ^= ret >> 15;
		ret *= 668265263U;
	}
	return ret;
}

#endif
