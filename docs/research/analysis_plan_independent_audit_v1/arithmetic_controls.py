from pathlib import Path
from fractions import Fraction
from decimal import Decimal, localcontext
import hashlib, json, random
from loom.tools.contracts import analysis_plan_ref as ref

HERE=Path(__file__).resolve().parent
rng=random.Random(20260930)
samples=[[Decimal(str(rng.randrange(10**12))).scaleb(rng.randrange(-80,81))
    for _ in range(rng.randrange(1,7))] for _ in range(128)]
rows=[]; summary=[]
for precision in (2,7,28,81):
    baseline_errors=0; repaired_errors=0
    for index,values in enumerate(samples):
        oracle=sum((Fraction(v) for v in values),Fraction(0))
        with localcontext() as ctx:
            ctx.prec=precision
            baseline=sum(values,Decimal(0)); repaired=ref.exact_sum(*values)
        baseline_wrong=Fraction(baseline)!=oracle; repaired_wrong=Fraction(repaired)!=oracle
        baseline_errors+=baseline_wrong; repaired_errors+=repaired_wrong
        rows.append({'precision':precision,'index':index,'operands':[str(v) for v in values],
            'oracle_numerator':str(oracle.numerator),'oracle_denominator':str(oracle.denominator),
            'baseline':str(baseline),'repaired':str(repaired),
            'baseline_wrong':baseline_wrong,'repaired_wrong':repaired_wrong})
    summary.append({'ambient_precision':precision,'cases':len(samples),'baseline_errors':baseline_errors,'repaired_errors':repaired_errors})
receipt={'schema':'loom.analysis_plan_resource_arithmetic_audit/1',
    'source_sha256':hashlib.sha256(Path(ref.__file__).read_bytes()).hexdigest(),'seed':20260930,
    'summary':summary,'rows':rows,'no_api_calls':True,'decision':'keep exact arithmetic' if all(s['repaired_errors']==0 for s in summary) else 'investigate'}
with (HERE/'ARITHMETIC_FIRST_RESULTS.json').open('x') as f:json.dump(receipt,f,indent=2,sort_keys=True);f.write('\n')
print(summary)
assert all(s['repaired_errors']==0 for s in summary)
