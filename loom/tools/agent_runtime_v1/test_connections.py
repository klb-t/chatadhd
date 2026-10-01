"""Offline provider/target contract tests; no credentials, bills or remote VMs."""
from copy import deepcopy
import json
import tempfile
import unittest
from jsonschema.exceptions import ValidationError

from .connections import gcp_target, openrouter_decisions, openrouter_request
from .experiments import local_environment
from .runtime import AgentError, Tool, observed


class ConnectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        tool = Tool('arbitrary.app.measure', '1', {'type': 'object', 'required': ['unit'],
            'properties': {'unit': {'const': 'K'}}}, {'type': 'object'}, (),
            {'kind': 'read'}, lambda env, args: observed(args), {})
        self.context = {'goal': {'task': 'synthetic'}, 'environment':
            local_environment(self.temp.name).descriptor(), 'observations': [],
            'catalog': [tool.descriptor()]}
        self.compiled = openrouter_request(self.context, model='fixture/model', parameters={'max_tokens': 128})
        self.name = next(iter(self.compiled['tool_names']))

    def response(self, calls):
        return json.dumps({'id': 'fixture-response', 'choices': [{'message': {
            'role': 'assistant', 'content': None, 'tool_calls': calls,
            'reasoning_details': [{'opaque': 'synthetic-pass-through'}]}}]})

    def call(self, ident='call-1', unit='K'):
        return {'id': ident, 'type': 'function', 'function': {
            'name': self.name, 'arguments': json.dumps({'unit': unit})}}

    def test_compiler_preserves_schema_and_explicit_model_without_dispatch(self):
        self.assertEqual(self.compiled['payload']['tools'][0]['function']['parameters'],
                         self.context['catalog'][0]['input_schema'])
        self.assertEqual(self.compiled['payload']['model'], 'fixture/model')
        self.assertFalse(self.compiled['network_dispatched'])
        self.assertEqual(self.compiled['secret_ref'], 'api_key')
        self.assertNotIn('Authorization', self.compiled)

    def test_all_multiple_requested_calls_and_reasoning_metadata_are_retained(self):
        result = openrouter_decisions(self.response([self.call(), self.call('call-2')]), self.compiled)
        self.assertEqual(len(result['actions']), 2)
        self.assertEqual(result['actions'][0]['tool'], 'arbitrary.app.measure')
        self.assertEqual(result['assistant_message']['reasoning_details'][0]['opaque'],
                         'synthetic-pass-through')
        self.assertEqual(result['dispatch'], 'none')

    def test_provider_unit_mismatch_does_not_coerce(self):
        with self.assertRaises(ValidationError):
            openrouter_decisions(self.response([self.call(unit='degC')]), self.compiled)

    def test_undeclared_tool_is_rejected(self):
        call = self.call(); call['function']['name'] = 'shell'
        with self.assertRaisesRegex(AgentError, 'provider_requested_undeclared_tool'):
            openrouter_decisions(self.response([call]), self.compiled)

    def test_duplicate_provider_call_identity_is_rejected(self):
        with self.assertRaisesRegex(AgentError, 'unsupported_or_duplicate_tool_call'):
            openrouter_decisions(self.response([self.call(), self.call()]), self.compiled)

    def test_request_cannot_replace_bound_model_or_messages(self):
        with self.assertRaisesRegex(AgentError, 'reserved_request_fields'):
            openrouter_request(self.context, model='fixture/model', parameters={'model': 'other'})

    def test_gcp_target_preserves_selected_machine_and_does_not_claim_access(self):
        target = gcp_target(project='synthetic-project', zone='europe-west4-a',
            instance='synthetic-agent', connection='iap_ssh', credential_ref='gcp.adc')
        self.assertEqual(target['resource'],
            'projects/synthetic-project/zones/europe-west4-a/instances/synthetic-agent')
        self.assertEqual(target['availability'], 'unverified')
        self.assertEqual(target['capabilities'], [])
        self.assertEqual(target['fallback'], 'none')
        self.assertFalse(target['provisioning_dispatched'])

    def test_gcp_invalid_target_is_not_interpreted_as_shell(self):
        with self.assertRaisesRegex(AgentError, 'invalid_gcp_instance'):
            gcp_target(project='synthetic-project', zone='europe-west4-a',
                instance='x;echo secret', connection='ssh', credential_ref='gcp.adc')


if __name__ == '__main__':
    unittest.main()
