// Same source compiles against immutable 161cc22 and the data-profile revision.
// All inputs are synthetic; only pre-existing public APIs are invoked.
#include <algorithm>
#include <iostream>
#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/graph_engine.h"
#include "loom/graph_memory.h"
#include "loom/knowledge.h"
#include "loom/materialize.h"
#include "loom/runtime.h"
#include "loom/semantic_analyzer.h"
#include "loom/util/fs.h"
using namespace loom;

template<class T> T get(Result<T> result) {
  if (!result) { std::cerr << result.error().to_string() << '\n'; std::exit(2); }
  return std::move(result).value();
}
void ok(Status status) { if (!status) { std::cerr << status.error().to_string() << '\n'; std::exit(2); } }
std::string replace_run(std::string value, const std::string& run) {
  std::size_t position = 0;
  while ((position = value.find(run, position)) != std::string::npos) { value.replace(position, run.size(), "RUN"); position += 3; }
  return value;
}
int main() {
  Json output;
  auto analyzer = get(SemanticAnalyzer::create());
  output["rules"] = analyzer->rules().to_json();
  Json analyses = Json::array();
  for (const std::string& text : {"", "hello", "court evidence dowód pozew", "database API deployment backend",
       "admin@example.com https://example.org/a +31 123 456 789", "2026-10-04 12:34 PM $123.50 30 EUR @User #Tag /a/b/file.py",
       "class Widget\nimport module\nfrom package import value", "frontend depends on backend service", "model GPT Claude LLM training embedding",
       "therapy diagnosis medication objaw leczenie", "sprint milestone termin roadmap", "123.45.67.89 encryption auth CVE", "Łódź Ą hello"}) {
    auto analysis = analyzer->analyse(text); auto row = analysis.to_json(); row.erase("timestamp");
    row["unified"] = SemanticAnalyzer::to_unified(analysis);
    Json thresholds = Json::array();
    for (int threshold : {0,1,2,3,20}) thresholds.push_back(analyzer->extract_topics(text,threshold));
    row["thresholds"] = thresholds;
    analyses.push_back(row);
  }
  output["analyses"] = analyses;
  fsutil::TempDir directory("loom_");
#ifdef LOOM_PARITY_CORE_ONLY
  auto db_owner = get(Database::open(directory.path() / "graph.db"));
  auto& db = *db_owner;
  Config config(directory.path() / "config.json");
#else
  RuntimeOptions options; options.data_dir = directory.path().string(); options.start_workers = false;
  auto runtime = get(Runtime::open(options));
  auto& db = runtime->db();
  auto& config = runtime->config();
#endif
  EventBus bus; GraphEngine graph(db,bus,*analyzer);
  auto conv = get(db.create_conv("A synthetic old conversation with a long title Ą"));
  NewMessage new_message; new_message.conv_id=conv.id; new_message.role="assistant"; new_message.text=std::string(520,'a')+" synthetic full context";
  auto mid=get(db.create_msg(new_message));
  Json analysis{{"entities",Json::array({Json{{"name","Widget"},{"kind","concept"},{"relevance",0.9}},Json{{"name","weak"},{"relevance",0.1}},Json{{"name","x"},{"relevance",0.9}}})},
                {"topics",Json::array({"TECH",Json{{"label","AI"},{"confidence",0.7}},Json{{"label","weak"},{"confidence",0.1}}})},
                {"relations",Json::array({Json{{"subject","Widget"},{"predicate","depends_on"},{"object","tech"}}})}};
  output["graph_changed"]=graph.ingest_analysis(mid,conv.id,analysis);
  Json nodes=Json::array(), edges=Json::array();
  for (const std::string& label : {"Widget","weak","x","tech","ai"}) {
    auto node=get(db.find_node(label));
    nodes.push_back(Json{{"label",label},{"found",node.has_value()},{"kind",node ? node->kind : ""}});
    if (!node) continue;
    for (const auto& edge : get(db.get_links(node->id))) {
      if (edge.src==mid) edges.push_back(Json{{"src","message"},{"dst",label},{"predicate",edge.link_type},{"weight",edge.weight}});
      else if (edge.src==node->id) { auto target=get(db.get_node(edge.dst)); if (target) edges.push_back(Json{{"src",label},{"dst",target->label},{"predicate",edge.link_type},{"weight",edge.weight}}); }
    }
  }
  std::sort(edges.begin(),edges.end(),[](const Json& a,const Json& b){return json::canonical(a)<json::canonical(b);});
  output["graph_nodes"]=nodes; output["graph_edges"]=edges;
  GraphMemorySelector selector(db,config,*analyzer);
  GraphSelectOptions graph_options; graph_options.analysis=analysis;
  output["graph_context"]=selector.select_context("",graph_options);
  output["graph_seeds"]=selector.extract_seed_labels("",analysis);
  output["tokens"]=Json::array({ContextSelector::estimate_tokens(""),ContextSelector::estimate_tokens("ąabcde")});
#ifndef LOOM_PARITY_CORE_ONLY
  auto pack=get(runtime->knowledge().pack()); auto& store=runtime->knowledge().store(); auto run=get(store.begin_run(pack->hash(),Json::object())).id;
  model::Entity project; project.kind="project";project.canonical_key="fixture";project.id=model::Entity::make_id(project.kind,project.canonical_key);project.label="Synthetic fixture";
  ok(store.put_entities(run,{project}));
  model::Instance instance;instance.id=model::Instance::make_id("software",project.id);instance.paradigm="software";instance.subject=project.id;instance.subject_label=project.label;instance.coverage=Json{{"covered",0.25}};
  ok(store.put_instances(run,{instance}));
  materialize::Materializer renderer(*runtime,store,pack);
  Json products=Json::array();
  for (auto rendered : {get(renderer.self_description(run)),get(renderer.dossier(run,instance.id)),get(renderer.backlog(run)),get(renderer.extrapolated_spec(run,instance.id))}) {
    auto data=rendered.data;data.erase("input_hash");
    products.push_back(Json{{"title",rendered.title},{"kind",rendered.kind},{"markdown",replace_run(rendered.markdown,run)},{"data",data}});
  }
  output["products"]=products;
#endif
  std::cout<<json::dump(output)<<'\n';
}
