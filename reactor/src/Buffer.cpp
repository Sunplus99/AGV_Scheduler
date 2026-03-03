#include <myreactor/Buffer.h>
#include <sys/uio.h>
#include <algorithm>
#include <string.h>
#include <arpa/inet.h>
#include <cassert>


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







