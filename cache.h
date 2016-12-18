#ifndef CACHE_H
#define CACHE_H

#define BYTESINMB 1048576 //how many bytes are in a megabyte

struct response_block {
	unsigned char *response;
	long size;
	void* next; //NULL if complete
};


struct cache_block {
	char host[2048];
	char path[2048];
	struct response_block *response;
	int lru;
	long size;
	int status_no;
	int has_type;
	char status[256];
	char c_type[256]; //content type
	void* prev; //NULL
	void* next; //NULL
	void* end; //points to the last response block
};

void*
search_cache(char *host, char *path);

int
free_up(long nbytes);

struct cache_block*
add_cache(char* host, char* path, char* reference, long nbytes, int status_no, char* status, int has_type, char* c_type);

int
add_response_block(struct cache_block* c_block_ptr, char *response, long nbytes);

int
could_fit(long nbytes);

int
can_fit(long nbytes);

void*
find_lru();

long
free_cache_block(void* cb_ptr);

long
get_current_cache_size();

int
get_cache_count();

void
set_max_cache_size(int mb);

#endif

