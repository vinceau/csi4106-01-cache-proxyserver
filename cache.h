#ifndef CACHE_H
#define CACHE_H

#define BYTESINMB 1048576 //how many bytes are in a megabyte

typedef struct R_block {
	unsigned char *text;
	long size;
	struct R_block* next; //NULL if complete
} R_block;

typedef struct C_block {
	char host[2048];
	char path[2048];
	R_block *response;
	int lru;
	long size;
	int status_no;
	int has_type;
	char status[256];
	char c_type[256]; //content type
	struct C_block* prev; //NULL
	struct C_block* next; //NULL
	R_block* end; //points to the last response block
} C_block;

void
set_max_cache_size(int mb);

long
get_current_cache_size();

int
get_cache_count();

int
can_fit(long nbytes);

int
could_fit(long nbytes);

C_block*
search_cache(char *host, char *path);

void
free_response_block(R_block* r);

long
free_cache_block(C_block* cb);

C_block*
find_lru();

int
free_up(long nbytes);

C_block*
add_cache(char* host, char* path, char* reference, long nbytes, int status_no, char* status, int has_type, char* c_type);

int
add_response_block(C_block* c_block_ptr, char *response, long nbytes);

#endif

