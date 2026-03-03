#ifndef OBJECT_POOL_H
#define OBJECT_POOL_H

#include <vector>
#include <mutex>

template <typename T,size_t BlockSize = 4096>
class ObjectPool{
public:
	// =========================================================
    // 【生命周期管理】
    // =========================================================
	ObjectPool() : freeList_(nullptr) { // 初始无空闲内存
		// 静态断言：对象的内存大小至少要能存下一个指针，否则嵌入式指针放不下
        // 这个断言是编译期检查，提前报错，避免运行时问题(断言表达式是常量表达式).
		static_assert(sizeof(T) >= sizeof(void*), "Object size must be >= sizeof(void*)");
	}

	~ObjectPool() {
		// std::lock_guard<std::mutex> lock(mutex_);
		for(void* ch : chunks_) {
			::operator delete(ch);
		}
	}

	ObjectPool(const ObjectPool&) = delete;
	ObjectPool& operator=(const ObjectPool&) = delete;

	// =========================================================
    // 【对外接口】 (Public API)
    // =========================================================

	// 构造对象 : 分配内存  + 构造函数
	template <typename... Args>            // ① 模板参数包
	T* construct(Args&&... args) {         // ② 万能引用 + 函数参数包
		T* ptr = allocate();               // ③ 申请内存

		if (ptr) {  
			try{  // 异常监控，包裹可能抛异常的操作
				new (ptr) T(std::forward<Args>(args)...); // ④ Placement New + 完美转发
			} catch(...) {  // 函数模板，类型位置，构造位置，用万能捕获
				deallocate(ptr); // 释放内存
				throw; // 异常传递
			}
			
		}
		return ptr;
	}

	// 销毁对象
	void destroy(T* ptr){
		if(ptr){
			ptr->~T();  // 显示调用析构函数
			deallocate(ptr);
		}
	}

	// 申请内存(裸内存) : 从空闲链表上取下一个节点
	T* allocate(){
		// thread_local , 不需要锁
		// std::lock_guard<std::mutex> lock(mutex_);

		// 1.空闲链表是否为空
		if(freeList_ == nullptr)
			expand();

		// 2.取出头结点
		void* ptr = freeList_;

		// 3.移动头指针
		freeList_ = *static_cast<void**>(ptr);

		return static_cast<T*>(ptr);
	}

	// 归还裸内存 ： 头插法将节点返回空闲列表
	void deallocate(T* ptr){
		if(ptr == nullptr)
			return;

		// std::lock_guard<std::mutex> lock(mutex_);

		// 1.存*next
		// *static_cast<void**>(ptr) = freeList_;  X错误
		*reinterpret_cast<void**>(ptr) = freeList_;

		// 2.移动头指针
		freeList_ = static_cast<void*>(ptr); 
		
		// ptr = nullptr; 
	}

	void preallocate(size_t count){
		expand(count);
	}
private:
	// =========================================================
    // 【内部实现】 (Implementation Details)
    // =========================================================

	// 申请和扩容 ： 一大块内存 Chunk
	void expand(size_t count = BlockSize) {
		// 1.申请大块内存 malloc
		size_t size = sizeof(T) * count;
		char * newBlock = static_cast<char*>(::operator new(size));
		// 记录到vector中，方便析构时释放
		chunks_.push_back(newBlock);

		// 2.大块内存分层小块，串成链表 : 维系两个指针，一轮串一块
		for (size_t i = 0; i < count ; ++i){ 
			char* curr = newBlock + i * sizeof(T);
			char* next = curr + sizeof(T);
			// 链接 ： 赋值*next
			*reinterpret_cast<void**>(curr) = next;
		}

		// 链接旧链表
		char* last = newBlock + (count-1) * sizeof(T);
		*reinterpret_cast<void**>(last) = freeList_;	

		freeList_ = newBlock;

	}

private:
	// 空闲链表：把原本存data的位置直接用来存*next（下一个空闲内存的地址）
	void* freeList_;   			// 空闲链表头指针
	std::vector<void*> chunks_;
	// std::mutex mutex_;  // thread_local , 不需要锁

};


#endif