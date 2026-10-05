"""Offline pooled transport tests: fake sessions and in-memory HTTP streams."""
import gzip
import http.client
import io
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

try:
    from . import research_programme_transport as transport
    from .test_research_programme_transport import config
except ImportError:
    import research_programme_transport as transport
    from test_research_programme_transport import config


def pooled_config(**changes):
    edited = config()
    edited.update(backend="requests_session", pool_connections=3,
                  pool_maxsize=7, pool_block=True)
    edited.update(changes)
    return edited


class FakeRaw:
    def __init__(self, raw=b"", error=None):
        self.source = io.BytesIO(raw)
        self.error = error
        self.read_calls = []

    def read1(self, amount, *, decode_content):
        self.read_calls.append((amount, decode_content))
        chunk = self.source.read(amount)
        if not chunk and self.error is not None:
            raise self.error
        return chunk


class FakeResponse:
    def __init__(self, raw=b"", status=200, error=None):
        self.status_code = status
        self.raw = FakeRaw(raw, error)
        self.close = Mock()


class FakeSession:
    def __init__(self):
        self.trust_env = None
        self.mount = Mock()
        self.request = Mock()
        self.close = Mock()


def fake_requests(session):
    return SimpleNamespace(Session=Mock(return_value=session),
        adapters=SimpleNamespace(HTTPAdapter=Mock(side_effect=lambda **kwargs: kwargs)),
        packages=SimpleNamespace(urllib3=SimpleNamespace(
            response=SimpleNamespace(HTTPResponse=FakeRaw))))


class RequestsSessionTransportTests(unittest.TestCase):
    def make_transport(self, **changes):
        session = FakeSession()
        requests = fake_requests(session)
        with patch.object(transport, "_requests_module", return_value=requests):
            instance = transport.OpenRouterTransport(pooled_config(**changes),
                                                      b"offline-fixture-token\n")
        return instance, session, requests

    def test_one_session_reused_for_get_post_with_exact_auth_bytes_and_routes(self):
        instance, session, requests = self.make_transport()
        body = b'{ "retain" : "spacing" }\r\n\x00\xff'
        responses = [FakeResponse(b"metadata"), FakeResponse(b"chat receipt")]
        session.request.side_effect = responses
        first = instance.request("GET", "generation", params={"id": "gen-a&key=b"})
        second = instance.request("POST", "chat", body)
        self.assertEqual(first["raw"], b"metadata")
        self.assertEqual(second["raw"], b"chat receipt")
        requests.Session.assert_called_once_with()
        self.assertEqual(session.request.call_count, 2)
        calls = session.request.call_args_list
        self.assertEqual(calls[0].args, ("GET", "https://openrouter.ai/api/v1/generation?id=gen-a%26key%3Db"))
        self.assertEqual(calls[1].args, ("POST", "https://openrouter.ai/api/v1/chat/completions"))
        self.assertIsNone(calls[0].kwargs["data"])
        self.assertIs(calls[1].kwargs["data"], body)
        for call, response in zip(calls, responses):
            self.assertNotIn("json", call.kwargs)
            self.assertFalse(call.kwargs["allow_redirects"])
            self.assertIs(call.kwargs["verify"], True)
            self.assertIs(call.kwargs["stream"], True)
            self.assertEqual(call.kwargs["timeout"], 17)
            self.assertEqual(call.kwargs["headers"]["Authorization"], "Bearer offline-fixture-token")
            self.assertEqual(call.kwargs["headers"]["Accept-Encoding"], "identity")
            self.assertTrue(all(not decode for _, decode in response.raw.read_calls))
            response.close.assert_called_once_with()

    def test_pool_options_are_data_and_both_adapters_disable_retries(self):
        _, session, requests = self.make_transport(pool_connections=19, pool_maxsize=23, pool_block=False)
        self.assertEqual(requests.adapters.HTTPAdapter.call_count, 2)
        for call in requests.adapters.HTTPAdapter.call_args_list:
            self.assertEqual(call.kwargs, {"pool_connections": 19, "pool_maxsize": 23,
                                          "pool_block": False, "max_retries": 0})
        self.assertEqual([call.args[0] for call in session.mount.call_args_list], ["https://", "http://"])

    def test_proxy_environment_is_explicit_and_auth_is_preserved(self):
        for mode, expected in (("disabled", False), ("environment", True)):
            with self.subTest(mode=mode):
                instance, session, _ = self.make_transport(proxy_mode=mode)
                self.assertIs(session.trust_env, expected)
                session.request.return_value = FakeResponse()
                instance.request("GET", "key")
                call = session.request.call_args
                prepared = SimpleNamespace(headers=dict(call.kwargs["headers"]))
                self.assertIs(call.kwargs["auth"](prepared), prepared)
                self.assertEqual(prepared.headers["Authorization"], "Bearer offline-fixture-token")

    def test_actual_requests_preparation_cannot_load_netrc_or_change_body(self):
        requests = transport._requests_module()
        session = requests.Session()
        self.addCleanup(session.close)
        session.trust_env = True
        body = b'{ "bytes" : true }\n'
        with patch.object(requests.sessions, "get_netrc_auth", side_effect=AssertionError("netrc read")) as netrc:
            prepared = session.prepare_request(requests.Request("POST", transport.ORIGIN + "/api/v1/chat/completions",
                data=body, headers={"Authorization": "Bearer offline-fixture-token"},
                auth=transport._PreserveBearerAuth()))
        netrc.assert_not_called()
        self.assertIs(prepared.body, body)
        self.assertEqual(prepared.headers["Authorization"], "Bearer offline-fixture-token")

    def test_actual_session_does_not_consume_or_decompress_redirect_body(self):
        requests = transport._requests_module()
        instance = transport.OpenRouterTransport(pooled_config(), "offline-fixture-token")
        self.addCleanup(instance.close)
        raw = gzip.compress(b"original redirect response bytes\x00\xff")
        response = requests.Response()
        response.status_code = 302
        response.url = transport.ORIGIN + "/api/v1/key"
        response.headers.update({"Location": "https://elsewhere.invalid/private",
                                 "Content-Encoding": "gzip"})
        response.raw = FakeRaw(raw)
        response.raw.close = Mock()
        response.raw.release_conn = Mock()
        adapter = instance._session.get_adapter(response.url)
        with patch.object(adapter, "send", return_value=response) as send:
            result = instance.request("GET", "key")
        self.assertEqual(result["raw"], raw)
        self.assertEqual(result["http_status"], 302)
        self.assertFalse(response._content_consumed)
        self.assertIsNone(response.next)
        send.assert_called_once()
        self.assertEqual(send.call_args.args[0].url, transport.ORIGIN + "/api/v1/key")
        self.assertIs(send.call_args.kwargs["verify"], True)
        response.raw.close.assert_called_once_with()

    def test_http_error_and_redirect_return_complete_first_response_without_retry(self):
        for method, route, status in (("POST", "chat", 429), ("GET", "key", 302), ("POST", "jev", 503)):
            with self.subTest(status=status):
                instance, session, _ = self.make_transport()
                raw = b"\x00\xff" * (2 * 1024 * 1024) + b"last bytes"
                response = FakeResponse(raw, status)
                session.request.return_value = response
                result = instance.request(method, route, b"fixture" if method == "POST" else None)
                self.assertEqual(result["http_status"], status)
                self.assertEqual(result["raw"], raw)
                session.request.assert_called_once()
                self.assertFalse(session.request.call_args.kwargs["allow_redirects"])
                response.close.assert_called_once_with()

    def test_read_failure_recovers_nested_partial_and_closes_response(self):
        instance, session, _ = self.make_transport()
        inner = http.client.IncompleteRead(b"last partial")
        outer = RuntimeError("credential body remote message", inner)
        outer.__cause__ = inner
        response = FakeResponse(b"first chunk", error=outer)
        session.request.return_value = response
        result = instance.request("POST", "chat", b"private body")
        self.assertEqual(result["raw"], b"first chunklast partial")
        self.assertEqual(result["transport_error"], "response_read_failed")
        self.assertNotIn("remote", repr(result))
        session.request.assert_called_once()
        response.close.assert_called_once_with()

    def test_read_failure_ignores_integer_partial_and_invalid_chunk_type(self):
        for error in (SimpleNamespace(partial=19), "not bytes"):
            with self.subTest(error=type(error).__name__):
                instance, session, _ = self.make_transport()
                response = FakeResponse()
                if isinstance(error, str):
                    response.raw.read1 = Mock(side_effect=[b"first", error])
                else:
                    failure = RuntimeError("private remote error")
                    failure.partial = error.partial
                    response.raw.read1 = Mock(side_effect=[b"first", failure])
                session.request.return_value = response
                result = instance.request("GET", "key")
                self.assertEqual(result["raw"], b"first")
                self.assertEqual(result["transport_error"], "response_read_failed")
                response.close.assert_called_once_with()

    def test_request_and_initialization_errors_are_constant_and_suppress_context(self):
        instance, session, _ = self.make_transport()
        session.request.side_effect = RuntimeError("offline-fixture-token private body remote error")
        with self.assertRaises(transport.TransportError) as failure:
            instance.request("POST", "chat", b"private body")
        self.assertEqual(str(failure.exception), "transport_failed")
        self.assertTrue(failure.exception.__suppress_context__)
        session.request.assert_called_once()
        session = FakeSession()
        requests = fake_requests(session)
        session.mount.side_effect = RuntimeError("secret proxy credentials")
        with patch.object(transport, "_requests_module", return_value=requests), \
                self.assertRaises(transport.TransportError) as failure:
            transport.OpenRouterTransport(pooled_config(), "offline-fixture-token")
        self.assertEqual(str(failure.exception), "requests_backend_initialization_failed")
        self.assertTrue(failure.exception.__suppress_context__)
        session.close.assert_called_once_with()

    def test_dependency_or_read1_capability_missing_fails_before_any_request(self):
        with patch.object(transport.importlib, "import_module", side_effect=ImportError("remote credentials")), \
                self.assertRaises(transport.TransportError) as failure:
            transport.OpenRouterTransport(pooled_config(), "offline-fixture-token")
        self.assertEqual(str(failure.exception), "requests_backend_unavailable")
        self.assertTrue(failure.exception.__suppress_context__)
        session = FakeSession()
        requests = fake_requests(session)
        requests.packages.urllib3.response.HTTPResponse = object
        with patch.object(transport, "_requests_module", return_value=requests), \
                self.assertRaises(transport.TransportError) as failure:
            transport.OpenRouterTransport(pooled_config(), "offline-fixture-token")
        self.assertEqual(str(failure.exception), "requests_raw_read1_unavailable")
        requests.Session.assert_not_called()
        session.request.assert_not_called()

    def test_invalid_backend_and_pool_data_fail_without_creating_session(self):
        cases = [("backend", value) for value in (None, "requests", {}, True)]
        cases += [(name, value) for name in ("pool_connections", "pool_maxsize")
                  for value in (None, 0, -1, 1.5, True, "3")]
        cases += [("pool_block", value) for value in (None, 0, 1, "true")]
        with patch.object(transport, "_requests_module") as load:
            for field, value in cases:
                with self.subTest(field=field, value=value), self.assertRaises(transport.TransportError) as failure:
                    transport.OpenRouterTransport(pooled_config(**{field: value}), "offline-fixture-token")
                self.assertEqual(str(failure.exception), "invalid_transport_" + field)
            for field in ("pool_connections", "pool_maxsize", "pool_block"):
                edited = pooled_config()
                del edited[field]
                with self.subTest(missing=field), self.assertRaises(transport.TransportError):
                    transport.OpenRouterTransport(edited, "offline-fixture-token")
        load.assert_not_called()

    def test_origin_route_method_and_body_validation_prevent_dispatch(self):
        instance, session, _ = self.make_transport()
        for method, route, body in (("GET", "key", {}), ("POST", "key", b"body"), ("get", "key", None)):
            with self.subTest(method=method, route=route), self.assertRaises(transport.TransportError):
                instance.request(method, route, body)
        instance._routes["key"]["path"] = "//elsewhere.invalid/private"
        with self.assertRaises(transport.TransportError):
            instance.request("GET", "key")
        session.request.assert_not_called()
        with self.assertRaises(transport.TransportError):
            transport.OpenRouterTransport(pooled_config(base_url="https://elsewhere.invalid"), "offline-fixture-token")

    def test_close_releases_pool_and_never_exposes_close_exception(self):
        instance, session, _ = self.make_transport()
        instance.close()
        session.close.assert_called_once_with()
        session.close.side_effect = RuntimeError("secret close detail")
        instance.close()


class InMemoryHTTPStreamTests(unittest.TestCase):
    def request_raw(self, wire):
        requests = transport._requests_module()
        client = http.client.HTTPResponse(SimpleNamespace(makefile=lambda *args, **kwargs: io.BytesIO(wire)))
        client.begin()
        raw = requests.packages.urllib3.response.HTTPResponse(body=client, headers=dict(client.headers.items()),
            original_response=client, preload_content=False, decode_content=False,
            enforce_content_length=True)
        response = SimpleNamespace(status_code=client.status, raw=raw, close=raw.close)
        session = FakeSession()
        session.request.return_value = response
        with patch.object(transport, "_requests_module", return_value=fake_requests(session)):
            instance = transport.OpenRouterTransport(pooled_config(), "offline-fixture-token")
        result = instance.request("GET", "key")
        session.request.assert_called_once()
        return result

    def test_chunked_truncation_keeps_every_body_byte(self):
        result = self.request_raw(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n5\r\nde")
        self.assertEqual(result["raw"], b"abcde")
        self.assertEqual(result["transport_error"], "response_read_failed")

    def test_chunked_framing_partial_is_not_appended_to_body(self):
        result = self.request_raw(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r")
        self.assertEqual(result["raw"], b"abc")
        self.assertEqual(result["transport_error"], "response_read_failed")

    def test_content_length_truncation_keeps_all_received_body(self):
        result = self.request_raw(b"HTTP/1.1 503 Error\r\nContent-Length: 19\r\n\r\nbody bytes")
        self.assertEqual(result["http_status"], 503)
        self.assertEqual(result["raw"], b"body bytes")
        self.assertEqual(result["transport_error"], "response_read_failed")

    def test_compressed_error_response_is_not_decompressed(self):
        body = gzip.compress(b"remote error body\x00\xff")
        wire = (b"HTTP/1.1 429 Error\r\nContent-Encoding: gzip\r\nContent-Length: "
                + str(len(body)).encode() + b"\r\n\r\n" + body)
        result = self.request_raw(wire)
        self.assertEqual(result["raw"], body)
        self.assertEqual(result["http_status"], 429)
        self.assertNotIn("transport_error", result)


class MethodHeaderPolicyTests(unittest.TestCase):
    def make_transport(self, backend, headers=None):
        edited = pooled_config(backend=backend)
        if headers is not None:
            edited["headers_by_method"] = headers
        if backend == "requests_session":
            session = FakeSession()
            session.request.side_effect = lambda *args, **kwargs: FakeResponse(b"response")
            with patch.object(transport, "_requests_module", return_value=fake_requests(session)):
                instance = transport.OpenRouterTransport(edited, "offline-fixture-token")
            dispatch = session.request
        else:
            instance = transport.OpenRouterTransport(edited, "offline-fixture-token")
            def opened(*args, **kwargs):
                response = io.BytesIO(b"response")
                response.getcode = lambda: 200
                return response
            instance._opener = Mock()
            instance._opener.open.side_effect = opened
            dispatch = instance._opener.open
        return instance, dispatch

    def headers_and_body(self, backend, call):
        if backend == "requests_session":
            return {name.lower(): value for name, value in call.kwargs["headers"].items()}, call.kwargs["data"]
        request = call.args[0]
        return {name.lower(): value for name, value in request.header_items()}, request.data

    def test_get_cache_headers_are_method_specific_and_keep_auth_identity_and_body(self):
        for backend in ("urllib", "requests_session"):
            with self.subTest(backend=backend):
                instance, dispatch = self.make_transport(backend,
                    {"GET": {"Cache-Control": "no-cache", "Pragma": "no-cache"}})
                body = b'{ "exact bytes" : true }\r\n'
                self.assertEqual(instance.request("GET", "key")["raw"], b"response")
                self.assertEqual(instance.request("POST", "chat", body)["raw"], b"response")
                get_headers, get_body = self.headers_and_body(backend, dispatch.call_args_list[0])
                post_headers, post_body = self.headers_and_body(backend, dispatch.call_args_list[1])
                self.assertEqual(get_headers["cache-control"], "no-cache")
                self.assertEqual(get_headers["pragma"], "no-cache")
                self.assertNotIn("cache-control", post_headers)
                self.assertNotIn("pragma", post_headers)
                for headers in (get_headers, post_headers):
                    self.assertEqual(headers["authorization"], "Bearer offline-fixture-token")
                    self.assertEqual(headers["accept-encoding"], "identity")
                    self.assertEqual(headers["content-type"], "application/json")
                self.assertIsNone(get_body)
                self.assertIs(post_body, body)
                self.assertEqual(dispatch.call_count, 2)

    def test_default_empty_policy_preserves_original_headers_for_both_backends(self):
        expected = {"authorization": "Bearer offline-fixture-token", "content-type": "application/json",
                    "accept-encoding": "identity"}
        for backend in ("urllib", "requests_session"):
            for configured in (None, {}, {"GET": {}}):
                with self.subTest(backend=backend, configured=configured):
                    instance, dispatch = self.make_transport(backend, configured)
                    instance.request("GET", "key")
                    headers, _ = self.headers_and_body(backend, dispatch.call_args)
                    self.assertEqual(headers, expected)

    def test_header_policy_snapshot_and_each_request_header_dict_are_independent(self):
        for backend in ("urllib", "requests_session"):
            with self.subTest(backend=backend):
                policy = {"GET": {"Cache-Control": "no-cache"}}
                instance, dispatch = self.make_transport(backend, policy)
                policy["GET"]["Cache-Control"] = "private changed value"
                policy["POST"] = {"Pragma": "no-cache"}
                returned = instance._headers("GET")
                returned["Cache-Control"] = "changed returned dictionary"
                instance.request("GET", "key")
                instance.request("POST", "chat", b"{}").get("raw")
                get_headers, _ = self.headers_and_body(backend, dispatch.call_args_list[0])
                post_headers, _ = self.headers_and_body(backend, dispatch.call_args_list[1])
                self.assertEqual(get_headers["cache-control"], "no-cache")
                self.assertNotIn("pragma", post_headers)

    def test_invalid_or_protected_header_policy_fails_before_backend_construction(self):
        invalid = [None, [], "GET", {"get": {}}, {1: {}}, {"GET": []}, {"GET": None},
                   {"GET": {"Bad\r\nName": "value"}}, {"GET": {"Bad Name": "value"}},
                   {"GET": {"": "value"}}, {"GET": {1: "value"}},
                   {"GET": {"Pragma": b"no-cache"}}, {"GET": {"Pragma": None}},
                   {"GET": {"Pragma": "private value\r\nInjected: data"}},
                   {"GET": {"Pragma": "private value\nInjected: data"}},
                   {"GET": {"Pragma": "invalid\x00value"}}, {"GET": {"Pragma": " leading space"}},
                   {"GET": {"Pragma": "no-cache", "pRAGMA": "duplicate"}}]
        invalid += [{"GET": {name: "private override"}} for header in
                    ("Authorization", "Host", "Content-Length", "Content-Type", "Accept-Encoding")
                    for name in (header, header.lower(), header.swapcase())]
        with patch.object(transport, "_requests_module") as load, \
                patch.object(transport.urllib.request, "build_opener") as build:
            for backend in ("urllib", "requests_session"):
                for index, headers in enumerate(invalid):
                    with self.subTest(backend=backend, invalid_index=index), \
                            self.assertRaises(transport.TransportError) as failure:
                        transport.OpenRouterTransport(pooled_config(backend=backend,
                            headers_by_method=headers), "offline-fixture-token")
                    self.assertEqual(str(failure.exception), "invalid_transport_headers")
        load.assert_not_called()
        build.assert_not_called()


if __name__ == "__main__":
    unittest.main()
