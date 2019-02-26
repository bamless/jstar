#include "rand.h"

#include <time.h>

#define STATE_VECTOR_LENGTH 624
#define STATE_VECTOR_M      397

typedef struct tagMTRand {
	unsigned long mt[STATE_VECTOR_LENGTH];
	int index;
} MTRand;

#define UPPER_MASK    	 0x80000000
#define LOWER_MASK       0x7fffffff
#define TEMPERING_MASK_B 0x9d2c5680
#define TEMPERING_MASK_C 0xefc60000

static void m_seedRand(MTRand* rand, unsigned long seed) {
	rand->mt[0] = seed & 0xffffffff;
	for(rand->index=1; rand->index<STATE_VECTOR_LENGTH; rand->index++) {
		rand->mt[rand->index] = (6069 * rand->mt[rand->index-1]) & 0xffffffff;
	}
}

MTRand seedRand(unsigned long seed) {
	MTRand rand;
	m_seedRand(&rand, seed);
	return rand;
}

unsigned long genRandLong(MTRand* rand) {
	unsigned long y;
	static unsigned long mag[2] = {0x0, 0x9908b0df}; /* mag[x] = x * 0x9908b0df for x = 0,1 */
	if(rand->index >= STATE_VECTOR_LENGTH || rand->index < 0) {
	/* generate STATE_VECTOR_LENGTH words at a time */
	int kk;
	if(rand->index >= STATE_VECTOR_LENGTH+1 || rand->index < 0) {
	  m_seedRand(rand, 4357);
	}
	for(kk=0; kk<STATE_VECTOR_LENGTH-STATE_VECTOR_M; kk++) {
	  y = (rand->mt[kk] & UPPER_MASK) | (rand->mt[kk+1] & LOWER_MASK);
	  rand->mt[kk] = rand->mt[kk+STATE_VECTOR_M] ^ (y >> 1) ^ mag[y & 0x1];
	}
	for(; kk<STATE_VECTOR_LENGTH-1; kk++) {
	  y = (rand->mt[kk] & UPPER_MASK) | (rand->mt[kk+1] & LOWER_MASK);
	  rand->mt[kk] = rand->mt[kk+(STATE_VECTOR_M-STATE_VECTOR_LENGTH)] ^ (y >> 1) ^ mag[y & 0x1];
	}
	y = (rand->mt[STATE_VECTOR_LENGTH-1] & UPPER_MASK) | (rand->mt[0] & LOWER_MASK);
	rand->mt[STATE_VECTOR_LENGTH-1] = rand->mt[STATE_VECTOR_M-1] ^ (y >> 1) ^ mag[y & 0x1];
	rand->index = 0;
	}
	y = rand->mt[rand->index++];
	y ^= (y >> 11);
	y ^= (y << 7) & TEMPERING_MASK_B;
	y ^= (y << 15) & TEMPERING_MASK_C;
	y ^= (y >> 18);
	return y;
}

double genRand(MTRand* rand) {
	return((double)genRandLong(rand) / (unsigned long)0xffffffff);
}

// For now there is only one global random generator (this will change)
static MTRand random;

NATIVE(bl_random) {
	BL_RETURN(NUM_VAL(genRand(&random)));
}

NATIVE(bl_initseed) {
	random = seedRand(time(NULL));
	BL_RETURN(NULL_VAL);
}
