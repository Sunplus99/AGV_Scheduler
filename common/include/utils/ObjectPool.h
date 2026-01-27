#ifndef OBJECT_POOL_H
#define OBJECT_POOL_H

#include <vector>
#include <mutex>
/*
核心目标是：
    复用对象内存，避免频繁new/delete带来的系统调用开销和内存碎片；
    支持任意类型的对象管理（模板特性）；
    多线程环境下安全使用；
        线程安全:用std::mutex保证多线程下申请 / 释放内存的原子性
    零额外内存开销管理空闲内存（嵌入式指针）。
*/

/*
在 C++ 的设计模式中，内存池（Memory Pool）通常扮演两个角色：
	对象工厂 (Object Factory)：帮你 new 对象并初始化。
		对应接口：construct() / destroy()
	分配器 (Allocator)：只提供“生肉”（裸内存），不负责“烹饪”（构造）。
		对应接口：allocate() / deallocate()
*/

/**
 * @brief 通用高性能对象池 (Thread-Safe)
 * * 核心技术：
 * 1. 空闲链表 (Free List)：将释放的内存块串起来重复利用。
        把释放的对象内存串成链表，下次申请时直接从链表取，无需向系统申请新内存
 * 2. 嵌入式指针 (Embedded Pointer)：利用对象释放后的内存空间存储 next 指针，零额外内存开销。
 利用对象释放后的内存空间存储 “下一个空闲节点的地址”，无需额外链表节点（内存利用率 100%）
 * 3. 块申请 (Chunk Allocation)：减少 malloc 系统调用次数。
        一次向系统申请一大块内存（默认 4096 个对象大小），切分成小块使用，减少malloc系统调用
Placement New:
    	手动控制 “内存分配” 和 “对象构造” 分离（内存复用，构造按需调用）
 */

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

	/*
	::operator delete(void* ptr)（全局、最基础的内存释放函数）是 C++ 语言核心特性，而非 “标准库函数”—— 编译器在编译阶段就原生识别并支持它，不需要通过任何头文件来声明，就像你使用 int 类型、if 关键字、for 循环一样，是编译器内置的 “基础能力”。
	*/
	~ObjectPool() {
		// std::lock_guard<std::mutex> lock(mutex_);
		for(void* ch : chunks_) {
			::operator delete(ch);
		}
	}

	/*
	禁用拷贝和赋值
	对象池是 “独占式” 资源（内存池的内存只能由一个池管理），拷贝 / 赋值会导致多个池持有同一块内存，析构时重复释放，引发崩溃。
	*/
	/*
	语法上： 拷贝构造和赋值函数的参数 不强制 必须是 const 引用，写成非 const 引用（ObjectPool&）也是合法的 C++ 语法。
	实际上： 我们 总是 使用 const 引用（const ObjectPool&）来禁用拷贝，是因为它具有 “通杀” 的效果。
	*/
	/*
	// 2. 允许移动（转让）：参数是 && (右值引用)，不能加 const
    // 因为移动需要“掏空”源对象，必须修改源对象，所以不能是 const
		ObjectPool(ObjectPool&& other) noexcept {
			// 接管 other 的资源指针
			this->resource_ = other.resource_;
			// 把 other 的指针置空（修改了 other），防止析构时 double free
			other.resource_ = nullptr; 
		}
	*/
	// 这里的 const& 拦截了所有试图“保留原件、制造副本”的行为
	ObjectPool(const ObjectPool&) = delete;
	ObjectPool& operator=(const ObjectPool&) = delete;

	// =========================================================
    // 【对外接口】 (Public API)
    // =========================================================

	/*
		template <typename... Args>      // Args 为类型包，被推导为: {int, int}
		T* construct(Args&&... args) {   // args 为参数包，被推导为: {101, 95}
		Args ...args ：符合 [类型 参数] ，其中...含义 respectively
		std::forward<Args>(args)... ： 
			std::forward<Args>(args) + ... ， 其中...表示“respectively ”的感觉,变为：
			 std::forward<{int, int}>({arg1,arg2}) -> std::forward<int>(arg1), std::forward<int>(arg2)
		
	*/
	// 构造对象 : 分配内存  + 构造函数
	template <typename... Args>            // ① 模板参数包
	T* construct(Args&&... args) {         // ② 万能引用 + 函数参数包
		T* ptr = allocate();               // ③ 申请内存

		if (ptr) {  
			try{  // 异常监控，包裹可能抛异常的操作
				new (ptr) T(std::forward<Args>(args)...); // ④ Placement New + 完美转发
			} catch(...) {  // 函数模板，类型位置，构造位置，用万能捕获
				/*
				这里的核心任务不是处理错误，而是防止内存泄漏
				如果写 catch(const std::exception& e)，就会漏掉这些奇葩异常，导致程序直接 crash（调用 std::terminate）。
				代码的潜台词是：“我不管你出了什么错，也不关心错误信息是什么，我只负责把我的内存收回来，然后你自己去外面解决。”
				*/
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
		// 从 ptr 指向的空闲内存里，取出 8 字节的原始数据，告诉编译器 “这 8 字节是一个地址”，然后把这个地址赋值给 m_freeList（更新空闲链表头）
        // 将 ptr 强转为 void**，解引用取出里面存的 "next地址"
        // static_cast<void**>(ptr)的核心作用：告诉编译器 “把ptr这个一级指针（装的是 0x1000），当作二级指针来用 —— 也就是把 0x1000 这个内存地址，看作一个‘装地址的盒子’”
        // 解引用 (*) 的本质，就是根据指针指向的类型，确定要操作内存的‘步长’（大小）和‘解释方式
        // 如果只写 *ptr，因为 ptr 是 void*，编译器会报错：“我不知道读多少字节！”
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
		/*为什么必须用 reinterpret_cast?
		① 语义层面：这块内存，上一秒存的是对象 T 的数据，下一秒我们要把它当成一个 void* 指针变量来用，表示空闲内存块
		② 语法层面：reinterpret_cast 暴力转换
		*/

		// 2.移动头指针
		freeList_ = static_cast<void*>(ptr); 
		
		// ptr = nullptr; 
		/*
		以上置空无意义：
			① C++ 的参数传递默认是“传值（Pass by Value）” 函数 void deallocate(T* ptr) 接收到的 ptr，只是外部调用者传入的指针的一个副本。在函数内部执行 ptr = nullptr;，只会把这个副本变成空。
			② 职责分离:
				内存池的职责：只负责管理“这块内存现在属于谁”。现在你把内存还给我，我就把它标记为“空闲”，仅此而已。
				调用者的职责：调用者既然决定把内存还回去，就应该自己保证不再使用这个指针。如果调用者为了安全想置空，应该在外部自己写 ptr = nullptr;。
		*/
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
		/*
		注释里的“malloc”是指代“分配裸内存”这一行为的通俗说法（语义），而代码中的 ::operator new 是 C++ 中实现这一行为的标准手段（语法）。
		*/
		// 1.申请大块内存 malloc
		size_t size = sizeof(T) * count;
		/* 
		::operator new(size) 的返回值类型死死地定为 void*。 
		别的指针隐式转成void*可以（比如char*,二级指针）
		void*转别的则怎必须要显示的（不论是二级指针还是char*）
		*/
		// 使用char*，因为步长计算是1（单位步长），可方便得利用 p + sizeof(T) 来指定步长为 sizeof(T)
		// char* newBlock = ::operator new(size);  X
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
		
		/* 为什么这里要用新块的尾巴去指旧链表的头？
		“这是一个基于时间复杂度的优化选择。 因为新申请的内存块是连续的，我可以通过指针偏移 O(1) 只有计算出新链表的尾节点位置。 而旧的空闲链表是离散的，想找到它的尾节点需要 O(N) 的遍历。 为了保证 expand 操作的高效性，我选择使用头插法：将新链表直接链接到旧链表的头部，这样无论当前池子是否为空，操作都能在常数时间内完成。”
		*/		

		freeList_ = newBlock;

	}

private:
	// 空闲链表：把原本存data的位置直接用来存*next（下一个空闲内存的地址）
	void* freeList_;   			// 空闲链表头指针
	std::vector<void*> chunks_;
	// std::mutex mutex_;  // thread_local , 不需要锁

};
/*
问：“这三个成员是什么时候创建的？”

答案： 是在 本线程第一次 调用 new AgvMessage() 的瞬间，在 进入 ObjectPool::allocate 函数体之前，由 C++ 运行时（Runtime）自动调用 ObjectPool 构造函数创建的。

这就是 “Lazy Initialization（懒加载）” 的极致体现——不见兔子不撒鹰，你不 new 对象，我就不初始化 Pool，绝不浪费一点资源。
*/


#endif