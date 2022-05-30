//========================================================//
//  cache.c                                               //
//  Source file for the Cache Simulator                   //
//                                                        //
//  Implement the I-cache, D-Cache and L2-cache as        //
//  described in the README                               //
//========================================================//

#include "cache.h"
#include "stdbool.h"
#include "math.h"
#include "assert.h"
#include "stdio.h"

//
// TODO:Student Information
//
const char *studentName = "Yiran Wu";
const char *studentID   = "A59004775";
const char *email       = "yiw073@ucsd.edu";

//------------------------------------//
//        Cache Configuration         //
//------------------------------------//

uint32_t icacheSets;     // Number of sets in the I$
uint32_t icacheAssoc;    // Associativity of the I$
uint32_t icacheHitTime;  // Hit Time of the I$

uint32_t dcacheSets;     // Number of sets in the D$
uint32_t dcacheAssoc;    // Associativity of the D$
uint32_t dcacheHitTime;  // Hit Time of the D$

uint32_t l2cacheSets;    // Number of sets in the L2$
uint32_t l2cacheAssoc;   // Associativity of the L2$
uint32_t l2cacheHitTime; // Hit Time of the L2$
uint32_t inclusive;      // Indicates if the L2 is inclusive

uint32_t blocksize;      // Block/Line size, in Byte
uint32_t memspeed;       // Latency of Main Memory

//------------------------------------//
//          Cache Statistics          //
//------------------------------------//

uint64_t icacheRefs;       // I$ references
uint64_t icacheMisses;     // I$ misses
uint64_t icachePenalties;  // I$ penalties

uint64_t dcacheRefs;       // D$ references
uint64_t dcacheMisses;     // D$ misses
uint64_t dcachePenalties;  // D$ penalties

uint64_t l2cacheRefs;      // L2$ references
uint64_t l2cacheMisses;    // L2$ misses
uint64_t l2cachePenalties; // L2$ penalties

//------------------------------------//
//        Cache Data Structures       //
//------------------------------------//

const double EPS=1e-8;
uint64_t cur_time=0;

struct AssocCache {
    uint32_t n_set, assoc, hit_time;
    uint32_t **tags, **times;
    uint32_t index_len, tag_len;
    uint64_t *ref_ptr, *miss_ptr, *penalty_ptr;
}icache, dcache, l2cache;

uint32_t offset_len;

//------------------------------------//
//          Cache Functions           //
//------------------------------------//

void malloc_cache(struct AssocCache *cache) {
    //printf("malloc cache\n");
    cache->tags = (uint32_t **) malloc(cache->n_set * sizeof(uint32_t *));
    cache->times = (uint32_t **) malloc(cache->n_set * sizeof(uint32_t *));
    for(int i=0;i<cache->n_set;++i) {
        cache->tags[i] = (uint32_t *) malloc(cache->assoc * sizeof(uint32_t));
        cache->times[i] = (uint32_t *) malloc(cache->assoc * sizeof(uint32_t));
        for (int j = 0; j < cache->assoc; ++j) cache->times[i][j] = 0;
    }
    //printf("malloc cache done\n");
}


// Initialize the Cache Hierarchy
//
void
init_cache()
{
  // Initialize cache stats
  icacheRefs        = 0;
  icacheMisses      = 0;
  icachePenalties   = 0;
  dcacheRefs        = 0;
  dcacheMisses      = 0;
  dcachePenalties   = 0;
  l2cacheRefs       = 0;
  l2cacheMisses     = 0;
  l2cachePenalties  = 0;

    offset_len = floor(log2(blocksize) + EPS);
    if(icacheAssoc==0) icacheAssoc=1;
    if(dcacheAssoc==0) dcacheAssoc=1;
    if(l2cacheAssoc==0) l2cacheAssoc=1;

    icache.n_set = icacheSets;
    icache.assoc = icacheAssoc;
    icache.hit_time = icacheHitTime;
    icache.index_len = floor(log2(icacheSets) + EPS);
    icache.tag_len = floor(32-icache.index_len-offset_len+EPS);
    //printf("offset len: %d\n", offset_len);
    //printf("icache index_len: %d\n", icache.index_len);
    //printf("icache tag_len: %d\n", icache.tag_len);
    //return;
    malloc_cache(&icache);
    icache.ref_ptr = &icacheRefs;
    icache.miss_ptr = &icacheMisses;
    icache.penalty_ptr = &icachePenalties;

    dcache.n_set = dcacheSets;
    dcache.assoc = dcacheAssoc;
    dcache.hit_time = dcacheHitTime;
    dcache.index_len = floor(log2(dcacheSets) + EPS);
    dcache.tag_len = floor(32-dcache.index_len-offset_len+EPS);
    malloc_cache(&dcache);
    dcache.ref_ptr = &dcacheRefs;
    dcache.miss_ptr = &dcacheMisses;
    dcache.penalty_ptr = &dcachePenalties;

    l2cache.n_set = l2cacheSets;
    l2cache.assoc = l2cacheAssoc;
    l2cache.hit_time = l2cacheHitTime;
    l2cache.index_len = floor(log2(l2cacheSets) + EPS);
    l2cache.tag_len = floor(32-l2cache.index_len-offset_len+EPS);
    malloc_cache(&l2cache);
    l2cache.ref_ptr = &l2cacheRefs;
    l2cache.miss_ptr = &l2cacheMisses;
    l2cache.penalty_ptr = &l2cachePenalties;
    //printf("init cache done\n");

}

inline uint32_t get_index(struct AssocCache *cache, uint32_t addr) {
    return (addr >> offset_len) & ((1<<cache->index_len)-1);
}

inline uint32_t get_tag(struct AssocCache *cache, uint32_t addr) {
    return addr >> (32-cache->tag_len);
}

inline uint32_t assemble_addr(struct AssocCache *cache, uint32_t index, uint32_t tag) {
    return ((tag << cache->index_len) | index) << offset_len;
}

void cache_has(struct AssocCache *cache, uint32_t addr, bool *has_flag, uint32_t **evict_time_pptr, uint32_t **evict_tag_pptr) {
    //printf("cache has\n");
    uint32_t index = get_index(cache, addr);
    uint32_t tag = get_tag(cache, addr);
    //printf("index: %d, tag: %d\n", index, tag);
    *has_flag = false;
    *evict_time_pptr = &cache->times[index][0];
    *evict_tag_pptr = &cache->tags[index][0];
    for(int i=0;i<cache->assoc;++i) {
        //printf("checking way %d\n", i);
        //printf("time: %d\n", cache->times[index][i]);
        //printf("cache tag: %d\n", cache->tags[index][i]);
        if(cache->times[index][i] !=0 && cache->tags[index][i] == tag) {
            *has_flag = true;
            cache->times[index][i] = cur_time;
            //printf("cache has done\n");
            return;
        }
        //printf("second if\n");
        if(cache->times[index][i] < **evict_time_pptr) {
            //printf("second if cond done\n");
            *evict_time_pptr = &(cache->times[index][i]);
            *evict_tag_pptr = &(cache->tags[index][i]);
        }
    }
    if(cache == &icache) {
        //if (!*has_flag)
        //    printf("icache missing cache index: %d, tag: %d\n", index, tag);
        //printf("icache decided to evict index: %d, tag: %d, time: %d, target tag:%d\n", index, **evict_tag_pptr, **evict_time_pptr, addr);

    }
    if(cache == &dcache) {

        //if (!*has_flag)
        //    printf("dcache missing cache index: %d, tag: %d\n", index, tag);
        //printf("dcache decided to evict index: %d, tag: %d, time: %d, target tag:%d\n", index, **evict_tag_pptr, **evict_time_pptr, addr);
    }
    //printf("cache has done\n");
}

void l2cache_add(uint32_t addr) {
    //printf("l2cache add index: %d, tag: %d", get_index(&l2cache, addr), get_tag(&l2cache, addr));
    bool has_flag;
    uint32_t *evict_time_ptr, *evict_tag_ptr;
    cache_has(&l2cache, addr, &has_flag, &evict_time_ptr, &evict_tag_ptr);

    (*evict_time_ptr) = cur_time;
    (*evict_tag_ptr) = get_tag(&l2cache, addr);
}
/*
void l2cache_evict(uint32_t addr) {

    bool has_flag;
    uint32_t *evict_time_ptr, *evict_tag_ptr;
    cache_has(&l2cache, addr, &has_flag, &evict_time_ptr, &evict_tag_ptr);

    assert(has_flag);
    *evict_time_ptr = 0;

}
*/
// Perform a memory access through the icache interface for the address 'addr'
// Return the access time for the memory operation
//
uint32_t
icache_access(uint32_t addr)
{
    //printf("icache access\n");
    //printf("icache access on index:%d, tag:%d\n", get_index(&icache, addr), get_tag(&icache, addr));
    ++cur_time;
    ++(*icache.ref_ptr);

    bool has_flag;
    uint32_t *evict_time_ptr, *evict_tag_ptr;
    cache_has(&icache, addr, &has_flag, &evict_time_ptr, &evict_tag_ptr);

    if(has_flag) {
        //printf("icache access done\n");
        return icache.hit_time;
    }
    else {
        uint64_t penalty = l2cache_access(addr);
        ++(*icache.miss_ptr);
        (*icache.penalty_ptr) += penalty;
        //l2cache_evict(addr);
        //printf("icache evicting tag: %d, time: %d\n", *evict_tag_ptr, *evict_time_ptr);
        //if(*evict_time_ptr) {
        //    uint32_t evicted_addr = assemble_addr(&icache, get_index(&icache, addr), *evict_tag_ptr);
        //    l2cache_add(evicted_addr);
        //}
        *evict_tag_ptr = get_tag(&icache, addr);
        *evict_time_ptr = cur_time;
        //printf("icache access done\n");
        return icache.hit_time + penalty;
    }
}

// Perform a memory access through the dcache interface for the address 'addr'
// Return the access time for the memory operation
//
uint32_t
dcache_access(uint32_t addr)
{
    //printf("dcache access\n");
    //printf("dcache access on index:%d, tag:%d\n", get_index(&dcache, addr), get_tag(&dcache, addr));
    ++cur_time;
    ++(*dcache.ref_ptr);

    bool has_flag;
    uint32_t *evict_time_ptr, *evict_tag_ptr;
    cache_has(&dcache, addr, &has_flag, &evict_time_ptr, &evict_tag_ptr);

    if(has_flag) {
        //printf("dcache access done\n");
        return dcache.hit_time;
    }
    else {
        uint64_t penalty = l2cache_access(addr);
        ++(*dcache.miss_ptr);
        (*dcache.penalty_ptr) += penalty;
        //l2cache_evict(addr);
        //printf("dcache evicting tag: %d, time: %d\n", *evict_tag_ptr, *evict_time_ptr);
        //if(*evict_time_ptr) {
        //    uint32_t evicted_addr = assemble_addr(&dcache, get_index(&dcache, addr), *evict_tag_ptr);
        //    l2cache_add(evicted_addr);
        //}
        *evict_tag_ptr = get_tag(&dcache, addr);
        *evict_time_ptr = cur_time;
        //printf("dcache access done\n");
        return dcache.hit_time + penalty;
    }
}

// Perform a memory access to the l2cache for the address 'addr'
// Return the access time for the memory operation
//
uint32_t
l2cache_access(uint32_t addr)
{
    //printf("l2cache access on index:%d, tag:%d\n", get_index(&l2cache, addr), get_tag(&l2cache, addr));
    ++(*l2cache.ref_ptr);

    bool has_flag;
    uint32_t *evict_time_ptr, *evict_tag_ptr;
    cache_has(&l2cache, addr, &has_flag, &evict_time_ptr, &evict_tag_ptr);

    if(has_flag) {

        //printf("l2cache access done\n");
        //printf("l2cache hit!\n");
        return l2cache.hit_time;
    }
    //printf("l2cache mis!\n");
    ++(*l2cache.miss_ptr);
    (*l2cache.penalty_ptr) += memspeed;
    l2cache_add(addr);

    //printf("l2cache access\n");
    return l2cache.hit_time + memspeed;
}
