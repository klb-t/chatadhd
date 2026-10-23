// The graph projection (M1 §2.2-2.6): everything the application already has - the ROM, the settings and their groups, the levels of
// the scope chain, the model roles and the models they resolve to, You and your profile, the memory, the conversations and their
// messages, the units, the flows and their steps, the perspectives, edge kinds, views and input bindings, devices, environments,
// tool sources and connectors - read on demand as nodes of ONE navigable graph. Nothing is written to the nodes table.
//
// Which sources exist, their address prefix, class, reader, parent, edges and default evidence is data (runtime/graph_sources.pack,
// user overlay <data_dir>/profiles/graph_sources.pack); this file owns only the readers (mechanism id -> function).
//
// One render = one Catalogue: every source read whole is read at most once (the paged ones - conversations, messages, the steps of a
// flow - are read per parent and page, a page never longer than the level's budget), and the scope-chain Resolver (and the ports
// profile) is opened at most once, however many settings or roles the render shows (resolver_opens() counts it; the tests check it).
//
// A node: {address, label, kind (class), source, parent, order?, value?, value_human?, count? | count_unknown_reason?, status?,
//          evidence {class, confidence, provenance {layer, source_ref, method, time}}, evidence_overrides? {property: evidence},
//          provenance {layer, layers[]}, has_children, same_as? [ids], attrs {}}.
// evidence.class is one of declared | observed | derived | inferred | extrapolated: declared for the ROM and presets, observed for what
// you said or did, inferred for what an analysis or a model concluded. A property whose provenance.method is model_inferred is never
// observed (the rule the tests check).
// Identity: conv:<id> is c_<id>, msg:<id> is m_<id>, a nodes row of kind message that points at m_<id> is msg:<id> (same_as);
// children and counts are by unique address.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Config;
class Database;
class MemoryEngine;
class ModelRegistry;
class Runtime;
class Secrets;
}  // namespace loom

namespace loom::projection {

struct Env {
  std::filesystem::path root;               // data dir (profiles/, views/, config.json)
  const Config* cfg = nullptr;              // the person level of values; null = presets only
  Database* db = nullptr;                   // conversations, messages, nodes; null = those sources say why they are unknown
  const MemoryEngine* memory = nullptr;     // null = the memory source says memory_unavailable
  const Secrets* secrets = nullptr;         // only whether a secret is set is ever read
  const ModelRegistry* models = nullptr;    // prices; null = cost unknown (no registry)
  std::string lang = "en";
  std::string conv_id;                      // the conversation the values are resolved for ("" = none)
  Json profile = nullptr;                   // an onboarding snapshot to show (null = the stored one, see user_id)
  std::string user_id;                      // which stored onboarding profile ("" = the only one)
};

// Env from a live runtime (config, database, memory, secrets, registry) and a request ({lang?, conv_id?, user_id?}).
Env env_of(Runtime& rt, const Json& request);

class Catalogue {
 public:
  static Result<Catalogue> open(Env env);

  const Env& env() const;
  // Effective source rows (hidden rows are not in it; they are in hidden_sources()).
  const Json& sources() const;
  const Json& hidden_sources() const;
  // The source row whose prefix the address starts with (longest prefix wins), or nullptr.
  const Json* source_of(std::string_view address) const;
  // c_<id> -> conv:<id>, m_<id> -> msg:<id>, a nodes row of kind message -> msg:<id>; anything else unchanged.
  std::string canonical(std::string_view address);
  // The reverse of the db forms: msg:<id> -> m_<id>, conv:<id> -> c_<id>; anything else unchanged.
  std::string db_id(std::string_view address) const;
  // The node at an address, or nullopt when no source knows it. A derived node of the perspective sent_to_model (sent:<request id>:<ref>,
  // M1-K1c3a) is read from the record stored with that request (receipt_of), not from a source: a view of the record, never stored.
  Result<std::optional<Json>> node(const std::string& address);
  // The projection of the record of one request (render_receipt over this catalogue with the kinds of the perspective sent_to_model),
  // computed once per request and render. `msg_id`: the request or its answer (database id, m_...). The render has "nodes" (id, address) and
  // "edges" (src/dst by id); receipt_edges() turns them into catalogue edges.
  Result<const Json*> receipt_of(const std::string& msg_id);

  struct Children {
    std::vector<Json> nodes;               // unique addresses, in source order
    std::vector<Json> edges;               // {src, dst, kind} touching these nodes
    std::int64_t total = 0;                // every child the sources know (paged sources: their full total)
    Json unknown = Json::array();          // [{source, reason}] sources that could not say
    Json hidden = Json::array();           // [{source, id, by?}] entries hidden by an overlay
    Json pages = Json::object();           // {source: {limit, offset, next_offset?, total}} of paged sources
    std::size_t sources_read = 0;          // child sources that answered (a total of 0 from them is a known 0)
    std::map<std::string, std::int64_t> totals;  // source id -> its children here (paged sources: their full total)
  };
  // The direct children of an address ("" = the root). `paging`: {offset?, limit?} for paged sources.
  Result<Children> children(const std::string& parent, const Json& paging = Json::object());
  // Every node of one source that is read whole (empty for a paged source or an unknown id).
  Result<std::vector<Json>> children_of_source(const std::string& source_id);
  // Every edge of the sources that are read whole (paged sources contribute the nodes read so far). A source that cannot be read adds
  // {source, reason} to `unknown` (when given).
  Result<std::vector<Json>> edges(Json* unknown = nullptr);
  // The same for the sources a card of `address` needs: its own source and those whose declared edges (graph_sources edges {to|from})
  // name its source or its prefix; never every source.
  Result<std::vector<Json>> edges_touching(const std::string& address, Json* unknown = nullptr);

  // The model roles resolved once for this render: {roles: [{id, label, why, status, policy, model, via, tier, chain, flow, mode?, cost,
  // flows_reading, settings}]} (the policy chain through this render's Resolver and ports profile).
  Result<Json> roles();
  // The meaning of an edge kind in the language of the render (graph_views edge_kinds), or null when no kind describes it.
  Json edge_meaning(const std::string& kind);
  // The name of an edge kind in the language of the render (graph_views edge_kinds label), else the kind's id.
  Json edge_label(const std::string& kind);
  // The id of the inverse edge kind (graph_views edge_kinds inverse), else "" when the kind names none or no kind describes it.
  std::string edge_inverse(const std::string& kind);
  // A setting's value for this render: through the scope chain (the conversation in view, its units, rules), else the person level;
  // null when no level gives one. Read once per key and render.
  Json setting(const std::string& key);
  // Why something is unknown, in the words of the screen (reason.<code>); the code itself when no word says it.
  std::string reason_words(const std::string& reason);
  // The name of a source in the language of the render (its label), else its id.
  std::string source_label(const std::string& id);
  // How many times this render opened the scope-chain Resolver (0 or 1).
  std::size_t resolver_opens() const;
  // True when the part of You at `parent` (subgraph:self/<id>) takes its members from the source (profile.subgraphs members).
  bool subgraph_reads(const std::string& parent, const std::string& source_id);

  struct Impl;

 private:
  Result<std::vector<Json>> edges_of(const std::set<std::string>* only, Json* unknown);
  std::shared_ptr<Impl> impl_;
};

// The slots of the profile interview of Env::profile or of the stored onboarding profile (Env::user_id, or the only one): [{id, section,
// category, state: known | unknown | declined | never, value, provenance, interviewed}]. No interview yet: every slot of the shipped
// scenario, unknown. Error (the message is the reason): database_unavailable, several_profiles_name_one, onboarding_unavailable.
Result<std::vector<Json>> profile_slots(const Env& env);

// The address a sentence names: an address or database id that a source knows (canonical form), else an alias of the data
// (graph_sources aliases: rozmowy -> the conversations), else the area or node whose name or id is the words (areas first, then the
// sources read whole), compared word by word with stems (graph_sources match: ustawień = ustawienia); unless exact_only, then a name
// that has every word of the sentence as a whole word. "" = nothing has that name (never a part of another word: rzeczy is not przeczyta).
Result<std::string> find_address(const Env& env, const std::string& words, bool exact_only = false);

// graph.navigate {focus?, perspectives?[], hops=1, limit=navigator_children_limit, query?, offset?, lang, conv_id?}
//  -> {schema, focus{address,label,kind,evidence,provenance{layer,layers[]}}, children[...], edges[{src,dst,kind,meaning}], breadcrumbs[],
//      counts{total,shown,collapsed}, hidden_by_you{count,items[]}, unknown[], pages, limit{value,from}, perspectives?[]}
Result<Json> navigate(const Env& env, const Json& request);
// graph.card {address, lang, conv_id?} -> the node with every property, why/effect/edit_rule, inheritance[] (the whole chain, with the
// unit levels that have no unit), relations grouped by edge kind, actions (actions.for on the node's element), evidence per property.
Result<Json> card(const Env& env, const Json& request);
// settings.resolved {conv_id?, lang} -> {schema, groups[{id,label,why,order,fields[...]}], hidden_keys[], profile_values[], counts}
Result<Json> settings_resolved(const Env& env, const Json& request);
// roles.resolved {conv_id?, lang} -> {schema, roles[{id,label,why,status,chain[],model,tier{id,reason},flow{...},mode?,cost{...},flows_reading[]}]}
Result<Json> roles_resolved(const Env& env, const Json& request);
// rom.override {id, patch} -> writes <data_dir>/profiles/graph_entry.pack overrides.rom.items.<id> (rom.h MountRegistry, mount "user").
Result<Json> rom_override(const std::filesystem::path& root, const Json& request);
// A perspective rendered over the projection (Perspectives::render, source "catalogue"): the edges whose kind is in `kinds`, from the
// request's seeds (addresses) through `hops` in `direction`; without seeds, every such edge.
Result<Json> render_catalogue(const Env& env, const std::set<std::string>& kinds, const Json& request);
// The perspective sent_to_model (Perspectives::render, source "receipt", M1-K1c2): the record of ONE request (loom.context_receipt/1, kept with
// the message the person sent) drawn as a graph. request: {msg_id | seeds[0] | focus (the request or its answer), lang?, label_chars?, limit?}.
// Nodes: the message, one node per item of the record (a ref that is a message or node of the database is THAT node, address and all; an
// aggregate - the instruction, the memory tree ... - is a derived node sent:<request id>:<ref>, derived=true), links sent_to_model message -> item
// (weight = tokens_est; metadata {class, why, source_kind, ref, label, tokens_est}) and, for what was left out, links left_out_of_model message ->
// element (metadata {group, reason, reason_human, group_count}). A projection: computed on demand from the stored record, nothing is written.
// No record (before M1, context_receipt_store=never): an empty graph with missing_reason and the words of the action context.receipt.
Result<Json> render_receipt(const Env& env, const std::set<std::string>& kinds, const Json& request);
// The same over a catalogue already open (graph.navigate, graph.card): the words, the labels of the edge kinds and the addresses are read once.
Result<Json> render_receipt(Catalogue& cat, const std::set<std::string>& kinds, const Json& request);
// The links of a render of the record as catalogue edges: {src, dst, kind, id, weight, metadata, derived} with src and dst as ADDRESSES (the
// render names nodes by id), only the links whose kind is in `kinds` (empty = every link). graph.navigate walks them like any other edge of the perspective.
std::vector<Json> receipt_edges(const Json& render, const std::set<std::string>& kinds);
// The catalogue row of a derived node of that perspective (address sent:<request id>:<ref>), built from the stored record: label, kind,
// class/section, why, tokens, derived:true and the words that it is a view of the record and not a stored node; nullopt when the request
// has no record or the record has no such node. The row is not yet "finished" (evidence words): Catalogue::node does that.
Result<std::optional<Json>> receipt_node(Catalogue& cat, const std::string& address);
// A word of that perspective (runtime/chat_ops.pack texts, pl + en) in the language of the env; the id itself when the data has none.
std::string receipt_words(const Env& env, const std::string& id);
// What graph.navigate says about the record next to the nodes and links: the render without them {request_msg_id, request_address, receipt{present,
// schema, tokens_est, truncated, ...}, counts, sections, dropped, note} and, with no record, {missing_reason, also_when, text}.
Json receipt_summary(const Json& render);

// The actions of the catalogue this module carries out ("graph.navigate", "graph.card", "settings.resolved", "roles.resolved", "rom.override").
bool is_projection_action(std::string_view action);
Result<Json> invoke(Runtime& rt, std::string_view action, const Json& request);

}  // namespace loom::projection
