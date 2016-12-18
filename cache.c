#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cache.h"


C_block* cache_start = NULL; //starting cache block
C_block* cache_end = NULL;   //ending cache block

int cache_count = 0; //current items in the cache
long cache_size = 0; //total size of the cache in bytes
int lru_count = 0;   //current maximum LRU count
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

/*
 * Returns true if it is possible to add nbytes of data to the existing
 * cache. False otherwise.
 */
int
can_fit(long nbytes)
{
	return max_cache_size == 0 || cache_size + nbytes < max_cache_size;
}

/*
 * Returns true if the maximum cache size is big enough to hold nbytes of data.
 * Does not consider the current size of the cache.
 */
int
could_fit(long nbytes)
{
	return max_cache_size == 0 || nbytes < max_cache_size;
}

/*
 * Searches the cache for the cache block matching <host> and <port>.
 *
 * Returns a pointer to the cache block if successful, and NULL otherwise.
 */
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

/*
 * Frees the memory allocated to the response block <r>.
 *
 * First free's the text, then goes down the linked list freeing every the
 * following blocks. Finally frees itself.
 *
 * It does this recursively.
 */
void
free_response_block(R_block* r)
{
	if (r != NULL) {
		if (r->text != NULL) free(r->text);
		free_response_block(r->next);
		free(r);
	}
}

/*
 * Frees the cache block <cb> and the associated request block linked list.
 *
 * Returns the amount of space gained after freeing the structure.
 */
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
		if (curr->lru < min->lru) min = curr;
		curr = curr->next;
	}
	return min;
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
		C_block* lru = find_lru();
		if (lru == NULL) return 0;
		free_cache_block(lru);
	}
	return 1;
}

/*
 * Creates a cache_block and adds it to the cache linked list. The body of the
 * response is stored in a response_block attached to the cache_block.
 *
 * Returns a pointer to the cache_block if successfully allocated space.
 * Returns NULL if failed to allocate space, or the required space is too big
 * for the cache itself.
 */
C_block*
add_cache(char *host, char *path, char *reference, long nbytes, int status_no, char* status, int has_type, char* c_type)
{
	//return if we couldn't allocate enough space
	if (!can_fit(nbytes) && !free_up(nbytes)) return NULL;

	//allocate space for the response block
	R_block* r_block = calloc(1, sizeof(R_block));
	if (r_block == NULL) {
		perror("Failed to allocate memory for cache's response block");
		return NULL;
	}

	//allocate space for the response text
	r_block->text = calloc(1, nbytes+1);
	if (r_block->text == NULL) {
		perror("Failed to allocate memory for response text");
		free(r_block);
		return NULL;
	}

	//allocate space for the cache block
	C_block *c_block = calloc(1, sizeof(C_block));
	if (c_block == NULL) {
		perror("Failed to allocate memory for cache block");
		free(r_block->text);
		free(r_block);
		return NULL;
	}

	//copy the text to the response_text
	memcpy(r_block->text, reference, nbytes);
	r_block->size = nbytes;
	r_block->next = NULL;

	//setup cache block
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

/*
 * This adds another response block to the chain of response blocks at <cb>.
 *
 * Returns true if a fail occured and false otherwise.
 *
 * Note! This does not check the maximum cache size! This is because there is
 * an off chance that the total number size of the response (sum of all the
 * individual blocks) could exceed the maximum size of the cache (chunked
 * encoding means we don't know how many bytes to expect).
 * As a result, technically when we go to free the LRU, we could end up freeing
 * the cache_block that we are trying to add to.
 */
int
add_response_block(C_block *cb, char* response, long nbytes)
{
	int failed = 1;
	//allocate space for response block
	R_block* rb = calloc(1, sizeof(R_block));
	if (rb == NULL) {
		perror("Failed to allocate memory for additional response block");
		return failed;
	}

	//allocate space for response text
	rb->text = calloc(1, nbytes+1);
	if (rb->text == NULL) {
		perror("Failed to allocate memory for response text");
		free(rb);
		return failed;
	}
	memcpy(rb->text, response, nbytes);
	rb->size = nbytes;
	rb->next = NULL;

	//add this block to the end of the cache block
	cb->end->next = rb;
	cb->size += nbytes;
	cb->end = rb;
	cache_size += nbytes;
	return !failed;
}

