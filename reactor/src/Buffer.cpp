#include <myreactor/Buffer.h>
#include <sys/uio.h>
#include <algorithm>
#include <string.h>
#include <arpa/inet.h>
#include <cassert>

/*
面试官： “你的 Buffer 类是怎么设计的？有什么优化吗？”

你（自信地）：

【开场：抛出痛点】 “在做网络编程时，我发现直接用 std::string 或 vector 做缓冲区有个大问题：效率太低。 比如我读了一个包，想要删除它，如果用 erase()，它会把后面所有数据往前挪，这是个 O(N) 的操作，CPU 全浪费在搬砖上了。

所以，我参考了 Muduo 的设计，自己实现了一个高性能 Buffer，主要做了三个核心优化：”

【优化一：双指针 + 惰性删除 (Lazy Deletion)】 “第一，我放弃了真正的删除，改用**‘双指针’设计。 我维护了 readerIndex 和 writerIndex 两个游标。 当我要取走数据时，比如我的 retrieve(len) 函数，我只是简单地把 readerIndex 往后移一移。 这就是‘惰性删除’**。原本 O(N) 的内存搬迁，瞬间变成了 O(1) 的整数加法，极其高效。”

【优化二：自动扩容与内存紧凑 (Memory Compaction)】 “第二，为了解决‘只读不删’导致的内存浪费，我设计了一个 makeSpace 机制。 当写入空间不够时，我不会无脑扩容（malloc），而是先检查头部是不是有别人读过的**‘废弃空间’。 如果废弃空间够用，我就触发一次‘内存紧凑’，把活着的数据搬到最前面去。 虽然这是一次拷贝，但它把扩容的次数大大降低了，这是一种典型的‘空间换时间’**策略。”

【优化三：分散读 (Scatter Read) —— 代码里的亮点】 “第三，也是我觉得最巧妙的一点，在 readFd 函数里，我使用了 Linux 的 readv 系统调用。 我利用了栈上一个 64K 的临时数组 (extrabuf)。 如果 Socket 发来的数据太多，Buffer 装不下，我就先让它溢出到这个栈数组里，然后再 append 进去。 这样做的好处是：我既不需要一开始就分配巨大的 Buffer（省内存），又能在一次系统调用里把所有数据都读完（省 CPU）。这就完美解决了‘不知道该给每个连接分多大内存’的难题。”
*/

using namespace myreactor;

Buffer::Buffer(size_t initialSize)
    : buffer_(kCheapPrepend + initialSize),
      readerIndex_(kCheapPrepend),
      writerIndex_(kCheapPrepend)
{}

// Buffer::~Buffer() {}

size_t Buffer::readableBytes() const {
    return writerIndex_ - readerIndex_;
}

size_t Buffer::writableBytes() const {
    return buffer_.size() - writerIndex_;
}


char* Buffer::beginWrite() { // 供内部使用 ::copy()
    return buffer_.data() + writerIndex_;
}

char* Buffer::beginRead() {  // 对内提供
    return buffer_.data() + readerIndex_;
}


ssize_t Buffer::readFd(int fd, int* savedErrno) {
    char extrabuf[65536]; // 栈上 64K 临时空间
    const size_t writable = writableBytes();

    struct iovec vec[2];
    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    /*
    这个 iovcnt 的选择本质上就是一种基于“历史高水位（High Water Mark）”的启发式策略,体现了一种自适应的设计思想。系统根据 Buffer 当前的内存水位（也就是历史峰值），动态决定是否启用栈缓冲兜底。
    Buffer 的 writable 大小实际上记录了历史流量的高水位线。如果系统曾经处理过大数据，Buffer 会保持较大的 Capacity。这种机制利用了空间局部性原理，避免了后续大流量到来时的重复扩容。
    当 writable 足够大时，减少一个 iovec（从 2 变 1）虽然对单次系统调用的性能提升有限，但这体现了减少内核态上下文切换开销的极致追求——既然用户态内存（Heap）已经足够，就不需要内核再去维护第二块内存（Stack）的映射。
    小 writable 表示 需要 extrabuf 兜底（防御性编程）
    大 writable 表示 不需要 extrabuf（性能优先） 
    */
    const int iovc = (writable < sizeof(extrabuf)) ? 2 : 1;

    const ssize_t n = ::readv(fd, vec, iovc);

    if(n < 0){
        *savedErrno = errno;
    }else if(static_cast<size_t>(n) <= writable) { // 第一块就装得下
        writerIndex_ += n;
    }else{ // 装不下，溢出部分在 extrabuf , 将其 append 进 buffer_
        writerIndex_ = buffer_.size();
        append(extrabuf, n-writable);
    }
    return n;
}

void Buffer::retrieve(size_t len) { //删
    if (len < readableBytes()) readerIndex_ += len;
    else { // len == readableBytes() (Connection::handleWrite 中以保证 len 不超过可读范围)
        // 这里涵盖了 len == readableBytes() 
        // 同时也防御了 len > readableBytes() 的错误调用情况
        retrieveAll();
    }
}

void Buffer::retrieveAll() {
    readerIndex_ = kCheapPrepend;
    writerIndex_ = kCheapPrepend;
}

// ===========================================
// 写入接口 (Append) 
// ===========================================

/*
维度一：「扩容」与「写入」分离控制，实现单一职责
    ensureWritableBytes(len)：仅负责「检查可写空间、按需扩容」，不涉及任何数据拷贝操作。
    std::copy(...)：仅负责「数据拷贝写入」，它默认认为目标缓冲区（beginWrite()指向的空间）已经足够容纳len字节数据，无需再做任何空间检查，只专注于完成数据从源到目标的拷贝。
    若直接使用std::string的append/insert，其内部是封装了「空间检查→扩容→数据拷贝」的一体化逻辑，无法拆分，带来问题：1.无法自定义扩容策略,比如针对大流量做特定大小预分配 2.职责混杂，难以维护与扩展，不符合「单一职责原则」
维度二：效率和安全性的平衡:一方面，std::copy 是一个通用的拷贝函数，在我们的Buffer场景中（处理的是char这类简单基础数据），编译器会自动对它进行优化，最终的执行效率和memcpy是一样的。另一方面，std::copy 比较安全，不仅能拷贝这类简单数据，还能妥善处理复杂数据类型，后续如果要扩展Buffer的功能，核心的写入逻辑完全不用改动。而memcpy只能处理连续内存中的简单数据，在安全性和适用范围上，都不如std::copy全面。
*/
// 【底层 (Kernel): 用 char* 进行精确的指针运算（步长=1），负责搬运数据。】
void Buffer::append(const void* data, size_t len) {
    ensureWritableBytes(len);

    const char* d = static_cast<const char*>(data);
    std::copy(d, d+len, beginWrite());

    writerIndex_ += len;
}

void Buffer::appendInt16(int16_t x){
    int16_t be16 = htons(x); // big end, 网络字节序
    append(&be16, sizeof(be16));
}

void Buffer::appendInt32(int32_t x){
    int32_t be32 = htonl(x); 
    append(&be32, sizeof(be32));
}

void Buffer::appendInt64(int64_t x) {
    int64_t be64 = htobe64(x);
    append(&be64, sizeof(be64));
}

void Buffer::ensureWritableBytes(size_t len) {
    if(writableBytes() < len) {
        makeSpace(len);
    }
}

/* 为什么不“先整理，再扩容”以实现百分百精细化扩容？
1.避免双重拷贝：第一步（搬运），把数据从后半段搬到了前半段；第二步：扩容）：resize 可能会触发 vector 的重新分配。此时vector 会申请一块全新的大内存，然后把整理过的数据再次拷贝到新内存里（最后释放旧内存）。而在目前的写法中，虽然多拷贝了一些“垃圾数据”到新内存，但有效数据只经历了一次拷贝
2.保证了语义的简洁性：分支 1（整理）：如果不用申请新内存就能装下 -> 那就整理（搬运）。否则就直接扩容
另外，多保留一点前面的空间也可能为下一次makeSpace提高了阈值
总之，扩容的时候多保留一点前面的空间而非百分百精细化扩容是一种空间与时间复杂度的权衡
*/
/* 用 ::copy() 优化为memmove
memmove vs memcpy (面试考点)：
    memcpy：不安全。它假设内存不重叠。如果有重叠，行为未定义（可能拷乱）。
    memmove：安全。它的名字虽然叫 "move"，但实际上是“安全的 copy”。它会检测源地址和目标地址的相对位置：
        如果 Dest < Source（向左）：从前往后拷。
        如果 Dest > Source（向右）：从后往前拷。
*/
void Buffer::makeSpace(size_t len){
    // 画图理解：可覆盖的总空间（前面读过的失效的+后面空的） 与 数据量+头部预留空间 相比较
    if(writableBytes()+readerIndex_ < kCheapPrepend+len){ // 真的不够，扩
        buffer_.resize(writerIndex_ + len);
    } else { // 整理内存后还是够的，开始整理（覆盖读过的失效的部分）
        std::copy(beginRead(), beginWrite(), buffer_.data()+kCheapPrepend);
        writerIndex_ = readableBytes() + kCheapPrepend; // 注意先后顺序，readableBytes()要不能被新的 readerIndex_ 更新
        readerIndex_ = kCheapPrepend;
    }
}

// ===========================================
// 头部插入接口 (Prepend) - 利用 kCheapPrepend
// ===========================================

void Buffer::prepend(const void* data, size_t len){
    assert(readerIndex_ >= len);

    readerIndex_ -= len;

    const char* d = static_cast<const char*>(data);
    std::copy(d, d+len, beginRead());
}

void Buffer::prependInt16(int16_t x){
    int16_t be16 = htons(x);
    prepend(&be16, sizeof(be16));
}

void Buffer::prependInt32(int32_t x){
    int32_t be32 = htonl(x);
    prepend(&be32, sizeof(be32));
}

void Buffer::prependInt64(int64_t x){
    int64_t be64 = htobe64(x);
    prepend(&be64, sizeof(be64));
}

// ===========================================
// 读与读取接口 (Peek & erase) 
// ===========================================
int16_t Buffer::peekInt16() const {
    ensureReadableBytes(sizeof(int16_t));
    int16_t be16 = 0;
    ::memcpy(&be16, data(), sizeof(int16_t));
    return ntohs(be16);
}

int32_t Buffer::peekInt32() const {
    ensureReadableBytes(sizeof(int32_t));
    int32_t be32 = 0;
    ::memcpy(&be32, data(), sizeof(int32_t));
    return ntohl(be32);
}

int64_t Buffer::peekInt64() const {
    ensureReadableBytes(sizeof(int64_t));
    int64_t be64 = 0;
    ::memcpy(&be64, data(), sizeof(int64_t));
    return be64toh(be64);
}

std::string Buffer::readAsString(size_t len){
    std::string res(beginRead(), len);
    retrieve(len);
    return res;
}
    
std::string Buffer::readAllAsString(){
    return readAsString(readableBytes());
}

// 2.2 【反序列化读取】
int16_t Buffer::readInt16(){
    int16_t res = peekInt16();
    retrieve(sizeof(int16_t));
    return res;
}

int32_t Buffer::readInt32(){
    int32_t res = peekInt32();
    retrieve(sizeof(int32_t));
    return res;
}

int64_t Buffer::readInt64(){
    int64_t res = peekInt64();
    retrieve(sizeof(int64_t));
    return res;
}

void Buffer::ensureReadableBytes(size_t len) const{
    assert(readableBytes() >= len);
}

void Buffer::erase(size_t len){
    retrieve(len);
}
// ===========================================
// 仿STL常用接口 
// ===========================================

size_t Buffer::size() const {
    return readableBytes();
}

bool Buffer::isempty() const {
    return readableBytes() == 0;
}

const char* Buffer::data() const { // 对内/外提供可读数据的起始指针 （::send()）
    return buffer_.data() + readerIndex_;
}

void Buffer::clear() {
    retrieveAll();
}







