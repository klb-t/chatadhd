// Frozen default output/request parity. All bytes are synthetic, all transports fake.
// This source deliberately uses only public APIs available on 161cc22.
#include <iostream>
#include <regex>
#include "loom/media_providers.h"
#include "loom/github_sync.h"
#include "loom/semantic_worker.h"
#include "loom/net/http.h"
#include "loom/util/fs.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/graph_engine.h"
#include "loom/semantic_analyzer.h"
#include "loom/semantic_llm.h"
using namespace loom;
Json requests(const net::ScriptedTransport& http) {
  Json out=Json::array();
  for (auto r:http.requests()) {
    const std::string ct=net::header_value(r.headers,"Content-Type");
    const auto pos=ct.find("boundary=");
    if(pos!=std::string::npos) {
      const auto boundary=ct.substr(pos+9);
      size_t at=0; while((at=r.body.find(boundary,at))!=std::string::npos) {r.body.replace(at,boundary.size(),"BOUNDARY");at+=8;}
      for(auto& h:r.headers) if(h.first=="Content-Type") h.second=ct.substr(0,pos+9)+"BOUNDARY";
    }
    Json headers=Json::array();for(const auto& h:r.headers)headers.push_back(Json::array({h.first,h.second}));
    out.push_back(Json{{"method",r.method},{"url",r.url},{"headers",headers},{"body",r.body},{"timeout_ms",r.timeout_ms}});
  }
  return out;
}
int main(int argc, char** argv){
 const bool include_batch = !(argc > 1 && std::string(argv[1]) == "--standalone");
 Json out=Json::object();
 {net::ScriptedTransport h;h.set_fallback(net::ScriptedTransport::Reply::json(200,Json{{"text"," hi "},{"segments",Json::array({Json{{"text"," segment "},{"avg_logprob",-.2}}})}}));GroqAsr p("fake",h);auto r=p.transcribe_bytes("audio","m4a",std::string("pl"));if(!r)return 2;out["groq"]={{"result",r->to_json()},{"requests",requests(h)}};}
 {net::ScriptedTransport h;h.set_fallback(net::ScriptedTransport::Reply::json(200,Json{{"results",Json::array({Json{{"alternatives",Json::array({Json{{"transcript","hello"},{"confidence",.75}}})}}})}}));GoogleSpeechAsr p("fake",h);auto r=p.transcribe_bytes("audio","flac",{});if(!r)return 2;out["google"]={{"result",r->to_json()},{"requests",requests(h)}};}
 {net::ScriptedTransport h;h.set_fallback(net::ScriptedTransport::Reply::json(200,Json{{"ParsedResults",Json::array({Json{{"ParsedText"," one\r\n two "},{"TextOverlay",Json{{"Lines",Json::array()}}}}})}}));OcrSpaceProvider p("fake",h);auto r=p.recognize_bytes("image","png",std::string("pl"));if(!r)return 2;out["ocr"]={{"result",r->to_json()},{"requests",requests(h)}};}
 {net::ScriptedTransport h;h.set_fallback(net::ScriptedTransport::Reply::json(200,Json::array()));SyncConfig c;c.repo="owner/repo";GitHubSync s(c,h);out["github"]={{"config",c.to_json()},{"connected",s.test_connection()}};auto files=s.list_remote_files();if(!files)return 2;out["github"]["requests"]=requests(h);}
 {
   if (include_batch) {
   fsutil::TempDir td; if(td.path().empty()){std::cerr<<"temporary directory unavailable\n";return 2;}
   Config cfg(td.path()/"config.json"); Secrets secrets(td.path()/"secrets.json");
   cfg.set("semantic_model", "vendor/model"); secrets.set("api_key", "fake"); secrets.set("anthropic_batch_key", "fake");
   auto db=Database::open(td.path()/"probe.db"); if(!db)return 2;
   auto analyzer=SemanticAnalyzer::create(); if(!analyzer){std::cerr<<"analyzer: "<<analyzer.error().message<<"\n";return 2;}
   EventBus bus; net::ScriptedTransport http;
   http.set_fallback(net::ScriptedTransport::Reply::json(201, Json{{"id","offline-batch"}}));
   SemanticLLM llm(cfg,secrets,http,**analyzer); GraphEngine graph(**db,bus,**analyzer);
   auto conv=(*db)->create_conv("batch"); if(!conv){std::cerr<<"conv: "<<conv.error().message<<"\n";return 2;}
   for(int i=0;i<501;++i){NewMessage m;m.conv_id=conv->id;m.role="user";m.text="batch sample with synthetic α🙂";auto msg=(*db)->create_msg(m);if(!msg){std::cerr<<"msg: "<<msg.error().message<<"\n";return 2;}}
   SemanticWorker worker(**db,llm,graph,cfg,secrets,bus,http,**analyzer);
   auto count=worker.drain_once(); if(!count){std::cerr<<"drain: "<<count.error().message<<"\n";return 2;}
   auto canonical=requests(http); if(canonical.size()!=1){std::cerr<<"batch requests="<<canonical.size()<<" enabled="<<llm.enabled()<<" pending="<<worker.status().pending<<"\n";return 2;}
   auto payload=json::parse(canonical[0]["body"].get<std::string>()); if(!payload)return 2;
   for(auto& request:(*payload)["requests"])request["custom_id"]="MESSAGE_ID";
   canonical[0]["body"]=payload->dump();
   out["worker_batch"]={{"drained",*count},{"status",worker.status().to_json()},{"requests",canonical}};
   }
 }
 WorkerOptions w;out["worker"]={{"drain_batch",w.drain_batch},{"idle_poll_ms",w.idle_poll.count()},{"llm_rate_limit",w.llm_rate_limit},{"startup_delay_ms",w.startup_delay.count()},{"batch_threshold",w.batch_threshold},{"batch_max_messages",w.batch_max_messages},{"batch_endpoint",w.batch_endpoint}};
 std::cout<<out.dump()<<'\n';
}
