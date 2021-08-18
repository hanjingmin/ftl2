#include "../../include/container.h"
#include "../../interface/interface.h"

typedef struct align_gc_buffer{
	uint8_t idx;
	char value[PAGESIZE];
	KEYT key[L2PGAP];
}align_gc_buffer;

typedef struct gc_value{
	uint32_t ppa;
	value_set *value;
	bool isdone;
}gc_value;

void invalidate_ppa(uint32_t t_ppa);
void validate_ppa(uint32_t t_ppa, KEYT *lbas, uint32_t max_idx);
ppa_t get_ppa(KEYT* lba, uint32_t max_idx);
#if 1 //NAM
ppa_t get_ppa_mapflush(KEYT* lba, uint32_t max_idx);
#endif
void do_gc();
void do_map_gc();

void *page_gc_end_req(algo_req *input);
