/* SPDX-License-Identifier: Apache-2.0 */
#include "kira/platform/scripts.hpp"
#include <array>
#include <cstdio>
#include <mutex>
#include "esp_err.h"
#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "http_util.hpp"
#include "kira/platform/worker.hpp"
namespace kira::platform::scripts { namespace {
constexpr const char* TAG="kira_scripts";
std::mutex g_mutex; RunStatus g_status;
const std::vector<Script> g_catalog={
 {"wake-pc","Wake PC","Wake a LAN host with a configured MAC address",{{StepKind::WakeOnLan,"00:00:00:00:00:00",""}},false},
 {"http-hook","HTTP hook","Call a LAN or HTTPS automation endpoint",{{StepKind::Http,"https://example.invalid/","GET"}},false},
};
bool parse_mac(const std::string& text,std::array<uint8_t,6>& mac){ unsigned v[6]; if(std::sscanf(text.c_str(),"%x:%x:%x:%x:%x:%x",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5])!=6)return false; for(size_t i=0;i<6;i++){if(v[i]>255)return false;mac[i]=static_cast<uint8_t>(v[i]);} return true; }
esp_err_t wol(const std::string& target){ std::array<uint8_t,6> mac{}; if(!parse_mac(target,mac))return ESP_ERR_INVALID_ARG; std::array<uint8_t,102> packet{}; packet.fill(0xFF); for(size_t n=0;n<16;n++) for(size_t i=0;i<6;i++) packet[6+n*6+i]=mac[i]; int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP); if(fd<0)return ESP_FAIL; int yes=1; setsockopt(fd,SOL_SOCKET,SO_BROADCAST,&yes,sizeof(yes)); sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons(9); addr.sin_addr.s_addr=htonl(INADDR_BROADCAST); int sent=sendto(fd,packet.data(),packet.size(),0,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)); close(fd); return sent==static_cast<int>(packet.size())?ESP_OK:ESP_FAIL; }
esp_err_t http(const Step& step){ detail::HttpRequest req; req.url=step.target; req.method=step.argument=="POST"?HTTP_METHOD_POST:HTTP_METHOD_GET; req.timeout_ms=10000; auto res=detail::http_fetch(req,4096); return res.err; }
void publish(bool running,bool success,const std::string& id,const std::string& msg,uint32_t done,uint32_t total){std::lock_guard l(g_mutex);g_status={running,success,id,msg,done,total};}
}
const std::vector<Script>& catalog(){return g_catalog;}
RunStatus status(){std::lock_guard l(g_mutex);return g_status;}
bool run_async(const std::string& id){ const Script* found=nullptr; for(const auto& s:g_catalog)if(s.id==id){found=&s;break;} if(!found||!found->configured)return false; {std::lock_guard l(g_mutex);if(g_status.running)return false;} Script script=*found; publish(true,false,id,"Starting",0,script.steps.size()); return submit_job("script_run",[script=std::move(script)]{uint32_t done=0; for(const auto& step:script.steps){esp_err_t err=step.kind==StepKind::WakeOnLan?wol(step.target):http(step); if(err!=ESP_OK){ESP_LOGW(TAG,"script %s failed at step %u: %s",script.id.c_str(),done+1,esp_err_to_name(err));publish(false,false,script.id,"Failed: "+std::string(esp_err_to_name(err)),done,script.steps.size());return;} ++done;publish(true,false,script.id,"Running",done,script.steps.size());} publish(false,true,script.id,"Completed",done,script.steps.size());});}
}
