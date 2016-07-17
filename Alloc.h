//////////////////////////////////////////////////////////
//以及空间配置器（malloc/relloc/free）
//内存分配失败以后处理的句柄handler类型
#include<iostream>
//#include"Trace.h"
using namespace std;
typedef void(*ALLOC_OOM_FUN) ();
template<int inst>
class __MallocAllocTemplate
{
private:
	static ALLOC_OOM_FUN __sMallocAllocOomHandler;
	static void *OoMalloc(size_t n)
	{
		ALLOC_OOM_FUN  handler;
		void *result;
		//1：内存分配成功，则直接返回
		//2：若失败，检查是否设置处理的handler，有则以后再分
		//配，不断重复直到分配成功
		//3：没有设置处理的handler，直接结束程序
		for (;;)
		{
			handler = __sMallocAllocOomHandler;
			if (0 == handler)
			{
				cerr << "out of memory" << endl;
				exit(-1);
			}
			(*handler)();
			result = malloc(n);
			if (result)
				return result;
		}
	}
	static void *OomRealloc(void *p, size_t n)
	{
		ALLOC_OOM_FUN handler;
		void *result;
		for (;;)
		{
			handler = __sMallocAllocOomHandler;
		}
		if (0 == handler)
		{
			cerr << "out of memory" << endl;
			exit(-1);
		}
		(*handler)();
		result = realloc(p, n);
		if (result)
			return result;
	}
public:
	static void *Allocate(size_t n)
	{
		//TRace
		cout << "一级配置器分配: " << n << endl;
		void *result = malloc(n);
		if (0 == result)
			result = OoMalloc(n);
		return result;
	}
	static void Deallocate(void *p, size_t n)
	{
		//trace
		free(p);
	}
	static void *Rellocate(void *p, size_t new_sz)
	{
		void *result = realloc(p, new_sz);
		return result;
	}
	static void(*SetMallocHandler(void(*f)()))()
	{
		void(*old)() = __sMallocAllocOomHandler;
		__sMallocAllocOomHandler = f;
		return (old);
	}
};

template<int inst>
ALLOC_OOM_FUN __MallocAllocTemplate<inst>::__sMallocAllocOomHandler = 0;


/////////////////////////////////////////////
//二级空间配置器
//
template<bool threads, int inst>
class __DefaultAllocTemplate
{
	typedef __MallocAllocTemplate<inst> MallocAlloc;
public:
	enum{ __ALIGN = 8 };//排列基准值
	enum{ __MAX_BYTES = 128 };//最大值
	enum{ __NFREELISTS = __MAX_BYTES / __ALIGN };//排列链大小
	static size_t ROUND_UP(size_t bytes)
	{
		//对齐
		//7 8
		//9  16
		return((bytes + __ALIGN - 1) & ~(__ALIGN - 1));
	}
	static size_t FREELIST_INDEX(size_t	bytes)//定位
	{
		//15  1
		//7   0

		return((bytes + __ALIGN - 1) / __ALIGN - 1);
	}
	union Obj
	{
		union Obj* _freeListLink;//指向下一个内存块的指针
		char _clientData[1]; //???
	};
	static Obj* volatile _freeList[__NFREELISTS];//16 防止编译器优化
	static char *_startFree;
	static char *_endFree;
	static size_t _heapSize;//系统堆分配的总大小
	//获取大块内存插入自由链表
	static void* Refill(size_t n);
	//从内存池中分配大块内存
	static char* ChunkAlloc(size_t size, int &nobjs);
	static void *Allocate(size_t n);
	static void Deallocate(void*p, size_t n);
	static void *Reallocate(void*p, size_t old_sz, size_t new_sz);
};
//初始化全局静态对象
template<bool threads, int inst>
typename __DefaultAllocTemplate<threads, inst>::Obj* volatile __DefaultAllocTemplate<threads, inst>::_freeList[__DefaultAllocTemplate<threads, inst>::__NFREELISTS];
template<bool threads, int inst>
char* __DefaultAllocTemplate<threads, inst>::_startFree = 0;

template<bool threads, int inst>
char*__DefaultAllocTemplate<threads, inst>::_endFree = 0;

template<bool threads, int inst>
size_t __DefaultAllocTemplate<threads, inst>::_heapSize = 0;;

template<bool threads, int inst>
void* __DefaultAllocTemplate<threads, inst>::Refill(size_t n)////从内存池中分配大块内存
{
	//trace
	//分配n bytes内存
	//
	cout << "从内存池中分配大块内存：n" << endl;
	int nobjs = 20;
	char *chunk = ChunkAlloc(n, nobjs);
	if (nobjs == 1)//只分配了一块
		return chunk;
	Obj *result, *cur;
	size_t index = FREELIST_INDEX(n);//定位到自由链表的下表
	result = (Obj*)chunk;
	//剩余的块链接到自由链表
	cur = (Obj*)(chunk + n);
	_freeList[index] = cur;
	for (int i = 2; i < nobjs; ++i)
	{
		cur->_freeListLink = (Obj*)(chunk + n*i);
		cur = cur->_freeListLink;
	}
	cur->_freeListLink = NULL;
	return result;
}

template<bool threads, int inst>
char*__DefaultAllocTemplate<threads, inst>::ChunkAlloc(size_t size, int& nobjs)
{
	//trace
	char *result;
	size_t bytesNeed = size*nobjs;
	size_t bytesLeft = _endFree - _startFree;
	//若内存池中内存足够 bytesLeft>bytesNeed，直接从内存池中取
	//若不足，但是够一个bytesLsft>=size,则直接取能取出来
	//若不足，从系统堆分配大块内存到内存池中
	if (bytesLeft >= bytesNeed)
	{
		//trace 足够分配 nobjs个对象
		cout << "足够分配 " << endl;
		result = _startFree;
		_startFree += bytesNeed;
	}
	else if (bytesLeft >= size)
	{
		//不够分配nobjs个。只能分配  个
		cout << "只能分配n个 " << endl;
		result = _startFree;
		nobjs = bytesLeft / size;
		_startFree += nobjs*size;
	}
	else
	{
		//若内存池中有小块剩余，则头插到合适的自由链表
		if (bytesLeft > 0)
		{
			size_t index = FREELIST_INDEX(bytesLeft);
			((Obj*)_startFree)->_freeListLink = _freeList[index];
			_freeList[index] = (Obj*)_startFree;
			_startFree = NULL;
			//trace 内存池空间不足，系统堆分配xxx内存
			cout << "内存池空间不足，系统分配到内存池 " << endl;
		}
		size_t bytesToGet = 2 * bytesNeed + ROUND_UP(_heapSize >> 4);
		_startFree = (char*)malloc(bytesToGet);
		if (_startFree == NULL)
		{
			//trace 系统堆不够，去自由链表看看
			cout << "系统堆不够，自由链表看看 " << endl;
			for (int i = size; i <= __MAX_BYTES; i += __ALIGN)
			{
				Obj*head = _freeList[FREELIST_INDEX(size)];
				if (head)
				{
					_startFree = (char*)head;
					head = head->_freeListLink;
					_endFree = _startFree + i;
					return ChunkAlloc(size, nobjs);
				}
			}
			//系统堆和自由链表都没了  去一级配置器
			//trace
			cout << "系统堆和自由链表都没了  去一级配置器 " << endl;
			_startFree = (char*)__MallocAllocTemplate<inst>::Allocate(bytesToGet);
		}
		_heapSize += bytesToGet;
		_endFree = _startFree + bytesToGet;
		//递归调用获取内存       
		return ChunkAlloc(size, nobjs);

	}
	return result;
}


template<bool threads, int inst>
void*__DefaultAllocTemplate<threads, inst>::Allocate(size_t n)
{
	//__TRACE_DEBUG("(n: %u)\n", n);
	//    //若n > __MAX_BYTES则直接在一级配置器中获取    
	//否则在二级配置器中获取    //  
	if (n>__MAX_BYTES)
	{
		cout << "大于128 去一级 " << endl;

		return MallocAlloc::Allocate(n);
	}
	size_t index = FREELIST_INDEX(n);
	void*ret = NULL;
	//    // 1.如果自由链表中没有内存则通过Refill进行填充    
	// 2.如果自由链表中有则直接返回一个节点块内存    
	// ps:多线程环境需要考虑加锁    
	Obj *head = _freeList[index];
	if (head == NULL)
	{
		return Refill(ROUND_UP(n));
	}
	else
	{
		//__TRACE_DEBUG("自由链表取内存:_freeList[%d]\n",index);
		_freeList[index] = head->_freeListLink;
		return head;
	}
}
template<bool threads, int inst>
void __DefaultAllocTemplate<threads, inst>::Deallocate(void*p, size_t n)
{
	//__TRACE_DEBUG("(p:%p, n: %u)\n", p, n);
	//    
	//若n > __MAX_BYTES则直接归还给一级配置器  
	//否则在放回二级配置器的自由链表   
	// 
	if (n>__MAX_BYTES)
	{
		MallocAlloc::Deallocate(p, n);
	}
	else
	{
		// ps:多线程环境需要考虑加锁    
		size_t index = FREELIST_INDEX(n);
		//头插回自由链表    
		Obj *tmp = (Obj*)p;
		tmp->_freeListLink = _freeList[index];
		_freeList[index] = tmp;
	}
}
template<bool threads, int inst>
void*__DefaultAllocTemplate<threads, inst>::Reallocate(void*p, size_t old_sz, size_t new_sz)
{
	void*result;
	size_t copy_sz;
	if (old_sz> (size_t)__MAX_BYTES&&new_sz> (size_t)__MAX_BYTES)
	{
		return(realloc(p, new_sz));
	}
	if (ROUND_UP(old_sz) == ROUND_UP(new_sz))
		return p;
	result = Allocate(new_sz);
	copy_sz = new_sz>old_sz ? old_sz : new_sz;
	memcpy(result, p, copy_sz);
	Deallocate(p, old_sz);
	return result;
}


