#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <netinet/in.h>
#include <sys/socket.h>

namespace pt::platform::source_live {
inline bool local_fixture_url(const std::string& url,const std::string& base) {
    return !base.empty()&&base.rfind("http://127.0.0.1:",0)==0&&
           (url==base||url.rfind(base+"/",0)==0);
}
inline bool public_https_url(const std::string& url) {
    if(url.size()>4096||url.rfind("https://",0)!=0)return false;
    for(unsigned char c:url)if(c<=32||c==127||c=='\\')return false;
    auto authority=url.substr(8,url.find_first_of("/?#",8)-8);
    if(authority.size()>253||authority.empty()||authority.find('@')!=std::string::npos)return false;
    if(authority.size()>4&&authority.compare(authority.size()-4,4,":443")==0)authority.resize(authority.size()-4);
    if(authority.empty()||authority.front()=='.'||authority.back()=='.'||authority.find("..")!=std::string::npos||authority.find('.')==std::string::npos)return false;
    for(unsigned char c:authority)if(!std::isalnum(c)&&c!='-'&&c!='.')return false;
    return true;
}
inline bool public_ipv4(uint32_t host) {
    const auto in=[&](uint32_t net,uint32_t mask){return (host&mask)==net;};
    return !(in(0,0xff000000)||in(0x0a000000,0xff000000)||in(0x64400000,0xffc00000)||
        in(0x7f000000,0xff000000)||in(0xa9fe0000,0xffff0000)||in(0xac100000,0xfff00000)||
        in(0xc0000000,0xffffff00)||in(0xc0000200,0xffffff00)||in(0xc0586300,0xffffff00)||
        in(0xc0a80000,0xffff0000)||in(0xc6120000,0xfffe0000)||in(0xc6336400,0xffffff00)||
        in(0xcb007100,0xffffff00)||(host&0xe0000000)==0xe0000000);
}
inline bool public_address(const sockaddr* address,socklen_t length) {
    if(!address)return false;
    if(address->sa_family==AF_INET&&length>=sizeof(sockaddr_in))
        return public_ipv4(ntohl(reinterpret_cast<const sockaddr_in*>(address)->sin_addr.s_addr));
    if(address->sa_family!=AF_INET6||length<sizeof(sockaddr_in6))return false;
    const auto* a=reinterpret_cast<const sockaddr_in6*>(address);const auto* b=a->sin6_addr.s6_addr;
    // First version intentionally accepts only native global unicast. Reject
    // mapped/translated IPv4 and transition/tunnel ranges as well as local IPs.
    if(a->sin6_scope_id||(b[0]&0xe0)!=0x20)return false;
    if(b[0]==0x20&&b[1]==0x01&&(b[2]<2||(b[2]==0x0d&&b[3]==0xb8)))return false;
    if((b[0]==0x20&&b[1]==0x02)||(b[0]==0x3f&&b[1]==0xfe))return false;
    return true;
}
} // namespace pt::platform::source_live
