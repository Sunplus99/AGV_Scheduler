#include <utils/AsyncLogging.h>
#include <chrono>
// 构造函数：只做零成本初始化
AsyncLogging::AsyncLogging(const std::string& basename)
    : basename_(basename),
      fp_(nullptr),
      stop_(true),
      thread_(nullptr) {}

AsyncLogging::~AsyncLogging() {
    stop(); // 兜底
}

bool AsyncLogging::start() {
    if (stop_ == false) return true;  // 重复启动优化

    // 1.尝试打开文件
    fp_ = fopen(basename_.c_str(), "a");
    if(!fp_) {
        fprintf(stderr, "AsyncLogging: Failed to open log file %s\n", basename_.c_str());
    }
    
    // 2.只有文件打开成功，才启动后台线程
    stop_ = false;
    thread_ = std::make_unique<std::thread> (&AsyncLogging::ThreadFunc, this);

    return true;
}

void AsyncLogging::stop() {
    if(stop_) return; // 重复停止优化
    stop_ = true;
    // 线程可能正阻塞在 wait_for 上，必须叫醒它，让它通过检测逻辑并退出
    cond_.notify_one(); 
    if(thread_->joinable())
        thread_->join();

    // 清理资源
    if(fp_) fclose(fp_); 
}

// 优化前： Stack Buffer --(拷贝构造)--> Temp String --(常引用传递)--> Append形参 --(拷贝)--> Vector Element 总代价：2 次 malloc + 2 次 memcpy
    /*void AsyncLogging::Append(const std::string& log_line){
        current_buffer_.push_back(std::move(log_line));
    }*/
// 优化后 (std::move)： Stack Buffer --(拷贝构造)--> Temp String --(移动)--> Append形参 --(移动)--> Vector Element 总代价：1 次 malloc + 1 次 memcpy + 2 次指针赋值(极快)
void AsyncLogging::Append(std::string log_line){
    std::lock_guard<std::mutex> lock(mutex_);

    current_buffer_.push_back(std::move(log_line));

    if(current_buffer_.size() > 1000)
        cond_.notify_one();
}

/*
为什么 ThreadPool 中的 task 变量定义在 while 循环内部？
    std::function 不仅存储函数指针，还可能存储 Lambda 表达式捕获的上下文资源（Captured Variables），例如巨大的 shared_ptr<Image> 或数据库连接。
    如果放在外部，task 执行完后，线程回到 wait() 睡眠。此时 task 对象依然存活，它内部捕获的大对象（比如 100MB 的图片）也就无法析构。这会导致资源泄漏（延后释放）：明明任务做完了，内存却一直被占着，直到下一次任务赋值覆盖时才释放。
    放在内部：利用 C++ 的 RAII 机制和作用域规则。单次循环结束（}），task 自动析构，立马释放所有捕获的资源。代价：极低。构造一个空的 std::function 开销几乎为 0。
为什么 AsyncLogging 中的 buffer 变量定义在 while 循环外部？
    高性能日志库的核心痛点是 内存分配 (Malloc/Free)。
    如果在内部：每次循环 std::vector buffer 都是新构造的，capacity 为 0。swap 给前端（生产者）之后，前端拿到的也是一个容量为 0 的 vector。前端下次 push_back 日志时，必须触发 malloc 和 realloc 扩容。 
    放在外部：buffer 的生命周期贯穿整个线程运行期。第一轮循环后，buffer 扩容到了比如 4MB。通过 swap，这个拥有 4MB capacity 的“空壳子”被换给了前端生产者。核心收益：前端生产者在写入后续日志时，直接在已有的 4MB 内存上操作，完全避免了堆内存分配。这是实现“零拷贝”或“低延迟”的关键。代价：需要在循环末尾手动调用 buffer.clear()，但这只是重置 size，不释放 capacity，正是我们想要的。
追问：如果把它们的位置对调（task 放外面，buffer 放里面），会有什么后果？
    两个变量位置的不同，本质上是 “资源及时释放 (Resource Release)” 与 “内存复用 (Memory Reuse)” 之间的权衡。“这两个看似简单的位置差异，其实体现了两种完全不同的优化方向。
    对于 Task，我们关注的是逻辑正确性和资源安全性，利用局部变量的作用域结束自动析构，防止上一个任务的残留资源阻塞内存，所以要**‘用完即弃’**（放里面）。
    对于 Buffer，我们关注的是极致的 IO 性能，利用 vector 的 capacity 特性，通过 swap 将申请好的内存块在前后端之间反复传递，避免高频的系统调用（malloc），所以要**‘循环利用’**（放外面）。”
*/
void AsyncLogging::ThreadFunc() {
    std::vector<std::string> write_buffer;

    while(true){
        {
            std::unique_lock<std::mutex> lock(mutex_);

            /* 1.阻塞等待
            让当前线程阻塞等待，且有明确的超时时间限制，它解决了 “无限制等待” 的问题（比如单纯的 wait() 会一直等，直到被唤醒）;如果缓冲区为空，就让出 CPU，挂起线程。
            调用 wait_for 后，当前线程释放锁并进入等待状态，要么被其他线程唤醒（条件满足），要么等待满指定时间（超时），无论哪种情况，唤醒后都会重新获取锁，然后函数返回。
                A. 超时 (3秒)：保证即使日志很少，最长 3 秒也会落盘一次，防止程序崩溃丢失太多数据。
                B. 被 notify (缓冲区满了 / 析构退出)。
            */
            if(current_buffer_.empty()) {
                cond_.wait_for(lock, std::chrono::seconds(3));
            }

            // 2. 退出检查 
            // 即使被唤醒，也可能是因为 stop 中的通知，如果没数据了，再彻底退出
            if(current_buffer_.empty() && stop_) break;

            // 3. 交换缓冲区 swap
            /*
            通过 swap，极快地交换了两个容器的内部指针：current_buffer_ 瞬间变空（前端可以继续欢快地写），write_buffer 拿到了所有数据（后端慢慢写盘）。
            */
            write_buffer.swap(current_buffer_);
        }

        // 4.执行磁盘 IO
        if(!write_buffer.empty() && fp_) {
            for(const auto& str : write_buffer)
                fwrite(str.c_str(), 1, str.size(), fp_);
        }

        // 5.刷盘 : 立即刷新缓冲区，防止程序崩溃时日志丢失
        // 仅仅 fwrite 只是写到了 C 库的缓冲区，fflush 才是真正交给操作系统
        fflush(fp_);

        // 6.清楚本地缓冲区，为一下轮 swap 准备
        write_buffer.clear();
    }

    // 防御性
    if(fp_) fflush(fp_);
}