"""Eight independent synthetic regressions for the text audit."""
import contextlib, io, json, sqlite3, sys, tempfile, unittest
from pathlib import Path
# Locate the repository from this evidence directory; no external PYTHONPATH.
repo = next(parent for parent in Path(__file__).resolve().parents
            if (parent / 'loom/tools/eval/archive_cost.py').is_file())
sys.path.insert(0, str(repo / 'loom/tools/eval'))
import archive_cost as a
class Extra(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.db=Path(self.tmp.name)/'x.db'
  with sqlite3.connect(self.db) as c:
   c.execute('CREATE TABLE messages(conv_id TEXT,role TEXT,text TEXT,status TEXT)')
   c.executemany('INSERT INTO messages VALUES (?,?,?,?)', [('a','user','ą\x00🙂','active'),('a','tool','tools','excluded'),('a','assistant','saved','version'),('b','user','gone','deleted'),('c','function','call','active'),('d',None,'future','new'),('e',None,None,None)])
 def tearDown(self): self.tmp.cleanup()
 def test_complete(self):
  s=a.archive_stats(self.db);self.assertEqual(s['raw'],{'conversations':5,'messages':7,'chars':27});self.assertEqual(s['tool_messages'],2);self.assertEqual(s['chars_tools'],9);self.assertEqual(s['chars_active'],7)
 def test_nul_unicode(self): self.assertEqual(a.archive_stats(self.db)['chars_active'],len('ą\x00🙂')+len('call'))
 def test_exact_projection_conversations(self):
  s=a.archive_stats(self.db);p=a.project_stats(s,include_versions=False,include_unknown_status=False,include_tools=False);self.assertEqual((p['conversations'],p['messages'],p['chars']),(1,1,3))
  p=a.project_stats(s,include_active=False,include_versions=False,include_unknown_status=False,include_deleted=True);self.assertEqual((p['conversations'],p['messages'],p['chars']),(1,1,4))
 def test_no_rates(self):
  with contextlib.redirect_stdout(io.StringIO()) as out: a.main(['--db',str(self.db),'--json'])
  d=json.loads(out.getvalue());self.assertIsNone(d['estimate']['models']);self.assertEqual(d['stats']['projected']['chars'],27);self.assertEqual(d['estimate']['local_import_cost_usd'],0)
 def test_rates_and_estimator(self):
  with contextlib.redirect_stdout(io.StringIO()) as out: a.main(['--db',str(self.db),'--input-price','2','--output-price','10','--chars-per-token-low','5','--chars-per-token-high','2','--prefix-tokens','4','--scope','active','--no-tools','--json'])
  e=json.loads(out.getvalue())['estimate'];self.assertEqual(e['prefix_tokens'],4);self.assertAlmostEqual(e['models_unrounded']['configured']['high'],(3/2*2+3/2*.4*10+4*2)/1e6)
 def test_bad_estimator_cache(self):
  for val in (0,-1,True):
   with self.assertRaises(ValueError): a.archive_stats(self.db,sqlite_cache_kib=val)
  s=a.archive_stats(self.db)
  for low,high in ((0,0),(3,4),(4,float('nan'))):
   with self.assertRaises(ValueError):a.estimate(s,chars_per_token_low=low,chars_per_token_high=high)
 def test_file_identical(self):
  before=self.db.read_bytes();names=list(Path(self.tmp.name).iterdir());a.archive_stats(self.db);self.assertEqual(self.db.read_bytes(),before);self.assertEqual(list(Path(self.tmp.name).iterdir()),names)
 def test_empty(self):
  with sqlite3.connect(self.db) as c:c.execute('DELETE FROM messages')
  s=a.archive_stats(self.db);self.assertEqual(s['raw'],{'conversations':0,'messages':0,'chars':0});self.assertEqual(s['conversation_chars'],{'median':0,'max':0})
if __name__=='__main__':unittest.main()
