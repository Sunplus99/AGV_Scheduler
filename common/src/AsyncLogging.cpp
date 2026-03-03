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


void AsyncLogging::Append(std::string log_line){
    std::lock_guard<std::mutex> lock(mutex_);

    current_buffer_.push_back(std::move(log_line));

    if(current_buffer_.size() > 1000)
        cond_.notify_one();
}

void AsyncLogging::ThreadFunc() {
    std::vector<std::string> write_buffer;

    while(true){
        {
            std::unique_lock<std::mutex> lock(mutex_);

            if(current_buffer_.empty()) {
                cond_.wait_for(lock, std::chrono::seconds(3));
            }

            // 2. 退出检查 
            // 即使被唤醒，也可能是因为 stop 中的通知，如果没数据了，再彻底退出
            if(current_buffer_.empty() && stop_) break;

            // 3. 交换缓冲区 swap
            write_buffer.swap(current_buffer_);
        }

        // 4.执行磁盘 IO
        if(!write_buffer.empty() && fp_) {
            for(const auto& str : write_buffer)
                fwrite(str.c_str(), 1, str.size(), fp_);
        }

        // 5.刷盘 : 立即刷新缓冲区，防止程序崩溃时日志丢失
        fflush(fp_);

        // 6.清楚本地缓冲区，为一下轮 swap 准备
        write_buffer.clear();
    }

    // 防御性
    if(fp_) fflush(fp_);
}