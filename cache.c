#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cache.h"

void *cache_start = NULL;
void *cache_end = NULL;

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

void *
search_cache(char *host, char *path)
{
	void *ref_ptr;
	ref_ptr = cache_start;

	while (ref_ptr != NULL) {
		struct cache_block *reference;
		reference = (struct cache_block*) ref_ptr;

		if (strcmp(reference->host, host) == 0 &&
				strcmp(reference->path, path) == 0) {
			reference->lru = ++lru_count;
			return (void*)reference;
		}
		ref_ptr = reference->next;
	}
	return NULL;
}

void
free_response_block(void* r_ptr)
{
	if (r_ptr == NULL) return;

	struct response_block* r_block = (struct response_block*)r_ptr;
	free_response_block(r_block->next);
	free(r_block);
}

long
free_cache_block(void* cb_ptr)
{
	if (cb_ptr == NULL) return 0;

	struct cache_block* min = (struct cache_block*) cb_ptr;

	//fix following blocks
	if (min->next == NULL) {
		//the min is at the end so update cache_end
		cache_end = min->prev;
	} else {
		//there's something after min so update its prev
		((struct cache_block*)min->next)->prev = min->prev;
	}

	//fix previous blocks
	if (min->prev == NULL) {
		//first element is the smallest
		cache_start = min->next;
	} else {
		//set min's prev, to min's next
		((struct cache_block*)min->prev)->next = min->next;
	}

	cache_count--;

	long space_freed = min->size;
	cache_size -= space_freed;
	free_response_block((void*)min->response);
	free(min);
	return space_freed;
}


/*
 * Returns a pointer to the Least Recently Used cache block
 */
void*
find_lru()
{
	if (cache_start == NULL) {
		//there's nothing in the cache
		return NULL;
	}

	void *ref_ptr;
	void *prev_ptr = NULL;
	void *min_prev_ptr = NULL;

	struct cache_block *min;
	struct cache_block *curr_c_block;

	//we want to find the item with the lowest LRU and make sure we can
	//free up at least nbytes worth of space

	//set the first element as the minimum
	min = (struct cache_block*) cache_start;
	ref_ptr = min->next;

	//while there is a next
	while (ref_ptr != NULL) {
		curr_c_block = (struct cache_block*)ref_ptr;

		if (curr_c_block->lru < min->lru) {
			min = curr_c_block;
			min_prev_ptr = prev_ptr;
		}

		prev_ptr = ref_ptr;
		ref_ptr = curr_c_block->next;
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
struct cache_block*
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

	struct response_block* r_block = calloc(1, sizeof(struct response_block));
	if (r_block == NULL) {
		perror("Failed to allocate memory for cache's response block");
		exit(1);
	}
	r_block->response = response_text;
	r_block->size = nbytes;
	r_block->next = NULL;

	struct cache_block *c_block = calloc(1, sizeof(struct cache_block));
	if (c_block == NULL) {
		perror("Failed to allocate memory for cache block");
		exit(1);
	}
	strncpy(c_block->host, host, sizeof(c_block->host));
	strncpy(c_block->path, path, sizeof(c_block->path));
	c_block->response = r_block;
	c_block->end = (void*)r_block;
	c_block->lru = ++lru_count;
	c_block->size = nbytes;
	c_block->status_no = status_no;
	c_block->has_type = has_type;
	strcpy(c_block->status, status);
	strcpy(c_block->c_type, c_type);
	c_block->next = NULL;

	cache_size += nbytes;
	cache_count++;

	//adding to the start of cache
	if (cache_start == NULL) {
		c_block->prev = NULL;
		cache_start = (void*)c_block;
	} else {
		//add to end cache
		c_block->prev = cache_end;
		((struct cache_block*)cache_end)->next = (void*)c_block;
	}
	cache_end = (void*)c_block;

	return c_block;
}


int
add_response_block(struct cache_block *c_block_ptr, char* response, long nbytes)
{
	/*
	if (!can_fit(nbytes)) {
		free_up(nbytes);
	}
	*/

	printf("adding a block to %s%s\n", c_block_ptr->host, c_block_ptr->path);
	printf("block length: %ld\n", nbytes);
	printf("extra block contents: <<%s>>\n", response);

	struct response_block* n_block = calloc(1, sizeof(struct response_block));
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

	n_block->response = response_text;
	n_block->size = nbytes;
	n_block->next = NULL;
	//add this block to the end of the cache block
	struct response_block* end = (struct response_block*)(c_block_ptr->end);
	end->next = n_block;
	c_block_ptr->size += nbytes;
	c_block_ptr->end = n_block;
	cache_size += nbytes;

	return 0;
}

