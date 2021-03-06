#include "internal/pumanode.h"
#include "internal/valgrind.h"
#include "internal/numa.h"

#include "internal/free.h"
#include "internal/alloc.h"
#include "internal/pumautil.h"

#include "internal/bitmask.h"

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

size_t pumaPageSize = 0;

void _cleanupNode(struct pumaNode* currentNode,
		struct pumaThreadList* list)
{
	bool found;

	void* lastElement;
	void* firstFreeElement;
	struct pumaNode* lastNode;

	while(currentNode->numElements < currentNode->capacity)
	{
		size_t firstFree =
				pumaFirstIndexOfValue(&currentNode->freeMask, MASKFREE, &found);

		lastElement = _getLastElement(list);
		if(lastElement != NULL)
			lastNode = _getNodeForElement(lastElement);
		if(found)
			firstFreeElement = _getElement(currentNode, firstFree);

		if(lastElement == NULL || !found || lastNode->index < currentNode->index
				|| (lastNode->index == currentNode->index && firstFreeElement >= lastElement))
		{
			break;
		}

		_allocElementOnNode(currentNode, firstFreeElement);

		memcpy(firstFreeElement, lastElement, currentNode->elementSize);

		_freeElementOnNode(lastElement, lastNode);
	}

	currentNode->dirty = false;
	currentNode->firstKnownFree = currentNode->numElements;
}

size_t _getBiggestCacheSize()
{
	size_t cacheSize = 0;

	int stats[] = {
			#ifdef _SC_LEVEL4_CACHE_SIZE
			_SC_LEVEL4_CACHE_SIZE,
			#else
			0,
			#endif

			#ifdef _SC_LEVEL3_CACHE_SIZE
			_SC_LEVEL3_CACHE_SIZE,
			#else
			0,
			#endif

			#ifdef _SC_LEVEL2_CACHE_SIZE
			_SC_LEVEL2_CACHE_SIZE,
			#else
			0,
			#endif

			#ifdef _SC_LEVEL1_CACHE_SIZE
			_SC_LEVEL1_CACHE_SIZE,
			#else
			0,
			#endif
	};

	unsigned int level = 0;

	while(cacheSize <= 0 && level < sizeof(stats) / sizeof(int))
		cacheSize = (size_t)sysconf(stats[level++]);

	return cacheSize;
}

struct pumaNode* _appendPumaNode(struct pumaThreadList* threadList,
		size_t elementSize)
{
	VALGRIND_MAKE_MEM_DEFINED(threadList, sizeof(struct pumaThreadList));
	struct pumaNode* tail = threadList->tail;
	int nextIndex = 0;

	struct pumaNode* retNode;

	if(tail != NULL)
	{
		nextIndex = tail->index + 1;

		if(!tail->active)
		{
			tail->active = true;
			retNode = tail;
			goto done;
		}
		else if(tail->next != NULL)
		{
			threadList->tail = tail->next;
			tail->next->active = true;
			tail->next->index = nextIndex;
			retNode = threadList->tail;
			goto done;
		}
	}

	size_t nodeSize = pumaPageSize * PUMA_NODEPAGES;

	retNode = numalloc_aligned_on_node(nodeSize, threadList->numaDomain);
	VALGRIND_MAKE_MEM_DEFINED(retNode, sizeof(struct pumaNode));

	size_t elementArraySize = nodeSize - sizeof(struct pumaNode);
	if(elementArraySize < elementSize)
	{
		fprintf(stderr, "An element size of %lu is too large for nodes of size "
				"%lu. Please specify a higher number for PUMA_NODEPAGES during "
				"PUMA compilation.", elementSize, nodeSize);
		exit(-1);
	}

	createPumaBitmaskForElemArray(&retNode->freeMask,
			(char*)retNode + sizeof(struct pumaNode), elementArraySize,
			MASKFREE, elementSize, &retNode->elementArray, &retNode->capacity);

	retNode->elementSize = elementSize;
	retNode->blockSize = nodeSize;
	retNode->numPages = nodeSize / pumaPageSize;
	retNode->numElements = 0;
	retNode->firstKnownFree = 0;
	retNode->prev = tail;
	retNode->next = NULL;
	retNode->pageUnit = pumaPageSize * ((retNode->elementSize + pumaPageSize - 1) / pumaPageSize);

	threadList->tail = retNode;

	if(tail != NULL)
		tail->next = retNode;

	if(threadList->head == NULL)
		threadList->head = retNode;

	retNode->dirty = false;

	retNode->active = true;

	retNode->index = nextIndex;

	retNode->threadList = threadList;

	{
#if !defined(NDEBUG) && !defined(NNUMA)
	assert((size_t)retNode % pumaPageSize == 0);
	size_t numPages = retNode->numPages;

	for(size_t p = 1; p < numPages; ++p)
	{
		char* currPage = ((char*)retNode) + p * pumaPageSize;
		*currPage = 1;
	}

	int nodes_status[numPages];
	memset(nodes_status, 0, sizeof(int) * numPages);

	size_t pumaPageSize = (size_t)sysconf(_SC_PAGESIZE);

	for(size_t p = 0; p < numPages; ++p)
	{
		void* currPage = ((char*)retNode) + p * pumaPageSize;
		nodes_status[p] = -1;
		long status = move_pages(0, 1, &currPage, NULL, &nodes_status[p], 0);
		assert(status == 0);
	}

	for(size_t p = 0; p < numPages; ++p)
			assert(nodes_status[p] == threadList->numaDomain);
#endif
	}

	VALGRIND_MAKE_MEM_NOACCESS(retNode->elementArray, elementArraySize);

done:
	threadList->numNodes = retNode->index + 1;

	{
#if !defined(NDEBUG) && !defined(NNUMA)
	assert((size_t)retNode % pumaPageSize == 0);
	size_t numPages = retNode->numPages;

	for(size_t p = 1; p < numPages; ++p)
	{
		char* currPage = ((char*)retNode) + p * pumaPageSize;
		*currPage = 1;
	}

	int nodes_status[numPages];
	memset(nodes_status, 0, sizeof(int) * numPages);

	size_t pumaPageSize = (size_t)sysconf(_SC_PAGESIZE);

	for(size_t p = 0; p < numPages; ++p)
	{
		void* currPage = ((char*)retNode) + p * pumaPageSize;
		nodes_status[p] = -1;
		long status = move_pages(0, 1, &currPage, NULL, &nodes_status[p], 0);
		assert(status == 0);
	}

	for(size_t p = 0; p < numPages; ++p)
			assert(nodes_status[p] == threadList->numaDomain);
#endif
	}

	VALGRIND_MAKE_MEM_NOACCESS(threadList, sizeof(struct pumaThreadList));

	return retNode;
}

void _freePumaNode(struct pumaNode* node)
{
	struct pumaThreadList* threadList = node->threadList;

	VALGRIND_MAKE_MEM_DEFINED(threadList, sizeof(struct pumaThreadList));
	if(node == threadList->tail)
		threadList->tail = node->prev;

	if(node == threadList->head)
		threadList->head = node->next;

	if(node->prev != NULL)
		node->prev->next = node->next;
	if(node->next != NULL)
		node->next->prev = node->prev;

	VALGRIND_MAKE_MEM_NOACCESS(threadList, sizeof(struct pumaThreadList));

	destroyPumaBitmask(&node->freeMask);
	nufree_aligned(node, node->blockSize);
}
