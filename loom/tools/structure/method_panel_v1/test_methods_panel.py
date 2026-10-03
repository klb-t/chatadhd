"""Independent synthetic mechanisms; no 48-pair corpus or gold is read."""
from collections import Counter
from pathlib import Path
import math
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parent))
import methods_panel as panel


class MethodsPanelMechanismTests(unittest.TestCase):
    def test_cosine_transparent_baseline_symmetry_scaling_and_empty(self):
        a,b=Counter(a=2,b=1),Counter(a=1,b=2)
        self.assertAlmostEqual(panel.cosine(a,b),.8)
        self.assertEqual(panel.cosine(a,b),panel.cosine(b,a))
        self.assertAlmostEqual(panel.cosine(a,Counter(a=4,b=2)),1)
        self.assertEqual(panel.cosine(a,Counter()),0)

    def test_tfidf_deduplicates_exact_sources_without_label_inputs(self):
        first,info=panel.tfidf_fit(["a b c","a b c","x y z"],panel.word_path_features)
        second,other=panel.tfidf_fit(["x y z","a b c"],panel.word_path_features)
        self.assertEqual(first,second)
        self.assertEqual(info,other)
        self.assertEqual(info["unique_source_count"],2)
        self.assertAlmostEqual(first["a b c"][("a","b")],1+math.log(3/2))

    def test_word_paths_retain_order_without_claiming_semantics(self):
        a=panel.word_path_features("P then Q")
        b=panel.word_path_features("Q then P")
        self.assertEqual(panel.lexical_features("P then Q"),panel.lexical_features("Q then P"))
        self.assertEqual(panel.cosine(a,b),0)
        self.assertNotEqual(panel.char_features("P then Q"),panel.char_features("Q then P"))

    def test_source_text_is_not_normalized_in_tfidf_source_identity(self):
        _,info=panel.tfidf_fit(["Żółw  jest", "żółw jest"],panel.char_features)
        self.assertEqual(info["unique_source_count"],2)
        self.assertEqual(panel.char_features("Żółw  jest"),panel.char_features("żółw jest"))

    def test_abstentions_keep_positive_planned_denominator(self):
        rows=[{"score":1.,"label":1},{"score":None,"label":1},{"score":0.,"label":0},{"score":None,"label":0}]
        value=panel.metrics(rows,.5)
        self.assertEqual(value["represented"],2)
        self.assertEqual(value["recall_represented"],1)
        self.assertEqual(value["recall_planned"],.5)
        self.assertEqual(value["abstained_positive"],1)
        self.assertEqual(value["abstained_negative"],1)
        self.assertEqual(value["missed_positive_opportunities"],1)

    def test_zero_positive_predictions_have_undefined_precision(self):
        value=panel.metrics([{"score":0.,"label":1}],.5)
        self.assertIsNone(value["precision"])
        self.assertEqual(value["precision_denominator"],0)
        self.assertEqual(value["recall_planned"],0)

    def test_rank_ties_are_complete_blocks_and_abstentions_explicit(self):
        rows=[{"score":.5,"label":1},{"score":.5,"label":0},{"score":None,"label":1}]
        rank=panel.ranking_metrics(rows)
        self.assertEqual(rank["represented"],2)
        self.assertEqual(rank["positive_represented"],1)
        self.assertEqual(rank["auroc"],.5)
        self.assertEqual(rank["average_precision_tie_blocks"],.5)
        self.assertEqual(rank["auroc_positive_negative_comparisons"],1)

    def test_union_adds_candidates_without_unknown_or_negative_veto(self):
        self.assertEqual(panel.nongating_union([.1,None,.8]),.8)
        self.assertIsNone(panel.nongating_union([None,None]))
        self.assertEqual(panel.nongating_union([.8,.1]),panel.nongating_union([.1,.8]))

    def test_empty_graph_does_not_self_match_as_evidence(self):
        empty={"nodes":[],"edges":[]}
        scores,status=panel.graph_scores(empty,empty)
        self.assertEqual(scores,{"role":None,"wl":None,"alignment":None})
        self.assertEqual(status,"unrepresented")

    def test_opaque_slot_graph_does_not_recover_named_proposition_identity(self):
        def graph(condition,consequence):
            return {"nodes":[{"id":"r","kind":"thought_operation","role":"transformation","qualifiers":{"operation":"implies"}},
                {"id":"a","kind":"opaque_source_slot","role":"constraint","qualifiers":{"argument_role":"condition"},"text":condition},
                {"id":"b","kind":"opaque_source_slot","role":"output","qualifiers":{"argument_role":"consequence"},"text":consequence}],
                "edges":[{"source":"r","target":"a","predicate":"condition","qualifiers":{}},{"source":"r","target":"b","predicate":"consequence","qualifiers":{}}]}
        scores,status=panel.graph_scores(graph("P","Q"),graph("Q","P"))
        self.assertEqual(scores["alignment"],1)
        self.assertEqual(status,"isomorphic")
        # This is a measured representation limitation, not a gold structural match.


if __name__=="__main__":unittest.main()
