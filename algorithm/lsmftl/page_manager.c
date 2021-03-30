#include "page_manager.h"
#include "io.h"
#include "lsmtree.h"
#include <stdlib.h>
#include <stdio.h>

extern lsmtree LSM;
uint32_t debug_piece_ppa=1668250;
extern uint32_t debug_lba;
static int test_cnt;

void validate_piece_ppa(blockmanager *bm, uint32_t piece_num, uint32_t *piece_ppa,
		uint32_t *lba, bool should_abort){
	for(uint32_t i=0; i<piece_num; i++){
		char *oob=bm->get_oob(bm, PIECETOPPA(piece_ppa[i]));
		memcpy(&oob[(piece_ppa[i]%2)*sizeof(uint32_t)], &lba[i], sizeof(uint32_t));
		if(piece_ppa[i]==debug_piece_ppa){
			EPRINT("validate here!\n", false);
		}
		if(!bm->populate_bit(bm, piece_ppa[i]) && should_abort){
			EPRINT("bit error", true);
		}
	}
}

bool page_manager_oob_lba_checker(page_manager *pm, uint32_t piece_ppa, uint32_t lba, uint32_t *idx){
	char *oob=pm->bm->get_oob(pm->bm, PIECETOPPA(piece_ppa));
	for(uint32_t i=0; i<L2PGAP; i++){
		if((*(uint32_t*)&oob[i*sizeof(uint32_t)])==lba){
			*idx=i;
			return true;
		}
	}
	*idx=L2PGAP;
	return false;
}

void invalidate_piece_ppa(blockmanager *bm, uint32_t piece_ppa, bool should_abort){
	if(piece_ppa==debug_piece_ppa){
		EPRINT("invalidate here!\n", false);
	}
	if(!bm->unpopulate_bit(bm, piece_ppa) && should_abort){
		EPRINT("bit error", true);
	}
}

page_manager* page_manager_init(struct blockmanager *_bm){
	page_manager *pm=(page_manager*)calloc(1,sizeof(page_manager));
	pm->bm=_bm;
	pm->current_segment[DATA_S]=_bm->get_segment(_bm, false);
	pm->seg_type_checker[pm->current_segment[DATA_S]->seg_idx]=SEPDATASEG;
	pm->reserve_segment[DATA_S]=_bm->get_segment(_bm, true);

	pm->current_segment[MAP_S]=_bm->get_segment(_bm, false);
	pm->seg_type_checker[pm->current_segment[MAP_S]->seg_idx]=MAPSEG;
	pm->reserve_segment[MAP_S]=_bm->get_segment(_bm, true);
	return pm;
}

void page_manager_free(page_manager* pm){
	free(pm->current_segment[DATA_S]);
	free(pm->current_segment[MAP_S]);
	free(pm->reserve_segment);
	free(pm);
}

void validate_map_ppa(blockmanager *bm, uint32_t map_ppa, uint32_t lba, bool should_abort){
	char *oob=bm->get_oob(bm, map_ppa);
	((uint32_t*)oob)[0]=lba;
	((uint32_t*)oob)[1]=UINT32_MAX;
	if(map_ppa*L2PGAP==debug_piece_ppa || map_ppa*L2PGAP+1==debug_piece_ppa){
			EPRINT("validate map here!\n",false);
	}
	if(!bm->populate_bit(bm, map_ppa*L2PGAP) && should_abort){
		EPRINT("bit error", true);
	}

	if(!bm->populate_bit(bm, map_ppa*L2PGAP+1) && should_abort){
		EPRINT("bit error", true);
	}
}

void invalidate_map_ppa(blockmanager *bm, uint32_t map_ppa, bool should_abort){
	if(map_ppa*L2PGAP==debug_piece_ppa || map_ppa*L2PGAP+1==debug_piece_ppa){
			EPRINT("invalidate map here!\n", false);
	}
	if(!bm->unpopulate_bit(bm, map_ppa*L2PGAP) && should_abort){
		EPRINT("bit error", true);
	} 

	if(!bm->unpopulate_bit(bm, map_ppa*L2PGAP+1) && should_abort){
		EPRINT("bit error", true);
	}
}

uint32_t page_manager_get_new_ppa(page_manager *pm, bool is_map, uint32_t type){
	uint32_t res;
	blockmanager *bm=pm->bm;
retry:
	__segment *seg=is_map?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];
	if(bm->check_full(bm, seg, MASTER_PAGE)){
		if(bm->is_gc_needed(bm)){
			//EPRINT("before get ppa, try to gc!!\n", true);
			if(__do_gc(pm,is_map, is_map?1:2)){ //just trim
				free(seg);
				pm->current_segment[is_map?MAP_S:DATA_S]=bm->get_segment(bm,false);
			}
			else{ //copy trim
				
			}
		}
		else{
			free(seg);
			pm->current_segment[is_map?MAP_S:DATA_S]=bm->get_segment(bm,false);
		}

		pm->seg_type_checker[pm->current_segment[is_map?MAP_S:DATA_S]->seg_idx]=type;
		goto retry;
	}
	res=bm->get_page_num(bm, seg);
	return res;
}

uint32_t page_manager_pick_new_ppa(page_manager *pm, bool is_map, uint32_t type){
	blockmanager *bm=pm->bm;
retry:
	__segment *seg=is_map?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];
	if(bm->check_full(bm, seg, MASTER_PAGE)){
		if(bm->is_gc_needed(bm)){
			//EPRINT("before get ppa, try to gc!!\n", true);
			if(__do_gc(pm,is_map, is_map?1:2)){ //just trim
				free(seg);
				pm->current_segment[is_map?MAP_S:DATA_S]=bm->get_segment(bm,false);
			}
			else{ //copy trim
				
			}
		}
		else{
			free(seg);
			pm->current_segment[is_map?MAP_S:DATA_S]=bm->get_segment(bm,false);
		}

		pm->seg_type_checker[pm->current_segment[is_map?MAP_S:DATA_S]->seg_idx]=type;
		goto retry;
	}
	return bm->pick_page_num(bm, seg);
}

bool page_manager_is_gc_needed(page_manager *pm, uint32_t needed_page, 
		bool is_map){
	blockmanager *bm=pm->bm;
	__segment *seg=is_map?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];
	if(seg->used_page_num + needed_page>= _PPS) return true;
	return bm->check_full(bm, seg, MASTER_PAGE) && bm->is_gc_needed(bm); 
}


uint32_t page_manager_get_remain_page(page_manager *pm, bool ismap){
	if(ismap){
		return _PPS-pm->current_segment[MAP_S]->used_page_num;
	}
	else{
		return _PPS-pm->current_segment[DATA_S]->used_page_num;
	}
}

uint32_t page_manager_get_total_remain_page(page_manager *pm, bool ismap){
	if(ismap){
		return pm->bm->remain_free_page(pm->bm, pm->current_segment[MAP_S]);
	}
	else{
		return pm->bm->remain_free_page(pm->bm, pm->current_segment[DATA_S])*2;
	}
}

uint32_t page_manager_get_reserve_new_ppa(page_manager *pm, bool ismap, uint32_t seg_idx){
	blockmanager *bm=pm->bm;
retry:
	__segment *seg=ismap?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];

	if(bm->check_full(bm, seg, MASTER_PAGE) || seg->seg_idx==seg_idx){
		free(seg);
		pm->current_segment[ismap?MAP_S:DATA_S]=pm->reserve_segment[ismap?MAP_S:DATA_S];
		pm->reserve_segment[ismap?MAP_S:DATA_S]=NULL;
		//pm->current_segment[ismap?MAP_S:DATA_S]=bm->get_segment(bm,false);
		//pm->seg_type_checker[pm->current_segment[is_map?MAP_S:DATA_S]->seg_idx]=is_map?MAPSEG:DATASEG;
		goto retry;
	}
	return bm->get_page_num(bm, seg);
}

uint32_t page_manager_get_reserve_remain_ppa(page_manager *pm, bool ismap, uint32_t seg_idx){
	blockmanager *bm=pm->bm;
retry:
	__segment *seg=ismap?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];
	
	if(bm->check_full(bm, seg, MASTER_PAGE) || seg->seg_idx==seg_idx){
		free(seg);
		pm->current_segment[ismap?MAP_S:DATA_S]=pm->reserve_segment[ismap?MAP_S:DATA_S];
		pm->reserve_segment[ismap?MAP_S:DATA_S]=NULL;
		//pm->current_segment[ismap?MAP_S:DATA_S]=bm->get_segment(bm,false);
		//pm->seg_type_checker[pm->current_segment[is_map?MAP_S:DATA_S]->seg_idx]=is_map?MAPSEG:DATASEG;
		goto retry;
	}
	return _PPS-seg->used_page_num;
}

uint32_t page_manager_change_reserve(page_manager *pm, bool ismap){
	blockmanager *bm=pm->bm;
	if(pm->reserve_segment[ismap?MAP_S:DATA_S]==NULL){
		pm->reserve_segment[ismap?MAP_S:DATA_S]=bm->change_reserve(bm, pm->current_segment[ismap?MAP_S:DATA_S]);
	}
	return 1;
}



uint32_t page_manager_move_next_seg(page_manager *pm, bool ismap, bool isreserve, uint32_t type){
	if(isreserve){
		pm->current_segment[ismap?MAP_S:DATA_S]=pm->reserve_segment[ismap?MAP_S:DATA_S];
		pm->reserve_segment[ismap?MAP_S:DATA_S]=NULL;
	}
	else{
		pm->current_segment[ismap?MAP_S:DATA_S]=pm->bm->get_segment(pm->bm, false);
		pm->seg_type_checker[pm->current_segment[ismap?MAP_S:DATA_S]->seg_idx]=type;
	}
}


bool __gc_mapping(page_manager *pm, blockmanager *bm, __gsegment *victim);
bool __gc_data(page_manager *pm, blockmanager *bm, __gsegment *victim);

void *gc_end_req(algo_req* req);
void *gc_map_check_end_req(algo_req *req);

static inline gc_read_node *gc_read_node_init(bool ismapping, uint32_t ppa){
	gc_read_node *res=(gc_read_node*)malloc(sizeof(gc_read_node));
	res->is_mapping=ismapping;
	res->data=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
	res->piece_ppa=ppa;
	res->lba=UINT32_MAX;
	fdriver_lock_init(&res->done_lock, 0);
	return res;
}

static inline void gc_read_node_free(gc_read_node *gn){
	//inf_free_valueset(gn->data, FS_MALLOC_R);
	fdriver_destroy(&gn->done_lock);
	free(gn);
}

static void gc_issue_read_node(gc_read_node *gn, lower_info *li){
	algo_req *req=(algo_req*)malloc(sizeof(algo_req));
	req->param=(void*)gn;
	req->end_req=gc_end_req;
	if(gn->is_mapping){
		req->type=GCMR;
		li->read(gn->piece_ppa, PAGESIZE, gn->data, ASYNC, req);
	}
	else{
		req->type=GCDR;
		li->read(PIECETOPPA(gn->piece_ppa), PAGESIZE, gn->data, ASYNC, req);
	}
}

static void gc_issue_write_node(uint32_t ppa, value_set *data, bool ismap, lower_info *li){
	algo_req *req=(algo_req*)malloc(sizeof(algo_req));
	req->param=(void*)data;
	req->type=ismap?GCMW:GCDW;
	req->end_req=gc_end_req;
	li->write(ppa, PAGESIZE, data, ASYNC, req);
}

static void gc_issue_mapcheck_read(gc_mapping_check_node *gmc, lower_info *li){
	algo_req *req=(algo_req*)malloc(sizeof(algo_req));
	req->param=(void*)gmc;
	req->end_req=gc_map_check_end_req;
	req->type=GCMR;
	li->read(gmc->map_ppa, PAGESIZE, gmc->mapping_data, ASYNC, req);
}

void *gc_map_check_end_req(algo_req *req){
	gc_mapping_check_node *gmc=(gc_mapping_check_node*)req->param;
	fdriver_unlock(&gmc->done_lock);
	free(req);
	return NULL;
}

void *gc_end_req(algo_req *req){
	gc_read_node *gn;
	value_set *v;
	switch(req->type){
		case GCMR:
		case GCDR:
			gn=(gc_read_node*)req->param;
			fdriver_unlock(&gn->done_lock);
			break;
		case GCMW:
			v=(value_set*)req->param;
			inf_free_valueset(v, FS_MALLOC_R);
			break;
		case GCDW:

			break;
	}
	free(req);
	return NULL;
}


bool  __do_gc(page_manager *pm, bool ismap, uint32_t target_page_num){
	bool res;
	__gsegment *victim_target;
	std::queue<uint32_t> temp_queue;
	uint32_t seg_idx;
	uint32_t remain_page=0;
retry:
	victim_target=pm->bm->get_gc_target(pm->bm);
	if(!victim_target){
		EPRINT("wtf??? no block!", true);
	}
	seg_idx=victim_target->seg_idx;
	if(ismap && pm->seg_type_checker[seg_idx]!=MAPSEG){
		temp_queue.push(seg_idx);
		goto retry;
	}
	else if(!ismap && (pm->seg_type_checker[seg_idx]!=DATASEG &&
					pm->seg_type_checker[seg_idx]!=SEPDATASEG)){
		temp_queue.push(seg_idx);
		goto retry;
	}

	switch(pm->seg_type_checker[seg_idx]){
		case DATASEG:
		case SEPDATASEG:
			res=__gc_data(pm, pm->bm, victim_target);
			remain_page=pm->bm->remain_free_page(pm->bm, pm->current_segment[DATA_S])*L2PGAP;
			break;
		case MAPSEG:
			res=__gc_mapping(pm, pm->bm, victim_target);
			remain_page=pm->bm->remain_free_page(pm->bm, pm->current_segment[MAP_S]);
			break;
	}

	if(remain_page<target_page_num)
		goto retry;

	while(temp_queue.size()){
		seg_idx=temp_queue.front();
		pm->bm->reinsert_segment(pm->bm, seg_idx);
		temp_queue.pop();
	}
	return res;
}

bool __gc_mapping(page_manager *pm, blockmanager *bm, __gsegment *victim){
	if(victim->invalidate_number==_PPS*2 || victim->all_invalid){
		bm->trim_segment(bm, victim, bm->li);
		page_manager_change_reserve(pm, true);
		return true;
	}
	else if(victim->invalidate_number>_PPS*2){
		EPRINT("????", true);
	}
	static int cnt=0;
	printf("mapping gc:%u\n", ++cnt);

	std::queue<gc_read_node*> *gc_target_queue=new std::queue<gc_read_node*>();
	uint32_t bidx;
	uint32_t pidx, page;
	gc_read_node *gn;

	for_each_page_in_seg(victim, page, bidx, pidx){
		if(bm->is_invalid_page(bm, page*L2PGAP)) continue;
		else{
			gn=gc_read_node_init(true, page);
			gc_target_queue->push(gn);
			gc_issue_read_node(gn, bm->li);
		}
	}

	while(!gc_target_queue->empty()){
		fdriver_lock(&gn->done_lock);
		gn=gc_target_queue->front();
		char *oob=bm->get_oob(bm, gn->piece_ppa);
		gn->lba=*(uint32_t*)oob;
		sst_file *target_sst_file=lsmtree_find_target_sst_mapgc(gn->lba, gn->piece_ppa);

		uint32_t ppa=page_manager_get_reserve_new_ppa(pm, true, victim->seg_idx);
		target_sst_file->file_addr.map_ppa=ppa;
		validate_map_ppa(pm->bm, ppa, gn->lba, true);
		gc_issue_write_node(ppa, gn->data, true, bm->li);
		gn->data=NULL;
		gc_read_node_free(gn);
		gc_target_queue->pop();
	}
	
	bm->trim_segment(bm, victim, bm->li);
	page_manager_change_reserve(pm, true);

	delete gc_target_queue;
	return false;
}

typedef struct gc_sptr_node{
	sst_file *sptr;
	uint32_t sidx;
	uint32_t ridx;
	write_buffer *wb;
}gc_sptr_node;

static gc_sptr_node * gc_sptr_node_init(sst_file *sptr, uint32_t validate_num, uint32_t sidx, uint32_t ridx){
	gc_sptr_node *res=(gc_sptr_node*)malloc(sizeof(gc_sptr_node));
	//res->gc_kv_node=new std::queue<key_value_pair>();
	res->sptr=sptr;
	res->wb=write_buffer_init_for_gc(_PPS*L2PGAP, LSM.pm, GC_WB,LSM.param.tiering_rhp);
	res->sidx=sidx;
	res->ridx=ridx;
	return res;
}

static void insert_target_sptr(gc_sptr_node* gsn, uint32_t lba, char *value){
	write_buffer_insert_for_gc(gsn->wb, lba, value);
}

static void move_sptr(gc_sptr_node *gsn, uint32_t seg_idx, uint32_t ridx, uint32_t sidx){
	// if no kv in gsn sptr should be checked trimed;
	if(gsn->wb->buffered_entry_num==0){
		gsn->sptr->trimed_sst_file=true;
	}
	else{
		sst_file *sptr=NULL;
		uint32_t round=0;
		while(gsn->wb->buffered_entry_num){
			sptr=sst_init_empty(BLOCK_FILE);
			uint32_t map_num=gsn->wb->buffered_entry_num/KP_IN_PAGE+
			(gsn->wb->buffered_entry_num%KP_IN_PAGE?1:0);
			key_ptr_pair **kp_set=(key_ptr_pair**)malloc(sizeof(key_ptr_pair*)*map_num);
			uint32_t kp_set_idx=0;
			key_ptr_pair *now_kp_set;
			bool force_stop=false;

			while((now_kp_set=write_buffer_flush_for_gc(gsn->wb, false, seg_idx, &force_stop, kp_set_idx))){
				kp_set[kp_set_idx]=now_kp_set;
				kp_set_idx++;
				if(force_stop) break;
			}


			uint32_t map_ppa;
			map_range *mr_set=(map_range*)malloc(sizeof(map_range) * kp_set_idx);
			for(uint32_t i=0; i<kp_set_idx; i++){
				algo_req *write_req=(algo_req*)malloc(sizeof(algo_req));
				value_set *data=inf_get_valueset((char*)kp_set[i], FS_MALLOC_W, PAGESIZE);
				write_req->type=COMPACTIONDATAW;
				write_req->param=data;
				write_req->end_req=comp_alreq_end_req;
				map_ppa=page_manager_get_reserve_new_ppa(LSM.pm, false, seg_idx);

				mr_set[i].start_lba=kp_set[i][0].lba;
				mr_set[i].end_lba=kp_get_end_lba((char*)kp_set[i]);
				mr_set[i].ppa=map_ppa;
				validate_map_ppa(LSM.pm->bm, map_ppa, mr_set[i].start_lba, true);
				io_manager_issue_write(map_ppa, data, write_req, false);
			}

			//	free(sptr->block_file_map);
			sptr->block_file_map=mr_set;

			sptr->file_addr.piece_ppa=kp_set[0][0].piece_ppa;
			sptr->end_ppa=map_ppa;
			sptr->map_num=kp_set_idx;
			sptr->start_lba=kp_set[0][0].lba;
			sptr->end_lba=mr_set[kp_set_idx-1].end_lba;
			sptr->_read_helper=gsn->wb->rh;
			if(round==0){
				level_sptr_update_in_gc(LSM.disk[2], ridx, sidx, sptr);
			}
			else{
				level_sptr_add_at_in_gc(LSM.disk[2], ridx, sidx+round, sptr);
			}

			gsn->wb->rh=NULL;
			round++;
			free(sptr);
			free(kp_set);
		}
	}


	write_buffer_free(gsn->wb);
	free(gsn);
}

bool __gc_data(page_manager *pm, blockmanager *bm, __gsegment *victim){
	static int cnt=0;
	printf("data gc:%u segidx:%u\n", ++cnt, victim->seg_idx);
	if(victim->invalidate_number==_PPS*2){
		bm->trim_segment(bm, victim, bm->li);
		page_manager_change_reserve(pm, false);
		return true;
	}
	else if(victim->invalidate_number>_PPS*2){
		EPRINT("????", true);
	}

	if(LSM.disk[2]->array[2].sst_set[3].end_ppa/16384==1){
		printf("break!\n");
	}
	/*
	if(cnt==5){
		LSM.global_debug_flag=true;
		printf("break!\n");
	}*/
	if(cnt==60){
		printf("break!\n");
	}

	std::queue<gc_read_node*> *gc_target_queue=new std::queue<gc_read_node*>();
	uint32_t bidx;
	uint32_t pidx, page;
	gc_read_node *gn;
	bool should_read;
	uint32_t read_page_num=0;
	uint32_t valid_piece_ppa_num=0;
	uint32_t temp[_PPS*2];
	/*
	for(uint32_t i=0; i<64; i++){
		printf("%u invalid_number:%u\n", i,victim->blocks[i]->invalidate_number);
	}
	printf("all_invalid: %u\n", victim->all_invalid);
	*/
	for_each_page_in_seg(victim, page, bidx, pidx){
		should_read=false;
		for(uint32_t i=0; i<L2PGAP; i++){
			if(bm->is_invalid_page(bm, page*L2PGAP+i)) continue;
			else{
				should_read=true;
				temp[valid_piece_ppa_num++]=page*L2PGAP+i;
			}
		}
		if(should_read){
			read_page_num++;
			gn=gc_read_node_init(false, page*L2PGAP);
			gc_target_queue->push(gn);
			gc_issue_read_node(gn, bm->li);	
		}
	}

	if(((_PPS*2-victim->invalidate_number)-(victim->validate_number-victim->invalidate_number))>=2){
		if(valid_piece_ppa_num+victim->invalidate_number!=victim->validate_number){
			printf("valid list\n");
			for(uint32_t i=0; i<64; i++){
				printf("%u invalid_number:%u validate_number:%u\n", i,victim->blocks[i]->invalidate_number, victim->blocks[i]->validate_number);
			}
			EPRINT("not match validation!",true);
		}
	}

	write_buffer *gc_wb=write_buffer_init(_PPS*L2PGAP, pm,GC_WB);
	value_set **free_target=(value_set**)malloc(sizeof(value_set*)*read_page_num);

	uint32_t* oob_lba;
	uint32_t ppa, piece_ppa;
	uint32_t q_idx=0;
	sst_file *sptr=NULL;
	uint32_t recent_version, target_version;
	uint32_t sptr_idx=0;
	bool isfirst=false;

	std::queue<gc_mapping_check_node*> *gc_mapping_queue=new std::queue<gc_mapping_check_node*>();
	gc_mapping_check_node *gmc=NULL;

	sst_file *prev_sptr=NULL;
	gc_sptr_node *gsn=NULL;
	while(!gc_target_queue->empty()){
		gn=gc_target_queue->front();
		fdriver_lock(&gn->done_lock);
		ppa=PIECETOPPA(gn->piece_ppa);
		oob_lba=(uint32_t*)bm->get_oob(bm, ppa);

		for(uint32_t i=0; i<L2PGAP; i++){
			if(bm->is_invalid_page(bm, ppa*L2PGAP+i)) continue;
			else{
				if(oob_lba[i]==UINT32_MAX) continue;

				piece_ppa=ppa*L2PGAP+i;
				gn->piece_ppa=piece_ppa;
				gn->lba=oob_lba[i];
				if(gn->piece_ppa==debug_piece_ppa && gn->lba==debug_lba){
					printf("break!\n");
				}
				/*check invalidation*/
				if(!sptr || (sptr && sptr->end_ppa*2<piece_ppa)){ //first round or new sst_file
					if(sptr){
						move_sptr(gsn,victim->seg_idx, gsn->ridx, gsn->sidx);
						prev_sptr=NULL;
					}
					sptr=level_find_target_run_idx(LSM.disk[LSM.param.LEVELN-1], oob_lba[i], piece_ppa, &target_version, &sptr_idx);
				}
				if(sptr==NULL || !(sptr && sptr->file_addr.piece_ppa<=piece_ppa && sptr->end_ppa*L2PGAP>=piece_ppa)){
					/*checking other level*/
					gmc=(gc_mapping_check_node*)malloc(sizeof(gc_mapping_check_node));
					gmc->mapping_data=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
					gmc->data_ptr=&gn->data->value[LPAGESIZE*i];
					gmc->piece_ppa=piece_ppa;
					gmc->lba=oob_lba[i];
					gmc->map_ppa=UINT32_MAX;
					gmc->type=MAP_READ_ISSUE;
					gmc->level=LSM.param.LEVELN-2;
					fdriver_lock_init(&gmc->done_lock, 0);
					gc_mapping_queue->push(gmc);
				}
				else{
					//sptr->trimed_sst_file=true;
					if(piece_ppa>=(sptr->end_ppa-sptr->map_num+1)*L2PGAP){
						/*mapaddress*/
						continue;
					}

					if(sptr->end_ppa/_PPS != victim->seg_idx){
						EPRINT("target sptr should be in victim", true);
					}

					recent_version=version_map_lba(LSM.last_run_version, oob_lba[i]);
					/*should figure out map ppa*/
					if(version_compare(LSM.last_run_version, recent_version, target_version)<=0){
						if(!prev_sptr){
							prev_sptr=sptr;
							gsn=gc_sptr_node_init(sptr, valid_piece_ppa_num, sptr_idx, target_version);
						}
						/*
						if(prev_sptr!=sptr){
							move_sptr(gsn, victim->seg_idx,gsn->ridx,gsn->sidx);
							prev_sptr=sptr;
							gsn=gc_sptr_node_init(sptr, valid_piece_ppa_num, sptr_idx, target_version);
						}*/

						insert_target_sptr(gsn, gn->lba, &gn->data->value[LPAGESIZE*i]);
					}
					else continue; //already invalidate
				}
			}
		}
		free_target[q_idx++]=gn->data;
		gc_read_node_free(gn);
		gc_target_queue->pop();
	}

	if(gsn){
		move_sptr(gsn, victim->seg_idx,target_version,sptr_idx);
	}

	sst_file *map_ptr;
	uint32_t found_piece_ppa;
	while(!gc_mapping_queue->empty()){
		gmc=gc_mapping_queue->front();
		if(gmc->piece_ppa==debug_piece_ppa && gmc->lba==debug_lba){
			printf("break!\n");
		}
retry:
		switch(gmc->type){
			case MAP_READ_ISSUE:
				if((int)gmc->level<0){
					printf("gmc->lba:%u piece_ppa:%u\n", gmc->lba, gmc->piece_ppa);
					EPRINT("mapping not found", true);
				}
				map_ptr=level_retrieve_sst_with_check(LSM.disk[gmc->level], gmc->lba);
				if(!map_ptr){
					gmc->level--;
					goto retry;
				}
				else{
					gmc->type=MAP_READ_DONE;
					gmc->map_ppa=map_ptr->file_addr.map_ppa;
					gc_issue_mapcheck_read(gmc, bm->li);
					gc_mapping_queue->pop();
					gc_mapping_queue->push(gmc);
					continue;
				}
			case MAP_READ_DONE:
				fdriver_lock(&gmc->done_lock);
				found_piece_ppa=kp_find_piece_ppa(gmc->lba, gmc->mapping_data->value);
				if(gmc->piece_ppa==found_piece_ppa){
					slm_remove_node(gmc->level, SEGNUM(gmc->piece_ppa));

					target_version=version_level_idx_to_version(LSM.last_run_version, gmc->level, LSM.param.LEVELN);
					recent_version=version_map_lba(LSM.last_run_version, gmc->lba);
					if(version_compare(LSM.last_run_version, recent_version, target_version)<=0){
						version_coupling_lba_ridx(LSM.last_run_version, gmc->lba, TOTALRUNIDX);
						write_buffer_insert_for_gc(gc_wb, gmc->lba, gmc->data_ptr);
					}
					else{
						goto out;
					}
				}
				else{
					gmc->type=MAP_READ_ISSUE;
					gmc->level--;
					goto retry;
				}
		}
out:
		fdriver_destroy(&gmc->done_lock);
		inf_free_valueset(gmc->mapping_data, FS_MALLOC_R);
		free(gmc);
		gc_mapping_queue->pop();
	}

	key_ptr_pair *kp_set;
	while((kp_set=write_buffer_flush_for_gc(gc_wb, false, victim->seg_idx, NULL,UINT32_MAX))){
		LSM.moved_kp_set->push(kp_set);
	}

	bm->trim_segment(bm, victim, bm->li);
	page_manager_change_reserve(pm, false);

	for(uint32_t i=0; i<q_idx; i++){
		inf_free_valueset(free_target[i], FS_MALLOC_R);
	}
	free(free_target);
	delete gc_target_queue;

	return false;
}
