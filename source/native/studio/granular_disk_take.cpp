// SPDX-License-Identifier: GPL-3.0-or-later
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif
#include "studio/granular_disk_take.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <random>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace studio {
namespace {
using State=GranularDiskTake::State;
using Issue=GranularDiskTake::Issue;
namespace fs=std::filesystem;
using namespace std::chrono_literals;
constexpr uint32_t preparing=0x80000000u;
constexpr size_t headerBytes=44;
constexpr size_t frameBytes=4;
constexpr size_t blockFrames=4096;
static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert((GranularDiskTake::RingFrames&(GranularDiskTake::RingFrames-1))==0);
static_assert(sizeof(float)==sizeof(uint32_t)&&std::numeric_limits<float>::is_iec559);

int16_t quantize(const float* sample) noexcept {
    // Keep this conversion in integer space, including its input load. An
    // optimizing fast-math build may assume float expressions cannot be NaN.
    uint32_t bits=0;std::memcpy(&bits,sample,sizeof(bits));
    const uint32_t exponent=(bits>>23)&255u;const bool negative=(bits>>31)!=0;
    if(exponent==255||exponent<111)return 0;
    const uint32_t limit=negative?32768u:32767u;
    if(exponent>=127)return static_cast<int16_t>(negative?-32768:32767);
    const uint32_t shift=135-exponent; // Mantissa times 2^(exponent-127-23+15).
    const auto magnitude=std::min(limit,((bits&0x7fffffu)+0x800000u+(1u<<(shift-1)))>>shift);
    return static_cast<int16_t>(negative?-static_cast<int>(magnitude):static_cast<int>(magnitude));
}
std::array<unsigned char,headerBytes> wav_header(int rate,uint64_t frames) {
    std::array<unsigned char,headerBytes> out{};
    auto tag=[&](int at,const char* t){std::memcpy(out.data()+at,t,4);};
    auto u32=[&](int at,uint32_t v){for(int i=0;i<4;++i)out[at+i]=static_cast<unsigned char>(v>>(8*i));};
    auto u16=[&](int at,uint16_t v){out[at]=static_cast<unsigned char>(v);out[at+1]=static_cast<unsigned char>(v>>8);};
    const auto bytes=static_cast<uint32_t>(frames*frameBytes);
    tag(0,"RIFF");u32(4,36+bytes);tag(8,"WAVE");tag(12,"fmt ");u32(16,16);
    u16(20,1);u16(22,2);u32(24,static_cast<uint32_t>(rate));u32(28,static_cast<uint32_t>(rate*4));u16(32,4);u16(34,16);
    tag(36,"data");u32(40,bytes);return out;
}
bool close_fd(int fd) {
#if defined(_WIN32)
    return _close(fd)==0;
#else
    return ::close(fd)==0;
#endif
}
size_t write_some(int fd,const unsigned char* data,size_t size) {
    size_t done=0;
    while(done<size){
#if defined(_WIN32)
        const auto n=_write(fd,data+done,static_cast<unsigned int>(size-done));
#else
        const auto n=::write(fd,data+done,size-done);
#endif
        if(n<0&&errno==EINTR)continue;
        if(n<=0)break;
        done+=static_cast<size_t>(n);
    }
    return done;
}
bool truncate_and_rewind(int fd,uint64_t length) {
#if defined(_WIN32)
    return _chsize_s(fd,length)==0&&_lseeki64(fd,0,SEEK_SET)==0;
#else
    return ftruncate(fd,static_cast<off_t>(length))==0&&lseek(fd,0,SEEK_SET)==0;
#endif
}
bool flush_fd(int fd) {
#if defined(_WIN32)
    return _commit(fd)==0;
#else
    int result;do{result=fsync(fd);}while(result<0&&errno==EINTR);return result==0;
#endif
}
std::string take_stem() {
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::random_device random;
    char suffix[17];
    std::snprintf(suffix, sizeof(suffix), "%08x%08x", unsigned(random()), unsigned(random()));
    return "Granular-" + std::to_string(now) + "-" + suffix;
}

// Reserve the entire namespace first. KNULLI Scarab's fuse-exfat has no hard
// links and its FUSE 3 rename callback ignores RENAME_NOREPLACE. Every normal
// app writer gets a new directory and never publishes into an existing one.
// On POSIX, use the held directory descriptor for all file operations.
class TakeDirectory {
public:
    fs::path path;
#if !defined(_WIN32)
    struct Identity {
        struct stat attributes{};
        bool valid = false;
    };
#else
    struct Identity { bool valid = true; };
#endif

    ~TakeDirectory() {
        if (owns_directory()) {
#if defined(_WIN32)
            ::RemoveDirectoryW(path.c_str());
#else
            ::rmdir(path.c_str());
#endif
        }
#if !defined(_WIN32)
        if (fd_ >= 0) close_fd(fd_);
#endif
    }

    bool reserve(const fs::path& parent) {
        const auto stem = take_stem();
        for (int attempt = 0; attempt < 32; ++attempt) {
            const auto candidate = parent / (attempt == 0 ? stem : stem + "-" + std::to_string(attempt));
#if defined(_WIN32)
            if (!::CreateDirectoryW(candidate.c_str(), nullptr)) {
                if (::GetLastError() == ERROR_ALREADY_EXISTS) continue;
                return false;
            }
#else
            if (::mkdir(candidate.c_str(), 0700) != 0) {
                if (errno == EEXIST) continue;
                return false;
            }
            fd_ = ::open(candidate.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (fd_ < 0) return false;
#endif
            path = candidate;
            return owns_directory();
        }
        return false;
    }

    int create(const fs::path& name) const {
        if (!owns_directory()) return -1;
#if defined(_WIN32)
        return _wopen((path / name).c_str(), _O_WRONLY | _O_BINARY | _O_CREAT | _O_EXCL | _O_NOINHERIT,
                      _S_IREAD | _S_IWRITE);
#else
        return ::openat(fd_, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
#endif
    }

    static Identity identity(int file) {
        Identity result;
#if !defined(_WIN32)
        result.valid = ::fstat(file, &result.attributes) == 0 && S_ISREG(result.attributes.st_mode);
#else
        (void)file;
#endif
        return result;
    }

    bool remove_owned(const fs::path& name, const Identity& written) const {
        if (!owns_directory() || !matches(name, written)) return false;
#if defined(_WIN32)
        return ::DeleteFileW((path / name).c_str());
#else
        return ::unlinkat(fd_, name.c_str(), 0) == 0;
#endif
    }

    bool publish(const fs::path& temp, const fs::path& final, const Identity& written,
                 uint64_t expectedBytes) const {
        if (!owns_directory() || !matches(temp, written)) return false;
#if defined(_WIN32)
        std::error_code ec;
        if (fs::file_size(path / temp, ec) != expectedBytes || ec) return false;
        return ::MoveFileW((path / temp).c_str(), (path / final).c_str());
#else
        if (static_cast<uint64_t>(written.attributes.st_size) != expectedBytes) return false;
        struct stat existing{};
        if (::fstatat(fd_, final.c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT)
            return false;
        // FUSE offers no atomic no-replace promise. The absence check is only
        // used inside this newly reserved directory, never on a shared path.
        return ::renameat(fd_, temp.c_str(), fd_, final.c_str()) == 0 && owns_directory();
#endif
    }

private:
    bool owns_directory() const {
        if (path.empty()) return false;
#if defined(_WIN32)
        const auto flags = ::GetFileAttributesW(path.c_str());
        return flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_DIRECTORY) &&
               !(flags & FILE_ATTRIBUTE_REPARSE_POINT);
#else
        struct stat opened{}, named{};
        return fd_ >= 0 && ::fstat(fd_, &opened) == 0 && ::lstat(path.c_str(), &named) == 0 &&
               S_ISDIR(named.st_mode) && opened.st_dev == named.st_dev && opened.st_ino == named.st_ino;
#endif
    }

    bool matches(const fs::path& name, const Identity& written) const {
        if (!written.valid) return false;
#if defined(_WIN32)
        const auto flags = ::GetFileAttributesW((path / name).c_str());
        return flags != INVALID_FILE_ATTRIBUTES && !(flags & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
#else
        struct stat named{};
        return ::fstatat(fd_, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(named.st_mode) &&
               written.attributes.st_dev == named.st_dev && written.attributes.st_ino == named.st_ino &&
               written.attributes.st_size == named.st_size;
#endif
    }
#if !defined(_WIN32)
    int fd_ = -1;
#endif
};

struct PartFile {
    explicit PartFile(const TakeDirectory& owner) : owner(owner) {}
    const TakeDirectory& owner;
    int fd=-1;uint64_t frames=0;
    fs::path temp,final;
    ~PartFile(){if(fd>=0)close_fd(fd);}
    bool open(const fs::path& path,int rate){
        final=owner.path/path;temp=final;temp+=".part";frames=0;fd=owner.create(temp.filename());
        if(fd<0){temp.clear();return false;}
        const auto h=wav_header(rate,0);
        if(write_some(fd,h.data(),h.size())==h.size())return true;
        discard_empty();return false;
    }
    void discard_empty(){
        if(fd<0)return;
        const auto written=TakeDirectory::identity(fd);
        close_fd(fd);fd=-1;
        if(owner.remove_owned(temp.filename(),written))temp.clear();
    }
    bool finish(int rate){
        if(fd<0||!frames)return false;
        const auto total=headerBytes+frames*frameBytes;const auto header=wav_header(rate,frames);
        const bool valid=truncate_and_rewind(fd,total)&&write_some(fd,header.data(),header.size())==header.size()&&flush_fd(fd);
        const auto written=TakeDirectory::identity(fd);
        const bool closed=close_fd(fd);fd=-1;
        if(!valid||!closed)return false;
        if(!owner.publish(temp.filename(),final.filename(),written,total))return false;
        temp.clear();return true;
    }
};
const char* issue_message(Issue issue){
    switch(issue){
    case Issue::None:return "Granular take saved.";
    case Issue::NoAudio:return "No audio frames reached the recorder.";
    case Issue::BufferOverrun:return "Storage could not keep up; recording stopped before dropping audio. Completed audio kept.";
    case Issue::FormatChanged:return "Audio format changed; recording stopped. Completed audio kept.";
    case Issue::LowSpace:return "Storage is nearly full; recording stopped. Completed audio kept.";
    case Issue::OpenFailed:return "Cannot open a new take file. Check the recording folder and free space.";
    case Issue::WriteFailed:return "Storage write failed. Completed audio kept; check free space and the card.";
    case Issue::FinalizeFailed:return "Could not finalize the take. Completed parts and the temporary recording are kept.";
    }
    return "Granular recorder stopped.";
}
} // namespace

uint64_t GranularDiskTake::compatible_segment_frames() noexcept {
    // RIFF size words are 32 bit. FAT32 limits the whole file, not just its
    // RIFF payload. A target with narrow off_t must also be able to seek/read
    // it in the existing Long WAV tool. No arbitrary time or 1GiB limit.
    uint64_t fileLimit=UINT32_MAX;
#if !defined(_WIN32)
    fileLimit=std::min(fileLimit,static_cast<uint64_t>(std::numeric_limits<off_t>::max()));
#endif
    return (fileLimit-headerBytes)/frameBytes;
}

struct GranularDiskTake::Impl {
    const Limits limits;
    const uint64_t segmentLimit;
    std::array<int16_t,RingFrames*2> ring{};
    std::atomic<uint32_t> gate{0},writing{0},fault{0},rate{0},done{1};
    alignas(64) std::atomic<uint32_t> writeIndex{0};
    alignas(64) std::atomic<uint32_t> readIndex{0};
    uint32_t generation=0; // Control thread only; high bit is reserved for Starting.
    mutable std::mutex snapshotMutex;
    Snapshot state;
    std::thread worker;

    explicit Impl(Limits l):limits(l),segmentLimit(l.segmentFrames?std::min(l.segmentFrames,compatible_segment_frames()):compatible_segment_frames()){}
    ~Impl(){gate.store(0);if(worker.joinable())worker.join();} // engine shutdown, not UI Stop/Poll
    void set_state(State phase,Issue issue=Issue::None,const std::string& recoverable={}){
        std::lock_guard<std::mutex> lock(snapshotMutex);
        state.state=phase;state.issue=issue;state.recoverablePart=recoverable;
        state.message=phase==State::Starting?"Preparing recording storage...":phase==State::Recording?"Recording the internal mix. Stop saves this take.":
            phase==State::Stopping?"Finishing the WAV on disk...":phase==State::Cancelled?"Recording cancelled before audio started.":issue_message(issue);
    }
    Issue storage_status(const fs::path& directory){
        const uint64_t margin=limits.reserveBytes>UINT64_MAX-PcmBufferBytes?UINT64_MAX:limits.reserveBytes+PcmBufferBytes;
        std::error_code ec;const auto space=fs::space(directory,ec);
        return ec?Issue::WriteFailed:space.available<margin?Issue::LowSpace:Issue::None;
    }
    void capture_fault(uint32_t token,Issue issue) noexcept {
        if(gate.compare_exchange_strong(token,0))fault.store(static_cast<uint32_t>(issue));
    }
    void append(const float* audio,int count,int channels,int sampleRate) noexcept {
        const uint32_t token=gate.load();if(!token||(token&preparing))return;
        writing.store(1); // SC handshake: stop cannot drain past a started write.
        if(gate.load()!=token){writing.store(0);return;}
        if(!audio||count<=0){writing.store(0);return;}
        if((channels!=1&&channels!=2)||sampleRate!=int(rate.load(std::memory_order_relaxed))){capture_fault(token,Issue::FormatChanged);writing.store(0);return;}
        const auto w=writeIndex.load(std::memory_order_relaxed),r=readIndex.load(std::memory_order_acquire);
        if(uint32_t(count)>RingFrames-uint32_t(w-r)){capture_fault(token,Issue::BufferOverrun);writing.store(0);return;}
        for(int i=0;i<count;++i){const auto index=(w+uint32_t(i))&(RingFrames-1);
            ring[index*2]=quantize(audio+i*channels);ring[index*2+1]=quantize(audio+i*channels+(channels==2?1:0));}
        writeIndex.store(w+uint32_t(count),std::memory_order_release);writing.store(0);
    }
    void run(const fs::path& directory,int sampleRate,uint32_t token){
        Issue problem=Issue::None;std::error_code ec;
        fs::create_directories(directory,ec);if(ec||!fs::is_directory(directory,ec)||ec){set_state(State::Failed,Issue::OpenFailed);return;}
        if(const auto storage=storage_status(directory);storage!=Issue::None){set_state(State::Failed,storage);return;}
        TakeDirectory owner;
        if(!owner.reserve(directory)){set_state(State::Failed,Issue::OpenFailed);return;}
        PartFile part(owner);
        uint64_t partNumber=1;
        auto open_next=[&]{return part.open(owner.path.filename().string()+"-"+std::to_string(partNumber++)+".wav",sampleRate);};
        if(!open_next()){set_state(State::Failed,Issue::OpenFailed,part.temp.string());return;}
        auto expected=token|preparing;
        if(!gate.compare_exchange_strong(expected,token)){part.discard_empty();set_state(State::Cancelled);return;}
        set_state(State::Recording);
        std::array<unsigned char,blockFrames*frameBytes> bytes{};
        uint64_t sinceSpaceCheck=0;
        auto finish_part=[&]{
            if(!part.finish(sampleRate))return false;
            std::lock_guard<std::mutex> lock(snapshotMutex);state.parts.push_back({part.final.string(),part.frames});return true;
        };
        while(true){
            const auto r=readIndex.load(std::memory_order_relaxed),w=writeIndex.load(std::memory_order_acquire);
            const uint32_t available=uint32_t(w-r);
            if(!available){
                if(!gate.load()&&!writing.load()){
                    // Re-read after the in-flight callback handshake, otherwise
                    // the final published block could be missed by the worker.
                    if(readIndex.load()==writeIndex.load())break;
                    continue;
                }
                std::this_thread::sleep_for(2ms);continue;
            }
            if(part.fd<0&&!open_next()){problem=Issue::OpenFailed;gate.store(0);break;}
            const auto take=static_cast<uint32_t>(std::min<uint64_t>({available,blockFrames,segmentLimit-part.frames}));
            for(uint32_t i=0;i<take;++i){const auto index=(r+i)&(RingFrames-1);
                for(int c=0;c<2;++c){const auto v=static_cast<uint16_t>(ring[index*2+c]);bytes[i*4+c*2]=static_cast<unsigned char>(v);bytes[i*4+c*2+1]=static_cast<unsigned char>(v>>8);}}
            // Once copied out, the producer may immediately reuse these slots;
            // the file write and segment flush never block the audio callback.
            readIndex.store(r+take,std::memory_order_release);
            const size_t written=write_some(part.fd,bytes.data(),size_t(take)*frameBytes);
            const auto complete=written/frameBytes;part.frames+=complete;sinceSpaceCheck+=written;
            {std::lock_guard<std::mutex> lock(snapshotMutex);state.writtenFrames+=complete;}
            if(written!=size_t(take)*frameBytes){problem=Issue::WriteFailed;gate.store(0);break;}
            if(part.frames==segmentLimit&&!finish_part()){problem=Issue::FinalizeFailed;gate.store(0);break;}
            if(sinceSpaceCheck>=1024u*1024u){
                if(gate.load()){const auto storage=storage_status(directory);if(storage!=Issue::None){problem=storage;gate.store(0);}}
                sinceSpaceCheck=0;
            }
        }
        gate.store(0);while(writing.load())std::this_thread::sleep_for(1ms);
        if(problem==Issue::None)problem=static_cast<Issue>(fault.load());
        if(part.fd>=0){
            if(!part.frames)part.discard_empty();
            else if(!finish_part())problem=Issue::FinalizeFailed;
        }
        bool completed=false;{std::lock_guard<std::mutex> lock(snapshotMutex);completed=!state.parts.empty();}
        if(!completed&&problem==Issue::None)problem=Issue::NoAudio;
        set_state(completed?(problem==Issue::None?State::Ready:State::Interrupted):State::Failed,problem,part.temp.string());
    }
};

GranularDiskTake::GranularDiskTake():GranularDiskTake(Limits{}){}
GranularDiskTake::GranularDiskTake(Limits limits):impl_(std::make_unique<Impl>(limits)){}
GranularDiskTake::~GranularDiskTake()=default;
bool GranularDiskTake::start(const std::string& directory,int sampleRate){
    auto& p=*impl_;
    if(active()||sampleRate<8000||sampleRate>192000||directory.empty()||directory.find("://")!=std::string::npos||!fs::path(directory).is_absolute())return false;
    if(p.worker.joinable())p.worker.join(); // done observed; no pending SD work remains.
    p.generation=(p.generation+1)&~preparing;if(!p.generation)p.generation=1;
    p.readIndex.store(0);p.writeIndex.store(0);p.fault.store(0);p.rate.store(static_cast<uint32_t>(sampleRate));
    {std::lock_guard<std::mutex> lock(p.snapshotMutex);p.state={};p.state.rate=sampleRate;}
    p.set_state(State::Starting);p.done.store(0);p.gate.store(p.generation|preparing);
    try{
        p.worker=std::thread([&p,directory,sampleRate,token=p.generation]{
            try{p.run(fs::path(directory),sampleRate,token);}
            catch(...){p.set_state(State::Failed,Issue::WriteFailed);}
            p.gate.store(0);p.done.store(1,std::memory_order_release);
        });
    }catch(...){p.gate.store(0);p.done.store(1);p.set_state(State::Failed,Issue::OpenFailed);return false;}
    return true;
}
void GranularDiskTake::request_stop() noexcept{impl_->gate.store(0);}
bool GranularDiskTake::active()const noexcept{return !impl_->done.load(std::memory_order_acquire);}
GranularDiskTake::Snapshot GranularDiskTake::snapshot()const{
    auto& p=*impl_;std::lock_guard<std::mutex> lock(p.snapshotMutex);auto result=p.state;
    if((result.state==State::Starting||result.state==State::Recording)&&!p.gate.load()&&active()){
        result.state=State::Stopping;result.message="Finishing the WAV on disk...";
    }
    return result;
}
void GranularDiskTake::append(const float* pcm,int frames,int channels,int rate) noexcept{impl_->append(pcm,frames,channels,rate);}
} // namespace studio
