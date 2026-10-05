#!/usr/bin/env python3
"""Time existing exact archive harness without modifying its source or native calls."""
import argparse, ctypes, importlib.util, json, pathlib, time
parser=argparse.ArgumentParser()
parser.add_argument("harness",type=pathlib.Path)
parser.add_argument("library",type=pathlib.Path)
parser.add_argument("output",type=pathlib.Path)
parser.add_argument("--compare",type=pathlib.Path,required=True)
parser.add_argument("--fixtures",type=pathlib.Path,required=True)
parser.add_argument("--materialized-dir",type=pathlib.Path,required=True)
args=parser.parse_args()
spec=importlib.util.spec_from_file_location("archive_exact_harness",args.harness)
harness=importlib.util.module_from_spec(spec)
spec.loader.exec_module(harness)
original_loader=ctypes.CDLL
measurements={}
class TimedFunction:
    def __init__(self, name, function):
        object.__setattr__(self,"name",name)
        object.__setattr__(self,"function",function)
    def __getattr__(self,name):
        return getattr(self.function,name)
    def __setattr__(self,name,value):
        setattr(self.function,name,value)
    def __call__(self,*args,**kwargs):
        start=time.perf_counter_ns()
        try:
            return self.function(*args,**kwargs)
        finally:
            measurements.setdefault(self.name,[]).append(time.perf_counter_ns()-start)
class TimedLibrary:
    def __init__(self,library):
        self.library=library
        self.functions={}
    def __getattr__(self,name):
        if name not in ("loom_init_ex","loom_archive_run","loom_shutdown"):
            return getattr(self.library,name)
        if name not in self.functions:
            self.functions[name]=TimedFunction(name,getattr(self.library,name))
        return self.functions[name]
def timed_loader(*arguments,**keywords):
    start=time.perf_counter_ns()
    library=original_loader(*arguments,**keywords)
    measurements["library_load"]=[time.perf_counter_ns()-start]
    return TimedLibrary(library)
ctypes.CDLL=timed_loader
start=time.perf_counter_ns()
try:
    receipt=harness.run(args.library.resolve(),args.output,args.fixtures.resolve(),args.materialized_dir.resolve())
finally:
    ctypes.CDLL=original_loader
harness_elapsed=time.perf_counter_ns()-start
harness.compare(args.compare,args.output,receipt)
timing={"schema":"archive.whole_timing/1","library":str(args.library.resolve()),"library_sha256":receipt["library_sha256"],"harness_sha256":harness.digest(args.harness),"timing_wrapper_sha256":harness.digest(pathlib.Path(__file__)),"elapsed_ns":measurements,"native_runtime_ns":sum(sum(measurements.get(name,[])) for name in ("loom_init_ex","loom_archive_run","loom_shutdown")),"harness_elapsed_ns":harness_elapsed,"exact_parity":True,"comparison":str(args.compare.resolve()),"artifact_count":len(receipt["artifacts"]),"artifact_bytes":sum(row["bytes"] for row in receipt["artifacts"]),"summary":receipt["summary"],"method":"perf_counter_ns around unmodified native calls; harness elapsed separately includes ELF SHA256 and artifact/DB proof capture; workers disabled; offline public fixture; no Runtime profile overlay"}
(args.output/"timing-receipt.json").write_text(json.dumps(timing,indent=2)+"\n")
print(json.dumps(timing,sort_keys=True))
