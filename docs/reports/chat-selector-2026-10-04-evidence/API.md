# W3 source-private API handoff

The shared graph format is defined only in W4 `loom/src/packet/METHOD_GRAPH.md`;
this document describes W3 invocation fields and adapters, not a second protocol.
Definitions and combinations remain native graph data. Public header/C ABI/HTTP
registration requires the assigned owners.

## API do podłączenia przez właścicieli

`context_execution.method_registry` przyjmuje `enabled`, pełny `profile` z natywnymi
DTO/vocabulary/selection, `receipt_ids`, `selection`, `run_context` i
`capability_bindings.<id>.actual_parameters`, `graph_blend_operation`, `diversity`.
Parametry liścia wymagają `limit/min_score`; native operations wymieniają pozostałe
zużywane klucze. Selection wymaga danych `parameter_layers`; fusion wymaga
`operation/signal`, RRF również `rrf_constant`. Żadnego presetu zastępczego w kodzie.
`recipe.definition.parameter_bindings` wiąże rzeczywiste runtime controls przed
hashowaniem; `request_bindings` po rejestracji wiąże finalny packet i request.
Hash przepisu/instrukcji i hash finalnie wysłanego żądania mają odrębne zakresy.
Prywatny `MethodRegistry::{load,resolve,prepare,bind_results,accept}` jest API silnika.
`result_methods.{fusion,selection}` wymaga jawnych standardowych selection overlays,
każdy z jedną bezpośrednią wersją rzeczywistego executor capability
`context_fusion` / `context_selection`; fusion tego etapu musi być absent/null.
Złożenie upstream ma dowolny DAG, signed weights i jawne fuzje. Wersje etapów nie
wykonują promptów/przepisów; zadeklarowane parametry muszą odpowiadać zużytym.
Brak/empty members raportuje unavailable; nie dziedziczy przypadkiem innych metod.
Raport resolve zawiera każdy liść, parametry, capability oraz przyczynę unavailable;
ContextSet goal.params.method_registry zawiera resolution i rzeczywisty result graph.

`context_execution.graph_reply` przyjmuje `mode/profile/receipt_ids/selection`,
`run_context`, `base_packet`, `host`, `apply_policy`, `admission`, transport główny
oraz `postprocess_transport/provider_manifests`. Kompletny `recipe.request` zawiera
model/messages/stream; output_binding ma graph_pointer i opcjonalny text_pointer.
Transport wymaga calls_authorized, usage_estimate, opcjonalnego confirmation oraz
ustawianego timeout. Automatic wymaga istniejącego store_request z target i jawnym
explicitly_accepted; `selection_scope:all_native_rows` jawnie wylicza zamknięte
wiersze i CAS pod blokadą tej samej bazy. Brak tej polityki zachowuje dokładny CAS
od klienta. Metadane wiadomości/context_trace zawierają `loom.chat_graph_reply/1`.
`chat::graph_reply_fragment(result,address)` używa oryginalnego packet/compilation.
Publiczne ABI/HTTP nie są edytowane w tym zakresie.


Legacy unified context, embedding and goal-typing settings are documented in the
[first-stage archived report](https://github.com/klb-t/chatadhd/blob/1c4c8a1025be67f8794823df093a8f2508fedc25/docs/reports/chat-selector-2026-10-04.md).
