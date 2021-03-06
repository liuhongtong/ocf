/*
 * Copyright(c) 2012-2018 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "metadata.h"
#include "../utils/utils_part.h"

/* Sets the given collision_index as the new _head_ of the Partition list. */
static void update_partition_head(struct ocf_cache *cache,
		ocf_part_id_t part_id, ocf_cache_line_t line)
{
	struct ocf_user_part *part = &cache->user_parts[part_id];

	part->runtime->head = line;
}

void ocf_metadata_remove_from_free_list(struct ocf_cache *cache,
		ocf_cache_line_t cline)
{
	struct ocf_part *free_list = cache->device->freelist_part;
	int is_head, is_tail;
	ocf_part_id_t invalid_part_id = PARTITION_INVALID;
	ocf_cache_line_t prev, next;
	ocf_cache_line_t line_entries = cache->device->collision_table_entries;

	ENV_BUG_ON(cline >= line_entries);

	/* Get Partition info */
	ocf_metadata_get_partition_info(cache, cline, NULL, &next, &prev);

	/* Find out if this node is Partition _head_ */
	is_head = (prev == line_entries);
	is_tail = (next == line_entries);

	/* Case 1: If we are head and there is only one node. So unlink node
	 * and set that there is no node left in the list.
	 */
	if (is_head && (free_list->curr_size == 1)) {
		ocf_metadata_set_partition_info(cache, cline, invalid_part_id,
				line_entries, line_entries);
		free_list->head = line_entries;
		free_list->tail = line_entries;
	} else if (is_head) {
		/* Case 2: else if this collision_index is partition list head,
		 * but many nodes, update head and return
		 */
		ENV_BUG_ON(next >= line_entries);

		free_list->head = next;
		ocf_metadata_set_partition_prev(cache, next, line_entries);
		ocf_metadata_set_partition_next(cache, cline, line_entries);
	} else if (is_tail) {
		/* Case 3: else if this cline is partition list tail */
		ENV_BUG_ON(prev >= line_entries);

		free_list->tail = prev;
		ocf_metadata_set_partition_prev(cache, cline, line_entries);
		ocf_metadata_set_partition_next(cache, prev, line_entries);
	} else {
		/* Case 4: else this collision_index is a middle node.
		 * There is no change to the head and the tail pointers.
		 */

		ENV_BUG_ON(next >= line_entries || prev >= line_entries);

		/* Update prev and next nodes */
		ocf_metadata_set_partition_prev(cache, next, prev);
		ocf_metadata_set_partition_next(cache, prev, next);

		/* Update the given node */
		ocf_metadata_set_partition_info(cache, cline, invalid_part_id,
				line_entries, line_entries);
	}

	free_list->curr_size--;
}

void ocf_metadata_add_to_free_list(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	struct ocf_part *free_list = cache->device->freelist_part;
	ocf_cache_line_t tail;
	ocf_cache_line_t line_entries = cache->device->collision_table_entries;
	ocf_part_id_t invalid_part_id = PARTITION_INVALID;

	ENV_BUG_ON(line >= line_entries);

	if (free_list->curr_size == 0) {
		free_list->head = line;
		free_list->tail = line;

		ocf_metadata_set_partition_info(cache, line, invalid_part_id,
				line_entries, line_entries);
	} else {
		tail = free_list->tail;

		ENV_BUG_ON(tail >= line_entries);

		ocf_metadata_set_partition_info(cache, line, invalid_part_id,
				line_entries, tail);
		ocf_metadata_set_partition_next(cache, tail, line);

		free_list->tail = line;
	}

	free_list->curr_size++;
}


/* Adds the given collision_index to the _head_ of the Partition list */
void ocf_metadata_add_to_partition(struct ocf_cache *cache,
		ocf_part_id_t part_id, ocf_cache_line_t line)
{
	ocf_cache_line_t line_head;
	ocf_cache_line_t line_entries = cache->device->collision_table_entries;
	struct ocf_user_part *part = &cache->user_parts[part_id];

	ENV_BUG_ON(!(line < line_entries));

	/* First node to be added/ */
	if (!part->runtime->curr_size) {

		update_partition_head(cache, part_id, line);
		ocf_metadata_set_partition_info(cache, line, part_id,
				line_entries, line_entries);

		if (!ocf_part_is_valid(part)) {
			/* Partition becomes empty, and is not valid
			 * update list of partitions
			 */
			ocf_part_sort(cache);
		}

	} else {
		/* Not the first node to be added. */
		line_head = part->runtime->head;

		ENV_BUG_ON(!(line_head < line_entries));

		ocf_metadata_set_partition_info(cache, line, part_id,
				line_head, line_entries);

		ocf_metadata_set_partition_prev(cache, line_head, line);

		update_partition_head(cache, part_id, line);
	}

	part->runtime->curr_size++;
}

/* Deletes the node with the given collision_index from the Partition list */
void ocf_metadata_remove_from_partition(struct ocf_cache *cache,
		ocf_part_id_t part_id, ocf_cache_line_t line)
{
	int is_head, is_tail;
	ocf_cache_line_t prev_line, next_line;
	uint32_t line_entries = cache->device->collision_table_entries;
	struct ocf_user_part *part = &cache->user_parts[part_id];

	ENV_BUG_ON(!(line < line_entries));

	/* Get Partition info */
	ocf_metadata_get_partition_info(cache, line, NULL,
			&next_line, &prev_line);

	/* Find out if this node is Partition _head_ */
	is_head = (prev_line == line_entries);
	is_tail = (next_line == line_entries);

	/* Case 1: If we are head and there is only one node. So unlink node
	 * and set that there is no node left in the list.
	 */
	if (is_head && (part->runtime->curr_size == 1)) {
		ocf_metadata_set_partition_info(cache, line,
				part_id, line_entries, line_entries);

		update_partition_head(cache, part_id, line_entries);

		if (!ocf_part_is_valid(part)) {
			/* Partition becomes not empty, and is not valid
			 * update list of partitions
			 */
			ocf_part_sort(cache);
		}

	} else if (is_head) {
		/* Case 2: else if this collision_index is partition list head,
		 * but many nodes, update head and return
		 */
		ENV_BUG_ON(!(next_line < line_entries));
		update_partition_head(cache, part_id, next_line);

		ocf_metadata_set_partition_next(cache, line, line_entries);

		ocf_metadata_set_partition_prev(cache, next_line,
				line_entries);
	} else if (is_tail) {
		/* Case 3: else if this collision_index is partition list tail
		 */
		ENV_BUG_ON(!(prev_line < line_entries));

		ocf_metadata_set_partition_prev(cache, line, line_entries);

		ocf_metadata_set_partition_next(cache, prev_line,
				line_entries);
	} else {
		/* Case 4: else this collision_index is a middle node.
		 * There is no change to the head and the tail pointers.
		 */

		ENV_BUG_ON(!(next_line < line_entries));
		ENV_BUG_ON(!(prev_line < line_entries));

		/* Update prev and next nodes */
		ocf_metadata_set_partition_next(cache, prev_line, next_line);

		ocf_metadata_set_partition_prev(cache, next_line, prev_line);

		/* Update the given node */
		ocf_metadata_set_partition_info(cache, line, part_id,
				line_entries, line_entries);
	}

	part->runtime->curr_size--;
}
