#include "ui/live_resample_files.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#if !defined(_WIN32)
#include <csignal>
#include <sys/resource.h>
#endif

namespace fs=std::filesystem;
using namespace pt::ui::live_resample_files;
namespace {
std::string read(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void write(const fs::path& p,const std::string& bytes){std::ofstream f(p,std::ios::binary);f<<bytes;f.close();assert(f);}
struct Gate {
    std::atomic<int> waiting{0};std::atomic<bool> go{false};
    void arrive(){++waiting;while(!go.load())std::this_thread::yield();}
    void start(){while(waiting.load()!=2)std::this_thread::yield();go=true;}
};
}
int main(int argc,char**argv){
    assert(argc==2);const fs::path root=fs::absolute(argv[1]);fs::create_directories(root);
    const auto existing=root/"foreign.part";write(existing,"USER TEMPORARY");
    assert(!create_exclusive(existing.string()));assert(read(existing)=="USER TEMPORARY");
    const std::array<PublishMode,3> modes={PublishMode::AUTO,PublishMode::ATOMIC_RENAME,PublishMode::EXCLUSIVE_COPY};
    // Two threads contend for an absent .part. Exactly one may open it, and the other's open must
    // not truncate the first stream, including before the first writer has finished its content.
    for(int n=0;n<100;++n){
        const auto p=root/("create-"+std::to_string(n)+".part");fs::remove(p);
        Gate gate;std::array<bool,2> ok{};std::array<std::thread,2> workers;
        for(int i=0;i<2;++i)workers[i]=std::thread([&,i]{gate.arrive();auto* f=create_exclusive(p.string());
            if(f){const auto bytes=std::string(8192,char('A'+i));ok[i]=std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size();ok[i]&=std::fclose(f)==0;}});
        gate.start();for(auto& worker:workers)worker.join();
        assert(int(ok[0])+int(ok[1])==1);assert(read(p)==std::string(8192,ok[0]?'A':'B'));
    }
    for(size_t m=0;m<modes.size();++m){
        const auto temp=root/("source-"+std::to_string(m)+".part");write(temp,"NEW AUDIO");
        const auto final=root/("foreign-"+std::to_string(m)+".wav");write(final,"USER AUDIO");
        std::error_code error;assert(!publish_no_replace(temp.string(),final.string(),error,modes[m]));
        assert(read(final)=="USER AUDIO"&&read(temp)=="NEW AUDIO");
        // Both completed candidates target an initially absent destination concurrently.
        // Cover the link path, the FAT-compatible rename path and the exclusive-copy fallback.
        for(int n=0;n<100;++n){
            const auto destination=root/("publish-"+std::to_string(m)+"-"+std::to_string(n)+".wav");fs::remove(destination);
            std::array<fs::path,2> candidate={root/"candidate-A.part",root/"candidate-B.part"};
            for(int i=0;i<2;++i)write(candidate[i],std::string(8192,char('A'+i)));
            Gate gate;std::array<bool,2> ok{};std::array<std::thread,2> workers;
            for(int i=0;i<2;++i)workers[i]=std::thread([&,i]{gate.arrive();std::error_code ec;
                ok[i]=publish_no_replace(candidate[i].string(),destination.string(),ec,modes[m]);});
            gate.start();for(auto& worker:workers)worker.join();
            if(int(ok[0])+int(ok[1])!=1)std::fprintf(stderr,"publication race: mode=%zu iteration=%d success=%d/%d\n",m,n,int(ok[0]),int(ok[1]));
            assert(int(ok[0])+int(ok[1])==1);
            assert(read(destination)==std::string(8192,ok[0]?'A':'B'));
            for(const auto& path:candidate)fs::remove(path);  // Drop only our own temporary names.
        }
        // Neither an absent symlink target nor the symlink itself may be overwritten.
        const auto target=root/("absent-target-"+std::to_string(m));fs::remove(target);
        const auto link=root/("foreign-link-"+std::to_string(m));fs::remove(link);
        fs::create_symlink(target,link,error);
        if(!error){
            assert(!create_exclusive(link.string()));assert(fs::is_symlink(link)&&!fs::exists(target));
            write(temp,"NEW AUDIO");assert(!publish_no_replace(temp.string(),link.string(),error,modes[m]));
            assert(fs::is_symlink(link)&&!fs::exists(target));
        }
    }
#if !defined(_WIN32)
    // Actual short-write/final-close failure in the fallback, without filling a real disk.
    const auto source=root/"full-disk-source.part",partial=root/"full-disk-destination.wav";
    write(source,std::string(8192,'P'));fs::remove(partial);
    struct rlimit previous{};assert(getrlimit(RLIMIT_FSIZE,&previous)==0);
    const auto handler=std::signal(SIGXFSZ,SIG_IGN);auto small=previous;small.rlim_cur=256;
    assert(setrlimit(RLIMIT_FSIZE,&small)==0);std::error_code error;
    const bool saved=publish_no_replace(source.string(),partial.string(),error,PublishMode::EXCLUSIVE_COPY);
    assert(setrlimit(RLIMIT_FSIZE,&previous)==0);std::signal(SIGXFSZ,handler);
    assert(!saved&&error&&read(source)==std::string(8192,'P'));
    assert(fs::file_size(partial)<=256); // Its own incomplete new file may remain, never Ready.
#endif
    std::puts("PASS: exclusive temporary creation, existing audio/temporary/symlink preservation, 100 creation races, 300 publication races (link, no-replace rename, exclusive-copy fallback), short-write failure");
}
