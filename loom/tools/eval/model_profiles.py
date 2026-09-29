"""T4: derive task-conditional profiles from a declared historical source extract.

No network, inference, canonical writes, automatic routing or truth promotion.
The optional source check needs the full repository, not the partial sandbox.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from decimal import Decimal
from pathlib import Path
from jsonschema import Draft202012Validator, FormatChecker

ROOT = Path(__file__).resolve().parents[3]
EXTRACT = ROOT / 'loom/tests/fixtures/model_profiles_v1/source_extract.json'
OUTPUT = ROOT / 'loom/tests/fixtures/model_profiles_v1.json'
SCHEMA = ROOT / 'docs/contracts/model_profile.schema.json'


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, allow_nan=False, indent=2)+'\n').encode('utf-8')


def digest(data):return hashlib.sha256(data).hexdigest()


def load(path):
    def pairs(items):
        out={}
        for k,v in items:
            if k in out:raise ValueError('duplicate JSON key')
            out[k]=v
        return out
    def bad(_):raise ValueError('nonfinite JSON number')
    return json.loads(Path(path).read_text(encoding='utf-8'),object_pairs_hook=pairs,parse_constant=bad)


def integer_product(n, p):
    if type(n) is not int or type(p) not in (int,float) or not math.isfinite(p) or n<0 or not 0<=p<=1:
        raise ValueError('invalid count or probability')
    x=n*p
    if abs(x-round(x))>1e-7:raise ValueError('aggregate does not identify an integer count')
    return round(x)


def evidence(source_id, location):return {'source_id':source_id,'location':location}


def metric(value, numerator, denominator, method, source, location, note, unit='fraction'):
    return dict(value=value,numerator=numerator,denominator=denominator,method=method,
                evidence=evidence(source,location),note=note,unit=unit)


def ratio(n,d,source,location,note):
    return metric(n/d if d else None,n,d,'counted_ratio',source,location,note)


def base_profile(pid,model,version,provider,operation,qid,recipe,source,location,n,available,observed_on,domain,shape):
    return {'id':pid,'representation':'derived_profile_projection',
            'key':{'model':model,'version':version,'version_status':'reported_returned_identifier' if version else 'requested_identifier_only',
                   'provider':provider,'operation':operation,'question_id':qid,'domain':domain,'context_shape':shape,
                   'recipe_id':recipe,'recipe_source':evidence(source,location)},
            'validity':{'observed_on':observed_on,'expires_on':None,'applicability':'historical_recipe_and_population_only','inherit_to_other_version':False},
            'population':{'planned':n,'available':available,'missing':n-available,'unit':'question decisions' if qid else 'planned model requests',
                          'dependent_observations':'Authored families, translations and variants; not independent trials.'},
            'metrics':{},'cost':{'reported_total_usd':None,'allocation':'shared_batch_only','batch_ref':'jev-structure-v1','note':'Twelve judgments share one request; per-question billing is unavailable.'},
            'latency':{'per_question_seconds':None,'batch_ref':'jev-structure-v1' if qid else 'native_v1_combined','note':'Per-question latency is unavailable; do not divide request latency by question count.'},
            'failure_modes':[],'calibration':{'empirically_certified':False,'ece':None,'reason':'Only aggregate evidence loaded; no raw probability bins or fresh population calibration.'},
            'promotion':{'automatic':False,'status':'policy_proposal_not_enabled','checks':['Preserve epistemic class and provenance.', 'Do not substitute a high classifier score for source validation.', 'Re-evaluate after model/provider/recipe or population change.']}}


def derive(data, source_hash):
    if data.get('schema')!='loom.model_profile_source_extract/1':raise ValueError('unknown source extract')
    jev=data['jev'];profiles=[]
    if sorted(q['id'] for q in jev['questions'])!=[f'q{i:02}' for i in range(1,13)]:raise ValueError('q01..q12 required exactly once')
    expected_fn=jev['confusion']['fn']
    if expected_fn!=0:raise ValueError('this historical derivation assumes the recorded FN=0')
    special={'q01':('Literal role binding differs from a plausible implicit conditional reading.','Errors may reflect recipe/goal mismatch, not inability to reason about implication.'),
             'q03':('One universality mismatch near threshold.','Category quantification and a proposition-level sufficient condition may be read differently.'),
             'q07':('Extra support judgments for analogy/identity descriptions.','A target T may have been supplied implicitly instead of using the evaluated target.'),
             'q11':('Extra causal judgments in conditional statements.','A plausible mechanism may have been added to what the source explicitly states.')}
    errors=retained_total=retained_errors=0
    for q in jev['questions']:
        qid=q['id'];n=q['n'];err=q['errors'];location=q['aggregate_pointer']
        if type(n) is not int or type(err) is not int or not 0<=err<=n:raise ValueError('bad aggregate count')
        keep=integer_product(n,q['retained_fraction']);ke=integer_product(keep,q['selective_error'])
        errors+=err;retained_total+=keep;retained_errors+=ke
        p=base_profile('jev-structure-v1.'+qid,jev['requested_model'],jev['returned_model'],jev['provider'],q['operation'],qid,
                       'jev-structure-v1/'+qid,'jev_questions',q['recipe_pointer'],n,n,jev['observed_on'],
                       ['authored structural families','pl','en'],'state:{text}; 12 fixed independent Noul questions; locally named roles where required')
        p['metrics']={'accuracy':ratio(n-err,n,'jev_aggregate',location+'/accuracy_available','Count reconstructed from recorded n and errors.'),
                      'positive_precision':metric(q['precision_reported'],6 if qid=='q01' else None,20 if qid=='q01' else None,'reported_scalar','jev_aggregate',location+'/precision_available','Positive denominator is not loaded for this question.' if qid!='q01' else 'q01: six true positives among twenty positive decisions, as recorded by the audit.'),
                      'positive_recall':metric(q['recall_reported'],None,None,'reported_scalar','jev_aggregate',location+'/recall_available','Do not infer the number of positive labels from a reported recall of 1.'),
                      'brier':metric(q['brier_reported'],None,None,'reported_scalar','jev_aggregate',location+'/brier_available','Reported mean squared probability error, not an ECE certification.',unit='mean_squared_error'),
                      'selective_coverage':ratio(keep,n,'jev_aggregate',location+'/selective_coverage_planned','Fixed historical diagnostic band p<=0.2 or p>=0.8.'),
                      'selective_error':ratio(ke,keep,'jev_aggregate',location+'/selective_error','Error among retained decisions; selection is not correction.')}
        p['failure_modes'].append({'kind':'reference_mismatch','status':'reported_observation','description':'Disagreements with the frozen narrow source-structure rubric; all historical errors were FP.', 'count':err,'evidence':evidence('jev_aggregate','/errors_by_question/'+qid if err else location+'/accuracy_available')})
        if qid in special:
            obs,hyp=special[qid]
            p['failure_modes'].append({'kind':'rubric_sensitivity','status':'interpretation_hypothesis','description':obs+' '+hyp,'count':None,'evidence':evidence('jev_report','Errors and a limitation of the questions themselves')})
        if qid=='q01':
            p['failure_modes'].append({'kind':'high_score_mismatch','status':'reported_observation','description':'Five development mismatches survived band 0.2/0.8, scores 0.81–0.86. A q01 quarantine was post hoc, not validated.','count':5,'evidence':evidence('jev_aggregate','/high_confidence/errors')})
            p['promotion']['checks'].append('Test expressed-source relation separately from sensible inferred abstraction (T3), without silently changing historical gold.')
        profiles.append(p)
    if errors!=26 or retained_total!=673 or retained_errors!=5 or sum(q['n'] for q in jev['questions'])!=768:
        raise ValueError('historical denominators disagree')
    native=data['native']
    for m in native['models']:
        if m['responses']+m['uncertain']+m['untouched']!=m['planned'] or m['duplicate_json']+m['contract_failures']!=m['responses']:
            raise ValueError('native disposition does not reconcile')
        p=base_profile('native_v1.'+m['model'],m['model'],None,m['provider'],'source_to_occurrence_graph',None,'native_v1','native_reconciled','Model and explicit provider table',
                       m['planned'],m['responses'],native['observed_on'],['authored development source packets','pl','en'],
                       'One-call graph JSON: local handles, occurrence scopes, UTF-8 byte source support')
        p['metrics']={'request_coverage':ratio(m['responses'],m['planned'],'native_reconciled','Model and explicit provider table','Missing responses include uncertain and never-started requests, not semantic failures.'),
                      'contract_acceptance_returned':ratio(0,m['responses'],'native_reconciled','Reconciliation and denominator','No returned graph passed the full contract.'),
                      'end_to_end_success_planned':ratio(0,m['planned'],'native_reconciled','Reconciliation and denominator','Denominator includes missing outcomes, explicitly separated below.'),
                      'semantic_accuracy_conditional_on_valid_contract':metric(None,None,0,'unavailable','native_reconciled','Reconciliation and denominator','No valid graph: conditional semantic accuracy is unavailable, not zero.')}
        p['cost']={'reported_total_usd':m['cost_usd'],'allocation':'per_model_returned_responses','batch_ref':'native_v1_combined','note':'Available returned usage only; uncertain charges remain unknown. No allocation by individual question.'}
        p['failure_modes']=[{'kind':k,'status':'reported_observation','description':desc,'count':m[k],'evidence':evidence('native_reconciled','Model and explicit provider table')}
                            for k,desc in [('duplicate_json','Duplicate-key response rejected without repair.'),('contract_failures','First graph contract violation; not a count of every semantic mistake.'),('uncertain','Started request with unknown outcome/charge, not automatically retried.'),('untouched','Never started; not an observed incorrect response.')]]
        p['promotion']['checks'].append('Separate semantic choice from deterministic identifiers/UTF-8 coordinates; test this new recipe before adoption.')
        profiles.append(p)
    if sum(Decimal(m['cost_usd']) for m in native['models'])!=Decimal(native['cost_usd']):raise ValueError('native costs disagree')
    batchjev={k:jev[k] for k in ('experiment_id','manifest_hash','observed_on','cases','decisions','cost_usd','input_tokens','output_tokens','p50_seconds','p95_seconds_nearest_rank','max_seconds','group_dependencies')}
    batchnative={k:native[k] for k in ('original_plan','started','responses','uncertain','untouched','accepted','cost_usd','uncertain_reservation_usd','pooled_first_contract_failures','original_manifest','continuation_manifest','original_archive_sha256','continuation_archive_sha256')}
    batchnative['note']='Two disjoint continuations of one 32-request plan; do not add 32+26 or assign pooled failure codes to models without per-row evidence.'
    return {'schema':'loom.model_profiles/1','source_extract_sha256':source_hash,'new_model_calls':0,'sources':data['sources'],'profiles':profiles,
            'batches':{'jev-structure-v1':batchjev,'native_v1_combined':batchnative},'mechanism_evidence':data['mechanism_evidence'],
            'owner_observations':[{'kind':'owner_task_class_assessment','source':evidence('requirements','R37'),'text':'Owner assesses current models as medium/medium-weak at generalising his philosophy. This is task-class feedback, not a measured numerical reliability.', 'model_ids':[],'benchmark_score':None,'status':'feedback_not_population_benchmark'}],
            'compensation':{'status':'proposed_unrun','operation':'induce_user_generative_principles',
              'instruction_pl':'Odtwórz kandydackie modele sposobu rozumowania z podanych źródeł. Oddziel cytowane deklaracje, interpretacje i nowe uogólnienia. Nie zastępuj modeli myślowych etykietami szkół ani narzuconymi osiami pojęciowymi. Każde uogólnienie powiąż z przykładami, zakresem i wyjątkami; zachowaj nieekwiwalentne alternatywy. Szukaj danych odróżniających hipotezy i kontrprzykładów. Brak kontrprzykładu nie jest potwierdzeniem. Nie przypisuj właścicielowi niewypowiedzianej zasady jako faktu ani nie promuj jej tylko za elegancję retrospektywnego wyjaśnienia. Gdy materiał nie rozstrzyga, wskaż brakujące rozróżnienie.',
              'ablation':{'unit':'Frozen source/task bundle; not each repeated response.',
                'arms':['A: fixed task and sources, without compensating instruction','B: same task/sources with the recorded compensating instruction','C: same task/sources with length-matched generic procedural reminder; exact text frozen before run'],
                'primary_criteria':['Source-faithful vs unsupported attributed principles, judged blind to arm.','Preservation of scope, exceptions and competing non-equivalent hypotheses.','Predictive reconstruction on untouched material if that material is actually available.'],
                'secondary_criteria':['Cost and latency of the whole workflow.','Loss of useful supported generalisations / excessive abstention.','Negative transfer on code-edit and ordinary source-extraction controls.'],
                'controls':['Freeze all sources, prompt arms including C, model/provider and scoring rubric before inference.','Counterbalance order; independent scoring and case-level paired analysis, no answer-level pseudo-replication.','A new study needs authorised budget; no model run or dataset for this ablation is claimed here.','Do not turn preference agreement into truth or model agreement into independent evidence.'],
                'current_results':None,'promotion':'requires_frozen_data_authorized_run_and_review'}}}


def validate(document, schema=None):
    schema=load(SCHEMA) if schema is None else schema
    issues=[str(e.json_path)+': '+str(e.validator) for e in Draft202012Validator(schema,format_checker=FormatChecker()).iter_errors(document)]
    if issues:return issues
    source_ids={s['id'] for s in document['sources']};ids=set()
    for p in document['profiles']:
        if p['id'] in ids:issues.append('duplicate profile id')
        ids.add(p['id'])
        if p['population']['available']+p['population']['missing']!=p['population']['planned']:issues.append('population mismatch')
        if p['key']['version_status']=='reported_returned_identifier' and not p['key']['version']:issues.append('missing returned version')
        for m in p['metrics'].values():
            v,n,d=m['value'],m['numerator'],m['denominator']
            if m['evidence']['source_id'] not in source_ids:issues.append('unknown metric evidence')
            if v is not None and (not math.isfinite(v) or (m['unit']=='fraction' and not 0<=v<=1)):issues.append('metric range')
            if m['method']=='unavailable' and v is not None:issues.append('unavailable metric has value')
            if m['method']=='counted_ratio':
                if n is None or d is None or n>d or (d==0 and v is not None) or (d>0 and (v is None or abs(v-n/d)>1e-10)):issues.append('ratio mismatch')
        if p['key']['question_id'] and p['cost']['reported_total_usd'] is not None:issues.append('per-question cost invented')
        if p['cost']['reported_total_usd'] is not None:
            try:
                amount=Decimal(p['cost']['reported_total_usd'])
                if not amount.is_finite() or amount<0:issues.append('cost range')
            except Exception:issues.append('invalid decimal cost')
        if p['key']['recipe_source']['source_id'] not in source_ids:issues.append('unknown recipe evidence')
        for f in p['failure_modes']:
            if f['evidence']['source_id'] not in source_ids:issues.append('unknown failure evidence')
    for obs in document['owner_observations']:
        if obs['source']['source_id'] not in source_ids:issues.append('unknown owner observation evidence')
    return issues


def verify_repo_sources(root, data):
    """Optional authoritative check; never fetches files or uses secret paths."""
    issues=[]
    for source in data['sources']:
        path=root/source['path']
        if not path.resolve().is_relative_to(root.resolve()) or not path.is_file():
            issues.append('missing source: '+source['id']);continue
        raw=path.read_bytes();blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
        if blob!=source['git_blob_sha']:issues.append('source version mismatch: '+source['id'])
    aggregate=root/'docs/research/inputs/jev-structure-analysis-2026-09-28.json'
    if aggregate.is_file():
        a=load(aggregate)
        for q in data['jev']['questions']:
            v=a['score']['groups']['question'][q['id']]
            for key,field in [('n','available'),('precision_reported','precision_available'),('recall_reported','recall_available'),('brier_reported','brier_available'),('retained_fraction','selective_coverage_planned'),('selective_error','selective_error')]:
                if abs(q[key]-v[field])>1e-12:issues.append('extract mismatch: '+q['id']+'/'+key)
            if q['errors']!=a['errors_by_question'].get(q['id'],0):issues.append('error count mismatch')
    questions=root/'loom/tests/fixtures/eval/jev_structure_pilot_v1/inputs.json'
    if questions.is_file():
        first=load(questions)[0]['questions']
        for q in data['jev']['questions']:
            if q['instructions']!=first[q['id']]['instructions']:issues.append('recipe text mismatch: '+q['id'])
    return issues


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,default=ROOT)
    parser.add_argument('--verify-repo-sources',action='store_true')
    parser.add_argument('--emit',type=Path,help='Create a new projection file; refuse overwrite.')
    args=parser.parse_args(argv)
    try:
        root=args.root;path=root/EXTRACT.relative_to(ROOT);data=load(path)
        result=derive(data,digest(path.read_bytes()));issues=validate(result,load(root/SCHEMA.relative_to(ROOT)))
        if not args.emit and result!=load(root/OUTPUT.relative_to(ROOT)):issues.append('generated profile differs from fixture')
        if args.verify_repo_sources:issues.extend(verify_repo_sources(root,data))
        if issues:print(json.dumps({'valid':False,'issues':issues}));return 1
        if args.emit:
            with args.emit.open('xb') as f:f.write(encoded(result))
        print(json.dumps({'valid':True,'profiles':len(result['profiles']),'model_calls':0,'full_repo_sources_checked':args.verify_repo_sources}));return 0
    except (OSError,ValueError,KeyError,TypeError):
        print(json.dumps({'valid':False,'error':'input_or_output_failure'}));return 2

if __name__=='__main__':raise SystemExit(main())
