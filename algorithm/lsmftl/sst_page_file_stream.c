#include "sst_page_file_stream.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

sst_pf_out_stream* sst_pos_init(sst_file *sst_set, inter_read_alreq_param **param, uint32_t set_num, 
		bool(*check_done)(inter_read_alreq_param *, bool), bool (*file_done)(inter_read_alreq_param*)){
	sst_pf_out_stream *res=(sst_pf_out_stream*)calloc(1, sizeof(sst_pf_out_stream));
	res->type=SST_PAGE_FILE_STREAM;
	res->sst_file_set=new std::queue<sst_file*>();
	res->check_flag_set=new std::queue<inter_read_alreq_param *>();
	for(uint32_t i=0; i<set_num; i++){
		res->sst_file_set->push(&sst_set[i]);
		res->check_flag_set->push(param[i]);
	}
	res->check_done=check_done;
	res->file_done=file_done;
	res->now=NULL;
	res->idx=0;
	res->now_file_empty=true;
	res->file_set_empty=false;
	return res;
}

sst_pf_out_stream *sst_pos_init_kp(key_ptr_pair *data){
	sst_pf_out_stream *res=(sst_pf_out_stream*)calloc(1, sizeof(sst_pf_out_stream));
	res->type=KP_PAIR_STREAM;
	res->kp_data=data;
	res->idx=0;
	res->now_file_empty=false;
	res->file_set_empty=true;
	return res;
}

void sst_pos_add(sst_pf_out_stream *os, sst_file *sst_set, inter_read_alreq_param **param, uint32_t set_num){
	for(uint32_t i=0; i<set_num; i++){
		os->sst_file_set->push(&sst_set[i]);
		os->check_flag_set->push(param[i]);
	}
	os->file_set_empty=os->sst_file_set->size()==0;
}

static inline void move_next_file(sst_pf_out_stream *os){
	os->file_done(os->check_flag_set->front());
	os->sst_file_set->pop();
	os->check_flag_set->pop();

	os->file_set_empty=os->sst_file_set->size()==0;
}

key_ptr_pair sst_pos_pick(sst_pf_out_stream *os){
	/*
	if(sst_pos_is_empty(os)){
		EPRINT("don't try pick at empty pos", true);
	}*/
	if(os->type==KP_PAIR_STREAM){
	//	printf("kp_pair_stream(os->idx):%d\n", os->idx);
		return os->kp_data[os->idx];
	}
retry:
	key_ptr_pair temp_res;
	if(os->now_file_empty){
		if(os->sst_file_set->size()==0){
			temp_res.lba=UINT32_MAX;
			os->file_set_empty=true;
			return temp_res;
		}

		if(os->now==os->sst_file_set->front()){
			EPRINT("please pop before get", true);
		}
		os->now=os->sst_file_set->front();
		os->check_done(os->check_flag_set->front(), true);
/*
		os->sst_file_set->pop();
		os->check_flag_set->pop();
*/
		os->now_file_empty=false;
		os->idx=0;
	}


	if(os->now==NULL){
		if(os->now==os->sst_file_set->front()){
			EPRINT("please pop before get", true);
		}
		os->now=os->sst_file_set->front();
		os->check_done(os->check_flag_set->front(), true);
/*
		os->sst_file_set->pop();
		os->check_flag_set->pop();
 */
		os->now_file_empty=false;
		os->idx=0;
	}

	key_ptr_pair res=*(key_ptr_pair*)&os->now->data[(os->idx)*sizeof(key_ptr_pair)];
	if(os->idx*sizeof(key_ptr_pair) >=PAGESIZE){
		os->now_file_empty=true;
	}
	else if(res.lba==UINT32_MAX){
		os->now_file_empty=true;
		if(os->type==SST_PAGE_FILE_STREAM){
			move_next_file(os);
		}
		goto retry;
	}
#ifdef DEBUG
	if(!os->isstart){
		os->isstart=true;
		os->prev_lba=res.lba;
	}
	else{
		if(!(os->prev_lba<=res.lba)){
			EPRINT("data error!", true);
		}
		os->prev_lba=res.lba;
	}
#endif
	return res;
}

void sst_pos_pop(sst_pf_out_stream *os){
	os->idx++;
	if(os->idx*sizeof(key_ptr_pair) >=PAGESIZE){
		os->now_file_empty=true;
		if(os->type==SST_PAGE_FILE_STREAM){
			move_next_file(os);
		}
	}
}

void sst_pos_free(sst_pf_out_stream *os){
	if(os->type==SST_PAGE_FILE_STREAM){
		if(os->sst_file_set->size()){
			EPRINT("remain file exist!", true);
		}
		if(os->check_flag_set->size()){
			EPRINT("remain param!", true);
		}
	}
	if(!os) return;
	delete os->sst_file_set;
	delete os->check_flag_set;
	free(os);
}

sst_pf_in_stream* sst_pis_init(bool make_read_helper, read_helper_param rhp){
	sst_pf_in_stream *res=(sst_pf_in_stream*)calloc(1,sizeof(sst_pf_in_stream));
	res->make_read_helper=make_read_helper;
	res->rhp=rhp;
	/*
	if(make_read_helper){
		res->rh=read_helper_init(rhp);
	}*/
	res->now=NULL;
	return res;
}

bool sst_pos_is_empty(sst_pf_out_stream *os){

	bool res=os->now_file_empty && os->file_set_empty;
	if(res){
		if(os->type==SST_PAGE_FILE_STREAM && os->sst_file_set->size()){
			EPRINT("wtf??\n", true);
		}
	}
	return res;
}

void sst_pis_set_space(sst_pf_in_stream *is, value_set *data, uint8_t type){
	is->now=sst_init_empty(type);
	is->now->data=data->value;
	memset(data->value, -1, PAGESIZE);
	is->idx=0;
	is->vs=data;

	if(is->make_read_helper){
		is->rh=read_helper_init(is->rhp);
	}
}

bool sst_pis_insert(sst_pf_in_stream *is, key_ptr_pair kp){
	if(kp.lba==UINT32_MAX){
		EPRINT("Empty data insert!", true);
	}
	if(is->idx==0){
		//set data;
		sst_pis_set_space(is, inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE), PAGE_FILE);
	}
	((key_ptr_pair*)is->vs->value)[is->idx++]=kp;

	if(is->rh){
		read_helper_stream_insert(is->rh, kp.lba, kp.piece_ppa);
	}

	if(is->idx * sizeof(key_ptr_pair)==PAGESIZE){
		return true;
	}
	return false;
}

void sst_pis_free(sst_pf_in_stream *is){
	if(is->idx){
		EPRINT("please check path", true);
	}
	free(is);
}