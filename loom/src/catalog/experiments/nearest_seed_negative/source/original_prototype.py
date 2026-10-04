import json,math,collections
from pathlib import Path
p=json.loads(Path('/workspace/scratch/73a479acdbc0/catalog-baseline.json').read_text())
rows=p['conversations']+p['auxiliary_documents']; n=len(rows)
terms=[{t['term']:t['tf'] for t in r['sketch']['top_terms']} for r in rows]
def grams(t):
 t='_'+t+'_'
 return [t] if len(t)<=4 else [t[i:i+4] for i in range(len(t)-3)]
gram_tfs=[]
for words in terms:
 d=collections.Counter()
 for term,tf in words.items():
  for gram in grams(term):d[gram]+=tf
 gram_tfs.append(d)
def vectors(counts):
 df=collections.Counter(k for d in counts for k in d)
 vs=[]
 for d in counts:
  v={k:(1+math.log(tf))*(math.log((1+n)/(1+df[k]))+1) for k,tf in d.items()}
  norm=math.sqrt(sum(x*x for x in v.values()))
  vs.append({k:x/norm for k,x in v.items()} if norm else {})
 return vs
def dot(a,b):return sum(x*b.get(k,0) for k,x in a.items())
seeds=[i for i,r in enumerate(rows) if r['label']=='relevant' and r['features']['id_hits']>0 and not r['features']['neg_context']]
spaces=[vectors(terms),vectors(gram_tfs)]
cs=[[max((dot(v,vectors_[j]) for j in seeds if j!=i),default=0) for i,v in enumerate(vectors_)] for vectors_ in spaces]
def pct(v,p):return sorted(v)[int(math.floor(p/100*(len(v)-1)+.5))] if v else 0
norms=[];meta=[]
for c in cs:
 policy=p['inputs']['native_thresholds']['catalog'];floor=pct([c[i] for i in range(n) if i not in seeds],policy['sem_floor_percentile']);ref=pct([c[i] for i in seeds],policy['sem_ref_percentile'])
 meta.append({'floor':floor,'ref':ref})
 norms.append([max(0,min(1.5,(x-floor)/(ref-floor))) if ref-floor>1e-9 else 0 for x in c])
measured=[]
for i,r in enumerate(rows):
 f=r['features'];before=max(1e-15,min(1-1e-15,r['score']))
 linear=math.log(before/(1-before))-1.5*f['sem_word']-2*f['sem_ngram']+1.5*norms[0][i]+2*norms[1][i]
 score=1/(1+math.exp(-linear))
 label='relevant' if score>=.7 else ('candidate' if score>=.35 else 'irrelevant')
 has_owner=any(x.get('project')=='owner' for x in r['reasons']['selection'])
 selected=label=='relevant' or (label=='candidate' and has_owner)
 measured.append({**r,'prototype_score':score,'prototype_label':label,'prototype_selected':selected,'prototype_word':norms[0][i],'prototype_gram':norms[1][i]})
labeled=[r for r in measured if r['gold']!='auxiliary']
tp=sum(r['gold']=='relevant' and r['prototype_selected'] for r in labeled);fp=sum(r['gold']!='relevant' and r['prototype_selected'] for r in labeled)
summary={'tp':tp,'fp':fp,'fn':45-tp,'tn':20-fp,'seeds':len(seeds),'normalization':meta,'rescued':[r['id'] for r in labeled if not r['selected'] and r['prototype_selected'] and r['gold']=='relevant'],'regressed':[r['id'] for r in labeled if r['selected'] and not r['prototype_selected']],'noise':[r['id'] for r in labeled if r['prototype_selected'] and r['gold']!='relevant']}
Path('/workspace/scratch/73a479acdbc0/catalog-prototype-experiment.json').write_text(json.dumps({'summary':summary,'rows':measured},indent=2))
print(json.dumps(summary,indent=2))
