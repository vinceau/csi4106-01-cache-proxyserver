#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cache.h"

C_block* cache_start = NULL;
C_block* cache_end = NULL;

int cache_count = 0;
long cache_size = 0;
int lru_count = 0;

long max_cache_size = 0; //in bytes

void
set_max_cache_size(int mb)
{
	max_cache_size = mb * BYTESINMB;
}

long
get_current_cache_size()
{
	return cache_size;
}

int
get_cache_count()
{
	return cache_count;
}

int
could_fit(long nbytes)
{
	return max_cache_size == 0 || nbytes < max_cache_size;
}

C_block*
search_cache(char *host, char *path)
{
	C_block* ref = cache_start;

	while (ref != NULL) {
		if (strcmp(ref->host, host) == 0 &&
				strcmp(ref->path, path) == 0) {
			ref->lru = ++lru_count;
			return ref;
		}
		ref = ref->next;
	}
	return NULL;
}

void
free_response_block(R_block* r)
{
	if (r == NULL) return;

	//free_response_block(r->next);
	free(r);
}

long
free_cache_block(C_block* cb)
{
	if (cb == NULL) return 0;

	//fix following blocks
	if (cb->next == NULL) {
		//the block is at the end so update cache_end
		cache_end = cb->prev;
	} else {
		//there's something after the block so update its prev
		cb->next->prev = cb->prev;
	}

	//fix previous blocks
	if (cb->prev == NULL) {
		//removing the first block
		cache_start = cb->next;
	} else {
		//set the prev block, to cb's next
		cb->prev->next = cb->next;
	}

	cache_count--;

	long space_freed = cb->size;
	cache_size -= space_freed;
	free_response_block(cb->response);
	free(cb);
	return space_freed;
}


/*
 * Returns a pointer to the Least Recently Used cache block
 */
C_block*
find_lru()
{
	//return if there's nothing in the cache
	if (cache_start == NULL) return NULL;

	//set the first element as the minimum
	C_block* min = cache_start;

	C_block* curr = min->next;

	//while there is a next
	while (curr != NULL) {
		if (curr->lru < min->lru) {
			min = curr;
		}
		curr = curr->next;
	}

	return min;
}

/*
 * This returns true if it is possible to add nbytes of data to the existing
 * cache. False otherwise.
 */
int
can_fit(long nbytes)
{
	return max_cache_size == 0 || cache_size + nbytes < max_cache_size;
}

/*
 * Returns true if successfully freed up at least nbytes of space
 */
int
free_up(long nbytes)
{
	if (!could_fit(nbytes)) return 0;

	long freed_space = 0;
	while (freed_space < nbytes) {
		void* lru = find_lru();
		if (lru == NULL) return 0;
		free_cache_block(lru);
	}
	return 1;
}

/*
 * Warning! This function will not check whether or not whether the total
 * space needed for the cache block is greater than the max_cache_size!!
 */
C_block*
add_cache(char *host, char *path, char *reference, long nbytes, int status_no, char* status, int has_type, char* c_type)
{
	if (!can_fit(nbytes)) {
		free_up(nbytes);
	}

	unsigned char* response_text = calloc(1, nbytes+1);
	if (response_text == NULL) {
		perror("Failed to allocate memory for response text");
		exit(1);
	}
	memcpy(response_text, reference, nbytes);

	R_block* r_block = calloc(1, sizeof(R_block));
	if (r_block == NULL) {
		perror("Failed to allocate memory for cache's response block");
		exit(1);
	}
	r_block->text = response_text;
	r_block->size = nbytes;
	r_block->next = NULL;

	C_block *c_block = calloc(1, sizeof(C_block));
	if (c_block == NULL) {
		perror("Failed to allocate memory for cache block");
		exit(1);
	}
	strncpy(c_block->host, host, sizeof(c_block->host));
	strncpy(c_block->path, path, sizeof(c_block->path));
	c_block->response = r_block;
	c_block->end = r_block;
	c_block->lru = ++lru_count;
	c_block->size = nbytes;
	c_block->status_no = status_no;
	c_block->has_type = has_type;
	strcpy(c_block->status, status);
	strcpy(c_block->c_type, c_type);
	c_block->next = NULL;

	cache_size += nbytes;
	cache_count++;

	if (cache_start == NULL) {
		//adding to the start of cache
		c_block->prev = NULL;
		cache_start = c_block;
	} else {
		//add to end cache
		c_block->prev = cache_end;
		cache_end->next = c_block;
	}
	cache_end = c_block;

	return c_block;
}


int
add_response_block(C_block *c_block_ptr, char* response, long nbytes)
{
	/*
	if (!can_fit(nbytes)) {
		free_up(nbytes);
	}
	*/

//	printf("adding a block to %s%s\n", c_block_ptr->host, c_block_ptr->path);
//	printf("block length: %ld\n", nbytes);
//	printf("extra block contents: <<%s>>\n", response);

	R_block* n_block = calloc(1, sizeof(R_block));
	if (n_block == NULL) {
		perror("Failed to allocate memory for additional response block");
		exit(1);
	}
	unsigned char* response_text = calloc(1, nbytes+1);
	if (response_text == NULL) {
		perror("Failed to allocate memory for response text");
		exit(1);
	}
	memcpy(response_text, response, nbytes);

	n_block->text = response_text;
	n_block->size = nbytes;
	n_block->next = NULL;
	//add this block to the end of the cache block
	c_block_ptr->end->next = n_block;
	c_block_ptr->size += nbytes;
	c_block_ptr->end = n_block;
	cache_size += nbytes;

	return 0;
}

