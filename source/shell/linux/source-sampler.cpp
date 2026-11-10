#include "linux/source-sampler.h"
#include "audio-decoders.h"
#include "songcore/project_io.h"
#include "songcore/wav_writer.h"
#include "vendor/dr_mp3/dr_mp3.h"
#include "vendor/dr_flac/dr_flac.h"
#include "vendor/minimp4/minimp4.h"
#include "vendor/faad2/include/neaacdec.h"
extern "C" {
#define STB_VORBIS_HEADER_ONLY
#include "vendor/stb_vorbis/stb_vorbis.c"
}
#include <opusfile.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <regex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace pt::platform {
namespace {
using json = songcore::json;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr size_t META_LIMIT = 4 * 1024 * 1024;
constexpr size_t MEDIA_LIMIT = 96 * 1024 * 1024;
constexpr int64_t PCM_LIMIT = 512LL * 1024 * 1024;
constexpr size_t RING = 32768;
constexpr int64_t CLIP_LIMIT_MS = ui::SOURCE_CLIP_MAX_MS;
struct Cancelled {};
struct NeedsRemux : std::runtime_error {NeedsRemux():std::runtime_error("This fragmented AAC container needs FFmpeg remuxing") {}};
using Cancel = std::function<bool()>;
void check(const Cancel& c) { if(c()) throw Cancelled{}; }
std::string text(const json& j,const char* key,const std::string& fallback={}) {
    const auto i=j.find(key); if(i==j.end()||i->is_null()) return fallback;
    if(i->is_string()) return i->get<std::string>().substr(0,16384);
    if(i->is_array()) { std::string s; for(const auto& v:*i) if(v.is_string()) {if(!s.empty())s+=" / ";s+=v.get<std::string>();if(s.size()>800)break;} return s.substr(0,1600); }
    if(i->is_number()||i->is_boolean()) return i->dump(); return fallback;
}
int64_t number(const json& j,const char* k,int64_t fallback=0) {
    const auto i=j.find(k);if(i==j.end())return fallback;
    try { double v=i->is_string()?std::stod(i->get<std::string>()):i->get<double>();return std::isfinite(v)&&v>=0&&v<1e12?static_cast<int64_t>(v):fallback; } catch(...) {return fallback;}
}
std::string clean(std::string s) {
    s=std::regex_replace(s,std::regex("<[^>]*>")," ");
    for(const auto& p:std::array<std::pair<const char*,const char*>,5>{{{"&amp;","&"},{"&quot;","\""},{"&#39;","'"},{"&lt;","<"},{"&gt;",">"}}}) {
        size_t i=0;while((i=s.find(p.first,i))!=std::string::npos){s.replace(i,std::strlen(p.first),p.second);i+=std::strlen(p.second);}
    }
    s=std::regex_replace(s,std::regex("[\\r\\n\\t ]+")," ");
    const auto first=s.find_first_not_of(' ');if(first==std::string::npos)return {};
    return s.substr(first,s.find_last_not_of(' ')-first+1).substr(0,800);
}
std::string encode(const std::string& s) {
    constexpr char hex[]="0123456789ABCDEF";std::string out;
    for(unsigned char c:s) if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out+=char(c);
    else {out+='%';out+=hex[c>>4];out+=hex[c&15];}return out;
}
std::string extension(std::string s) {
    s=s.substr(0,s.find('?'));const auto p=s.find_last_of('.');if(p==std::string::npos)return {};
    s=s.substr(p+1);std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});return s;
}
std::string testBase() {const char* p=std::getenv("PT_SOURCE_TEST_BASE");return p?p:"";}
bool allowed(const std::string& url) {
    const auto tb=testBase();if(!tb.empty()&&tb.rfind("http://127.0.0.1:",0)==0&&url.rfind(tb,0)==0)return true;
    if(url.rfind("https://",0)!=0||url.size()>16384)return false;
    auto host=url.substr(8,url.find_first_of("/?#",8)-8);if(host.find('@')!=std::string::npos||host.find(':')!=std::string::npos)return false;
    std::transform(host.begin(),host.end(),host.begin(),[](unsigned char c){return char(std::tolower(c));});
    for(const std::string domain:{"archive.org","wikimedia.org","googlevideo.com","youtube.com","youtube-nocookie.com"})
        if(host==domain||(host.size()>domain.size()&&host.compare(host.size()-domain.size(),domain.size(),domain)==0&&host[host.size()-domain.size()-1]=='.'))return true;
    return false;
}

// libcurl is supplied by the firmware (or the package). Loading it at runtime
// lets the tracker work offline on firmware without networking libraries.
struct CurlVersionInfo {
    int age;const char* version;unsigned int versionNumber;const char* host;int features;
    const char* sslVersion;long sslVersionNumber;const char* zlibVersion;const char*const* protocols;
};
struct CurlApi {
    void* lib=nullptr;
    void*(*init)()=nullptr;int(*setopt)(void*,int,...)=nullptr;
    int(*perform)(void*)=nullptr;void(*cleanup)(void*)=nullptr;
    int(*getinfo)(void*,int,...)=nullptr;const char*(*error)(int)=nullptr;
    std::string path,version,tls,failure="HTTPS library unavailable. Update the device network runtime; offline music remains usable.";
    CurlApi(){
        // Onion also ships a smaller parasyte libcurl without HTTPS. MainUI can inherit
        // that directory before the normal library. Prefer the installed Onion TLS build,
        // and require HTTPS capability instead of treating any libcurl as usable.
        const char* names[]={
#ifdef PT_MIYOO_PLUS_ARCHIVE
            "/mnt/SDCARD/.tmp_update/lib/libcurl.so.4",
#endif
            "libcurl.so.4","libcurl.so","libcurl.4.dylib","libcurl.dylib","/usr/lib/libcurl.4.dylib"};
        for(const char* name:names){
            lib=dlopen(name,RTLD_NOW|RTLD_LOCAL);if(!lib)continue;
            init=reinterpret_cast<decltype(init)>(dlsym(lib,"curl_easy_init"));
            setopt=reinterpret_cast<decltype(setopt)>(dlsym(lib,"curl_easy_setopt"));
            perform=reinterpret_cast<decltype(perform)>(dlsym(lib,"curl_easy_perform"));
            cleanup=reinterpret_cast<decltype(cleanup)>(dlsym(lib,"curl_easy_cleanup"));
            getinfo=reinterpret_cast<decltype(getinfo)>(dlsym(lib,"curl_easy_getinfo"));
            error=reinterpret_cast<decltype(error)>(dlsym(lib,"curl_easy_strerror"));
            auto info=reinterpret_cast<const CurlVersionInfo*(*)(int)>(dlsym(lib,"curl_version_info"));
            auto global=reinterpret_cast<int(*)(long)>(dlsym(lib,"curl_global_init"));
            bool https=false;const auto* detail=info?info(0):nullptr;
            if(detail&&detail->protocols)for(const char*const* protocol=detail->protocols;*protocol;++protocol)
                if(std::strcmp(*protocol,"https")==0)https=true;
            Dl_info loaded{};const bool located=init&&dladdr(reinterpret_cast<void*>(init),&loaded)!=0;
            const char* loadedPath=located&&loaded.dli_fname?loaded.dli_fname:name;
            std::fprintf(stderr,"sources: curl path=%s version=%s TLS=%s HTTPS=%s\n",loadedPath,
                         detail&&detail->version?detail->version:"unknown",detail&&detail->sslVersion?detail->sslVersion:"none",https?"yes":"no");
            if(!https)failure="Selected network library has no HTTPS. Update the device runtime; offline music remains usable.";
            if(!init||!setopt||!perform||!cleanup||!getinfo||!error||!https||!global||global(3)!=0){dlclose(lib);lib=nullptr;continue;}
            path=loadedPath;version=detail&&detail->version?detail->version:"unknown";tls=detail&&detail->sslVersion?detail->sslVersion:"unknown";
            failure.clear();return;
        }
    }
};
CurlApi& curl(){static CurlApi api;return api;}
struct Transfer {std::string data;FILE* file=nullptr;size_t count=0,limit=0;const Cancel* cancel=nullptr;bool overflow=false;};
size_t receive(char* data,size_t size,size_t count,void* context){
    auto& t=*static_cast<Transfer*>(context);const size_t n=size*count;
    if((*t.cancel)()||n>t.limit-t.count){t.overflow=n>t.limit-t.count;return 0;}
    if(t.file){if(std::fwrite(data,1,n,t.file)!=n)return 0;}else t.data.append(data,n);
    t.count+=n;return n;
}
int progress(void* context,int64_t,int64_t,int64_t,int64_t){return (*static_cast<const Cancel*>(context))()?1:0;}
void http(const std::string& url,Transfer& transfer,const Cancel& cancel,const std::string& ca){
#ifdef PT_MIYOO_PLUS_ARCHIVE
    if(testBase().empty()&&std::time(nullptr)<1577836800)
        throw std::runtime_error("Device clock is too old for HTTPS. Set the correct current date/time in Onion Clock.");
#endif
    auto& a=curl();if(!a.lib)throw std::runtime_error(a.failure);
    std::string next=url;
    for(int redirects=0;redirects<6;++redirects){
        check(cancel);if(!allowed(next))throw std::runtime_error("Source redirected to an unsupported address");
        void* easy=a.init();if(!easy)throw std::runtime_error("Cannot create source connection");
        struct Guard{CurlApi& a;void* p;~Guard(){a.cleanup(p);}} guard{a,easy};
        transfer.cancel=&cancel;std::array<char,256> errorBuffer{};
        auto option=[&](int id,auto value){const int code=a.setopt(easy,id,value);
            if(code)throw std::runtime_error("Network option "+std::to_string(id)+" failed (CURL "+std::to_string(code)+"). Update device network runtime.");};
        option(10010,errorBuffer.data()); // CURLOPT_ERRORBUFFER; keeps diagnostics even when strerror is stripped.
        option(10002,next.c_str()); // CURLOPT_URL
        option(10001,&transfer);option(20011,&receive);
        option(10018,"TrackWerk/0.9.0 (private audio source browser)");
        option(52,0L);option(78,15L);option(13,transfer.file?180L:35L);
        option(99,1L);option(43,0L);option(20219,&progress);option(10057,&cancel);
        option(181,testBase().empty()?2L:3L); // HTTPS; HTTP only for explicit local fixture server.
        option(182,testBase().empty()?2L:3L);
        if(!ca.empty())option(10065,ca.c_str());
        const int code=a.perform(easy);check(cancel);
        long status=0;a.getinfo(easy,0x200002,&status);
        if(status==301||status==302||status==303||status==307||status==308){
            char* redirected=nullptr;a.getinfo(easy,0x10001F,&redirected);
            if(!redirected)throw std::runtime_error("Source returned an empty redirect");next=redirected;
            transfer.data.clear();transfer.count=0;if(transfer.file){std::rewind(transfer.file);if(ftruncate(fileno(transfer.file),0)!=0)throw std::runtime_error("Cannot reset source cache");}continue;
        }
        if(transfer.overflow)throw std::runtime_error("Source exceeds the bounded download size (96 MiB audio / 4 MiB metadata)");
        if(code){
            const char* hint=code==1?"HTTPS unsupported. Check device network runtime.":
                code==5||code==6?"DNS lookup failed. Reconnect WiFi; check router DNS.":
                code==7?"Cannot reach source. Check WiFi/router.":
                code==28?"Connection timed out. Retry on a working WiFi connection.":
                code==35?"TLS handshake failed. Check device runtime and connection.":
                code==60?"Certificate rejected. Check date/time and CA bundle.":
                code==77?"CA bundle cannot be read. Reinstall this app's certs.":"Source connection failed.";
            const std::string detail=errorBuffer[0]?errorBuffer.data():a.error(code);
            std::fprintf(stderr,"sources: CURL %d %s; detail=%s; library=%s\n",code,hint,detail.c_str(),a.path.c_str());
            throw std::runtime_error("CURL "+std::to_string(code)+": "+hint+" "+detail);
        }
        if(status!=200)throw std::runtime_error("Source returned HTTP "+std::to_string(status));return;
    }
    throw std::runtime_error("Too many source redirects");
}
json getJson(const std::string& url,const Cancel& cancel,const std::string& ca){
    Transfer t;t.limit=META_LIMIT;http(url,t,cancel,ca);auto j=json::parse(t.data,nullptr,false);
    if(j.is_discarded()||!j.is_object())throw std::runtime_error("Source returned invalid metadata");return j;
}
struct Track {std::string id,title,creator,kind="audio",url,page,license,description,tags,instruments,ext,format;int64_t duration=0;};
json itemJson(const Track& t){return {{"id",t.id},{"title",t.title},{"creator",t.creator},{"kind",t.kind},{"url",t.page},{"license",t.license},{"description",t.description},{"tags",t.tags},{"instruments",t.instruments},{"duration_ms",t.duration}};}
std::string archiveBase(){const auto t=testBase();return t.empty()?"https://archive.org":t+"/archive";}
std::string commonsBase(){const auto t=testBase();return t.empty()?"https://commons.wikimedia.org/w/api.php":t+"/commons/api.php";}
std::vector<Track> archiveSearch(const std::string& query,const std::string& genre,bool random,const Cancel& cancel,const std::string& ca){
    std::string terms=query+" "+genre;if(clean(terms).find_first_not_of(' ')==std::string::npos)terms="music";
    std::string safe;size_t start=0;while(start<terms.size()){auto end=terms.find(' ',start);auto word=terms.substr(start,end-start);if(!word.empty()){if(!safe.empty())safe+=" AND ";safe+='"';for(char c:word){if(c=='"'||c=='\\')safe+='\\';safe+=c;}safe+='"';}if(end==std::string::npos)break;start=end+1;}
    const std::string q="mediatype:audio AND ("+safe+") AND -access-restricted-item:true AND (format:\"VBR MP3\" OR format:\"MP3\" OR format:\"Ogg Vorbis\" OR format:\"FLAC\" OR format:\"WAVE\")";
    auto url=archiveBase()+"/advancedsearch.php?q="+encode(q)+"&fl[]=identifier&fl[]=title&fl[]=creator&fl[]=licenseurl&fl[]=description&fl[]=subject&rows=40&output=json";
    std::mt19937 rng(std::random_device{}());if(random){const char* sorts[]={"downloads desc","addeddate desc","identifier asc"};url+="&sort[]="+encode(sorts[rng()%3]);}
    auto data=getJson(url+"&page=1",cancel,ca);auto response=data.value("response",json::object());
    if(random){int pages=std::clamp<int>((number(response,"numFound")+39)/40,1,12);int page=1+rng()%pages;if(page>1)response=getJson(url+"&page="+std::to_string(page),cancel,ca).value("response",json::object());}
    std::vector<Track> out;const auto docs=response.value("docs",json::array());
    if(!docs.is_array())return out;
    for(const auto& d:docs){if(!d.is_object())continue;Track t;t.id=text(d,"identifier");if(!std::regex_match(t.id,std::regex("[A-Za-z0-9_.-]{1,250}")))continue;t.kind="album";t.title=clean(text(d,"title",t.id));t.creator=clean(text(d,"creator"));t.page="https://archive.org/details/"+encode(t.id);t.license=text(d,"licenseurl");t.description=clean(text(d,"description"));t.tags=clean(text(d,"subject"));out.push_back(std::move(t));if(out.size()==40)break;}
    if(random)std::shuffle(out.begin(),out.end(),rng);return out;
}
std::vector<Track> archiveOpen(const Track& item,const Cancel& cancel,const std::string& ca){
    if(!std::regex_match(item.id,std::regex("[A-Za-z0-9_.-]{1,250}")))throw std::runtime_error("Invalid Archive item identifier");
    const auto data=getJson(archiveBase()+"/metadata/"+encode(item.id),cancel,ca);const auto meta=data.value("metadata",json::object());
    if(text(meta,"access-restricted-item")=="true")throw std::runtime_error("This Archive item is access restricted");
    const auto files=data.value("files",json::array());std::map<std::string,std::pair<int,Track>> best;
    #ifdef PT_MIYOO_PLUS_ARCHIVE
    const std::map<std::string,int> formats={{"mp3",0},{"ogg",2},{"opus",3},{"flac",4},{"wav",5}};
#else
    const std::map<std::string,int> formats={{"mp3",0},{"m4a",1},{"ogg",2},{"opus",3},{"flac",4},{"wav",5}};
#endif
    if(!files.is_array())throw std::runtime_error("No audio files in Archive item");int scanned=0;
    for(const auto& f:files){check(cancel);if(++scanned>5000)break;if(!f.is_object())continue;const auto name=text(f,"name");auto rank=formats.find(extension(name));if(name.empty()||rank==formats.end()||text(f,"private")=="true")continue;
        if(number(f,"size")>MEDIA_LIMIT)continue;Track t;t.id=item.id+":"+name;t.title=clean(text(f,"title",fs::path(name).stem().string()));t.creator=clean(text(meta,"creator",item.creator));t.page=item.page;t.license=text(meta,"licenseurl",item.license);t.url=archiveBase()+"/download/"+encode(item.id)+"/"+encode(name);t.ext=rank->first;t.description=clean(text(meta,"description",item.description));t.tags=clean(text(meta,"subject",item.tags));t.instruments=clean(text(meta,"instrumentation",text(meta,"instruments")));try{t.duration=static_cast<int64_t>(std::stod(text(f,"length","0"))*1000);}catch(...){}
        const auto key=fs::path(text(f,"original",name)).replace_extension().string();auto i=best.find(key);if(i==best.end()||rank->second<i->second.first)best[key]={rank->second,std::move(t)};
    }
    std::vector<Track> out;for(auto& e:best){out.push_back(std::move(e.second.second));if(out.size()==150)break;}if(out.empty())throw std::runtime_error("No supported public audio tracks below 96 MiB in this item");return out;
}
std::vector<Track> commonsDetails(const json& data){
    std::vector<Track> out;if(!data.contains("query")||!data["query"].is_object())return out;
    const auto pages=data["query"].value("pages",json::array());if(!pages.is_array())return out;
    for(const auto& p:pages){if(!p.is_object()||!p.contains("imageinfo")||!p["imageinfo"].is_array()||p["imageinfo"].empty())continue;auto i=p["imageinfo"][0];const auto mime=text(i,"mime");if(text(i,"mediatype")!="AUDIO")continue;
        Track t;t.id=text(p,"title");t.url=text(i,"url");if(!allowed(t.url))continue;
        const auto ext=extension(t.url);if(ext!="mp3"&&ext!="wav"&&ext!="ogg"&&ext!="oga"&&ext!="opus"&&ext!="flac"&&ext!="m4a")continue;t.ext=ext;
#ifdef PT_MIYOO_PLUS_ARCHIVE
        if(ext=="m4a")continue;
#endif
        auto m=i.value("extmetadata",json::object());auto field=[&](const char* k){auto v=m.value(k,json::object());return v.is_object()?clean(text(v,"value")):std::string{};};
        t.title=field("ObjectName");if(t.title.empty())t.title=t.id.rfind("File:",0)==0?t.id.substr(5):t.id;t.creator=field("Artist");if(t.creator.empty())t.creator="Wikimedia Commons";t.page=text(i,"descriptionurl","https://commons.wikimedia.org/wiki/"+encode(t.id));t.license=field("LicenseUrl");t.description=field("ImageDescription");t.tags=field("Categories");t.instruments=field("Instrumentation");out.push_back(std::move(t));if(out.size()==40)break;
    }return out;
}
std::vector<Track> commonsSearch(std::string query,const std::string& genre,bool random,const Cancel& cancel,const std::string& ca){
    query+=" "+genre;if(query.find_first_not_of(' ')==std::string::npos)query="music";
    const auto url=commonsBase()+"?format=json&formatversion=2&action=query&generator=search&gsrnamespace=6&gsrlimit=40&gsrsearch="+encode(query+" filetype:audio")+"&prop=imageinfo&iiprop=url%7Cmime%7Cmediatype%7Cextmetadata";
    auto j=getJson(url,cancel,ca);if(j.contains("error"))throw std::runtime_error("Commons search is unavailable; try another query");auto tracks=commonsDetails(j);if(random){std::mt19937 rng(std::random_device{}());std::shuffle(tracks.begin(),tracks.end(),rng);}return tracks;
}
std::string runProcess(const std::string& program,const std::vector<std::string>& args,const Cancel& cancel,int timeoutSeconds=90,const std::string& boundedOutput={},bool youtube=true){
    auto actual=args;const auto qjs=(fs::path(program).parent_path()/"qjs").string();
    // The release bundles EJS and QuickJS; source extraction must not download
    // executable helper code or select an arbitrary installed JS interpreter.
    if(youtube){actual.insert(actual.begin(),{"--no-remote-components","--no-cache-dir"});
        if(access(qjs.c_str(),X_OK)==0)actual.insert(actual.begin(),{"--js-runtimes","quickjs:"+qjs});}
    std::vector<char*> argv;argv.push_back(const_cast<char*>(program.c_str()));for(const auto& a:actual)argv.push_back(const_cast<char*>(a.c_str()));argv.push_back(nullptr);
    int pipes[2];if(pipe(pipes)!=0)throw std::runtime_error("Cannot start YouTube helper");
    const pid_t child=fork();if(child<0){close(pipes[0]);close(pipes[1]);throw std::runtime_error("Cannot start YouTube helper");}
    if(child==0){setpgid(0,0);dup2(pipes[1],STDOUT_FILENO);close(pipes[0]);close(pipes[1]);const int null=open("/dev/null",O_WRONLY);if(null>=0){dup2(null,STDERR_FILENO);close(null);}execv(program.c_str(),argv.data());_exit(127);}
    setpgid(child,child); // Race-safe with the identical child-side operation.
    int status=0;
    auto terminate=[&]{if(kill(-child,SIGKILL)!=0)kill(child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){};};
    close(pipes[1]);fcntl(pipes[0],F_SETFL,fcntl(pipes[0],F_GETFL)|O_NONBLOCK);std::string out;const auto deadline=Clock::now()+std::chrono::seconds(timeoutSeconds);bool done=false;
    while(!done){
        char block[8192];ssize_t n;while((n=read(pipes[0],block,sizeof block))>0){if(out.size()+size_t(n)>8*META_LIMIT){terminate();close(pipes[0]);throw std::runtime_error("YouTube metadata is too large");}out.append(block,size_t(n));}
        if(cancel()||Clock::now()>deadline){terminate();close(pipes[0]);check(cancel);throw std::runtime_error(youtube?"YouTube helper timed out":"Audio container helper timed out");}
        if(!boundedOutput.empty()){std::error_code error;const auto size=fs::file_size(boundedOutput,error);if(!error&&size>MEDIA_LIMIT){terminate();close(pipes[0]);throw std::runtime_error(youtube?"YouTube audio exceeds the 96 MiB download limit":"Audio container exceeds the 96 MiB limit");}}
        done=waitpid(child,&status,WNOHANG)==child;if(!done)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    char block[8192];ssize_t n;while((n=read(pipes[0],block,sizeof block))>0&&out.size()+size_t(n)<=8*META_LIMIT)out.append(block,size_t(n));close(pipes[0]);
    if(!WIFEXITED(status)||WEXITSTATUS(status)!=0)throw std::runtime_error(youtube?"YouTube helper could not read this public source; access may have changed":"FFmpeg could not prepare this audio container");
    if(!boundedOutput.empty()){std::error_code error;const auto size=fs::file_size(boundedOutput,error);if(error||size==0||size>MEDIA_LIMIT)throw std::runtime_error(youtube?"YouTube did not provide bounded, readable audio":"Audio container helper did not provide bounded, readable audio");}return out;
}
std::vector<Track> youtubeSearch(const std::string& tool,const std::string& query,const std::string& genre,bool random,const Cancel& cancel){
    std::string q=clean(query+" "+genre);if(q.find_first_not_of(' ')==std::string::npos)q="music";
    if(random){const char* extra[]={"live music","instrumental","vintage music","jazz session","library music"};std::mt19937 rng(std::random_device{}());q+=" "+std::string(extra[rng()%5]);}
    auto raw=runProcess(tool,{"--ignore-config","--no-warnings","--skip-download","--flat-playlist","--dump-single-json","--socket-timeout","15","--", "ytsearch20:"+q},cancel);
    auto j=json::parse(raw,nullptr,false);if(j.is_discarded())throw std::runtime_error("Invalid YouTube metadata");std::vector<Track> out;auto entries=j.value("entries",json::array());if(!entries.is_array())return out;
    for(const auto& e:entries){if(!e.is_object()||text(e,"live_status")=="is_live")continue;Track t;t.id=text(e,"id");if(!std::regex_match(t.id,std::regex("[A-Za-z0-9_-]{6,20}")))continue;t.title=clean(text(e,"title",t.id));t.creator=clean(text(e,"channel",text(e,"uploader")));t.page="https://www.youtube.com/watch?v="+t.id;t.description=clean(text(e,"description"));t.duration=number(e,"duration")*1000;t.kind="audio";out.push_back(std::move(t));}
    if(random){std::mt19937 rng(std::random_device{}());std::shuffle(out.begin(),out.end(),rng);}return out;
}
Track youtubeResolve(const std::string& tool,Track t,const Cancel& cancel){
    auto raw=runProcess(tool,{"--ignore-config","--no-warnings","--no-playlist","--skip-download","--dump-single-json","--socket-timeout","15","-f","bestaudio[ext=m4a]/bestaudio[ext=mp3]/bestaudio[ext=opus]","--",t.page},cancel);
    auto j=json::parse(raw,nullptr,false);if(j.is_discarded()||!j.is_object())throw std::runtime_error("Invalid YouTube audio metadata");
    if(text(j,"live_status")=="is_live"||j.value("is_live",false))throw std::runtime_error("Live streams are not supported");
    t.url=text(j,"url");t.ext=text(j,"ext");t.format=text(j,"format_id");
    if(text(j,"vcodec")!="none"||t.format.empty()||!allowed(t.url)||t.url.find(".m3u8")!=std::string::npos||text(j,"protocol").find("m3u8")!=std::string::npos)throw std::runtime_error("No supported direct public audio-only stream");
    t.description=clean(text(j,"description",t.description));t.license=clean(text(j,"license"));t.duration=number(j,"duration")*1000;return t;
}

struct File {FILE* f=nullptr;explicit File(const std::string& path,const char* mode):f(std::fopen(path.c_str(),mode)){}~File(){if(f)std::fclose(f);}File(const File&)=delete;};
void writeFloats(FILE* file,const float* data,int frames,int channels,int64_t& total,const Cancel& cancel){
    check(cancel);if(frames<=0||channels<1||channels>2)return;
    const int64_t bytes=int64_t(frames)*channels*sizeof(float);if((total+frames)*channels*int64_t(sizeof(float))>PCM_LIMIT)throw std::runtime_error("Track is too long for the bounded 512 MiB preview cache; choose a shorter recording");
    if(std::fwrite(data,1,size_t(bytes),file)!=size_t(bytes))throw std::runtime_error("Not enough free space for preview cache");total+=frames;
}
uint32_t u32(const unsigned char* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
uint16_t u16(const unsigned char* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
struct PcmInfo {int rate=0,channels=0;int64_t frames=0;};
int mp4Read(int64_t offset,void* output,size_t bytes,void* context){
    auto* file=static_cast<FILE*>(context);return offset<0||fseeko(file,offset,SEEK_SET)!=0||std::fread(output,1,bytes,file)!=bytes?-1:0;
}
PcmInfo decodeAac(const std::string& path,FILE* output,const Cancel& cancel){
    File input(path,"rb");if(!input.f)throw std::runtime_error("Cannot open AAC cache");
    const auto size=fs::file_size(path);MP4D_demux_t demux{};
    if(!MP4D_open(&demux,&mp4Read,input.f,int64_t(size)))throw std::runtime_error("Cannot read this MP4 audio container");
    struct DemuxGuard{MP4D_demux_t* d;~DemuxGuard(){MP4D_close(d);}}dg{&demux};
    int track=-1;for(unsigned i=0;i<demux.track_count;++i)if(demux.track[i].handler_type==MP4D_HANDLER_TYPE_SOUN&&demux.track[i].object_type_indication==MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3){track=int(i);break;}
    if(track<0)throw std::runtime_error("This MP4 does not contain AAC audio");auto* tr=&demux.track[track];
    if(!tr->dsi||!tr->dsi_bytes||tr->sample_count>10000000)throw std::runtime_error("Unsupported AAC audio index");
    if(tr->sample_count==0)throw NeedsRemux{};
    NeAACDecHandle dec=NeAACDecOpen();if(!dec)throw std::runtime_error("Cannot open AAC decoder");struct AacGuard{NeAACDecHandle d;~AacGuard(){NeAACDecClose(d);}}ag{dec};
    auto config=NeAACDecGetCurrentConfiguration(dec);config->outputFormat=FAAD_FMT_FLOAT;config->downMatrix=1;
    if(!NeAACDecSetConfiguration(dec,config))throw std::runtime_error("Cannot configure AAC decoder");
    unsigned long rate=0;unsigned char channels=0;if(NeAACDecInit2(dec,tr->dsi,tr->dsi_bytes,&rate,&channels)<0)throw std::runtime_error("Unsupported AAC configuration");
    PcmInfo info;std::vector<unsigned char> packet;std::array<float,8192> block{};
    for(unsigned i=0;i<tr->sample_count;++i){check(cancel);unsigned bytes=0,timestamp=0,duration=0;const auto offset=MP4D_frame_offset(&demux,unsigned(track),i,&bytes,&timestamp,&duration);
        if(!bytes)continue;if(bytes>1024*1024||offset>size||bytes>size-offset)throw std::runtime_error("Invalid AAC packet");
        packet.resize(bytes);if(mp4Read(int64_t(offset),packet.data(),bytes,input.f)!=0)throw std::runtime_error("Truncated AAC audio");
        NeAACDecFrameInfo fi{};const auto* decoded=static_cast<const float*>(NeAACDecDecode(dec,&fi,packet.data(),bytes));
        if(fi.error)throw std::runtime_error("Damaged AAC audio");if(!decoded||!fi.samples||!fi.channels)continue;
        if(!info.rate){info.rate=int(fi.samplerate);info.channels=tr->SampleDescription.audio.channelcount==1?1:std::min<int>(2,fi.channels);}
        if(info.rate!=int(fi.samplerate)||fi.channels>8)throw std::runtime_error("AAC format changes are not supported");
        const int frames=int(fi.samples/fi.channels);for(int start=0;start<frames;){int count=std::min<int>(4096,frames-start);for(int n=0;n<count;++n)for(int c=0;c<info.channels;++c)block[n*info.channels+c]=decoded[(start+n)*fi.channels+std::min<int>(c,fi.channels-1)];writeFloats(output,block.data(),count,info.channels,info.frames,cancel);start+=count;}
    }return info;
}
PcmInfo decodeWave(const std::string& path,FILE* out,const Cancel& cancel){
    File input(path,"rb");if(!input.f)throw std::runtime_error("Cannot open audio cache");unsigned char h[12];if(std::fread(h,1,12,input.f)!=12||std::memcmp(h,"RIFF",4)||std::memcmp(h+8,"WAVE",4))throw std::runtime_error("Unsupported WAV header");
    int format=0,channels=0,rate=0,bits=0,align=0;int64_t dataSize=0,dataPos=0;
    while(std::fread(h,1,8,input.f)==8){const auto bytes=u32(h+4);const auto pos=ftello(input.f);if(!std::memcmp(h,"fmt ",4)){if(bytes<16||bytes>4096)throw std::runtime_error("Invalid WAV format");std::vector<unsigned char> f(bytes);if(std::fread(f.data(),1,bytes,input.f)!=bytes)throw std::runtime_error("Truncated WAV format");format=u16(f.data());channels=u16(f.data()+2);rate=int(u32(f.data()+4));align=u16(f.data()+12);bits=u16(f.data()+14);if(format==65534&&bytes>=40)format=u16(f.data()+24);}else if(!std::memcmp(h,"data",4)){dataSize=bytes;dataPos=pos;}if(fseeko(input.f,pos+bytes+(bytes&1),SEEK_SET)!=0)break;if(format&&dataPos)break;}
    if(channels<1||channels>2||rate<8000||rate>192000||align!=channels*(bits/8)||!((format==1&&(bits==8||bits==16||bits==24||bits==32))||(format==3&&bits==32))||!dataPos)throw std::runtime_error("WAV must be mono/stereo PCM or float audio");
    if(dataSize%align)throw std::runtime_error("Truncated WAV frames");if(fseeko(input.f,dataPos,SEEK_SET)!=0)throw std::runtime_error("Cannot seek WAV data");
    PcmInfo info{rate,channels,0};std::array<unsigned char,32768> raw{};std::array<float,8192> values{};int64_t remaining=dataSize;
    while(remaining>0){check(cancel);const int frames=int(std::min<int64_t>(remaining/align,4096/channels));const size_t count=size_t(frames)*align;if(std::fread(raw.data(),1,count,input.f)!=count)throw std::runtime_error("Truncated WAV audio");for(int i=0;i<frames*channels;++i){const auto* p=raw.data()+i*(bits/8);float value=0;if(format==3){std::memcpy(&value,p,4);if(!std::isfinite(value))value=0;}else if(bits==8)value=(int(p[0])-128)/128.f;else if(bits==16)value=int16_t(u16(p))/32768.f;else if(bits==24){int32_t n=int32_t(p[0])|(int32_t(p[1])<<8)|(int32_t(p[2])<<16);if(n&0x800000)n|=int32_t(0xff000000);value=n/8388608.f;}else value=int32_t(u32(p))/2147483648.f;values[i]=value;}writeFloats(out,values.data(),frames,channels,info.frames,cancel);remaining-=count;}
    return info;
}
PcmInfo decode(const std::string& path,const std::string& ext,const std::string& cache,const Cancel& cancel){
    File output(cache,"wb");if(!output.f)throw std::runtime_error("Cannot create source PCM cache");PcmInfo info;std::array<float,16384> block{};
    if(ext=="wav")info=decodeWave(path,output.f,cancel);
    else if(ext=="mp3"){
        drmp3 mp3{};if(!drmp3_init_file(&mp3,path.c_str(),nullptr))throw std::runtime_error("Cannot decode this MP3");struct Guard{drmp3* p;~Guard(){drmp3_uninit(p);}}g{&mp3};info.rate=int(mp3.sampleRate);info.channels=int(mp3.channels);if(info.channels<1||info.channels>2)throw std::runtime_error("Only mono/stereo source audio is supported");while(true){check(cancel);const int n=int(drmp3_read_pcm_frames_f32(&mp3,4096,block.data()));if(!n)break;writeFloats(output.f,block.data(),n,info.channels,info.frames,cancel);}
    }else if(ext=="flac"){
        drflac* flac=drflac_open_file(path.c_str(),nullptr);if(!flac)throw std::runtime_error("Cannot decode this FLAC");struct Guard{drflac* p;~Guard(){drflac_close(p);}}g{flac};info.rate=int(flac->sampleRate);info.channels=int(flac->channels);if(info.channels<1||info.channels>2)throw std::runtime_error("Only mono/stereo source audio is supported");while(true){check(cancel);const int n=int(drflac_read_pcm_frames_f32(flac,4096,block.data()));if(!n)break;writeFloats(output.f,block.data(),n,info.channels,info.frames,cancel);}
    }else if(ext=="ogg"||ext=="oga"||ext=="opus"){
        int error=0;OggOpusFile* opus=op_open_file(path.c_str(),&error);
        if(opus){struct Guard{OggOpusFile* p;~Guard(){op_free(p);}}g{opus};info.rate=48000;info.channels=2;while(true){check(cancel);int n=op_read_float_stereo(opus,block.data(),int(block.size()));if(n==OP_HOLE)continue;if(n<0)throw std::runtime_error("Damaged Opus audio");if(!n)break;writeFloats(output.f,block.data(),n,2,info.frames,cancel);}}
        else {stb_vorbis* vorbis=stb_vorbis_open_filename(path.c_str(),&error,nullptr);if(!vorbis)throw std::runtime_error("Cannot decode this Ogg audio");struct Guard{stb_vorbis* p;~Guard(){stb_vorbis_close(p);}}g{vorbis};const auto vi=stb_vorbis_get_info(vorbis);info.rate=int(vi.sample_rate);info.channels=std::min(2,vi.channels);while(true){check(cancel);int n=stb_vorbis_get_samples_float_interleaved(vorbis,info.channels,block.data(),int(block.size()));if(!n)break;writeFloats(output.f,block.data(),n,info.channels,info.frames,cancel);}}
    }else if(ext=="m4a"||ext=="mp4"){
        info=decodeAac(path,output.f,cancel);
    }else throw std::runtime_error("This audio format is not supported by the Linux source browser");
    if(info.frames<=0||info.rate<8000||info.rate>192000)throw std::runtime_error("Audio source contains no usable frames");
    if(std::fflush(output.f)!=0)throw std::runtime_error("Could not finish source cache");return info;
}
void put16(FILE* f,uint16_t n){unsigned char b[]={static_cast<unsigned char>(n),static_cast<unsigned char>(n>>8)};if(std::fwrite(b,1,2,f)!=2)throw std::runtime_error("Cannot write sample");}
void put32(FILE* f,uint32_t n){put16(f,uint16_t(n));put16(f,uint16_t(n>>16));}
void header(FILE* f,int rate,int channels,int64_t frames){const auto bytes=uint32_t(frames*channels*2);std::fwrite("RIFF",1,4,f);put32(f,36+bytes);std::fwrite("WAVEfmt ",1,8,f);put32(f,16);put16(f,1);put16(f,uint16_t(channels));put32(f,uint32_t(rate));put32(f,uint32_t(rate*channels*2));put16(f,uint16_t(channels*2));put16(f,16);std::fwrite("data",1,4,f);put32(f,bytes);}
}

struct SourceSampler::Impl {
    std::string root,cacheDir,ca,tool,ffmpeg;std::thread worker;std::mutex queueMutex,snapshotMutex;std::condition_variable wake;
    struct Job{std::string action;json args;uint64_t epoch;};std::deque<Job> jobs;std::atomic<bool> quit{false};std::atomic<uint64_t> cancelEpoch{0};std::string snapshot;
    json state;std::atomic<bool> queryRequested{false};std::vector<Track> results,tracks,sourceResults,sourceTracks;std::string provider="archive",query,genre;Track active;
    int selected=-1,trackIndex=-1;int64_t inMs=0,outMs=0;bool recording=false,sortTitle=false,prepared=false;
    PcmInfo pcm;std::string pcmPath;FILE* pcmFile=nullptr;
    std::array<float,RING*2> ring{};std::atomic<uint64_t> readIndex{0},writeIndex{0};std::atomic<int> readers{0},outputRate{48000};
    std::atomic<bool> playing{false},enabled{false},atEnd{false};std::atomic<int64_t> position{0};std::atomic<uint64_t> playEpoch{0};
    std::atomic<int> sourceRate{44100};std::atomic<int64_t> length{0};int producerRate=48000;double producerFrame=0;int64_t playBase=0;uint64_t audioEpoch=0,audioFrames=0;
    std::array<float,4096*2> readBlock{};int64_t readBase=-1;int readCount=0;
    Clock::time_point lastPublish=Clock::now();
    explicit Impl(const std::string& r):root(r),cacheDir((fs::path(r)/"Cache"/"Digging").string()){
        fs::create_directories(cacheDir);fs::create_directories(fs::path(r)/"Samples"/"Digging");
        if(const char* overrideCA=std::getenv("POCKETTRACKER_CACERT")){if(fs::is_regular_file(overrideCA))ca=overrideCA;}
        if(ca.empty())for(const auto& p:{fs::path(r)/"../tools/cacert.pem",fs::path(r)/"tools/cacert.pem",fs::path(r)/"../certs/cacert.pem"})if(fs::is_regular_file(p)){ca=p.lexically_normal().string();break;}
#ifndef PT_MIYOO_PLUS_ARCHIVE
        if(const char* t=std::getenv("PT_YTDLP")){if(access(t,X_OK)==0)tool=t;}
        if(tool.empty())for(const auto& p:{fs::path(r)/"../tools/yt-dlp",fs::path(r)/"tools/yt-dlp",fs::path(r)/"../tools/yt-dlp_linux_aarch64"})if(access(p.c_str(),X_OK)==0){tool=p.lexically_normal().string();break;}
        if(const char* f=std::getenv("PT_FFMPEG")){if(access(f,X_OK)==0)ffmpeg=f;}
        if(ffmpeg.empty())for(const char* p:{"/usr/bin/ffmpeg","/bin/ffmpeg","/usr/local/bin/ffmpeg"})if(access(p,X_OK)==0){ffmpeg=p;break;}
        if(ffmpeg.empty())if(const char* paths=std::getenv("PATH")){std::string path=paths;size_t start=0;while(start<path.size()){const auto end=path.find(':',start);const auto directory=path.substr(start,end-start);if(!directory.empty()&&directory.front()=='/'){const auto candidate=(fs::path(directory)/"ffmpeg").string();if(access(candidate.c_str(),X_OK)==0){ffmpeg=candidate;break;}}if(end==std::string::npos)break;start=end+1;}}
#endif
        state={{"status","idle"},{"message","Choose a genre and Random, or search for music."},{"busy",false},{"rendering",false},{"clip_ready",false},{"progress",0.0},{"error",""}};
#ifdef PT_MIYOO_PLUS_ARCHIVE
        if(std::time(nullptr)<1577836800)message("clock_required","Set the correct current date/time in Onion Clock before HTTPS. Offline music stays available.");
#endif
        if(!curl().lib){state["status"]="network_unavailable";state["error"]=curl().failure;}
        publish();worker=std::thread([this]{run();});
    }
    ~Impl(){quit=true;++cancelEpoch;playing=false;enabled=false;wake.notify_all();if(worker.joinable())worker.join();quiesce();if(pcmFile)std::fclose(pcmFile);}
    void quiesce(){enabled.store(false);while(readers.load()!=0)std::this_thread::yield();}
    int64_t currentMs()const{return pcm.rate?position.load()*1000/pcm.rate:0;}
    void publish(){
        state["network_runtime"]={{"library",curl().path},{"version",curl().version},{"TLS",curl().tls},{"HTTPS_available",curl().lib!=nullptr}};
        state["query"]=query;state["selected_index"]=selected;state["track_index"]=trackIndex;state["playing"]=playing.load();state["recording"]=recording;
        state["duration_ms"]=pcm.rate?pcm.frames*1000/pcm.rate:0;state["position_ms"]=currentMs();state["in_ms"]=inMs;state["out_ms"]=outMs;
        state["title"]=active.title;state["creator"]=active.creator;state["description"]=active.description;state["tags"]=active.tags;state["instruments"]=active.instruments;state["source_url"]=active.page;state["license_url"]=active.license;state["sort_order"]=sortTitle?"title":"source";
#ifdef PT_MIYOO_PLUS_ARCHIVE
        state["providers"]=json::array({{{"id","archive"},{"available",curl().lib!=nullptr}},{{"id","commons"},{"available",curl().lib!=nullptr}}});
#else
        state["providers"]=json::array({{{"id","archive"},{"available",curl().lib!=nullptr}},{{"id","commons"},{"available",curl().lib!=nullptr}},{{"id","youtube"},{"available",!tool.empty()}}});
#endif
        state["results"]=json::array();for(const auto& t:results)state["results"].push_back(itemJson(t));state["tracks"]=json::array();for(const auto& t:tracks)state["tracks"].push_back(itemJson(t));
        auto raw=state.dump(-1,' ',true);{std::lock_guard<std::mutex> lock(snapshotMutex);snapshot=std::move(raw);}lastPublish=Clock::now();
    }
    void message(const std::string& status,const std::string& msg){state["status"]=status;state["message"]=msg;state["error"]="";}
    void clearClip(){state["clip_ready"]=false;state["clip_path"]="";state["clip_frames"]=0;state["clip_rate"]=0;state["clip_channels"]=0;state["progress"]=0.;}
    void pause(){playing=false;quiesce();}
    void dropTrack(){pause();if(pcmFile){std::fclose(pcmFile);pcmFile=nullptr;}prepared=false;pcm={};position=0;active={};trackIndex=-1;recording=false;readBase=-1;readCount=0;inMs=outMs=0;clearClip();}
    void seek(int64_t ms,bool play){
        if(!prepared)throw std::runtime_error("Select an audio track first");pause();position=std::clamp<int64_t>(ms*pcm.rate/1000,0,pcm.frames);producerFrame=double(position.load());producerRate=outputRate.load();playBase=position.load();readIndex=writeIndex=0;atEnd=false;readBase=-1;readCount=0;++playEpoch;
        if(play){fill();playing=true;enabled=true;}
    }
    float sample(int64_t frame,int channel){
        frame=std::clamp<int64_t>(frame,0,pcm.frames-1);
        if(frame<readBase||frame>=readBase+readCount){readBase=frame;if(fseeko(pcmFile,frame*pcm.channels*int64_t(sizeof(float)),SEEK_SET)!=0)throw std::runtime_error("Cannot seek audio preview cache");readCount=int(std::fread(readBlock.data(),sizeof(float)*pcm.channels,4096,pcmFile));if(readCount<=0)throw std::runtime_error("Could not read audio preview cache");}
        return readBlock[size_t(frame-readBase)*pcm.channels+std::min(channel,pcm.channels-1)];
    }
    void fill(){
        if(!prepared||!pcmFile)return;auto r=readIndex.load(std::memory_order_acquire);auto w=writeIndex.load(std::memory_order_relaxed);const auto maximum=RING-2;
        while(w-r<maximum&&producerFrame<pcm.frames){const int64_t frame=int64_t(producerFrame);const double fraction=producerFrame-frame;float l0=sample(frame,0),r0=sample(frame,1),l1=sample(std::min(frame+1,pcm.frames-1),0),r1=sample(std::min(frame+1,pcm.frames-1),1);ring[(w%RING)*2]=float(l0+(l1-l0)*fraction);ring[(w%RING)*2+1]=float(r0+(r1-r0)*fraction);++w;producerFrame+=double(pcm.rate)/producerRate;}
        writeIndex.store(w,std::memory_order_release);if(producerFrame>=pcm.frames)atEnd=true;
    }
    void audio(float* output,int frames,int rate){
        if(rate<8000||rate>192000||frames<=0)return;outputRate.store(rate);readers.fetch_add(1);
        if(!enabled.load()){readers.fetch_sub(1);return;}
        std::fill(output,output+size_t(frames)*2,0.f);
        if(producerRate!=rate){readers.fetch_sub(1);return;}
        const auto epoch=playEpoch.load();if(audioEpoch!=epoch){audioEpoch=epoch;audioFrames=0;}
        auto r=readIndex.load(std::memory_order_relaxed);const auto w=writeIndex.load(std::memory_order_acquire);const int n=int(std::min<uint64_t>(frames,w-r));
        for(int i=0;i<n;++i){output[i*2]=ring[((r+i)%RING)*2];output[i*2+1]=ring[((r+i)%RING)*2+1];}
        audioFrames+=uint64_t(n);position.store(std::min<int64_t>(length.load(),playBase+int64_t(double(audioFrames)*sourceRate.load()/rate)));readIndex.store(r+uint64_t(n),std::memory_order_release);
        if(n<frames&&atEnd.load()){position.store(length.load());playing=false;enabled=false;}
        readers.fetch_sub(1);
    }
    void prepareTrack(int index,bool autoPlay,const Cancel& cancel){
        if(index<0||index>=int(tracks.size()))throw std::runtime_error("Choose a valid track");Track t=tracks[index];pause();recording=false;clearClip();state["busy"]=true;message("loading","Downloading and preparing audio preview; Stop cancels.");publish();
        if(provider=="youtube")t=youtubeResolve(tool,t,cancel);if(!allowed(t.url))throw std::runtime_error("Unsupported audio source address");
        const auto media=(fs::path(cacheDir)/"source.media").string();const auto cache=(fs::path(cacheDir)/"preview.f32.tmp").string();
        try{
            if(provider=="youtube"){
                std::remove(media.c_str());
                // Googlevideo's audio transport uses chunking and extractor-
                // supplied headers. Let the bundled audio-only downloader do
                // that job; never fall back to downloading a video stream.
                runProcess(tool,{"--ignore-config","--no-warnings","--no-playlist","--no-progress","--quiet","--no-mtime","--no-part","--http-chunk-size","1M","--max-filesize","96M","--socket-timeout","15","--retries","2","--fragment-retries","2","-f",t.format,"-o",media,"--",t.page},cancel,180,media);
            }else{
                File f(media,"wb");if(!f.f)throw std::runtime_error("Cannot create download cache");Transfer transfer;transfer.file=f.f;transfer.limit=MEDIA_LIMIT;http(t.url,transfer,cancel,ca);if(std::fflush(f.f)!=0)throw std::runtime_error("Cannot finish download cache");
            }
        }catch(...){std::remove(media.c_str());throw;}
        check(cancel);message("decoding","Preparing audio cache on the SD card; Stop cancels.");publish();PcmInfo next;
        const auto normalized=media+".fixed.m4a";
        try{
            try{next=decode(media,t.ext.empty()?extension(t.url):t.ext,cache,cancel);}
            catch(const NeedsRemux&){
                if(ffmpeg.empty())throw std::runtime_error("Fragmented AAC needs FFmpeg. Use the standard KNULLI Brick build or provide PT_FFMPEG.");
                message("decoding","Preparing fragmented AAC container with FFmpeg; audio is copied without re-encoding.");publish();std::remove(normalized.c_str());
                runProcess(ffmpeg,{"-nostdin","-hide_banner","-loglevel","error","-y","-protocol_whitelist","file,pipe","-i",media,"-map","0:a:0","-vn","-sn","-dn","-c:a","copy","-movflags","+faststart",normalized},cancel,180,normalized,false);
                next=decode(normalized,"m4a",cache,cancel);std::remove(normalized.c_str());
            }
            check(cancel);
        }catch(...){std::remove(media.c_str());std::remove(normalized.c_str());std::remove(cache.c_str());throw;}
        std::remove(media.c_str());dropTrack();const auto finalCache=(fs::path(cacheDir)/"preview.f32").string();if(std::rename(cache.c_str(),finalCache.c_str())!=0)throw std::runtime_error("Cannot finish audio cache");
        pcmPath=finalCache;pcm=next;pcmFile=std::fopen(pcmPath.c_str(),"rb");if(!pcmFile){pcm={};throw std::runtime_error("Cannot reopen audio cache");}prepared=true;active=std::move(t);trackIndex=index;sourceRate=pcm.rate;length=pcm.frames;outMs=std::min<int64_t>(30000,pcm.frames*1000/pcm.rate);state["busy"]=false;
        seek(0,autoPlay);message("ready",autoPlay?"Playing. A toggles pause; IN/OUT select your sample.":"Audio ready. Play to listen, then set IN/OUT.");
    }
    void render(const Cancel& cancel){
        if(!prepared)throw std::runtime_error("Select an audio track first");if(outMs<=inMs||outMs-inMs>CLIP_LIMIT_MS)throw std::runtime_error("Choose an IN/OUT range up to "+std::to_string(CLIP_LIMIT_MS/1000)+" seconds");pause();recording=false;clearClip();state["busy"]=true;state["rendering"]=true;message("rendering","Rendering marked audio to PCM16 WAV.");publish();
        const int64_t first=std::clamp<int64_t>(inMs*pcm.rate/1000,0,pcm.frames),last=std::clamp<int64_t>(outMs*pcm.rate/1000,first,pcm.frames);if(last<=first)throw std::runtime_error("The selected region contains no frames");
        const auto stamp=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();const std::string path=(fs::path(root)/"Samples"/"Digging"/("Digging-"+std::to_string(stamp)+".wav")).string(),temporary=path+".tmp";
        try{
            File source(pcmPath,"rb"),output(temporary,"wb");if(!source.f||!output.f)throw std::runtime_error("Cannot create rendered sample");if(fseeko(source.f,first*pcm.channels*int64_t(sizeof(float)),SEEK_SET)!=0)throw std::runtime_error("Cannot seek selected audio");header(output.f,pcm.rate,pcm.channels,last-first);
            std::array<float,8192> values{};std::array<unsigned char,16384> bytes{};int64_t done=0;
            while(done<last-first){check(cancel);const int n=int(std::min<int64_t>(4096,(last-first)-done));const int samples=n*pcm.channels;if(std::fread(values.data(),sizeof(float),samples,source.f)!=size_t(samples))throw std::runtime_error("Source cache ended before OUT");for(int i=0;i<samples;++i){const float value=std::isfinite(values[i])?std::clamp(values[i],-1.f,1.f):0.f;const int16_t sample=int16_t(std::clamp<long>(std::lrint(double(value)*32768.),-32768,32767));const uint16_t u=uint16_t(sample);bytes[i*2]=static_cast<unsigned char>(u);bytes[i*2+1]=static_cast<unsigned char>(u>>8);}if(std::fwrite(bytes.data(),1,size_t(samples)*2,output.f)!=size_t(samples)*2)throw std::runtime_error("SD card is full");done+=n;state["progress"]=double(done)/(last-first);}
            if(std::fflush(output.f)!=0)throw std::runtime_error("Cannot finish rendered sample");if(std::fclose(output.f)!=0){output.f=nullptr;throw std::runtime_error("Cannot close rendered sample");}output.f=nullptr;check(cancel);if(std::rename(temporary.c_str(),path.c_str())!=0)throw std::runtime_error("Cannot publish rendered sample");
            json attribution={{"title",active.title},{"creator",active.creator},{"source_url",active.page},{"license_url",active.license},{"in_ms",inMs},{"out_ms",outMs},{"sample_rate",pcm.rate},{"channels",pcm.channels},{"frames",last-first}};const auto raw=attribution.dump(2);File side(path+".source.json","wb");if(side.f)std::fwrite(raw.data(),1,raw.size(),side.f);
            state["clip_ready"]=true;state["clip_path"]=path;state["clip_frames"]=last-first;state["clip_rate"]=pcm.rate;state["clip_channels"]=pcm.channels;state["progress"]=1.;state["rendering"]=false;state["busy"]=false;message("clip_ready","Clip ready. Import loads it into the selected instrument.");
        }catch(...){std::remove(temporary.c_str());state["rendering"]=false;throw;}
    }
    void reorder(bool title){
        const auto resultId=selected>=0&&selected<int(results.size())?results[selected].id:std::string{};
        const auto trackId=trackIndex>=0&&trackIndex<int(tracks.size())?tracks[trackIndex].id:std::string{};
        results=sourceResults;tracks=sourceTracks;if(title){auto compare=[](const Track& a,const Track& b){return a.title<b.title;};std::stable_sort(results.begin(),results.end(),compare);std::stable_sort(tracks.begin(),tracks.end(),compare);}
        selected=-1;for(size_t i=0;i<results.size();++i)if(results[i].id==resultId)selected=int(i);if(selected<0&&!results.empty())selected=0;
        trackIndex=-1;for(size_t i=0;i<tracks.size();++i)if(tracks[i].id==trackId)trackIndex=int(i);sortTitle=title;message("ready",title?"Titles A-Z; current audio and markers kept.":"Original order; current audio and markers kept.");
    }
    void dispatch(const Job& job){
        const auto& a=job.action;const auto& args=job.args;Cancel cancel=[&]{return quit.load()||cancelEpoch.load()!=job.epoch;};check(cancel);
        if(a=="search"||a=="discover"){
            dropTrack();provider=text(args,"provider","archive");query=text(args,"query").substr(0,240);genre=text(args,"genre").substr(0,60);results.clear();sourceResults.clear();tracks.clear();sourceTracks.clear();selected=-1;state["busy"]=true;message("searching","Finding public audio sources...");publish();
            if(provider=="archive")results=archiveSearch(query,genre,a=="discover",cancel,ca);else if(provider=="commons")results=commonsSearch(query,genre,a=="discover",cancel,ca);else if(provider=="youtube"&&!tool.empty())results=youtubeSearch(tool,query,genre,a=="discover",cancel);else throw std::runtime_error("This source is unavailable; use Archive or Commons");
            sourceResults=results;selected=results.empty()?-1:0;sortTitle=false;message("results",results.empty()?"No matching audio found. Try another genre or query.":"Choose an album or track. Right enters the list; A opens it.");state["busy"]=false;
        }else if(a=="open"){
            const int index=int(number(args,"index",selected));if(index<0||index>=int(results.size()))throw std::runtime_error("Choose a valid result");dropTrack();tracks.clear();sourceTracks.clear();selected=index;state["busy"]=true;message("loading","Reading available audio tracks...");publish();if(provider=="archive")tracks=archiveOpen(results[index],cancel,ca);else tracks={results[index]};sourceTracks=tracks;trackIndex=tracks.empty()?-1:0;sortTitle=false;state["busy"]=false;message("tracks","Choose a track and press A to listen.");
        }else if(a=="track"||a=="listen"){
            const int index=int(number(args,"index",trackIndex));if(a=="listen"&&index==trackIndex&&prepared){if(playing.load())pause();else seek(position.load()>=pcm.frames?0:currentMs(),true);message("ready",playing.load()?"Playing source audio.":"Source paused.");}else prepareTrack(index,a=="listen",cancel);
        }else if(a=="play"){if(position.load()>=pcm.frames)seek(0,true);else seek(currentMs(),true);message("playing","Playing source audio.");}
        else if(a=="pause"){if(recording){outMs=std::min<int64_t>(currentMs(),inMs+CLIP_LIMIT_MS);recording=false;}pause();message("paused","Source paused.");}
        else if(a=="seek"){recording=false;const bool wasPlaying=playing.load();seek(number(args,"position_ms",currentMs()),wasPlaying);message("ready","Position changed.");}
        else if(a=="mark_in"||a=="mark_out"||a=="set_region"){
            if(!prepared)throw std::runtime_error("Select an audio track first");const auto duration=pcm.frames*1000/pcm.rate;int64_t start=inMs,end=outMs;
            if(a=="mark_in")start=number(args,"position_ms",currentMs());else if(a=="mark_out")end=number(args,"position_ms",currentMs());else{start=number(args,"in_ms",inMs);end=number(args,"out_ms",outMs);}
            start=std::clamp<int64_t>(start,0,std::max<int64_t>(0,duration-1));end=std::clamp<int64_t>(end,0,duration);if(a=="mark_in"&&end<=start)end=std::min<int64_t>(duration,start+30000);if(end<=start)throw std::runtime_error("OUT must follow IN");end=std::min<int64_t>(end,start+CLIP_LIMIT_MS);inMs=start;outMs=end;clearClip();message("ready","IN/OUT updated.");
        }else if(a=="record"){
            if(!prepared)throw std::runtime_error("Select an audio track first");const bool enable=args.contains("enabled")&&args["enabled"].is_boolean()?args["enabled"].get<bool>():!recording;clearClip();if(enable){inMs=std::min<int64_t>(currentMs(),std::max<int64_t>(0,pcm.frames*1000/pcm.rate-1));outMs=inMs;recording=true;if(!playing.load())seek(inMs,true);message("recording","Marking region. Record again finishes and renders.");}else{outMs=std::min<int64_t>(currentMs(),inMs+CLIP_LIMIT_MS);recording=false;render(cancel);}
        }else if(a=="render")render(cancel);
        else if(a=="sort")reorder(text(args,"order")=="title");
        else if(a=="cancel"||a=="stop"){if(recording){outMs=std::min<int64_t>(currentMs(),inMs+CLIP_LIMIT_MS);recording=false;}pause();state["busy"]=false;state["rendering"]=false;message("stopped","Source stopped. Play resumes at the current position.");}
        else if(a=="info")message("source",active.page.empty()?"Source page appears above; no web browser is installed.":"Source: "+active.page+" | License: "+active.license);
        else if(a=="query_dialog"){queryRequested=true;message("ready","Use the on-screen search keyboard, or select Random.");}
        else if(a=="test_load"&&!testBase().empty()){
            dropTrack();Track t;t.id="local-fixture";t.title=text(args,"title","Local test fixture");t.url=text(args,"url");t.ext=text(args,"ext","wav");t.page=t.url;tracks={t};sourceTracks=tracks;provider="archive";prepareTrack(0,false,cancel);
        }
    }
    void run(){
        while(!quit.load()){
            Job job;bool have=false;{std::unique_lock<std::mutex> lock(queueMutex);if(jobs.empty())wake.wait_for(lock,std::chrono::milliseconds(5));if(!jobs.empty()){job=std::move(jobs.front());jobs.pop_front();have=true;}}
            try{
                if(have)dispatch(job);
                if(playing.load()){
                    if(producerRate!=outputRate.load())seek(currentMs(),true);fill();
                }
                if(recording&&(currentMs()>=inMs+CLIP_LIMIT_MS||position.load()>=pcm.frames)){outMs=std::min<int64_t>(currentMs(),inMs+CLIP_LIMIT_MS);recording=false;const auto epoch=cancelEpoch.load();render([&]{return quit.load()||cancelEpoch.load()!=epoch;});}
            }catch(const Cancelled&){pause();state["busy"]=false;state["rendering"]=false;recording=false;message("stopped","Source operation cancelled.");}
            catch(const std::exception& e){pause();state["busy"]=false;state["rendering"]=false;recording=false;state["status"]="error";state["message"]=e.what();state["error"]=e.what();}
            catch(...){pause();state["busy"]=false;state["rendering"]=false;recording=false;state["status"]="error";state["message"]="Source operation failed.";state["error"]="Source operation failed.";}
            if(have||Clock::now()-lastPublish>std::chrono::milliseconds(80))publish();
        }
    }
};

SourceSampler::SourceSampler(const std::string& appRoot):p_(std::make_unique<Impl>(appRoot)){}
SourceSampler::~SourceSampler()=default;
bool SourceSampler::command(const std::string& action,const std::string& argsJson){
    static const std::array<const char*,21> actions={{"search","discover","sort","listen","query_dialog","open","track","play","pause","seek","mark_in","mark_out","set_region","record","render","cancel","stop","info","test_load","",""}};
    if(argsJson.size()>8192||std::none_of(actions.begin(),actions.end(),[&](const char* a){return action==a;})||action.empty())return false;
    if(action=="test_load"&&testBase().empty())return false;auto args=json::parse(argsJson.empty()?"{}":argsJson,nullptr,false);if(args.is_discarded()||!args.is_object())return false;
    std::lock_guard<std::mutex> lock(p_->queueMutex);if(p_->quit.load()||p_->jobs.size()>=32)return false;
    if(action=="stop"||action=="cancel"||action=="search"||action=="discover"){++p_->cancelEpoch;if(action=="stop"||action=="cancel"){p_->playing=false;p_->enabled=false;p_->jobs.clear();}}
    p_->jobs.push_back({action,std::move(args),p_->cancelEpoch.load()});p_->wake.notify_one();return true;
}
std::string SourceSampler::state_json(){std::lock_guard<std::mutex> lock(p_->snapshotMutex);return p_->snapshot;}
bool SourceSampler::take_query_request(){return p_->queryRequested.exchange(false);}
void SourceSampler::render_audio(float* stereo,int frames,int sampleRate){if(stereo)p_->audio(stereo,frames,sampleRate);}
void SourceSampler::audio_hook(void* source,float* stereo,int frames,int sampleRate){if(source)static_cast<SourceSampler*>(source)->render_audio(stereo,frames,sampleRate);}

} // namespace pt::platform
