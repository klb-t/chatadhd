#!/usr/bin/env python3
"""Rebuild safe, authored examples using the actual existing GraphPacket codec."""
from pathlib import Path
import hashlib
import json
import sys
from copy import deepcopy
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'loom/tools/structure'))
from agentic_graph_v1.packet import make_packet, validate_packet
OUT = ROOT / 'loom/data/graph_perspectives'
OUT.mkdir(exist_ok=True)
def save(name, value):
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')

def ref(ident, *, snapshot=None, canonical=None, representation=None):
    result = {'source':'demo-source', 'selector':'/objects/' + ident, 'canonicalId':canonical or ident, 'representation':representation or ident}
    if snapshot: result['snapshot'] = snapshot
    return result

structures = [
    {'id':'physical','label':'Części źródła','relations':['parts','has_formula'], 'description':'Kontener i jego adresowalne części.'},
    {'id':'logical','label':'Struktura logiczna','relations':['logical','represents'], 'description':'Rekordy, pola i alternatywne reprezentacje tej samej tożsamości.'},
    {'id':'computational','label':'Zależności obliczeniowe','relations':['has_formula','uses','produces','justified_by','informs'], 'description':'Wejścia, operacja, wynik i zapisane uzasadnienie.'},
    {'id':'history','label':'Komentarze i wersje','relations':['comments','versions','corrects','supersedes'], 'description':'Odrębne wersje i komentarze; bez zmiany rozdzielczości.'},
    {'id':'provenance','label':'Pochodzenie i nadpisania','relations':['derived_from','overridden_by','justified_by'], 'description':'Ślad pochodzenia i jawne źródło zmiany.'},
    {'id':'conversation','label':'Rozmowa i instrukcje','relations':['parts','corrects','supersedes','derived_from'], 'description':'Wiadomość źródłowa, korekta i aktualna instrukcja.'},
]
values = {
 'structure':'computational', 'resolution':'summary',
 'traversal':{'relations':structures[2]['relations'],'direction':'both','hops':4},
 'temporal':{'compareSnapshots':[]}, 'evidence':['source','computed','inferred','unknown'],
 'visual':[
   {'id':'peripheral','dimension':'relevance','match':{'distance':3},'style':{'opacity':0.6},'explanation':'Relacja poboczna: odległość grafowa co najmniej 3.'},
   {'id':'past-version','dimension':'time','match':{'snapshot':'v1'},'style':{'opacity':0.45},'explanation':'Wersja historyczna v1 według wybranego profilu czasu.'},
   {'id':'unmeasured','dimension':'uncertainty','match':{'evidence':'unknown'},'style':{'blur':0.35},'explanation':'Status rozpoznania nieznany; pewność nie została zmierzona.'},
   {'id':'active','dimension':'relevance','match':{'focus':True},'style':{'color':'#89e2cc'},'explanation':'Aktualny punkt skupienia.'},
 ],
 'budget':{'query':600,'render':80,'page':64},
 'localResolution':[
   {'id':'active-detail','match':{'focus':True},'resolution':'full'},
   {'id':'inputs','match':{'kind':'input'},'resolution':'summary'},
   {'id':'table-context','match':{'kind':'record'},'resolution':'aggregate','aggregate':{'id':'table-context','label':'Pozostałe rekordy'}},
   {'id':'other-containers','match':{'kind':'container'},'resolution':'label'},
 ],
 'goal':{'id':'inspect','description':'Przejrzyj źródło i zapisane zależności bez uruchamiania analizy.'},
}
labels = {'structure':'Struktura','resolution':'Rozdzielczość','traversal':'Relacje i zasięg','temporal':'Czas i wersje','evidence':'Status epistemiczny','visual':'Mapowania prezentacji','budget':'Budżet widoku','localResolution':'Lokalne wyjątki rozdzielczości','goal':'Cel'}
capabilities = [{'id':'graph.perspective.'+key,'label':labels[key],'target':key,'status':'supported'} for key in values]
capabilities.append({'id':'graph.perspective.futureTexture','label':'Przyszły renderer tekstury','target':'futureTexture','status':'unsupported','reason':'Bieżący renderer nie implementuje tej możliwości. Dane pozostają zachowane.'})
pack = {'schema':'loom.default_layers_pack/1','pack_id':'loom.graph-perspectives.demo','revision':1,
 'policy':{'excluded_area_new_defaults':'proposal'},
 'entries':[{'id':'gp.'+key,'key':'graph.perspective.'+key,'area':'graph.perspectives','revision':1,'label':labels[key], 'value':value} for key,value in values.items()]}
save('graph-perspectives.pack.json',pack)

presets=[]
for st in structures:
    focus = ref('e_cell',snapshot='v2')
    if st['id']=='conversation': focus=ref('e_conversation')
    if st['id']=='provenance': focus=ref('e_effective')
    presets.append({'id':st['id'],'label':st['label'],'description':st['description'],'focus':focus,
      'actions':[{'op':'override','key':'graph.perspective.structure','value':st['id']},
                 {'op':'override','key':'graph.perspective.traversal','value':{'relations':st['relations'],'direction':'both','hops':4}}]})
ui_words = {
 'title':'Perspektywy grafu','subtitle':'Jedno źródło, przecinające się struktury i jawny plan przejścia.',
 'preset':'Perspektywa','interfaceLevel':'Poziom interfejsu','basic':'Basic','advanced':'Advanced','expert':'Expert',
 'focus':'Punkt skupienia','focusAction':'Przejdź do obiektu','back':'Wstecz','forward':'Dalej','navigation':'Nawigacja',
 'snapshot':'Snapshot','compareSnapshot':'Porównaj z wersją','none':'Brak','visible':'Widoczne','analysis':'Wybrane do analizy','permitted':'Dostępne według uprawnień',
 'why':'Dlaczego to widzę?','whyHidden':'Dlaczego tego nie widzę?','configuration':'Składniki perspektywy','import':'Importuj','export':'Eksportuj',
 'applyJson':'Zastosuj JSON','save':'Zapisz','restore':'Odtwórz','saved':'Zapisano perspektywę','restored':'Odtworzono perspektywę',
 'loading':'Ładowanie','error':'Błąd','unsupported':'Nieobsługiwana możliwość','unavailable':'Źródło niedostępne','budget':'Budżet',
 'selectionTime':'Czas selekcji','renderTime':'Czas aktualizacji widoku','canonicalIdentity':'Tożsamość obiektu','representation':'Reprezentacja','related':'Powiązany obiekt',
 'details':'Szczegóły','plan':'Plan zapytania','raw':'Dane źródłowe','localOnly':'Zmiana perspektywy nie uruchamia analizy ani nie rozszerza uprawnień.',
 'sourceStatus':'Status źródła','relation':'Rodzaje relacji','direction':'Kierunek','depth':'Zasięg','capabilities':'Możliwości adaptera',
 'analysisExport':'Eksportuj plan analizy','analysisNotice':'Plan wymaga jawnego uruchomienia przez istniejący workflow analizy.','profileDiff':'Różnice perspektyw',
 'disable':'Wyłącz','exclude':'Wyklucz trwale','reenable':'Włącz ponownie','override':'Nadpisz','clearOverride':'Usuń nadpisanie',
 'effective':'Efektywne','disabled':'Wyłączone','excluded':'Wykluczone','proposal':'Propozycja','missing':'Brak wartości','source':'Źródłowe','computed':'Obliczone','inferred':'Wywnioskowane','unknown':'Nieznane',
 'available':'Dostępne','unloaded':'Niezaładowane','denied':'Brak uprawnień','incoming':'Przychodzące','outgoing':'Wychodzące','both':'Oba kierunki',
 'component':'Składnik','applyComponent':'Zastosuj składnik','scenario':'Scenariusz','graph':'Graf','inspectRelation':'Sprawdź relację','partial':'Częściowy wynik','complete':'Pełny wynik','permissionsNotice':'Uprawnienia pochodzą z hosta; perspektywa ich nie zmienia.','selected':'Wybrane','restoreError':'Nie można odtworzyć zapisu','download':'Pobierz','importFile':'Importuj plik',
 'label':'Etykieta','summary':'Podsumowanie','full':'Pełna treść','aggregate':'Agregat','native':'Natywny resolver R40',
}
save('catalog.json',{'schema':'loom.graph_perspectives_catalog/1','capabilities':capabilities,'structures':structures,'presets':presets,
 'ui':ui_words,'controls':{'level':'basic','storageKey':'loom.graph-perspectives.demo.v1','defaultPreset':'computational'},
 'snapshots':[{'id':'v1','label':'v1: wartość 5'},{'id':'v2','label':'v2: wartość 7'}],
 'targets':[{'id':i,'label':l,'ref':ref(i,**extra)} for i,l,extra in [('e_cell','Komórka/pole',{'snapshot':'v2'}),('e_formula','Operacja',{}),('e_result','Wynik',{}),('e_effective','Efektywne ustawienie',{}),('e_conversation','Rozmowa',{}),('e_unloaded','Nieotwarte źródło',{}),('e_unavailable','Niedostępne źródło',{}),('e_unknown','Nieznana struktura',{})]],
 'sourceDescriptor':{'id':'demo-source','label':'Bezpieczny pakiet demonstracyjny','structures':structures,'capabilities':capabilities,'status':'available'},
 'navigation':[{'id':s['id'],'label':s['label'],'structure':s['id'],'relations':s['relations']} for s in structures],
 'evidenceNotes':{'confidence':'Brak skalibrowanej oceny. Wymagane numeryczne pole native confidence=0 jest wyłącznie wartością transportową; publiczna pewność pozostaje null.'}})

specs = [
 ('e_file','container','Pakiet źródłowy',{}),('e_table','table','Zbiór rekordów',{}),('e_record','record','Rekord logiczny',{}),
 ('e_cell','field','Wartość: 7',{'ref':ref('e_cell',snapshot='v2'),'value':7}),
 ('e_cell_logic','field','Pole rekordu: 7',{'ref':ref('e_cell_logic',snapshot='v2',canonical='e_cell'),'value':7}),
 ('e_formula','operation','Suma: 3 + 4',{'evidence':'computed','expression':'sum(inputs)'}),
 ('e_input_a','input','Wejście A: 3',{'value':3}),('e_input_b','input','Wejście B: 4',{'value':4}),
 ('e_result','result','Wynik: 7',{'evidence':'computed','value':7}),
 ('e_justification','rationale','Zapisane uzasadnienie: suma wejść',{'text':'W demonstracji wybrano sumę dwóch jawnych wartości wejściowych.'}),
 ('e_decision','decision','Zapisana decyzja demonstracyjna',{'text':'Przyjęto wynik 7 w tym autorskim przykładzie; relacja jest zapisana w źródle.'}),
 ('e_comment','comment','Komentarz: sprawdź źródło wejść',{}),
 ('e_version_v1','field','Wersja v1: 5',{'ref':ref('e_version_v1',snapshot='v1',canonical='e_cell'),'value':5}),
 ('e_profile','profile','Zewnętrzny profil',{}),('e_profile_field','setting','Pole budżetu prezentacji: 80',{'value':80}),
 ('e_effective','setting','Efektywny budżet prezentacji: 24',{'value':24,'evidence':'computed'}),
 ('e_override','override','Jawne nadpisanie użytkownika: 24',{'value':24,'source_refs':['demo-source#/profile/overrides/budget']}),
 ('e_conversation','conversation','Rozmowa demonstracyjna',{}),('e_message','message','Wiadomość źródłowa: użyj skrótów',{'text':'Używaj skróconych objaśnień.'}),
 ('e_correction','message','Korekta: objaśniaj symbole',{'text':'Korekta: objaśniaj symbole, zachowując zwięzłość.'}),
 ('e_instruction','instruction','Aktualna instrukcja: objaśniaj symbole zwięźle',{'text':'Objaśniaj symbole zwięźle.'}),
 ('e_unloaded','resource','Dołączone źródło, jeszcze nieotwarte',{'status':'unloaded','reason':'Referencja zachowana; projektowanie wnętrza wymaga resolvera źródła.'}),
 ('e_unavailable','resource','Źródło czasowo niedostępne',{'status':'unavailable','reason':'Demonstracyjny adapter zgłasza niedostępność transportu.'}),
 ('e_unknown','extension','Nieznana struktura: retained-extension',{'status':'unknown','evidence':'unknown','recognition':{'status':'unknown','known_paths':[],'unknown_paths':['/objects/e_unknown/payload'],'alternatives':[{'interpretation':'candidate-A','status':'unverified'},{'interpretation':'candidate-B','status':'unverified'}]},'payload':{'retained-extension':{'sequence':[2,3,5]}}}),
 ('e_other','container','Inny kontener jako węzeł zbiorczy',{}),
 ('e_record_other','record','Pozostały rekord A',{'value':12}),('e_record_other2','record','Pozostały rekord B',{'value':16}),
]
objects={}; entities=[]
for ident, kind, label, extra in specs:
    attrs={'ref':ref(ident),'status':'available','evidence':'source','confidence':None,'confidence_status':'uncalibrated',**extra}
    objects[ident]={'kind':kind,'label':label,**deepcopy(attrs)}
    entities.append({'id':ident,'kind':kind,'canonical_key':attrs['ref']['canonicalId'],'label':label,'labels':{'pl':label},'aliases':[],
      'parent':'','first_seen':'','last_seen':'','evidence_class':'user','origin':'user','confidence':0,'status':'active',
      'attrs':{'perspective':attrs,'fixture':'safe-authored-example'}})
relationships=[
 ('e_file','parts','e_table',['physical']),('e_table','parts','e_cell',['physical']),('e_table','parts','e_other',['physical']),
 ('e_table','logical','e_record',['logical']),('e_record','logical','e_cell_logic',['logical']),('e_cell','represents','e_cell_logic',['logical']),
 ('e_cell','has_formula','e_formula',['physical','computational']),('e_formula','uses','e_input_a',['computational']),('e_formula','uses','e_input_b',['computational']),
 ('e_formula','produces','e_result',['computational']),('e_result','justified_by','e_justification',['computational','provenance']),('e_justification','informs','e_decision',['computational']),
 ('e_cell','comments','e_comment',['history']),('e_cell','versions','e_version_v1',['history']),('e_file','parts','e_unloaded',['physical']),('e_file','parts','e_unavailable',['physical']),('e_file','parts','e_unknown',['physical']),
 ('e_profile','parts','e_profile_field',['physical']),('e_effective','derived_from','e_profile_field',['provenance']),('e_effective','overridden_by','e_override',['provenance']),
 ('e_conversation','parts','e_message',['conversation']),('e_correction','corrects','e_message',['conversation','history']),('e_instruction','supersedes','e_message',['conversation','history']),('e_instruction','derived_from','e_correction',['conversation','provenance']),
 ('e_table','parts','e_record_other',['physical']),('e_table','parts','e_record_other2',['physical']),
]
source_data={'schema':'loom.graph-perspectives.authored-example/1','notice':'Fictional safe data; no private source exports. Domain labels are replaceable by another adapter.', 'objects':objects,
 'relationships':[{'from':a,'kind':p,'to':b,'structures':s} for a,p,b,s in relationships]}
source_text=json.dumps(source_data,ensure_ascii=False,sort_keys=True,separators=(',',':'))
source_hash=hashlib.sha256(source_text.encode()).hexdigest()
locator={'source':'demo-source','member':'demo.source.json','json_pointer':'','byte_start':0,'byte_len':len(source_text.encode()),'time_start':None,'time_end':None,'line':None}
observation={'id':'ob_demo_source','unit':'unit_demo_source','kind':'code_block','text':source_text,'locator':locator,'lang':'pl','date':'2026-10-09','ordinal':0,'artifact_type':'authored_safe_json','speaker':'fixture-author','attrs':{'raw_sha256':source_hash}}
claims=[]
for i,(a,p,b,st) in enumerate(relationships):
    claim={'id':f'cl_demo_{i}','subject':a,'predicate':p,'object':b,'value':None,
      'qualifiers':{'valid_from':'','valid_to':'','version':'','branch':'','scope':'safe-authored-example','lang':'pl','extra':{'perspective':{'structures':st,'evidence':'source','confidence':None,'evidence_status':'explicit_source_relation'}}},
      'assessment':{'basis':{'support':[{'observation':'ob_demo_source','locator':deepcopy(locator),'quote':json.dumps(source_data['relationships'][i],ensure_ascii=False,sort_keys=True,separators=(',',':')),'extractor':'safe-fixture-projection/1','quality':0}], 'derivation':None},
       'evidence_class':'observed','origin':'user','confidence':0,'premises':{'claims':[],'principles':[],'assumptions':[]},'counter':{'observations':[],'claims':[]},'status':'active',
       'consequences':{'claims':[],'predictions':[],'checks':[]},'open':{'slots':[],'questions':[],'fill_query':None},'expected_property':None,'check_state':'n/a','alternatives':[]}}
    claims.append(claim)
packet=make_packet(entities=entities,claims=claims,sources=[{'observation':observation,'known_at':'2026-10-09T07:00:00Z','text_sha256':source_hash}],
 task={'operation':'graph.perspectives.demonstrate','source_descriptor':{'id':'demo-source','label':'Bezpieczne dane autorskie','structures':structures},'confidence_policy':'uncalibrated; numeric zero only satisfies native DTO; attrs confidence null is authoritative for presentation'},
 origin={'kind':'recorded','actor':'safe-fixture-author','model':None,'recipe_sha256':None,'response_sha256':None}, known_at='2026-10-09T07:00:00Z')
validate_packet(packet)
save('demo.packet.json',packet)
(OUT/'demo.source.json').write_text(source_text)
# Existing application-profile format accepted by the real profile loader.
profile=json.loads((ROOT/'loom/web/src/profiles/data/loom-default.json').read_text())
profile['id']='graph-perspectives-safe-external-profile';profile['label']='Profil demonstracyjny ze źródła zewnętrznego'
save('external-profile.json',profile)
print(json.dumps({'entities':len(entities),'claims':len(claims),'packet_id':packet['packet_id'],'confidence':'uncalibrated/null display','output':str(OUT)}))
