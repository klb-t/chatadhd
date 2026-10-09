"""Safe fixtures exercise syntax, not user data or model quality."""
import unittest

from loom.tools.resource_graph.parsers import (
    ParseError, UnsupportedFormat, parse, parser_descriptor, register_parser, recognize,
)


class SyntaxParsers(unittest.TestCase):
    def test_json_unknown_fields_and_types_survive(self):
        self.assertEqual(parse(b'{"unknown":{"a":[null,false,1,2.5,"x"]}}', "json"),
                         {"unknown": {"a": [None, False, 1, 2.5, "x"]}})
        self.assertEqual(parser_descriptor("json")["semantics"], "unrecognized")

    def test_json_rejects_ambiguous_or_nonstandard_values(self):
        for raw in (b'{"x":1,"x":2}', b'{"x":NaN}', b'{"x":Infinity}', b'{"x":1e999}'):
            with self.subTest(raw=raw), self.assertRaises(ParseError):
                parse(raw, "json")

    def test_budget_is_caller_policy(self):
        raw = b'{"x":[0,1,2]}'
        with self.assertRaises(ParseError):
            parse(raw, "json", options={"max_nodes": 3})
        self.assertEqual(parse(raw, "json", options={"max_nodes": 10})["x"], [0, 1, 2])
        with self.assertRaises(ParseError):
            parse(raw, "json", options={"max_bytes": 1})
        with self.assertRaises(ParseError):
            parse(raw, "json", options={"max_depth": 1})

    def test_yaml_unknown_tags_and_alias_are_preserved_without_execution(self):
        value = parse(b'a: &ref {flag: true, value: 3}\nb: *ref\nc: !unknown hi\n', "yaml")
        self.assertEqual(value["a"], {"flag": True, "value": 3})
        self.assertEqual(value["b"], {"$yaml_alias": "/a"})
        self.assertEqual(value["c"], {"$yaml_tag": "!unknown", "$yaml_value": "hi"})
        dangerous = parse(b'!!python/object/apply:os.system ["echo no"]', "yaml")
        self.assertEqual(dangerous["$yaml_tag"], "tag:yaml.org,2002:python/object/apply:os.system")

    def test_yaml_self_alias_and_merge_remain_addressable_syntax(self):
        self.assertEqual(parse(b'&self {child: *self}', "yaml"), {"child": {"$yaml_alias": ""}})
        value = parse(b'base: &b {a: 1}\nmerged: {<<: *b, b: 2}', "yaml")
        self.assertEqual(value["merged"]["<<"], {"$yaml_alias": "/base"})
        self.assertNotIn("a", value["merged"])

    def test_yaml_nonstring_keys_and_date_keep_types_and_values(self):
        value = parse(b'1: first\n"1": second', "yaml")
        self.assertEqual(value["$yaml_mapping"], [[1, "first"], ["1", "second"]])
        self.assertEqual(parse(b'date: 2026-10-09', "yaml")["date"],
                         {"$yaml_tag": "tag:yaml.org,2002:timestamp", "$yaml_value": "2026-10-09"})

    def test_yaml_alias_and_depth_budgets_precede_construction(self):
        with self.assertRaises(ParseError):
            parse(b'a: &a [1]\nb: [*a, *a]', "yaml", options={"max_aliases": 1})
        with self.assertRaises(ParseError):
            parse(b'[[[0]]]', "yaml", options={"max_depth": 2})
        with self.assertRaises(ParseError):
            parse(b'a: 1\na: 2', "yaml")

    def test_xml_attributes_mixed_content_comments_and_namespaces(self):
        value = parse(b'<root xmlns:q="urn:q" attr="x">before<q:item/>after<!--note--></root>', "xml")
        self.assertEqual(value["attributes"], {"attr": "x"})
        self.assertEqual(value["text"], "before")
        self.assertEqual(value["children"][0]["tag"], "{urn:q}item")
        self.assertEqual(value["children"][0]["tail"], "after")
        self.assertEqual(value["children"][1]["kind"], "comment")
        self.assertEqual(value["children"][1]["text"], "note")

    def test_xml_external_entities_and_dtd_are_forbidden(self):
        for raw in (b'<!DOCTYPE a [<!ENTITY leak SYSTEM "file:///etc/passwd">]><a>&leak;</a>',
                    b'<!DOCTYPE a><a/>'):
            with self.subTest(raw=raw), self.assertRaises(ParseError):
                parse(raw, "xml")

    def test_xml_xinclude_is_data_only(self):
        value = parse(b'<root xmlns:xi="http://www.w3.org/2001/XInclude"><xi:include href="http://example.invalid"/></root>', "xml")
        self.assertEqual(value["children"][0]["attributes"]["href"], "http://example.invalid")

    def test_csv_keeps_empty_cells_duplicate_headers_and_embedded_newlines(self):
        value = parse(b'k,k,\r\n1,"line\nline",\r\n', "csv")
        self.assertEqual(value, [["k", "k", ""], ["1", "line\nline", ""]])
        self.assertEqual(parse(b'1;2', "csv", options={"csv_delimiter": ";"}), [["1", "2"]])

    def test_unrecognized_format_and_custom_registration(self):
        with self.assertRaises(UnsupportedFormat):
            parse(b"abc", "fixture.unregistered")
        register_parser("fixture.pairs", lambda data, options: data.decode().split("|"),
                        descriptor={"version": "fixture/1", "semantics": "unrecognized"}, replace=True)
        self.assertEqual(parse(b"a|b", "fixture.pairs"), ["a", "b"])
        self.assertEqual(parser_descriptor("fixture.pairs")["version"], "fixture/1")

    def test_recognition_retains_ambiguous_syntax_alternatives(self):
        results = recognize(b'{"private-field": 42}', candidates=["json", "yaml", "xml"])
        self.assertEqual([item["recognized"] for item in results], [True, True, False])
        self.assertTrue(all(not item.get("semantic_recognition", False) for item in results))
        self.assertNotIn("private-field", str(results))

    def test_recognition_uses_new_registration_without_provider_switch(self):
        register_parser("fixture.token", lambda data, options: {"token": data.decode()}, replace=True)
        results = recognize(b"unknown", candidates=["fixture.token", "fixture.missing"])
        self.assertTrue(results[0]["recognized"])
        self.assertFalse(results[1]["recognized"])
        self.assertEqual(results[1]["diagnostic"], "UnsupportedFormat")

    def test_yaml_tagged_container_alias_pointer_uses_actual_representation(self):
        value = parse(b'!box {a: &x [1], b: *x}', "yaml")
        self.assertEqual(value["$yaml_value"]["b"], {"$yaml_alias": "/$yaml_value/a"})


if __name__ == "__main__":
    unittest.main()
